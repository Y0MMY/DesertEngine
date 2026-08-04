#include "PropertyEditorBuilder.hpp"
#include <Editor/Core/DragPayloads.hpp>
#include <Editor/Core/MultiEdit.hpp>

#include <Engine/Reflection/ReflectionRegistry.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Runtime/Services/Font/FontService.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/Image.hpp>

#include <Editor/Core/CommandHistory.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/ThemeManager.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Widgets/UIHelper/ImGuiUI.hpp>
#include <Editor/Import/TextureDnD.hpp>

#include <Common/Core/Units.hpp>

#include <ImGui/imgui.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;
    using namespace Desert::Reflection;

    namespace
    {
        void* FieldPtr( void* object, const FieldInfo& f )
        {
            return static_cast<char*>( object ) + f.Offset;
        }

        // Resolves a texture asset handle to its runtime image for a thumbnail preview. Returns a
        // NON-owning shared_ptr (the image is owned by the image service); UICacheTexture keys its ImGui
        // descriptors by VkImageView, so wrapping the same image each frame is safe (no leak).
        std::shared_ptr<Graphic::Image2D> ResolveTextureImage( uint64_t handle )
        {
            if ( handle == 0 )
                return nullptr;
            auto* tex = Runtime::ResourceRegistry::GetTextureService()->Get( Common::UUID( handle ) );
            if ( !tex )
                return nullptr;
            auto* img = static_cast<Graphic::Image2D*>(
                 Runtime::ResourceRegistry::GetImageService()->Resolve( tex->GetImageHandle() ) );
            if ( !img )
                return nullptr;
            return std::shared_ptr<Graphic::Image2D>( img, []( Graphic::Image2D* ) {} );
        }

        // (texture drop resolution moved to Editor::TextureDnD::ResolveOrImport — import-on-demand)

        // Enums carry any integral underlying type — read/write exactly Size bytes.
        int64_t ReadEnum( const void* p, std::size_t size )
        {
            switch ( size )
            {
                case 1:  return *static_cast<const int8_t*>( p );
                case 2:  return *static_cast<const int16_t*>( p );
                case 8:  return *static_cast<const int64_t*>( p );
                default: return *static_cast<const int32_t*>( p );
            }
        }

        // PROPERTY(Length) — the field is a distance in world units, i.e. CENTIMETRES (Common/Core/Units).
        // No conversion happens anywhere: the widget just labels the number and drags it a centimetre at a
        // time instead of the 0.01 step that suits unitless ratios.
        bool DrawLength( const char* id, float* v, int n, const Reflection::PropertyMetadata& meta )
        {
            const float mn = meta.RangeMin;
            const float mx = meta.RangeMax;
            return meta.HasRange
                        ? ImGui::SliderScalarN( id, ImGuiDataType_Float, v, n, &mn, &mx, "%.1f cm" )
                        : ImGui::DragScalarN( id, ImGuiDataType_Float, v, n, 1.0f, nullptr, nullptr, "%.1f cm" );
        }

        void WriteEnum( void* p, std::size_t size, int64_t value )
        {
            switch ( size )
            {
                case 1:  *static_cast<int8_t*>( p )  = static_cast<int8_t>( value ); break;
                case 2:  *static_cast<int16_t*>( p ) = static_cast<int16_t>( value ); break;
                case 8:  *static_cast<int64_t*>( p ) = value; break;
                default: *static_cast<int32_t*>( p ) = static_cast<int32_t>( value ); break;
            }
        }
    } // namespace

    bool PropertyEditorBuilder::DrawField( void* object, const FieldInfo& field,
                                           const Assets::AssetManager* assetMgr, UI::UIHelper* uiHelper,
                                           const void* defaultObject, bool mixed )
    {
        if ( field.Meta.Hidden )
            return false;

        // PROPERTY(Header("...")) — a labelled section break drawn above this field.
        if ( !field.Meta.Header.empty() )
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled( "%s", field.Meta.Header.c_str() );
        }

        void*       p     = FieldPtr( object, field );
        const auto& label = field.DisplayName();
        bool        changed = false;

        // The default value of THIS field, if the owning type provided a default instance.
        const void* defFieldPtr =
             defaultObject ? static_cast<const std::byte*>( defaultObject ) + field.Offset : nullptr;

        // Nested reflected struct: draw its fields recursively under a collapsible node.
        if ( field.Type == FieldType::Struct )
        {
            ImGui::PushID( field.Name.c_str() );
            if ( field.StructType )
            {
                if ( ImGui::TreeNodeEx( label.c_str(), ImGuiTreeNodeFlags_DefaultOpen ) )
                {
                    for ( const auto& sub : field.StructType->Fields )
                    {
                        if ( DrawField( p, sub, assetMgr, uiHelper, defFieldPtr ) )
                            changed = true;
                    }
                    ImGui::TreePop();
                }
            }
            else
            {
                ImGui::TextDisabled( "%s: (unresolved struct)", label.c_str() );
            }
            ImGui::PopID();
            return changed && !field.Meta.ReadOnly;
        }

        // Capture the field bytes BEFORE the widget so an interactive edit (drag/slider/color/checkbox)
        // can record its "before" state for undo. Limited to small POD fields (value editors).
        const bool  trackUndo = !field.Meta.ReadOnly && field.Type != FieldType::AssetHandle &&
                               field.Type != FieldType::Struct && field.Size > 0 && field.Size <= 64;
        std::vector<uint8_t> beforeBytes;
        if ( trackUndo )
        {
            beforeBytes.resize( field.Size );
            std::memcpy( beforeBytes.data(), p, field.Size );
        }

        ImGui::PushID( field.Name.c_str() );
        ImGui::BeginDisabled( field.Meta.ReadOnly );

        ImGui::Columns( 2 );
        ImGui::SetColumnWidth( 0, 150.0f );
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( label.c_str() );
        // Multi-select: this field's value differs across the selected objects until the user edits it.
        if ( mixed )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(mixed)" );
        }
        // Hover the label for help: PROPERTY(Tooltip("...")) when authored, otherwise fall back to
        // revealing the underlying C++ field name (useful when DisplayName is a friendlier alias).
        if ( ImGui::IsItemHovered() )
        {
            if ( !field.Meta.Tooltip.empty() )
                ImGui::SetTooltip( "%s", field.Meta.Tooltip.c_str() );
            else if ( field.Name != label )
                ImGui::SetTooltip( "%s", field.Name.c_str() );
        }

        // Reset-to-default: for trivially-copyable value fields (not strings/structs/containers) that DIFFER
        // from their default, show a revert button right-aligned in the label column (UE-style). memcpy is
        // safe here because these field types own no heap.
        const bool resettable = defFieldPtr && !field.Meta.ReadOnly && !field.IsContainer &&
                                field.Type != FieldType::String && field.Type != FieldType::Struct &&
                                field.Size > 0;
        if ( resettable && std::memcmp( p, defFieldPtr, field.Size ) != 0 )
        {
            const float bw = ImGui::GetFrameHeight();
            ImGui::SameLine( ImGui::GetColumnWidth() - bw - 2.0f );
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.0f, 0.0f, 0.0f, 0.0f ) );
            ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetSelectedColor() );
            if ( ImGui::SmallButton( ICON_MDI_BACKUP_RESTORE ) )
            {
                std::memcpy( p, defFieldPtr, field.Size );
                changed = true;
            }
            ImGui::PopStyleColor( 2 );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Reset to default" );
        }

        ImGui::NextColumn();
        ImGui::PushItemWidth( -1 );

        switch ( field.Type )
        {
            case FieldType::Bool:
                changed = ImGui::Checkbox( "##v", static_cast<bool*>( p ) );
                break;

            case FieldType::Int:
            {
                int* v = static_cast<int*>( p );
                changed = field.Meta.HasRange
                              ? ImGui::SliderInt( "##v", v, (int)field.Meta.RangeMin, (int)field.Meta.RangeMax )
                              : ImGui::DragInt( "##v", v );
                break;
            }
            case FieldType::UInt:
            {
                changed = ImGui::DragScalar( "##v", ImGuiDataType_U32, p );
                break;
            }
            case FieldType::Float:
            {
                float* v = static_cast<float*>( p );
                if ( field.Meta.IsLength )
                {
                    changed = DrawLength( "##v", v, 1, field.Meta );
                    break;
                }
                changed = field.Meta.HasRange
                              ? ImGui::SliderFloat( "##v", v, field.Meta.RangeMin, field.Meta.RangeMax )
                              : ImGui::DragFloat( "##v", v, 0.01f );
                break;
            }
            case FieldType::Double:
            {
                changed = ImGui::DragScalar( "##v", ImGuiDataType_Double, p, 0.01f );
                break;
            }
            case FieldType::Vec2:
                changed = field.Meta.IsLength ? DrawLength( "##v", static_cast<float*>( p ), 2, field.Meta )
                                              : ImGui::DragFloat2( "##v", static_cast<float*>( p ), 0.01f );
                break;

            case FieldType::Vec3:
                changed = field.Meta.IsColor    ? ImGui::ColorEdit3( "##v", static_cast<float*>( p ) )
                          : field.Meta.IsLength ? DrawLength( "##v", static_cast<float*>( p ), 3, field.Meta )
                                                : ImGui::DragFloat3( "##v", static_cast<float*>( p ), 0.01f );
                break;

            case FieldType::Vec4:
                changed = field.Meta.IsColor    ? ImGui::ColorEdit4( "##v", static_cast<float*>( p ) )
                          : field.Meta.IsLength ? DrawLength( "##v", static_cast<float*>( p ), 4, field.Meta )
                                                : ImGui::DragFloat4( "##v", static_cast<float*>( p ), 0.01f );
                break;

            case FieldType::String:
            {
                auto* s = static_cast<std::string*>( p );
                char  buf[256];
                std::snprintf( buf, sizeof( buf ), "%s", s->c_str() );
                if ( ImGui::InputText( "##v", buf, sizeof( buf ) ) )
                {
                    *s      = buf;
                    changed = true;
                }
                break;
            }
            case FieldType::AssetHandle:
            {
                // Font slot: a font is referenced by asset handle (never a raw path). The user picks one of the
                // preloaded fonts from the dropdown or drags a .ttf from the Content Browser. FontService owns
                // the handle<->path registry; "Default" (handle 0) falls back to the engine's built-in font.
                if ( field.Meta.AssetType == "FontAsset" )
                {
                    uint64_t* handle = static_cast<uint64_t*>( p );
                    auto*     fs     = Runtime::ResourceRegistry::GetFontService();

                    const std::string curPath = fs ? fs->PathForHandle( *handle ) : "";
                    const std::string preview =
                         *handle == 0 ? "Default"
                                      : ( curPath.empty() ? "(missing)"
                                                          : std::filesystem::path( curPath ).stem().string() );
                    if ( ImGui::BeginCombo( "##font", preview.c_str() ) )
                    {
                        if ( ImGui::Selectable( "Default", *handle == 0 ) )
                        {
                            *handle = 0;
                            changed = true;
                        }
                        if ( fs )
                        {
                            for ( const auto& f : fs->AvailableFonts() )
                            {
                                const uint64_t h   = fs->RegisterFont( f );
                                const bool     sel = ( h == *handle );
                                if ( ImGui::Selectable( std::filesystem::path( f ).stem().string().c_str(), sel ) )
                                {
                                    *handle = h;
                                    changed = true;
                                }
                                if ( sel )
                                    ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if ( ImGui::BeginDragDropTarget() )
                    {
                        if ( const ImGuiPayload* pl =
                                  ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::FontFile ) )
                        {
                            const std::string path( static_cast<const char*>( pl->Data ),
                                                    pl->DataSize > 0 ? pl->DataSize - 1 : 0 );
                            if ( fs && !path.empty() )
                            {
                                *handle = fs->RegisterFont( path );
                                changed = true;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if ( ImGui::IsItemHovered() )
                        ImGui::SetTooltip( "Pick a preloaded font or drag a .ttf here from the Content Browser" );
                    break;
                }

                // Icon slot: a vector icon is an .svg imported into an SDF (IconService owns the
                // handle<->path registry). Pick a discovered icon or drag an .svg from the Content Browser —
                // adding icons never touches C++.
                if ( field.Meta.AssetType == "IconAsset" )
                {
                    uint64_t* handle = static_cast<uint64_t*>( p );
                    auto*     is     = Runtime::ResourceRegistry::GetIconService();

                    const std::string curPath = is ? is->PathForHandle( *handle ) : "";
                    const std::string preview =
                         *handle == 0 ? "None"
                                      : ( curPath.empty() ? "(missing)"
                                                          : std::filesystem::path( curPath ).stem().string() );

                    // Swatch of the bound icon. The SDF's alpha channel holds a sharpened coverage mask
                    // (see IconService), so a plain alpha-blended draw shows the real silhouette.
                    const float          swatch = ImGui::GetFrameHeight();
                    Runtime::Icon* const icon   = ( is && *handle != 0 ) ? is->Get( *handle ) : nullptr;
                    if ( uiHelper && icon && icon->Atlas )
                    {
                        const ImVec2 p0 = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddRectFilled( p0, ImVec2( p0.x + swatch, p0.y + swatch ),
                                                                   IM_COL32( 28, 30, 36, 255 ), 3.0f );
                        uiHelper->Image( icon->Atlas, ImVec2( swatch, swatch ), ImVec2( icon->U0, icon->V0 ),
                                         ImVec2( icon->U1, icon->V1 ) );
                        if ( ImGui::IsItemHovered() )
                        {
                            ImGui::BeginTooltip();
                            uiHelper->Image( icon->Atlas, ImVec2( 128.0f, 128.0f ), ImVec2( icon->U0, icon->V0 ),
                                             ImVec2( icon->U1, icon->V1 ) );
                            ImGui::TextUnformatted( std::filesystem::path( curPath ).filename().string().c_str() );
                            ImGui::EndTooltip();
                        }
                    }
                    else
                    {
                        ImGui::Dummy( ImVec2( swatch, swatch ) ); // keep the combo aligned when unset
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth( -1.0f );
                    if ( ImGui::BeginCombo( "##icon", preview.c_str() ) )
                    {
                        if ( ImGui::Selectable( "None", *handle == 0 ) )
                        {
                            *handle = 0;
                            changed = true;
                        }
                        if ( is )
                        {
                            for ( const auto& f : is->AvailableIcons() )
                            {
                                const uint64_t h   = is->RegisterIcon( f );
                                const bool     sel = ( h == *handle );
                                if ( ImGui::Selectable( std::filesystem::path( f ).stem().string().c_str(), sel ) )
                                {
                                    *handle = h;
                                    changed = true;
                                }
                                if ( sel )
                                    ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if ( ImGui::BeginDragDropTarget() )
                    {
                        if ( const ImGuiPayload* pl = ImGui::AcceptDragDropPayload( "AssetFile" ) )
                        {
                            const std::string path( static_cast<const char*>( pl->Data ),
                                                    pl->DataSize > 0 ? pl->DataSize - 1 : 0 );
                            if ( is && !path.empty() )
                            {
                                *handle = is->RegisterIcon( path );
                                changed = true;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if ( ImGui::IsItemHovered() )
                        ImGui::SetTooltip( "Pick a vector icon or drag an .svg here from the Content Browser" );
                    break;
                }

                // Video slot: a video is referenced by asset handle (never a raw path). Drag a .mpg from the
                // Content Browser (a generic AssetFile payload). VideoService owns the handle<->path registry.
                if ( field.Meta.AssetType == "VideoAsset" )
                {
                    uint64_t* handle = static_cast<uint64_t*>( p );
                    auto*     vs     = Runtime::ResourceRegistry::GetVideoService();

                    const std::string curPath = vs ? vs->PathForHandle( *handle ) : "";
                    const std::string display = *handle == 0 ? "None"
                                                : curPath.empty()
                                                     ? "(missing)"
                                                     : std::filesystem::path( curPath ).filename().string();
                    ImGui::Button( display.c_str(), ImVec2( -1.0f, 0.0f ) );
                    if ( ImGui::BeginDragDropTarget() )
                    {
                        if ( const ImGuiPayload* pl = ImGui::AcceptDragDropPayload( "AssetFile" ) )
                        {
                            const std::string path( static_cast<const char*>( pl->Data ),
                                                    pl->DataSize > 0 ? pl->DataSize - 1 : 0 );
                            if ( vs && !path.empty() )
                            {
                                *handle = vs->RegisterVideo( path );
                                changed = true;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if ( ImGui::IsItemHovered() )
                        ImGui::SetTooltip( "Drag a .mpg (MPEG1) here from the Content Browser" );
                    if ( *handle != 0 )
                    {
                        ImGui::SameLine();
                        if ( ImGui::SmallButton( "x##clearvideo" ) )
                        {
                            *handle = 0;
                            changed = true;
                        }
                    }
                    break;
                }

                // Texture slot: shows the bound asset name, accepts a drag-dropped TEXTURE_ASSET (asset
                // path payload, emitted by the FileExplorer) and has a Clear button. Thumbnails are a
                // later visual pass (resolving handle -> Image2D needs runtime verification).
                uint64_t* handle = static_cast<uint64_t*>( p );

                // Always show the texture's filename (consistent); a bound-but-unresolvable handle reads
                // "(missing)", never a raw "Asset #N".
                std::string display = "None";
                if ( *handle != 0 )
                {
                    display = "(missing)";
                    if ( assetMgr )
                    {
                        if ( auto tex = assetMgr->FindByHandle<Assets::TextureAsset>( Common::UUID( *handle ) ) )
                        {
                            const auto& src = tex->GetSourcePath();
                            const auto& path = !src.empty() ? src : tex->GetMetadata().Filepath.string();
                            display          = std::filesystem::path( path ).filename().string();
                        }
                    }
                }

                ImGui::Button( display.c_str(), ImVec2( -1.0f, 0.0f ) );

                // Hover thumbnail preview of the bound texture.
                if ( uiHelper && *handle != 0 && ImGui::IsItemHovered() )
                {
                    if ( auto img = ResolveTextureImage( *handle ) )
                    {
                        ImGui::BeginTooltip();
                        uiHelper->Image( img, ImVec2( 128.0f, 128.0f ) );
                        ImGui::EndTooltip();
                    }
                }

                if ( ImGui::BeginDragDropTarget() )
                {
                    if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::TextureAsset ) )
                    {
                        const std::string path( static_cast<const char*>( payload->Data ),
                                                payload->DataSize > 0 ? payload->DataSize - 1 : 0 );
                        if ( assetMgr )
                        {
                            // Resolve to a registered texture, importing+cooking on demand if the dropped
                            // source isn't registered yet (so any texture under Resources/ just works).
                            const auto resolved = TextureDnD::ResolveOrImport(
                                 const_cast<Assets::AssetManager&>( *assetMgr ), path );
                            if ( static_cast<uint64_t>( resolved ) != 0 )
                            {
                                *handle = static_cast<uint64_t>( resolved );
                                changed = true;
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if ( *handle != 0 )
                {
                    ImGui::SameLine();
                    if ( ImGui::SmallButton( "Clear" ) )
                    {
                        *handle = 0;
                        changed = true;
                    }
                }
                break;
            }

            case FieldType::Enum:
            {
                const auto& values = field.EnumValues;
                if ( values.empty() )
                {
                    ImGui::TextDisabled( "(enum: no values)" );
                    break;
                }

                const int64_t current = ReadEnum( p, field.Size );
                int           idx     = 0;
                for ( size_t k = 0; k < values.size(); ++k )
                    if ( values[k].Value == current )
                        idx = static_cast<int>( k );

                if ( ImGui::BeginCombo( "##v", values[idx].Name.c_str() ) )
                {
                    for ( size_t k = 0; k < values.size(); ++k )
                    {
                        const bool selected = ( static_cast<int>( k ) == idx );
                        if ( ImGui::Selectable( values[k].Name.c_str(), selected ) )
                        {
                            WriteEnum( p, field.Size, values[k].Value );
                            changed = true;
                        }
                        if ( selected )
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                break;
            }

            default:
                ImGui::TextDisabled( "(unsupported)" );
                break;
        }

        // Record an undo command on edit commit. One widget is active at a time, so a single pending
        // capture is enough: grab "old" the frame the widget activates, push old->new on commit.
        if ( trackUndo )
        {
            static void*                s_PendingTarget = nullptr;
            static std::vector<uint8_t> s_PendingOld;

            if ( ImGui::IsItemActivated() )
            {
                s_PendingTarget = p;
                s_PendingOld    = beforeBytes;
            }
            if ( ImGui::IsItemDeactivatedAfterEdit() && s_PendingTarget == p )
            {
                CommandHistory::Get().Push( p, s_PendingOld.data(), p, field.Size );
                s_PendingTarget = nullptr;
            }
        }

        ImGui::PopItemWidth();
        ImGui::NextColumn();
        ImGui::Columns( 1 );

        ImGui::EndDisabled();
        ImGui::PopID();

        return changed && !field.Meta.ReadOnly;
    }

    bool PropertyEditorBuilder::Draw( void* object, const TypeInfo& type, const Assets::AssetManager* assetMgr,
                                      UI::UIHelper* uiHelper )
    {
        if ( !object )
            return false;

        bool anyChanged = false;

        // Property byte-commands hold raw field pointers, so they must not outlive the object they edit.
        // Drop them when the SELECTED ENTITY changes (several components of one entity draw through here
        // each frame — keying on the object pointer cleared the history every frame). Structural commands
        // are UUID-addressed and survive; Ctrl+Z/Y themselves are handled globally by EditorLayer.
        {
            static uint64_t s_LastSelected = 0;
            const auto&     sel            = Core::SelectionManager::GetSelected();
            const uint64_t  current        = sel.has_value() ? static_cast<uint64_t>( *sel ) : 0;
            if ( current != s_LastSelected )
            {
                CommandHistory::Get().DropVolatile();
                s_LastSelected = current;
            }
        }

        // Group by category, preserving first-seen order.
        std::vector<std::pair<std::string, std::vector<const FieldInfo*>>> categories;
        auto bucket = [&]( const std::string& cat ) -> std::vector<const FieldInfo*>&
        {
            for ( auto& [name, vec] : categories )
                if ( name == cat )
                    return vec;
            categories.emplace_back( cat, std::vector<const FieldInfo*>{} );
            return categories.back().second;
        };

        for ( const auto& field : type.Fields )
        {
            if ( field.Meta.Hidden )
                continue;
            bucket( field.Meta.Category.empty() ? "Default" : field.Meta.Category ).push_back( &field );
        }

        for ( auto& [catName, fields] : categories )
        {
            if ( fields.empty() )
                continue;

            if ( ImGui::CollapsingHeader( catName.c_str(), ImGuiTreeNodeFlags_DefaultOpen ) )
            {
                // The type's default-constructed instance (member initializers) — powers reset-to-default.
                const void* defaultObject = type.GetDefaultInstance ? type.GetDefaultInstance() : nullptr;
                for ( const auto* field : fields )
                {
                    if ( DrawField( object, *field, assetMgr, uiHelper, defaultObject ) )
                        anyChanged = true;
                }
            }
        }

        return anyChanged;
    }

    bool PropertyEditorBuilder::Draw( void* object, const std::string& typeName,
                                      const Assets::AssetManager* assetMgr, UI::UIHelper* uiHelper )
    {
        const TypeInfo* type = ReflectionRegistry::Get().Find( typeName );
        if ( !type )
        {
            ImGui::TextDisabled( "<type '%s' not reflected>", typeName.c_str() );
            return false;
        }
        return Draw( object, *type, assetMgr, uiHelper );
    }

    namespace
    {
        // A trivially-copyable value field can be compared/broadcast with memcmp/memcpy across the
        // selection. Strings, nested structs and containers own heap and are excluded (edited on the
        // primary only).
        bool IsBroadcastable( const FieldInfo& field )
        {
            return !field.IsContainer && field.Type != FieldType::Struct &&
                   field.Type != FieldType::String && field.Size > 0 && field.Size <= 64;
        }
    } // namespace

    bool PropertyEditorBuilder::DrawMulti( void* primary, const std::vector<void*>& others,
                                           const TypeInfo& type, const Assets::AssetManager* assetMgr,
                                           UI::UIHelper* uiHelper )
    {
        if ( !primary )
            return false;
        if ( others.empty() )
            return Draw( primary, type, assetMgr, uiHelper );

        bool anyChanged = false;

        // Same category grouping as Draw(), but each field is marked "(mixed)" when it differs across
        // the selection, and a POD edit on the primary is broadcast to every other object.
        std::vector<std::pair<std::string, std::vector<const FieldInfo*>>> categories;
        auto bucket = [&]( const std::string& cat ) -> std::vector<const FieldInfo*>&
        {
            for ( auto& [name, vec] : categories )
                if ( name == cat )
                    return vec;
            categories.emplace_back( cat, std::vector<const FieldInfo*>{} );
            return categories.back().second;
        };
        for ( const auto& field : type.Fields )
        {
            if ( field.Meta.Hidden )
                continue;
            bucket( field.Meta.Category.empty() ? "Default" : field.Meta.Category ).push_back( &field );
        }

        for ( auto& [catName, fields] : categories )
        {
            if ( fields.empty() )
                continue;
            if ( !ImGui::CollapsingHeader( catName.c_str(), ImGuiTreeNodeFlags_DefaultOpen ) )
                continue;

            for ( const auto* field : fields )
            {
                const bool broadcastable = IsBroadcastable( *field );
                const bool mixed = broadcastable &&
                                   AnyFieldDiffers( primary, others, field->Offset, field->Size );

                const bool changed = DrawField( primary, *field, assetMgr, uiHelper, nullptr, mixed );
                if ( changed )
                {
                    anyChanged = true;
                    if ( broadcastable )
                        BroadcastField( primary, others, field->Offset, field->Size );
                }
            }
        }

        return anyChanged;
    }

    bool PropertyEditorBuilder::DrawMulti( void* primary, const std::vector<void*>& others,
                                           const std::string& typeName, const Assets::AssetManager* assetMgr,
                                           UI::UIHelper* uiHelper )
    {
        const TypeInfo* type = ReflectionRegistry::Get().Find( typeName );
        if ( !type )
        {
            ImGui::TextDisabled( "<type '%s' not reflected>", typeName.c_str() );
            return false;
        }
        return DrawMulti( primary, others, *type, assetMgr, uiHelper );
    }
} // namespace Desert::Editor
