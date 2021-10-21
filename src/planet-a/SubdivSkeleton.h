/**
 * Open Space Program
 * Copyright © 2019-2021 Open Space Program Project
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
#pragma once

#include <Corrade/Containers/ArrayViewStl.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace planeta
{

template<typename T>
using ArrayView_t = Corrade::Containers::ArrayView<T>;

/**
 * @brief Generates reusable sequential IDs
 */
template<typename ID_T, bool NO_AUTO_RESIZE = false>
class IdRegistry
{
    using id_int_t = std::underlying_type_t<ID_T>;

public:

    IdRegistry() = default;
    IdRegistry(size_t capacity) { m_exists.reserve(capacity); };

    ID_T create();

    /**
     * @return Array size required to fit all currently existing IDs
     */
    id_int_t size_required() const noexcept { return m_exists.size(); }

    size_t capacity() const { return m_exists.capacity(); }

    void reserve(size_t n) { m_exists.reserve(n); }

    void reserve_more(size_t n)
    {
        reserve(n + m_exists.size() - m_deleted.size());
    }

    void remove(ID_T id);

    bool exists(ID_T id) const noexcept;

private:
    std::vector<bool> m_exists; // this guy is weird, be careful
    std::vector<id_int_t> m_deleted;

}; // class IdRegistry

template<typename ID_T, bool NO_AUTO_RESIZE>
ID_T IdRegistry<ID_T, NO_AUTO_RESIZE>::create()
{
    // Attempt to reuse a deleted ID
    if ( ! m_deleted.empty())
    {
        id_int_t const id = m_deleted.back();
        m_deleted.pop_back();
        m_exists[id] = true;
        return ID_T(id);
    }

    if constexpr (NO_AUTO_RESIZE)
    {
        if (m_exists.size() == m_exists.capacity())
        {
            throw std::runtime_error("ID over max capacity with automatic resizing disabled");
        }
    }

    // Create a new Id
    id_int_t const id = m_exists.size();
    m_exists.push_back(true);
    return ID_T(id);
}

//-----------------------------------------------------------------------------

/**
 * @brief A multitree directed acyclic graph of reusable IDs where new IDs can
 *        be created from two other parent IDs.
 */
template<typename ID_T>
class SubdivIdTree : private IdRegistry<ID_T>
{
    using id_int_t = std::underlying_type_t<ID_T>;

    static_assert(std::is_integral<id_int_t>::value && sizeof(ID_T) <= 4,
                  "ID_T must be an integral type, 4 bytes or less in size");

    using combination_t = uint64_t;

public:

    using IdRegistry<ID_T>::size_required;

    ID_T create_root()
    {
        ID_T const id = IdRegistry<ID_T>::create();
        m_idChildCount.resize(size_required());
        m_idChildCount[size_t(id)] = 0;
        return id;
    };

    std::pair<ID_T, bool> create_or_get(ID_T a, ID_T b)
    {
        combination_t const combination = hash_id_combination(a, b);

        // Try emplacing a blank element under this combination of IDs, or get
        // existing element
        auto const& [it, success] = m_parentsToId.try_emplace(combination, 0);

        if (success)
        {
            // The space was free, and a new element was succesfully emplaced

            // Create a new ID for real, replacing the blank one from before
            it->second = id_int_t(create_root());

            // Keep track of the new ID's parents
            m_idToParents.resize(size_required());
            m_idToParents[it->second] = combination;

            // Increase child count of the two parents
            m_idChildCount[size_t(a)] ++;
            m_idChildCount[size_t(b)] ++;
        }

        return { ID_T(it->second), success };
    }

    ID_T get(ID_T a, ID_T b) const;

    std::pair<ID_T, ID_T> get_parents(ID_T a);

    size_t capacity() const { return capacity(); }

    void reserve(size_t n)
    {
        IdRegistry<ID_T>::reserve(n);
        m_idToParents.reserve(IdRegistry<ID_T>::capacity());
        m_idChildCount.reserve(IdRegistry<ID_T>::capacity());
    }

    void reserve_more(size_t n)
    {
        IdRegistry<ID_T>::reserve_more(n);
        m_idToParents.reserve(IdRegistry<ID_T>::capacity());
        m_idChildCount.reserve(IdRegistry<ID_T>::capacity());
    }

