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
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Assets/Mesh/PBRSurfaceParams.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
// The reflection rules for glm types and for the .demat schema. Needed HERE because this file now
// reads a .demat back — it has to recover the material's existing identity so re-cooking one produces
// the same bytes, and a cook that is not reproducible cannot be compared against a record.
#include <Engine/Assets/Serialization/Material.hpp>
#include <Engine/Core/Serialize/CustomReflect.hpp>
#include <Engine/Core/Serialize/GLMReflect.hpp>

#include <Common/Utilities/ContentManifest.hpp>
#include <Common/Utilities/ContentUpdate.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Utilities/PakFile.hpp>
#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <system_error>
#include <utility>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui; // engine headers introduce a Desert::ImGui that would otherwise shadow ::ImGui

    namespace
    {
        // Project collections root. A FUNCTION, not a static: the constant is remapped to the current
        // project at startup, and a namespace-scope capture would run before that remap.
        std::string CollectionsRoot()
        {
            return Common::Constants::Path::COLLECTIONS_PATH.generic_string();
        }

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

        // The install record for one collection: what this materializer last handed the project.
        //
        // An UNREADABLE record is an ERROR, not an empty one. Reading a damaged record as empty would
        // make every file look locally added and freeze the collection for ever, which is the failure
        // the record exists to end — so it is reported and the collection is left alone until somebody
        // deletes the record and lets it be rebuilt.
        Common::ResultStr<Common::Utils::ContentManifest>
        ReadInstallRecord( const std::filesystem::path& recordPath )
        {
            const auto raw = Common::Utils::FileSystem::ReadFileContent( recordPath );
            if ( !raw )
                return Common::MakeFormattedError<Common::Utils::ContentManifest>( "{}", raw.GetError() );
            return Common::Utils::ContentManifest::Parse( raw.GetValue() );
        }

        // Turn the manifest's detected materials into real .demat assets next to the meshes, so the existing
        // sidecar resolution (MeshMaterial::ResolveSidecar) binds them to every mesh — for both the offscreen
        // previews and dropping a card into the scene. We write into meshes/ (ResolveSidecar's "any .demat in
        // the mesh folder" fallback picks it up). NOTE: a single shared material per collection is the atlas
        // case (one diffuse, many cards); multi-material packs would need per-mesh resolution (future, via
        // the Material index already carried here).
        //
        // THIS USED TO SKIP ON `exists()`, AND THAT WAS WRONG IN BOTH DIRECTIONS. The comment promised
        // "only MISSING files are written so user edits are never clobbered", but comparing the
        // EXISTENCE of a path cannot tell a user edit from a file we wrote ourselves last run — its own
        // "possibly user-edited" admitted as much. So the promise was half-kept and half-broken: edits
        // did survive, and so did every stale material. A corrected AlphaCutoff or a fixed texture
        // mapping in an updated collection.json was ignored, silently and permanently, for anyone who
        // had opened that collection once. The only cure was deleting the file by hand.
        //
        // The record is what makes the promise true rather than aspirational: with "what we last wrote"
        // written down, "the person changed it" and "the source changed it" become two different
        // questions with two different answers. The matrix and the all-or-nothing refusal live in
        // Common/Utilities/ContentUpdate.hpp, shared with the game-patch path — this function supplies
        // the three manifests and the bytes, and takes no policy decision of its own.
        void MaterializeMaterials( Assets::AssetManager& mgr, ImportManager& importer,
                                   const std::filesystem::path& collectionDir, const Manifest& manifest )
        {
            if ( !manifest.Materials || manifest.Materials->empty() )
                return;

            std::error_code             ec;
            const std::filesystem::path meshDir = collectionDir / "meshes";
            std::filesystem::create_directories( meshDir, ec );

            const std::filesystem::path recordPath =
                 collectionDir / std::filesystem::path( Common::Utils::kInstallRecordFileName );
            const bool hasRecord = std::filesystem::exists( recordPath, ec );

            Common::Utils::ContentManifest recorded;
            if ( hasRecord )
            {
                auto read = ReadInstallRecord( recordPath );
                if ( !read )
                {
                    LOG_ERROR( "[Collections] {} could not be read ({}), so no material in this collection "
                               "can be updated without risking someone's edits. Delete it to start the "
                               "record over.",
                               recordPath.generic_string(), read.GetError() );
                    return;
                }
                recorded = read.ExtractValue();
            }

            // Resolve a texture source path -> a registered TextureAsset handle (cooks the .tex if needed +
            // registers). Texture handles are deterministic (TextureImporter), so the handle is the same even
            // after a Cooked/ wipe -> existing .demat references stay valid.
            auto resolveTex = [&]( const std::optional<std::string>& path ) -> Assets::AssetHandle
            {
                if ( !path || path->empty() )
                    return Common::UUID::Null();
                return importer.ImportAndRegisterTexture( mgr, *path );
            };

            Common::Utils::ContentManifest                            incoming;
            Common::Utils::ContentManifest                            onDisk;
            std::vector<std::pair<std::string, Assets::MaterialData>> cooked; // key -> what we would write
            std::vector<std::string>                                  cookedBytes;

            for ( const auto& mat : *manifest.Materials )
            {
                // ALWAYS (re)cook + register the manifest's textures, whatever happens to the .demat —
                // otherwise a deleted Cooked/ leaves the material's texture references dangling ("missing")
                // because nothing re-cooks them.
                const Assets::AssetHandle albedo    = resolveTex( mat.Albedo );
                const Assets::AssetHandle normal    = resolveTex( mat.Normal );
                const Assets::AssetHandle roughness = resolveTex( mat.Roughness );
                const Assets::AssetHandle metallic  = resolveTex( mat.Metallic );
                const Assets::AssetHandle ao        = resolveTex( mat.AO );
                const Assets::AssetHandle opacity   = resolveTex( mat.Opacity );

                const std::string key = "meshes/" + SanitizeName( mat.Name ) +
                                        std::string( Common::Constants::Extensions::MATERIAL_EXTENSION );
                const std::filesystem::path dematPath = collectionDir / std::filesystem::path( key );

                // THE COOK HAS TO BE DETERMINISTIC OR THE COMPARISON MEANS NOTHING. MaterialId is a random
                // UUID, so re-cooking a material with a fresh one would produce different bytes every run
                // and read as "the source changed it" for ever — and would renumber an identity that
                // meshes and scenes already reference. The existing file's own id is therefore reused.
                //
                // exists() still appears, but it is no longer the DECISION — it only keeps the read
                // quiet. The read primitive is soft on purpose and logs an error for a file that is not
                // there, and on a first install every material in the collection is legitimately absent.
                std::optional<Common::UUID> identity;
                std::string                 diskBytes;
                if ( std::filesystem::exists( dematPath, ec ) )
                {
                    if ( const auto existing = Common::Utils::FileSystem::ReadFileContent( dematPath ); existing )
                        diskBytes = existing.GetValue();
                }
                if ( !diskBytes.empty() )
                {
                    onDisk.Insert( { key, static_cast<uint64_t>( diskBytes.size() ),
                                     Common::Utils::PakContentHash( diskBytes.data(), diskBytes.size() ) } );
                    if ( const auto parsed = rfl::json::read<Assets::MaterialData>( diskBytes );
                         parsed.has_value() )
                        identity = parsed.value().MaterialId;
                }

                Assets::PBRSurfaceParams p;
                p.AlbedoTexture    = albedo;
                p.NormalTexture    = normal;
                p.RoughnessTexture = roughness;
                p.MetallicTexture  = metallic;
                p.AOTexture        = ao;
                p.OpacityTexture   = opacity;
                p.AlphaCutoff      = mat.AlphaCutoff.value_or( mat.Opacity ? 0.5f : 0.0f );
                p.MaterialId       = identity ? *identity : Common::UUID::Generate();

                Assets::MaterialData data  = p.ToMaterialData();
                std::string          bytes = rfl::json::write( data );
                incoming.Insert( { key, static_cast<uint64_t>( bytes.size() ),
                                   Common::Utils::PakContentHash( bytes.data(), bytes.size() ) } );
                cooked.emplace_back( key, std::move( data ) );
                cookedBytes.push_back( std::move( bytes ) );
            }

            // EVERY COLLECTION THAT EXISTS TODAY PREDATES THE RECORD, and with no record every one of its
            // materials reads as "the person added this" and is never touched again — the very freeze
            // being fixed, preserved for exactly the projects that have the problem. So the first run
            // adopts what it can prove is ours: the materials whose bytes already equal what the source
            // is offering. Anything that differs stays untouched, because nothing on disk can tell an
            // edit from an older release.
            if ( !hasRecord )
                recorded = Common::Utils::AdoptUnrecordedInstall( onDisk, incoming );

            // onDisk is built from the keys this materializer owns, NOT by walking the collection: a
            // directory walk would see every mesh and texture as a file the person added, and one of them
            // could then be reported as a conflict for a material that has nothing to do with it.
            const auto plan = Common::Utils::PlanContentUpdate(
                 recorded, onDisk, incoming, Common::Utils::ContentAuthorship::LocallyAuthored );
            if ( !plan.CanApply() )
            {
                // Refusal by default, with the files named. The dialog that should offer the choice
                // belongs to the collection-update task, not here; until it exists, doing nothing and
                // saying which files disagree is the answer that cannot destroy work.
                std::string names;
                for ( const auto& key : plan.Conflicts )
                    names += ( names.empty() ? "" : ", " ) + key;
                LOG_ERROR( "[Collections] {} was NOT updated: {} material(s) were changed both here and in "
                           "the collection ({}). Nothing was written.",
                           collectionDir.filename().string(), plan.Conflicts.size(), names );
                return;
            }

            const auto applied =
                 Common::Utils::ApplyContentUpdate( plan, recorded, incoming, collectionDir,
                                                    [&]( const std::string& key ) -> std::optional<std::string>
                                                    {
                                                        for ( size_t i = 0; i < cooked.size(); ++i )
                                                            if ( cooked[i].first == key )
                                                                return cookedBytes[i];
                                                        return std::nullopt;
                                                    } );
            if ( !applied )
            {
                LOG_ERROR( "[Collections] {} was NOT updated: {}", collectionDir.filename().string(),
                           applied.GetError() );
                return;
            }

            // Register only what was actually written, exactly as before: a material in the service under
            // a handle whose .demat does not hold those values renders from numbers no future run can
            // reload. loadAfterCreate=false is still right — the file on disk now holds precisely the
            // MaterialData set below, because that is the value it was serialized from.
            for ( const auto& step : plan.Steps )
            {
                if ( step.Action != Common::Utils::ContentAction::Write )
                    continue;
                const auto at = std::find_if( cooked.begin(), cooked.end(),
                                              [&step]( const auto& c ) { return c.first == step.Key; } );
                if ( at == cooked.end() )
                    continue;
                const std::filesystem::path dematPath = collectionDir / std::filesystem::path( step.Key );
                auto asset = mgr.CreateAsset<Assets::SurfaceMaterialAsset>( Assets::AssetPriority::High,
                                                                            dematPath.generic_string(), false );
                if ( !asset )
                {
                    LOG_WARN( "[Collections] Could not create material asset {}", dematPath.string() );
                    continue;
                }
                asset->Data() = at->second;
                Runtime::ResourceRegistry::GetMaterialService()->Register( asset );
                LOG_INFO( "[Collections] Materialized '{}' -> {}", step.Key, dematPath.generic_string() );
            }

            // The record LAST, and never before the files: writing it first would make a failed write look
            // like a delivered one. Written when anything moved OR when there is no record yet — the
            // second case is the adoption above, which has nothing to show in the counters and is the
            // whole point of the first run.
            if ( !hasRecord || applied.GetValue().Written || applied.GetValue().Removed )
            {
                if ( const auto written = Common::Utils::FileSystem::WriteContentToFileAtomic(
                          recordPath, applied.GetValue().Recorded.Serialize() );
                     !written )
                    LOG_ERROR( "[Collections] {} materialized, but the install record could not be written "
                               "({}) — the next update will treat these files as locally added and leave "
                               "them alone.",
                               collectionDir.filename().string(), written.GetError() );
            }
            if ( applied.GetValue().KeptLocalEdit )
                LOG_INFO( "[Collections] {}: {} material(s) kept because they were edited here",
                          collectionDir.filename().string(), applied.GetValue().KeptLocalEdit );
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

        std::error_code ec;
        const std::filesystem::path root( CollectionsRoot() );
        if ( !std::filesystem::exists( root, ec ) )
            return;

        for ( const auto& dir : std::filesystem::directory_iterator( root, ec ) )
        {
            if ( !dir.is_directory() )
                continue;
            const auto manifestPath = dir.path() / "collection.json";
            if ( !std::filesystem::exists( manifestPath, ec ) )
                continue;

            const auto raw = Common::Utils::FileSystem::ReadFileContent( manifestPath );
            if ( !raw )
            {
                LOG_WARN( "[Collections] {}", raw.GetError() );
                continue;
            }
            const auto parsed = rfl::json::read<Manifest>( raw.GetValue() );
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
        // Thumbnail capture is driven editor-wide by EditorLayer via ThumbnailService.

        // Content renders inside the window EditorLayer's central loop already opened for this panel. A
        // self-Begin here opened a SECOND, icon-less "Collections" window (different ImGui id) once panel
        // titles gained an icon (### suffix) — so it's removed.
        if ( ImGui::Button( ICON_MDI_REFRESH " Rescan" ) )
        {
            Rescan();
            m_OpenCollection = -1;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth( 220.0f );
        ImGui::InputTextWithHint( "##collsearch", ICON_MDI_MAGNIFY " Search...", m_Search, sizeof( m_Search ) );

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
            ThumbnailService::Get().RequestMesh(
                 handle, item.MeshPath, MeshMaterial::ResolveSidecar( *m_AssetManager, item.MeshPath ) );
        }

        ImGui::Button( ICON_MDI_CUBE_OUTLINE, img );
    }
} // namespace Desert::Editor
