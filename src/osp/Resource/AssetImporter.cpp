#include <iostream>

#include <Corrade/Containers/Optional.h>
#include <Corrade/PluginManager/Manager.h>
#include <Magnum/Trade/AbstractImporter.h>
#include <Magnum/Trade/MeshObjectData3D.h>
#include <Magnum/Trade/SceneData.h>
#include <Magnum/Trade/MeshData.h>
#include <Magnum/Trade/TextureData.h>
#include <Magnum/GL/TextureFormat.h>
#include <Magnum/Trade/ImageData.h>
#include <Magnum/ImageView.h>
#include <Magnum/Trade/MaterialData.h>
#include <Magnum/Trade/PbrMetallicRoughnessMaterialData.h>
#include <Magnum/MeshTools/Compile.h>
#include <Magnum/GL/Mesh.h>


#include <MagnumExternal/TinyGltf/tiny_gltf.h>

#include "Package.h"
#include "AssetImporter.h"

using Corrade::Containers::Optional;
using Magnum::Trade::ImageData2D;
using Magnum::Trade::MeshData;
using Magnum::Trade::SceneData;
using Magnum::GL::Texture2D;
using Magnum::GL::Mesh;

namespace osp
{

void osp::AssetImporter::load_sturdy_file(std::string const& filepath, Package& pkg)
{
    PluginManager pluginManager;
    TinyGltfImporter gltfImporter{pluginManager};

    // Open .sturdy.gltf file
    gltfImporter.openFile(filepath);
    if (!gltfImporter.isOpened() || gltfImporter.defaultScene() == -1)
    {
        std::cout << "Error: couldn't open file: " << filepath << "\n";
    }

    load_sturdy(gltfImporter, pkg);

    gltfImporter.close();
}

void osp::AssetImporter::load_part(TinyGltfImporter& gltfImporter,
    Package& pkg, unsigned id)
{
    // It's a part
    std::cout << "PART!\n";

    // Recursively add child nodes to part
    PrototypePart part;
    proto_add_obj_recurse(gltfImporter, pkg, part, 0, id);

    // Parse extra properties
    tinygltf::Node const& node = *static_cast<tinygltf::Node const*>(
        gltfImporter.object3D(id)->importerState());

    tinygltf::Value const& extras = node.extras;
    if (!extras.Has("machines"))
    {
        std::cout << "Error: no machines found in "
            << gltfImporter.object3DName(id) << "!\n";
        return;
    }
    tinygltf::Value const& machines = extras.Get("machines");
    std::cout << "JSON machines!\n";
    auto const& machArray = machines.Get<tinygltf::Value::Array>();

    // Loop through machine configs
            // machArray looks like:
            // [
            //    { "type": "Rocket", stuff... },
            //    { "type": "Control", stuff...}
            // ]
    for (tinygltf::Value const& value : machArray)
    {
        std::string type = value.Get("type").Get<std::string>();
        std::cout << "test: " << type << "\n";

        if (type.empty())
        {
            continue;
        }

        // TODO: more stuff
        PrototypeMachine machine;
        machine.m_type = std::move(type);
        part.get_machines().emplace_back(std::move(machine));
    }

    pkg.add<PrototypePart>(gltfImporter.object3DName(id), std::move(part));
}

void osp::AssetImporter::load_sturdy(TinyGltfImporter& gltfImporter, Package& pkg)
{
    std::cout << "Found " << gltfImporter.object3DCount() << " nodes\n";
    Optional<SceneData> sceneData = gltfImporter.scene(gltfImporter.defaultScene());
    if (!sceneData)
    {
        std::cout << "Error: couldn't load scene data\n";
        return;
    }

    // Loop over and discriminate all top-level nodes
    // Currently, part_* are the only nodes that necessitate special handling
    for (unsigned childID : sceneData->children3D())
    {
        const std::string& nodeName = gltfImporter.object3DName(childID);
        std::cout << "Found node: " << nodeName << "\n";

        if (nodeName.compare(0, 5, "part_") == 0)
        {
            load_part(gltfImporter, pkg, childID);
        }
    }

    // Load all associated mesh data
    // Temporary: eventually if would be preferable to retrieve the mesh names only
    for (unsigned i = 0; i < gltfImporter.meshCount(); i++)
    {
        using Magnum::MeshPrimitive;

        std::string const& meshName = gltfImporter.meshName(i);
        std::cout << "Mesh: " << meshName << "\n";

        Optional<MeshData> meshData = gltfImporter.mesh(i);
        if (!meshData || (meshData->primitive() != MeshPrimitive::Triangles))
        {
            std::cout << "Error: mesh " << meshName
                << " not composed of triangles\n";
            continue;
        }
        pkg.add<MeshData>(meshName, std::move(*meshData));
    }

    // Load all associated image data
    // Temporary: eventually it would be preferable to retrieve the URIs only
    for (unsigned i = 0; i < gltfImporter.textureCount(); i++)
    {
        auto imgID = gltfImporter.texture(i)->image();
        std::string const& imgName = gltfImporter.image2DName(imgID);
        std::cout << "Loading image: " << imgName << "\n";

        Optional<ImageData2D> imgData = gltfImporter.image2D(imgID);
        if (!imgData)
        {
            continue;
        }
        pkg.add<ImageData2D>(imgName, std::move(*imgData));
    }
}

DependRes<ImageData2D> osp::AssetImporter::load_image(
    const std::string& filepath, Package& pkg)
{
    using Magnum::Trade::AbstractImporter;
    using Corrade::PluginManager::Manager;
    using Corrade::Containers::Pointer;

    Manager<AbstractImporter> manager;
    Pointer<AbstractImporter> importer
        = manager.loadAndInstantiate("AnyImageImporter");
    if (!importer || !importer->openFile(filepath))
    {
        std::cout << "Error: could not open file " << filepath << "\n";
        return DependRes<ImageData2D>();
    }

    Optional<ImageData2D> image = importer->image2D(0);
    if (!image)
    {
        std::cout << "Error: could not read image in file " << filepath << "\n";
        return DependRes<ImageData2D>();
    }

    return pkg.add<ImageData2D>(filepath, std::move(*image));
}

DependRes<Mesh> osp::AssetImporter::compile_mesh(
    const DependRes<MeshData> meshData, Package& pkg)
{
    using Magnum::GL::Mesh;

    if (meshData.empty())
    {
        std::cout << "Error: requested MeshData resource \"" << meshData.name()
            << "\" not found\n";
        return DependRes<Mesh>();
    }

    return pkg.add<Mesh>(meshData.name(), Magnum::MeshTools::compile(*meshData));
}

DependRes<Texture2D> osp::AssetImporter::compile_tex(
    const DependRes<ImageData2D> imageData, Package& package)
{
    using Magnum::GL::SamplerWrapping;
    using Magnum::GL::SamplerFilter;
    using Magnum::GL::textureFormat;

    if (imageData.empty())
    {
        std::cout << "Error: requested ImageData2D resource \"" << imageData.name()
            << "\" not found\n";
        return DependRes<Texture2D>();
    }

    Magnum::ImageView2D view = *imageData;

    Magnum::GL::Texture2D tex;
    tex.setWrapping(SamplerWrapping::ClampToEdge)
        .setMagnificationFilter(SamplerFilter::Linear)
        .setMinificationFilter(SamplerFilter::Linear)
        .setStorage(1, textureFormat((*imageData).format()), (*imageData).size())
        .setSubImage(0, {}, view);

    return package.add<Texture2D>(imageData.name(), std::move(tex));
}

//either an appendable package, or
void AssetImporter::proto_add_obj_recurse(TinyGltfImporter& gltfImporter, 
                                           Package& package,
                                           PrototypePart& part,
                                           unsigned parentProtoIndex,
                                           unsigned childGltfIndex)
{
    using Corrade::Containers::Pointer;
    using Corrade::Containers::Optional;
    using Magnum::Trade::ObjectData3D;
    using Magnum::Trade::MeshObjectData3D;
    using Magnum::Trade::ObjectInstanceType3D;
    using Magnum::Trade::MaterialData;
    using Magnum::Trade::MaterialType;
    using Magnum::Trade::PbrMetallicRoughnessMaterialData;

    // Add the object to the prototype
    Pointer<ObjectData3D> childData = gltfImporter.object3D(childGltfIndex);
    std::vector<PrototypeObject>& protoObjects = part.get_objects();
    const std::string& name = gltfImporter.object3DName(childGltfIndex);

    // I think I've been doing too much C
    PrototypeObject obj;
    obj.m_parentIndex = parentProtoIndex;
    obj.m_childCount = childData->children().size();
    obj.m_translation = childData->translation();
    obj.m_rotation = childData->rotation();
    obj.m_scale = childData->scaling();
    obj.m_type = ObjectType::NONE;
    obj.m_name = name;

    std::cout << "Adding obj to Part: " << name << "\n";
    int meshID = childData->instance();

    bool hasMesh = (
            childData->instanceType() == ObjectInstanceType3D::Mesh
            && meshID != -1);

    if (name.compare(0, 4, "col_") == 0)
    {
        // It's a collider
        obj.m_type = ObjectType::COLLIDER;

        // do some stuff here
        obj.m_objectData = ColliderData{ECollisionShape::BOX};

        std::cout << "obj: " << name << " is a collider\n";
    }
    else if (hasMesh)
    {
        // It's a drawable mesh
        const std::string& meshName = gltfImporter.meshName(meshID);
        std::cout << "obj: " << name << " uses mesh: " << meshName << "\n";
        obj.m_type = ObjectType::MESH;

        // The way it's currently set up is that the mesh's names are the same
        // as their resource paths. So the resource path is added to the part's
        // list of strings, and the object's mesh is set to the index to that
        // string.
        obj.m_objectData = DrawableData{
            static_cast<unsigned>(part.get_strings().size())};
        part.get_strings().push_back(meshName);

        MeshObjectData3D& mesh = static_cast<MeshObjectData3D&>(*childData);
        Pointer<MaterialData> mat = gltfImporter.material(mesh.material());

        if (mat->types() & MaterialType::PbrMetallicRoughness)
        {
            const auto& pbr = mat->as<PbrMetallicRoughnessMaterialData>();

            auto imgID = gltfImporter.texture(pbr.baseColorTexture())->image();
            std::string const& imgName =gltfImporter.image2DName(imgID);
            std::cout << "Base Tex: " << imgName << "\n";
            std::get<DrawableData>(obj.m_objectData).m_textures.push_back(
                static_cast<unsigned>(part.get_strings().size()));
            part.get_strings().push_back(imgName);

            if (pbr.hasNoneRoughnessMetallicTexture())
            {
                imgID = gltfImporter.texture(pbr.metalnessTexture())->image();
                std::cout << "Metal/rough texture: "
                    << gltfImporter.image2DName(imgID) << "\n";
            } else
            {
                std::cout << "No metal/rough texture found for " << name << "\n";
            }
            
        } else
        {
            std::cout << "Error: unsupported material type\n";
        }
    }

    int objIndex = protoObjects.size();
    protoObjects.push_back(std::move(obj));

    for (unsigned childId: childData->children())
    {
        proto_add_obj_recurse(gltfImporter, package, part, objIndex, childId);
    }
}

}
