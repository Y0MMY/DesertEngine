// reflect-cpp MUST be included before any header that pulls <windows.h>, otherwise the min/max
// macros break its templates (C2589). NOMINMAX guards the rest of the TU.
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <rflcpp/rfl.hpp>
#include <Editor/Core/DragPayloads.hpp>
#include <rflcpp/rfl/json.hpp>

#include "CollectionsPanel.hpp"

#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Import/MeshDnD.hpp>
#include <Editor/Import/MeshMaterial.hpp>
#include <Editor/Import/ImportManager.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/PBRMaterialAsset.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <system_error>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui; // engine headers introduce a Desert::ImGui that would otherwise shadow ::ImGui

    namespace
    {
        // Resources/Assets/Collections — see Common::Constants::Path::COLLECTIONS_PATH.
        const std::string k_CollectionsRoot = Common::Constants::Path::COLLECTIONS_PATH.generic_string();

        // On-disk manifest shape (collection.json). Optional fields tolerate missing keys.
        struct ManifestItem
        {
            std::string                Name;
            std::optional<std::string> Category;
            std::string                Mesh; // working-dir-relative source path
            std::optional<std::string> Thumbnail;
            std::optional<int>         Material; // index into Manifest::Materials (the mesh's PBR material)
        };

        // A PBR material the splitter detected from the pack's texture files (paths by filename suffix). The
        // editor materializes these into real .demat assets (the engine owns that format; the tool stays
        // engine-free). Cutout/foliage carries AlphaCutoff (TwoSided is reserved for a future shader feature).
        struct ManifestMaterial
        {
            std::string                Name;
            std::optional<std::string> Albedo, Opacity, Normal, Roughness, Metallic, AO;
            std::optional<float>       AlphaCutoff;
            std::optional<bool>        TwoSided;
        };

        struct Manifest
        {
            std::string                             Name;
            std::optional<std::string>              Author;
            std::optional<std::vector<ManifestMaterial>> Materials;
            std::vector<ManifestItem>               Items;
        };

        std::string ToLower( std::string s )
        {
            std::transform( s.begin(), s.end(), s.begin(),
                            []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
            return s;
        }

        // Filesystem-safe material file name (the splitter already emits clean stems; this is belt-and-braces).
        std::string SanitizeName( const std::string& in )
        {
            std::string out;
            out.reserve( in.size() );
            for ( char c : in )
                out += ( std::isalnum( static_cast<unsigned char>( c ) ) || c == '-' || c == '_' ) ? c : '_';
            return out.empty() ? std::string( "material" ) : out;
        }

        // Turn the manifest's detected materials into real .demat assets next to the meshes, so the existing
        // sidecar resolution (MeshMaterial::ResolveSidecar) binds them to every mesh — for both the offscreen
        // previews and dropping a card into the scene. We write into meshes/ (ResolveSidecar's "any .demat in
        // the mesh folder" fallback picks it up). Only MISSING files are written so user edits in the Material
        // panel are never clobbered. NOTE: a single shared material per collection is the atlas case (one
        // diffuse, many cards); multi-material packs would need per-mesh resolution (future, via the Material
        // index already carried here).
        void MaterializeMaterials( Assets::AssetManager& mgr, ImportManager& importer,
                                   const std::filesystem::path& collectionDir, const Manifest& manifest )
        {
            if ( !manifest.Materials || manifest.Materials->empty() )
                return;

            std::error_code             ec;
            const std::filesystem::path meshDir = collectionDir / "meshes";
            std::filesystem::create_directories( meshDir, ec );

            // Resolve a texture source path -> a registered TextureAsset handle (cooks the .tex if needed +
            // registers). Texture handles are deterministic (TextureImporter), so the handle is the same even
            // after a Cooked/ wipe -> existing .demat references stay valid.
            auto resolveTex = [&]( const std::optional<std::string>& path ) -> Assets::AssetHandle
            {
                if ( !path || path->empty() )
                    return Common::UUID::Null();
                return importer.ImportAndRegisterTexture( mgr, *path );
            };

            for ( const auto& mat : *manifest.Materials )
            {
                // ALWAYS (re)cook + register the manifest's textures, even if the .demat already exists —
                // otherwise a deleted Cooked/ leaves the material's texture references dangling ("missing")
                // because nothing re-cooks them. The .demat WRITE below is still skip-if-exists.
                const Assets::AssetHandle albedo    = resolveTex( mat.Albedo );
                const Assets::AssetHandle normal    = resolveTex( mat.Normal );
                const Assets::AssetHandle roughness = resolveTex( mat.Roughness );
                const Assets::AssetHandle metallic  = resolveTex( mat.Metallic );
                const Assets::AssetHandle ao        = resolveTex( mat.AO );
                const Assets::AssetHandle opacity   = resolveTex( mat.Opacity );

                const std::filesystem::path dematPath =
                     meshDir / ( SanitizeName( mat.Name ) + Common::Constants::Extensions::MATERIAL_EXTENSION );
                if ( std::filesystem::exists( dematPath, ec ) )
                    continue; // keep the existing (possibly user-edited) .demat; its handles already match

                // loadAfterCreate=false: the file doesn't exist yet; reflected defaults are valid in memory.
                auto asset = mgr.CreateAsset<Assets::PBRMaterialAsset>(
                     Assets::AssetPriority::High, dematPath.generic_string(), false );
                if ( !asset )
                {
                    LOG_WARN( "[Collections] Could not create material asset {}", dematPath.string() );
                    continue;
                }

                auto& d            = asset->Data();
                d.AlbedoTexture    = albedo;
                d.NormalTexture    = normal;
                d.RoughnessTexture = roughness;
                d.MetallicTexture  = metallic;
                d.AOTexture        = ao;
                d.OpacityTexture   = opacity;
                d.AlphaCutoff      = mat.AlphaCutoff.value_or( mat.Opacity ? 0.5f : 0.0f );

                Common::Utils::FileSystem::WriteContentToFile( dematPath, asset->Save() );
                Runtime::ResourceRegistry::GetMaterialService()->Register( asset );
                LOG_INFO( "[Collections] Materialized '{}' -> {}", mat.Name, dematPath.generic_string() );
            }
        }
    } // namespace

    CollectionsPanel::CollectionsPanel( Assets::AssetManager* assetManager )
         : IPanel( "Collections" ), m_AssetManager( assetManager )
    {
        m_UIHelper = std::make_unique<UI::UIHelper>();
        m_UIHelper->Init();
        m_Thumbs         = std::make_unique<ThumbnailCache>();
        m_ImportManager  = std::make_unique<ImportManager>();
        Rescan();
    }

    void CollectionsPanel::Rescan()
    {
        m_Collections.clear();
        m_Categories.clear();
        m_Categories.emplace_back( "All" );

        std::error_code ec;
        const std::filesystem::path root( k_CollectionsRoot );
        if ( !std::filesystem::exists( root, ec ) )
            return;

        for ( const auto& dir : std::filesystem::directory_iterator( root, ec ) )
        {
            if ( !dir.is_directory() )
                continue;
            const auto manifestPath = dir.path() / "collection.json";
            if ( !std::filesystem::exists( manifestPath, ec ) )
                continue;

            const auto raw     = Common::Utils::FileSystem::ReadFileContent( manifestPath );
            const auto parsed  = rfl::json::read<Manifest>( raw );
            if ( !parsed.has_value() )
            {
                LOG_WARN( "[Collections] Failed to parse {}: {}", manifestPath.string(),
                          parsed.error().what() );
                continue;
            }

            const auto&      m = parsed.value();

            // Materialize the manifest's detected materials into .demat files (once) so meshes resolve them.
            if ( m_AssetManager && m_ImportManager )
                MaterializeMaterials( *m_AssetManager, *m_ImportManager, dir.path(), m );

            LoadedCollection lc;
            lc.Name   = m.Name.empty() ? dir.path().filename().string() : m.Name;
            lc.Author = m.Author.value_or( "" );
            for ( const auto& it : m.Items )
            {
                CollectionItem ci;
                ci.Name      = it.Name;
                ci.Category  = it.Category.value_or( "Uncategorized" );
                ci.MeshPath  = it.Mesh;
                ci.Thumbnail = it.Thumbnail.value_or( "" );
                if ( std::find( m_Categories.begin(), m_Categories.end(), ci.Category ) == m_Categories.end() )
                    m_Categories.push_back( ci.Category );
                lc.Items.push_back( std::move( ci ) );
            }
            m_Collections.push_back( std::move( lc ) );
        }
    }

    void CollectionsPanel::OnUIRender()
    {
        if ( !m_SowPanel )
            return;

        // Drive any in-flight offscreen preview render (one capture at a time, over a few frames).
        if ( m_ThumbRenderer )
            m_ThumbRenderer->Tick();

        if ( ImGui::Begin( m_PanelName.c_str(), &m_SowPanel ) )
        {
            if ( ImGui::Button( ICON_MDI_REFRESH " Rescan" ) )
            {
                Rescan();
                m_OpenCollection = -1;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth( 220.0f );
            ImGui::InputTextWithHint( "##collsearch", ICON_MDI_MAGNIFY " Search...", m_Search,
                                      sizeof( m_Search ) );

            if ( m_OpenCollection < 0 || m_OpenCollection >= (int)m_Collections.size() )
            {
                m_OpenCollection = -1;
                DrawCollectionList(); // top level: the collections themselves
            }
            else
            {
                DrawCollectionContents( m_Collections[m_OpenCollection] ); // inside one collection: its meshes
            }
        }
        ImGui::End();
    }

    // Reusable wrapping grid; calls drawCell(index) for each cell. Returns nothing.
    template <typename Fn>
    static void DrawGrid( float cardW, float spacing, int count, Fn&& drawCell )
    {
        const float avail   = ImGui::GetContentRegionAvail().x;
        const int   columns = std::max( 1, (int)( ( avail + spacing ) / ( cardW + spacing ) ) );
        ImGui::BeginChild( "##cgrid", ImVec2( 0, 0 ), false );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 7.0f );
        int col = 0;
        for ( int i = 0; i < count; ++i )
        {
            ImGui::PushID( i );
            ImGui::BeginGroup();
            drawCell( i );
            ImGui::EndGroup();
            ImGui::PopID();
            if ( ++col % columns != 0 )
                ImGui::SameLine( 0.0f, spacing );
            else
                col = 0;
        }
        ImGui::PopStyleVar();
        ImGui::EndChild();
    }

    void CollectionsPanel::DrawCollectionList()
    {
        ImGui::Separator();
        if ( m_Collections.empty() )
        {
            ImGui::TextDisabled( "No collections found." );
            ImGui::TextDisabled( "Drop a pack into Resources/Collections/<Name>/ with a collection.json" );
            return;
        }

        const std::string searchLower = ToLower( m_Search );
        const float       cardW       = 116.0f;

        // Build the filtered index list first so the grid is contiguous.
        std::vector<int> shown;
        for ( int i = 0; i < (int)m_Collections.size(); ++i )
            if ( searchLower.empty() ||
                 ToLower( m_Collections[i].Name ).find( searchLower ) != std::string::npos )
                shown.push_back( i );

        DrawGrid( cardW, 12.0f, (int)shown.size(),
                  [&]( int k )
                  {
                      const int   ci   = shown[k];
                      const auto& coll = m_Collections[ci];

                      // Cover = the first item's rendered preview; folder icon if the collection is empty.
                      if ( !coll.Items.empty() )
                          DrawCard( coll.Items[0], cardW, cardW );
                      else
                          ImGui::Button( ICON_MDI_FOLDER, ImVec2( cardW, cardW ) );

                      if ( ImGui::IsItemClicked() )
                          m_OpenCollection = ci;
                      if ( ImGui::IsItemHovered() )
                          ImGui::SetTooltip( "%s\n%zu items", coll.Name.c_str(), coll.Items.size() );

                      ImGui::PushTextWrapPos( ImGui::GetCursorPosX() + cardW );
                      ImGui::TextUnformatted( coll.Name.c_str() );
                      ImGui::PopTextWrapPos();
                      ImGui::TextDisabled( "%zu items", coll.Items.size() );
                  } );
    }

    void CollectionsPanel::DrawCollectionContents( const LoadedCollection& coll )
    {
        ImGui::SameLine();
        if ( ImGui::Button( ICON_MDI_ARROW_LEFT " Collections" ) )
        {
            m_OpenCollection = -1;
            return;
        }
        ImGui::SameLine();
        ImGui::TextDisabled( "/" );
        ImGui::SameLine();
        ImGui::TextUnformatted( coll.Name.c_str() );
        ImGui::Separator();

        const std::string searchLower = ToLower( m_Search );
        const float       cardW       = 104.0f;

        std::vector<int> shown;
        for ( int i = 0; i < (int)coll.Items.size(); ++i )
            if ( searchLower.empty() ||
                 ToLower( coll.Items[i].Name ).find( searchLower ) != std::string::npos )
                shown.push_back( i );

        DrawGrid( cardW, 12.0f, (int)shown.size(),
                  [&]( int k )
                  {
                      const auto& item = coll.Items[shown[k]];
                      DrawCard( item, cardW, cardW );

                      if ( ImGui::BeginDragDropSource( ImGuiDragDropFlags_None ) )
                      {
                          // Same payload the File Explorer emits, so mesh drop targets (Foliage etc.) work.
                          ImGui::SetDragDropPayload( ::Desert::Editor::DragPayloads::MeshAsset, item.MeshPath.c_str(),
                                                     item.MeshPath.size() + 1 );
                          ImGui::Text( ICON_MDI_CUBE_OUTLINE " %s", item.Name.c_str() );
                          ImGui::EndDragDropSource();
                      }
                      if ( ImGui::IsItemHovered() )
                          ImGui::SetTooltip( "%s\n[%s]\n%s", item.Name.c_str(), item.Category.c_str(),
                                             item.MeshPath.c_str() );

                      ImGui::PushTextWrapPos( ImGui::GetCursorPosX() + cardW );
                      ImGui::TextUnformatted( item.Name.c_str() );
                      ImGui::PopTextWrapPos();
                  } );
    }

    void CollectionsPanel::DrawCard( const CollectionItem& item, float cardW, float imgH )
    {
        const ImVec2      img( cardW, imgH );
        const std::string pngPath = ThumbnailCache::DiskPath( item.MeshPath );

        // Rendered preview already on disk + decoded? Show it.
        std::error_code ec;
        if ( m_UIHelper && m_Thumbs && std::filesystem::exists( pngPath, ec ) )
        {
            if ( auto image = m_Thumbs->Get( pngPath ) )
            {
                m_UIHelper->ImageButton( "##cthumb", image, img );
                return;
            }
        }

        // No preview yet: resolve+cook the mesh once (cached), queue an offscreen render, and show a
        // placeholder icon meanwhile. Skinned/failed sources resolve to null -> just the icon, no render.
        Assets::AssetHandle handle = Common::UUID::Null();
        if ( m_AssetManager )
        {
            auto it = m_ResolvedMesh.find( item.MeshPath );
            if ( it != m_ResolvedMesh.end() )
                handle = it->second;
            else
                handle = m_ResolvedMesh[item.MeshPath] =
                     MeshDnD::ResolveOrImport( *m_AssetManager, item.MeshPath );
        }

        if ( static_cast<uint64_t>( handle ) != 0 )
        {
            if ( !m_ThumbRenderer )
                m_ThumbRenderer = std::make_unique<AssetThumbnailRenderer>();
            if ( !m_ThumbRenderer->HasPending() )
                m_ThumbRenderer->RequestMesh( handle, pngPath,
                                              MeshMaterial::ResolveSidecar( *m_AssetManager, item.MeshPath ) );
        }

        ImGui::Button( ICON_MDI_CUBE_OUTLINE, img );
    }
} // namespace Desert::Editor
