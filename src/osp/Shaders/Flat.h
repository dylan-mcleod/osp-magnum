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


#include <osp/Active/opengl/SysRenderGL.h>

#include <Magnum/Shaders/FlatGL.h>

namespace osp::shader
{

using FlatGL3D = Magnum::Shaders::FlatGL3D;


struct ACtxDrawFlat
{
    FlatGL3D                    shaderUntextured    {Corrade::NoCreate};
    FlatGL3D                    shaderDiffuse       {Corrade::NoCreate};

    active::DrawTransforms_t    *pDrawTf            {nullptr};
    active::DrawEntColors_t     *pColor             {nullptr};
    active::TexGlEntStorage_t   *pDiffuseTexId      {nullptr};
    active::MeshGlEntStorage_t  *pMeshId            {nullptr};

    active::TexGlStorage_t      *pTexGl             {nullptr};
    active::MeshGlStorage_t     *pMeshGl            {nullptr};

    active::MaterialId materialId { lgrn::id_null<active::MaterialId>() };

    constexpr void assign_pointers(active::ACtxSceneRender&     rScnRender,
                                   active::ACtxSceneRenderGL&   rScnRenderGl,
                                   active::RenderGL&            rRenderGl) noexcept
    {
        pDrawTf         = &rScnRender   .m_drawTransform;
        pColor          = &rScnRender   .m_color;
        pDiffuseTexId   = &rScnRenderGl .m_diffuseTexId;
        pMeshId         = &rScnRenderGl .m_meshId;
        pTexGl          = &rRenderGl    .m_texGl;
        pMeshGl         = &rRenderGl    .m_meshGl;
    }
};

void draw_ent_flat(
        active::DrawEnt ent,
        active::ViewProjMatrix const& viewProj,
        active::EntityToDraw::UserData_t userData) noexcept;

struct ArgsForSyncDrawEntFlat
{
    active::DrawEntSet_t const&             hasMaterial;
    active::RenderGroup::Storage_t *const   pStorageOpaque;
    active::RenderGroup::Storage_t *const   pStorageTransparent;
    active::DrawEntSet_t const&             opaque;
    active::DrawEntSet_t const&             transparent;
    active::TexGlEntStorage_t const&        diffuse;
    ACtxDrawFlat&                           rData;
};

inline void sync_drawent_flat(active::DrawEnt ent, ArgsForSyncDrawEntFlat const args)
{
    using namespace osp::active;

    auto const entInt = std::size_t(ent);

    bool const hasMaterial = args.hasMaterial.test(entInt);
    bool const hasTexture = (args.diffuse.size() > std::size_t(ent)) && (args.diffuse[ent].m_glId != lgrn::id_null<TexGlId>());

    FlatGL3D *pShader = hasTexture
                      ? &args.rData.shaderDiffuse
                      : &args.rData.shaderUntextured;

    if (args.pStorageTransparent != nullptr)
    {
        auto value = (hasMaterial && args.transparent.test(entInt))
                   ? std::make_optional(EntityToDraw{&draw_ent_flat, {&args.rData, pShader}})
                   : std::nullopt;

        storage_assign(*args.pStorageTransparent, ent, std::move(value));
    }

    if (args.pStorageOpaque != nullptr)
    {
        auto value = (hasMaterial && args.opaque.test(entInt))
                   ? std::make_optional(EntityToDraw{&draw_ent_flat, {&args.rData, pShader}})
                   : std::nullopt;

        storage_assign(*args.pStorageOpaque, ent, std::move(value));
    }
}

template<typename ITA_T, typename ITB_T>
void sync_drawent_flat(
        ITA_T const&                    first,
        ITB_T const&                    last,
        ArgsForSyncDrawEntFlat const    args)
{
    std::for_each(first, last, [&args] (active::DrawEnt const ent)
    {
        sync_drawent_flat(ent, args);
    });
}


} // namespace osp::shader
