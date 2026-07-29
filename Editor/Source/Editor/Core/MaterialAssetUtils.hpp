#pragma once

// Demo/builder-side material authoring: meshes are coloured through REAL material assets in slots
// (UE-style), never through the per-entity MaterialComponent override channel — that channel is a
// runtime-only script seed, not an authoring surface.

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Runtime/Services/Material/MaterialService.hpp>

#include <Common/Core/Constants.hpp>
#include <Common/Core/UUID.hpp>
#include <Common/Utilities/FileSystem.hpp>

// rfl serialization environment (same as SurfaceMaterialAsset.cpp) — needed to write a fresh
// material file with its stable GUID before the asset is created/registered.
#include <Engine/Core/Serialize/GLMReflect.hpp>
#include <Engine/Core/Serialize/CustomReflect.hpp>
#include <rflcpp/rfl/json.hpp>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <system_error>
#include <utility>

namespace Desert::Editor::MaterialAssetUtils
{
    // Creates (or reuses, by name) a StaticMeshPBR material ASSET (.demat) carrying the given
    // schema params, registers its shell with the MaterialService (runtime material builds lazily
    // on first Get, so this is safe before shaders are preloaded) and returns the handle to drop
    // into a mesh material slot. An EXISTING file is never rewritten — user edits to a demo
    // material survive restarts.
    inline Assets::AssetHandle
    CreatePBRMaterialAsset( const Assets::AssetManager* am, const std::string& name,
                            std::initializer_list<std::pair<const char*, glm::vec4>> params )
    {
        if ( !am )
            return Common::UUID::Null();

        const std::string           ext = Common::Constants::Extensions::MATERIAL_EXTENSION;
        const std::filesystem::path dir = Common::Constants::Path::MATERIAL_PATH;
        std::error_code             ec;
        std::filesystem::create_directories( dir, ec );
        const std::filesystem::path path = dir / ( name + ext );

        if ( auto existing = am->FindByPath<Assets::SurfaceMaterialAsset>( path.generic_string() ) )
        {
            Runtime::ResourceRegistry::GetMaterialService()->RegisterAsset( existing );
            return existing->GetMetadata().Handle;
        }

        if ( !std::filesystem::exists( path, ec ) )
        {
            Assets::MaterialData data;
            data.MaterialId = Common::UUID();
            for ( const auto& [pname, value] : params )
                data.SetParam( pname, value );
            Common::Utils::FileSystem::WriteContentToFile( path.generic_string(),
                                                           rfl::json::write( data ) );
        }

        auto asset = const_cast<Assets::AssetManager&>( *am ).CreateAsset<Assets::SurfaceMaterialAsset>(
             Assets::AssetPriority::High, path.generic_string() );
        if ( !asset )
            return Common::UUID::Null();

        Runtime::ResourceRegistry::GetMaterialService()->RegisterAsset( asset );
        return asset->GetMetadata().Handle;
    }

    inline Assets::AssetHandle CreatePBRMaterialAsset( const Assets::AssetManager* am,
                                                       const std::string& name, const glm::vec4& albedo,
                                                       float roughness )
    {
        return CreatePBRMaterialAsset(
             am, name,
             { { "AlbedoColor", albedo }, { "RoughnessFactor", glm::vec4( roughness, 0.0f, 0.0f, 0.0f ) } } );
    }
} // namespace Desert::Editor::MaterialAssetUtils