    static constexpr combination_t
    hash_id_combination(id_int_t a, id_int_t b) noexcept
    {
        id_int_t const ls = (a > b) ? a : b;
        id_int_t const ms = (a > b) ? b : a;

        return combination_t(ls)
                | (combination_t(ms) << (sizeof(id_int_t) * 8));
    }

    static constexpr combination_t
    hash_id_combination(ID_T a, ID_T b) noexcept
    {
        return hash_id_combination( id_int_t(a), id_int_t(b) );
    }

private:
    std::unordered_map<combination_t, id_int_t> m_parentsToId;
    std::vector<combination_t> m_idToParents;
    std::vector<uint8_t> m_idChildCount;

}; // class SubdivTree



//-----------------------------------------------------------------------------

enum class SkVrtxId : uint32_t {};

/**
 * @brief Uses a SubdivIdTree to manage relationships between Vertex IDs, and
 *        adds reference counting features.
 *
 * This class does NOT store vertex data like positions and normals.
 */
class SubdivSkeleton
{

public:

    SkVrtxId vrtx_create_root()
    {
        SkVrtxId const vrtxId = m_vrtxIdTree.create_root();
        m_vrtxRefCount.resize(m_vrtxIdTree.size_required());
        m_vrtxRefCount[size_t(vrtxId)] = 0;
        return vrtxId;
    };

    SkVrtxId vrtx_create_or_get_child(SkVrtxId a, SkVrtxId b)
    {
        auto const [vrtxId, created] = m_vrtxIdTree.create_or_get(a, b);
        if (created)
        {
            m_vrtxRefCount.resize(m_vrtxIdTree.size_required());
            m_vrtxRefCount[size_t(vrtxId)] = 0;
        }
        return vrtxId;
    }

    SubdivIdTree<SkVrtxId> vrtx_ids() const noexcept { return m_vrtxIdTree; }

    void vrtx_reserve(size_t n)
    {
        m_vrtxIdTree.reserve(n);
        m_vrtxRefCount.reserve(m_vrtxIdTree.capacity());
    }

    void vrtx_reserve_more(size_t n)
    {
        m_vrtxIdTree.reserve_more(n);
        m_vrtxRefCount.reserve(m_vrtxIdTree.capacity());
    }

    void vrtx_refcount_add(SkVrtxId id) noexcept { m_vrtxRefCount[size_t(id)] ++; };
    void vrtx_refcount_remove(SkVrtxId id) noexcept { m_vrtxRefCount[size_t(id)] --; };

    ArrayView_t<uint8_t> vrtx_get_refcounts() { return m_vrtxRefCount; };

private:

    SubdivIdTree<SkVrtxId> m_vrtxIdTree;

    // access using VrtxIds from m_vrtxTree
    std::vector<uint8_t> m_vrtxRefCount;

    std::vector<SkVrtxId> m_maybeDelete;

}; // class SubdivSkeleton

//-----------------------------------------------------------------------------

enum class SkTriId : uint32_t {};
enum class SkTriGroupId : uint32_t {};

struct SkeletonTriangle
{
    // Vertices are ordered counter-clockwise, starting from top:
    // 0: Top   1: Left   2: Right
    //       0
    //      / \
    //     /   \
    //    /     \
    //   1 _____ 2
    //
    std::array<SkVrtxId, 3> m_vertices;

    std::optional<SkTriGroupId> m_children;

}; // struct SkeletonTriangle

// Skeleton triangles are added and removed in groups of 4
struct SkTriGroup
{
    // Subdivided triangles are arranged in m_triangles as followed:
    // 0: Top   1: Left   2: Right   3: Center
    //
    //        /\
    //       /  \
    //      / t0 \
    //     /______\
    //    /\      /\
    //   /  \ t3 /  \
    //  / t1 \  / t2 \
    // /______\/______\
    //
    // Center is upside-down, it's 'top' vertex is the bottom-middle one
    // This arrangement may not apply for root triangles.
    std::array<SkeletonTriangle, 4> m_triangles;

    SkTriId m_parent;
    uint8_t m_depth;
};

/**
 * @return Group ID of a SkeletonTriangle's group specified by Id
 */
