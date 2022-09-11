/**
 * Open Space Program
 * Copyright © 2019-2022 Open Space Program Project
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "execute_simple.h"

#include <Corrade/Containers/ArrayViewStl.h>

#include <algorithm>
#include <cassert>

namespace osp
{

void task_enqueue(TaskTags const& tags, ExecutionContext &rExec, BitSpanConst_t const query)
{
    std::size_t const tagIntSize = tags.tag_ints_per_task();
    assert(query.size() == tagIntSize);

    auto const taskTagInts = Corrade::Containers::arrayView(tags.m_taskTags);

    for (uint32_t const currTask : tags.m_tasks.bitview().zeros())
    {
        unsigned int &rQueuedCount = rExec.m_taskQueuedCounts[currTask];

        if (rQueuedCount == 0)
        {
            // Task not yet queued, check if any tags match the query
            std::size_t const offset = currTask * tagIntSize;
            auto const currTaskTagInts = taskTagInts.slice(offset, offset + tagIntSize);

            bool anyTagMatches = false;
            auto taskTagIntIt = std::begin(currTaskTagInts);

            for (uint64_t const queryInt : query)
            {
                if ((queryInt & *taskTagIntIt) != 0)
                {
                    anyTagMatches = true;
                    break;
                }
                std::advance(taskTagIntIt, 1);
            }

            if (anyTagMatches)
            {
                rQueuedCount = 1;
                auto const view = lgrn::bit_view(currTaskTagInts);
                for (uint32_t const tag : view.ones())
                {
                    rExec.m_tagIncompleteCounts[tag] ++;
                }
            }
        }
    }
}

static bool compare_tags(BitSpanConst_t const mask, BitSpanConst_t const taskTags) noexcept
{
    auto taskTagIntIt = std::begin(taskTags);
    for (uint64_t const maskInt : mask)
    {
        uint64_t const taskTagInt = *taskTagIntIt;

        // No match if a 1 bit in taskTagInt corresponds with a 0 in maskInt
        if ((maskInt & taskTagInt) != taskTagInt)
        {
            return false;
        }
        std::advance(taskTagIntIt, 1);
    }
    return true;
}

void task_list_available(TaskTags const& tags, ExecutionContext const& exec, BitSpan_t tasksOut)
{
    assert(tasksOut.size() == tags.m_tasks.vec().size());

    // Bitmask makes it easy to compare the tags of a task
    // 1 = allowed (default), 0 = not allowed
    // All tags of a task must be allowed for the entire task to run.
    // aka: All ones in a task's bits must corrolate to a one in the mask
    std::vector<uint64_t> mask(tags.m_tags.vec().size(), ~uint64_t(0));
    auto maskBits = lgrn::bit_view(mask);

    // Check dependencies of each tag
    // Set them 0 (disallow) in the mask if the tag has incomplete dependencies
    auto dependsView = Corrade::Containers::arrayView<TaskTags::Tag const>(tags.m_tagDepends);
    auto currDepends = dependsView;
    for (uint32_t const currTag : tags.m_tags.bitview().zeros())
    {
        bool satisfied = true;

        for (TaskTags::Tag const dependTag : currDepends.prefix(tags.m_tagDependsPerTag))
        {
            if (dependTag == lgrn::id_null<TaskTags::Tag>())
            {
                break;
            }

            if (exec.m_tagIncompleteCounts[std::size_t(dependTag)] != 0)
            {
                satisfied = false;
                break;
            }
        }

        if ( ! satisfied)
        {
            maskBits.reset(currTag);
        }

        currDepends = currDepends.exceptPrefix(tags.m_tagDependsPerTag);
    }

    // TODO: Check Limits with exec.m_tagRunningCounts

    auto tasksOutBits = lgrn::bit_view(tasksOut);

    std::size_t const tagIntSize = tags.tag_ints_per_task();
    BitSpanConst_t const taskTagInts = Corrade::Containers::arrayView(tags.m_taskTags);

    // Iterate all tasks and use mask to match which ones can run
    for (uint32_t const currTask : tags.m_tasks.bitview().zeros())
    {
        if (exec.m_taskQueuedCounts[currTask] == 0)
        {
            continue; // Task not queued to run
        }

        std::size_t const offset = currTask * tagIntSize;
        auto const currTaskTagInts = taskTagInts.slice(offset, offset + tagIntSize);

        if (compare_tags(mask, currTaskTagInts))
        {
            tasksOutBits.set(currTask);
        }
    }
}

void task_start(TaskTags const& tags, ExecutionContext &rExec, TaskTags::Task const task)
{
    auto taskTagInts = Corrade::Containers::arrayView(tags.m_taskTags);
    std::size_t const tagIntSize = tags.tag_ints_per_task();
    std::size_t const offset = std::size_t(task) * tagIntSize;
    auto currTaskTagInts = taskTagInts.slice(offset, offset + tagIntSize);

    auto const view = lgrn::bit_view(currTaskTagInts);
    for (uint32_t const tag : view.ones())
    {
        rExec.m_tagRunningCounts[tag] ++;
    }
}

void task_finish(TaskTags const& tags, ExecutionContext &rExec, TaskTags::Task const task)
{
    auto taskTagInts = Corrade::Containers::arrayView(tags.m_taskTags);
    std::size_t const tagIntSize = tags.tag_ints_per_task();
    std::size_t const offset = std::size_t(task) * tagIntSize;
    auto currTaskTagInts = taskTagInts.slice(offset, offset + tagIntSize);

    rExec.m_taskQueuedCounts[std::size_t(task)] --;

    auto const view = lgrn::bit_view(currTaskTagInts);
    for (uint32_t const tag : view.ones())
    {
        rExec.m_tagRunningCounts[tag] --;
        rExec.m_tagIncompleteCounts[tag] --;
    }
}

} // namespace osp