#pragma once

#include <Editor/Core/AssetOpenRequest.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Runtime/Services/Material/MaterialService.hpp>

#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>

#include <filesystem>
#include <string>
#include <system_error>

namespace Desert::Editor
{
    // "Open the Material Editor on the `.demat` at this PATH."
    //
    // One implementation for the two callers that have a path rather than a handle — the asset browser's
    // double-click, and EditorLayer's boot-time --open-panel resolution. Written once because resolving a
    // material file to a REGISTERED asset has three steps that are easy to get subtly different (find,
    // create-and-load if this is the first ask, register the shell), and two copies of it would be the
    // two-implementations-of-one-quantity shape this engine keeps paying for.
    //
    // The request carries a HANDLE and not the path, because the handle is the document's identity: it is
    // what open-or-focus is keyed on and what the window's ImGui id is built from. So the resolution has to
    // happen on this side of the wire.
    //
    // The three outcomes are kept apart because the two callers need different ones. EditorLayer's
    // --open-panel is GUESSING at what a string means, so it has to be able to try and move on — but only
    // when the string was never a material to begin with. A `.demat` that failed to resolve has already been
    // reported here, and a caller that then reported it again in its own words would print two messages, the
    // second of them wrong.
    enum class MaterialDocumentRequest
    {
        NotAMaterialPath, // the string names no `.demat` on disk; nothing was logged, nothing was wrong
        Failed,           // it IS a material file and it would not resolve — logged, with the path
        Requested,        // queued; the window appears on the next frame
    };

    inline MaterialDocumentRequest RequestMaterialDocument( Assets::AssetManager* assetManager,
                                                            const std::string&    assetPath )
    {
        if ( !assetManager )
            return MaterialDocumentRequest::NotAMaterialPath;

        // Checked BEFORE anything is created: AssetManager will happily mint a record for a path with no
        // file behind it, so without this a mistyped argument would open an empty document instead of
        // producing the error that names what it could have meant.
        std::error_code ec;
        const auto      path = std::filesystem::path( assetPath );
        if ( path.extension() != Common::Constants::Extensions::MATERIAL_EXTENSION ||
             !std::filesystem::exists( path, ec ) )
        {
            return MaterialDocumentRequest::NotAMaterialPath;
        }

        auto asset = assetManager->FindByPath<Assets::SurfaceMaterialAsset>( assetPath );
        if ( !asset )
        {
            // First time anything asked for this file: the same create-if-missing the material thumbnail row
            // does, and the component deserializer on a cold start.
            asset =
                 assetManager->CreateAsset<Assets::SurfaceMaterialAsset>( Assets::AssetPriority::High, assetPath );
            if ( asset && !asset->IsReadyForUse() )
                asset->Load();
        }

        if ( !asset )
        {
            LOG_ERROR( "[Assets] '{}' could not be opened as a material — no Material Editor window was "
                       "created.",
                       assetPath );
            return MaterialDocumentRequest::Failed;
        }

        // The window draws the material through the per-slot route, which resolves through the material
        // service. Registered LAZILY (the shell only): this runs at boot as well as from a click, and the
        // runtime material binds textures, which needs shaders that may not be loaded yet. The first Get
        // builds it.
        if ( auto* materialService = Runtime::ResourceRegistry::GetMaterialService() )
        {
            if ( !materialService->Get( asset->GetMetadata().Handle ) )
                materialService->RegisterAsset( asset );
        }

        Core::AssetOpenRequests::Request( asset->GetMetadata().Handle, Assets::AssetTypeID::Material );
        return MaterialDocumentRequest::Requested;
    }
} // namespace Desert::Editor