constexpr SkTriGroupId tri_group_id(SkTriId id) noexcept
{
    return SkTriGroupId( uint32_t(id) / 4 );
}

/**
 * @return Sibling index of a SkeletonTriangle by Id
 */
constexpr uint8_t tri_sibling_index(SkTriId id) noexcept
{
    return uint32_t(id) % 4;
}

/**
 * @return Id of a SkeletonTriangle from it's group Id and sibling index
 */
constexpr SkTriId tri_id(SkTriGroupId id, uint8_t siblingIndex) noexcept
{
    return SkTriId(uint32_t(id) * 4 + siblingIndex);
}

/**
 * @brief A subdividable mesh with reference counted triangles and vertices;
 *        A SubdivSkeleton that also features triangles.
 *
 * This class does NOT store vertex data like positions and normals.
 */
class SubdivTriangleSkeleton : public SubdivSkeleton
{
public:

    std::array<SkVrtxId, 3> vrtx_create_middles(
            std::array<SkVrtxId, 3> const& vertices)
    {
        return {
            vrtx_create_or_get_child(vertices[0], vertices[1]),
            vrtx_create_or_get_child(vertices[1], vertices[2]),
            vrtx_create_or_get_child(vertices[2], vertices[0])
        };
    }

    void vrtx_create_chunk_edge_recurse(
            unsigned int level,
            SkVrtxId a, SkVrtxId b,
            ArrayView_t<SkVrtxId> rOut)
    {
        if (level == 0)
        {
            return;
        }

        SkVrtxId const mid = vrtx_create_or_get_child(a, b);
        size_t const halfSize = rOut.size() / 2;
        rOut[halfSize] = mid;
        vrtx_create_chunk_edge_recurse(level - 1, a, mid, rOut.prefix(halfSize));
        vrtx_create_chunk_edge_recurse(level - 1, mid, b, rOut.suffix(halfSize));
    }

    void tri_group_resize_fit_ids()
    {
        m_triData.resize(m_triIds.size_required());
        m_triRefCount.resize(m_triIds.size_required() * 4);
    }

    SkTriGroupId tri_group_create(
            uint8_t depth,
            SkTriId parent,
            std::array<std::array<SkVrtxId, 3>, 4> vertices)
    {
        SkTriGroupId const groupId = m_triIds.create();
        tri_group_resize_fit_ids();

        SkTriGroup &rGroup = m_triData[size_t(groupId)];
        rGroup.m_parent = parent;
        rGroup.m_depth = depth;

        for (int i = 0; i < 4; i ++)
        {
            SkeletonTriangle &rTri = rGroup.m_triangles[i];
            rTri.m_children = std::nullopt;
            rTri.m_vertices = vertices[i];

            for (SkVrtxId vrtxId : vertices[i])
            {
                vrtx_refcount_add(vrtxId);
            }
        }
        return groupId;
    }

    SkeletonTriangle& tri_at(SkTriId triId)
    {
        auto groupIndex = size_t(tri_group_id(triId));
        uint8_t siblingIndex = tri_sibling_index(triId);
        return m_triData.at(groupIndex).m_triangles[siblingIndex];
    }

    SkTriGroupId tri_subdiv(SkTriId triId, std::array<SkVrtxId, 3> vrtxMid);

    void tri_group_reserve(size_t n)
    {
        m_triIds.reserve(n);
        m_triData.reserve(m_triIds.capacity());
        m_triRefCount.reserve(m_triIds.capacity() * 4);
    }

    void tri_group_reserve_more(size_t n)
    {
        m_triIds.reserve(n);
        m_triData.reserve(m_triIds.capacity());
        m_triRefCount.reserve(m_triIds.capacity() * 4);
    }

    void tri_refcount_add(SkTriId id) noexcept { m_triRefCount[size_t(id)] ++; };
    void tri_refcount_remove(SkTriId id) noexcept { m_triRefCount[size_t(id)] --; };

private:

    IdRegistry<SkTriGroupId> m_triIds;

    // access using SkTriGroupId from m_triIds
    std::vector<SkTriGroup> m_triData;
    std::vector<uint8_t> m_triRefCount;

}; // class SubdivTriangleSkeleton



}
