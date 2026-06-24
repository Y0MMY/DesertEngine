#include "MeshEditorPanel.hpp"

#include <ImGui/imgui.h>
#include <ImGui/imgui_internal.h>
#include <ImGuizmo.h>

#include <Common/Core/Math/Ray.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Geometry/StaticMesh.hpp>
#include <Engine/Geometry/PrimitiveMeshFactory.hpp>
#include <Engine/Assets/Mesh/StaticMeshAsset.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/ECS/System/MeshECSSystem.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/EditorResources.hpp>
#include <Editor/Core/ThemeManager.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Widgets/UIHelper/ImGuiUI.hpp>
#include <Editor/Widgets/Controls/Controls.hpp>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Desert::Editor
{

namespace ImGui = ::ImGui;

// ─── colour palette (matches editor dark theme) ──────────────────────────────
static constexpr ImVec4 kColHeader      = { 0.13f, 0.14f, 0.17f, 1.0f };
static constexpr ImVec4 kColSectionBg   = { 0.11f, 0.12f, 0.14f, 1.0f };
static constexpr ImVec4 kColSelected    = { 1.00f, 0.55f, 0.10f, 1.0f };  // orange
static constexpr ImVec4 kColSelectedDim = { 0.60f, 0.33f, 0.06f, 0.45f };
static constexpr ImVec4 kColAccent      = { 0.26f, 0.59f, 0.98f, 1.0f };  // blue
static constexpr ImVec4 kColMuted       = { 0.55f, 0.55f, 0.60f, 1.0f };
static constexpr ImVec4 kColGreen       = { 0.30f, 0.85f, 0.40f, 1.0f };
static constexpr ImVec4 kColRed         = { 0.90f, 0.25f, 0.25f, 1.0f };

static constexpr int    kVertexPageSize = 200;

// ─── helpers ─────────────────────────────────────────────────────────────────
namespace
{
    void SectionHeader( const char* icon, const char* label )
    {
        ImGui::PushStyleColor( ImGuiCol_ChildBg, kColHeader );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, { 6.f, 4.f } );

        ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetIconColor() );
        ImGui::TextUnformatted( icon );
        ImGui::PopStyleColor();
        ImGui::SameLine( 0, 6 );
        ImGui::PushFont( EditorResources::GetBoldFont() );
        ImGui::TextUnformatted( label );
        ImGui::PopFont();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::Separator();
    }

    bool ToolButton( const char* icon, const char* tooltip, bool active )
    {
        if ( active )
            ImGui::PushStyleColor( ImGuiCol_Button, kColAccent );
        else
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.2f, 0.2f, 0.22f, 1.0f } );

        bool clicked = ImGui::Button( icon, { 30.f, 26.f } );
        ImGui::PopStyleColor();

        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "%s", tooltip );

        return clicked;
    }

    bool ActionButton( const char* icon, const char* label, const char* tooltip,
                       ImVec4 col = { 0.22f, 0.22f, 0.24f, 1.0f } )
    {
        ImGui::PushStyleColor( ImGuiCol_Button, col );
        char buf[64];
        snprintf( buf, sizeof( buf ), "%s %s", icon, label );
        bool clicked = ImGui::Button( buf );
        ImGui::PopStyleColor();
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "%s", tooltip );
        return clicked;
    }
} // namespace

// ─── Selection helpers ────────────────────────────────────────────────────────
bool MeshEditorPanel::Selection::Contains( size_t idx ) const
{
    return std::find( VertexIndices.begin(), VertexIndices.end(), idx ) != VertexIndices.end();
}

void MeshEditorPanel::Selection::Toggle( size_t idx )
{
    auto it = std::find( VertexIndices.begin(), VertexIndices.end(), idx );
    if ( it != VertexIndices.end() )
        VertexIndices.erase( it );
    else
        VertexIndices.push_back( idx );
}

glm::vec3 MeshEditorPanel::Selection::GetCenter( const ::Desert::DynamicMesh& mesh ) const
{
    if ( VertexIndices.empty() ) return glm::vec3( 0.f );
    glm::vec3 c{ 0.f };
    for ( auto i : VertexIndices )
        c += mesh.GetVertices()[i].Position;
    return c / (float)VertexIndices.size();
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────
MeshEditorPanel::MeshEditorPanel( std::shared_ptr<::Desert::Core::Scene> scene )
    : IPanel( "Mesh Editor" )
    , m_Scene( scene )
    , m_UIHelper( std::make_unique<UI::UIHelper>() )
{
    s_Instance = this;
    m_UIHelper->Init();
    m_SowPanel = false;
}

MeshEditorPanel::~MeshEditorPanel()
{
    if ( s_Instance == this )
        s_Instance = nullptr;
}

// ─── Main render ─────────────────────────────────────────────────────────────
void MeshEditorPanel::OnUIRender()
{
    if ( !m_TargetEntity || !m_Mesh )
    {
        ImGui::PushStyleColor( ImGuiCol_Text, kColMuted );
        ImGui::SetCursorPosY( ImGui::GetContentRegionAvail().y * 0.45f );
        float textW = ImGui::CalcTextSize( "No mesh selected for editing" ).x;
        ImGui::SetCursorPosX( ( ImGui::GetContentRegionAvail().x - textW ) * 0.5f );
        ImGui::TextUnformatted( ICON_MDI_VECTOR_SQUARE "  No mesh selected for editing" );
        ImGui::PopStyleColor();
        return;
    }

    DrawToolbar();
    DrawStatsBar();
    ImGui::Separator();

    // ── Splitter ─────────────────────────────────────────────────────────────
    const float totalW     = ImGui::GetContentRegionAvail().x;
    const float leftW      = totalW * m_SplitRatio;
    const float splitterW  = 4.f;
    const float rightW     = totalW - leftW - splitterW;
    const float paneH      = ImGui::GetContentRegionAvail().y;

    // Left pane
    ImGui::PushStyleColor( ImGuiCol_ChildBg, kColSectionBg );
    ImGui::BeginChild( "##LeftPane", { leftW, paneH }, false );
    ImGui::PopStyleColor();

    DrawVertexList();
    ImGui::Spacing();
    DrawPropertiesPanel();
    ImGui::Spacing();
    DrawTopologyPanel();

    ImGui::EndChild();

    // Draggable splitter
    ImGui::SameLine( 0, 0 );
    ImGui::PushStyleColor( ImGuiCol_Button,        { 0.18f, 0.18f, 0.20f, 1.f } );
    ImGui::PushStyleColor( ImGuiCol_ButtonHovered, { 0.3f,  0.3f,  0.35f, 1.f } );
    ImGui::PushStyleColor( ImGuiCol_ButtonActive,  kColAccent );
    ImGui::Button( "##Splitter", { splitterW, paneH } );
    ImGui::PopStyleColor( 3 );
    if ( ImGui::IsItemActive() )
    {
        float delta   = ImGui::GetIO().MouseDelta.x;
        m_SplitRatio += delta / totalW;
        m_SplitRatio  = std::clamp( m_SplitRatio, 0.15f, 0.65f );
    }
    if ( ImGui::IsItemHovered() )
        ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );

    // Right pane
    ImGui::SameLine( 0, 0 );
    ImGui::PushStyleColor( ImGuiCol_ChildBg, { 0.07f, 0.07f, 0.08f, 1.f } );
    ImGui::BeginChild( "##RightPane", { rightW, paneH }, false );
    ImGui::PopStyleColor();

    DrawPreviewViewport();

    ImGui::EndChild();
}

// ─── Toolbar ─────────────────────────────────────────────────────────────────
void MeshEditorPanel::DrawToolbar()
{
    ImGui::PushStyleColor( ImGuiCol_ChildBg, kColHeader );
    ImGui::BeginChild( "##Toolbar", { 0, 36 }, false );

    ImGui::SetCursorPosY( ( 36.f - 26.f ) * 0.5f );

    if ( ToolButton( ICON_MDI_CURSOR_DEFAULT, "Select (S)",  m_CurrentTool == EditorTool::Select ) )
        m_CurrentTool = EditorTool::Select;
    ImGui::SameLine( 0, 2 );
    if ( ToolButton( ICON_MDI_AXIS_ARROW,     "Move (G)",    m_CurrentTool == EditorTool::Move ) )
        m_CurrentTool = EditorTool::Move;
    ImGui::SameLine( 0, 2 );
    if ( ToolButton( ICON_MDI_TUNE,           "Modify (M)",  m_CurrentTool == EditorTool::Modify ) )
        m_CurrentTool = EditorTool::Modify;

    ImGui::SameLine( 0, 14 );
    ImGui::PushStyleColor( ImGuiCol_Separator, { 0.35f, 0.35f, 0.38f, 1.f } );
    ImGui::SeparatorEx( ImGuiSeparatorFlags_Vertical );
    ImGui::PopStyleColor();
    ImGui::SameLine( 0, 14 );

    // Recalc normals toggle
    ImGui::PushStyleColor( ImGuiCol_Button,
        m_AutoRecalcNormals ? ImVec4{ 0.18f, 0.55f, 0.25f, 1.f } : ImVec4{ 0.22f, 0.22f, 0.24f, 1.f } );
    if ( ImGui::Button( ICON_MDI_VECTOR_POLYLINE "  Auto-N", { 0, 26 } ) )
        m_AutoRecalcNormals = !m_AutoRecalcNormals;
    ImGui::PopStyleColor();
    if ( ImGui::IsItemHovered() )
        ImGui::SetTooltip( "Auto-recalculate normals after every edit" );

    ImGui::SameLine( 0, 6 );
    if ( ActionButton( ICON_MDI_REFRESH, "Recalc N", "Recalculate all normals from triangle faces" ) )
    {
        RecalcNormals();
        CommitMeshEdit();
    }

    ImGui::SameLine( 0, 6 );
    if ( ActionButton( ICON_MDI_VECTOR_SQUARE, "Weld", "Merge vertices closer than 0.001 units",
                       { 0.28f, 0.20f, 0.48f, 1.f } ) )
    {
        WeldVertices( 0.001f );
    }

    ImGui::SameLine( 0, 6 );
    if ( ActionButton( ICON_MDI_FLIP_HORIZONTAL, "Flip N", "Flip all normals",
                       { 0.48f, 0.20f, 0.20f, 1.f } ) )
    {
        FlipNormals();
        CommitMeshEdit();
    }

    // Mesh-wide actions — left-aligned (no absolute positioning, so they never clip or overlap).
    ImGui::SameLine( 0, 14 );
    ImGui::PushStyleColor( ImGuiCol_Separator, { 0.35f, 0.35f, 0.38f, 1.f } );
    ImGui::SeparatorEx( ImGuiSeparatorFlags_Vertical );
    ImGui::PopStyleColor();
    ImGui::SameLine( 0, 14 );

    if ( ActionButton( ICON_MDI_LAYERS_OUTLINE, "Flatten", "Merge all submeshes into one",
                       { 0.20f, 0.32f, 0.48f, 1.f } ) )
    {
        m_Mesh->Flatten();
        m_Selection.VertexIndices.clear();
        CommitMeshEdit();
    }
    ImGui::SameLine( 0, 6 );
    if ( ActionButton( ICON_MDI_UPLOAD, "Upload", "Upload mesh changes to GPU now",
                       { 0.20f, 0.45f, 0.28f, 1.f } ) )
    {
        CommitMeshEdit();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(); // ChildBg
}

// ─── Stats bar ────────────────────────────────────────────────────────────────
void MeshEditorPanel::DrawStatsBar()
{
    const auto& verts   = m_Mesh->GetVertices();
    const auto& indices = m_Mesh->GetIndices();

    ImGui::PushStyleColor( ImGuiCol_Text, kColMuted );
    ImGui::Text( ICON_MDI_DOTS_HEXAGON " Vertices: " );
    ImGui::SameLine();
    ImGui::PushStyleColor( ImGuiCol_Text, { 0.9f, 0.9f, 0.9f, 1.f } );
    ImGui::Text( "%zu", verts.size() );
    ImGui::PopStyleColor();

    ImGui::SameLine( 0, 16 );
    ImGui::PushStyleColor( ImGuiCol_Text, kColMuted );
    ImGui::Text( ICON_MDI_TRIANGLE_OUTLINE " Triangles: " );
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor( ImGuiCol_Text, { 0.9f, 0.9f, 0.9f, 1.f } );
    ImGui::Text( "%zu", indices.size() );
    ImGui::PopStyleColor();

    ImGui::SameLine( 0, 16 );
    ImGui::PushStyleColor( ImGuiCol_Text, kColMuted );
    ImGui::Text( ICON_MDI_CURSOR_DEFAULT " Selected: " );
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor( ImGuiCol_Text, m_Selection.VertexIndices.empty() ? kColMuted : kColSelected );
    ImGui::Text( "%zu", m_Selection.VertexIndices.size() );
    ImGui::PopStyleColor();

    ImGui::PopStyleColor(); // first kColMuted push
}

// ─── Vertex list ─────────────────────────────────────────────────────────────
void MeshEditorPanel::DrawVertexList()
{
    SectionHeader( ICON_MDI_FORMAT_LIST_NUMBERED, "Vertices" );

    // Search box
    ImGui::SetNextItemWidth( -1 );
    ImGui::PushStyleColor( ImGuiCol_FrameBg, { 0.15f, 0.15f, 0.17f, 1.f } );
    bool searchChanged = ImGui::InputTextWithHint( "##VSearch", ICON_MDI_MAGNIFY " Search…",
                                                   m_SearchBuf, sizeof( m_SearchBuf ) );
    ImGui::PopStyleColor();
    Utils::ImGuiUtilities::DrawItemActivityOutline( 2.f, false, ImColor{ 80, 80, 80 } );
    if ( searchChanged )
        m_VertexPageOffset = 0;

    // Select all / clear row
    ImGui::PushStyleColor( ImGuiCol_Text, kColMuted );
    if ( ImGui::SmallButton( "All" ) )  SelectAll();
    ImGui::SameLine();
    if ( ImGui::SmallButton( "None" ) ) ClearSelection();
    ImGui::PopStyleColor();

    // Build filtered index list
    const auto&        verts = m_Mesh->GetVertices();
    std::vector<size_t> filtered;
    filtered.reserve( verts.size() );
    for ( size_t i = 0; i < verts.size(); ++i )
    {
        if ( m_SearchBuf[0] != '\0' )
        {
            char label[32];
            snprintf( label, sizeof( label ), "Vertex %zu", i );
            if ( !strstr( label, m_SearchBuf ) ) continue;
        }
        filtered.push_back( i );
    }

    // Pagination
    const int totalPages = std::max( 1, (int)( (filtered.size() + kVertexPageSize - 1) / kVertexPageSize ) );
    const int curPage    = m_VertexPageOffset / kVertexPageSize;

    if ( totalPages > 1 )
    {
        ImGui::PushStyleColor( ImGuiCol_Text, kColMuted );
        ImGui::Text( "Page %d / %d", curPage + 1, totalPages );
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if ( curPage > 0 && ImGui::SmallButton( ICON_MDI_CHEVRON_LEFT ) )
            m_VertexPageOffset = std::max( 0, m_VertexPageOffset - kVertexPageSize );
        ImGui::SameLine();
        if ( curPage < totalPages - 1 && ImGui::SmallButton( ICON_MDI_CHEVRON_RIGHT ) )
            m_VertexPageOffset += kVertexPageSize;
    }

    const float listH  = std::min( 240.f, (float)( std::min<size_t>( filtered.size(), kVertexPageSize ) ) * 20.f + 4.f );
    ImGui::PushStyleColor( ImGuiCol_ChildBg, { 0.09f, 0.09f, 0.11f, 1.f } );
    ImGui::BeginChild( "##VList", { 0, listH }, true );

    const size_t pageStart = (size_t)m_VertexPageOffset;
    const size_t pageEnd   = std::min( pageStart + (size_t)kVertexPageSize, filtered.size() );

    for ( size_t fi = pageStart; fi < pageEnd; ++fi )
    {
        const size_t idx  = filtered[fi];
        const bool   isSel = m_Selection.Contains( idx );

        // Coloured item background for selected vertices
        if ( isSel )
        {
            ImGui::PushStyleColor( ImGuiCol_Header,        kColSelectedDim );
            ImGui::PushStyleColor( ImGuiCol_HeaderHovered, { 0.75f, 0.40f, 0.08f, 0.60f } );
        }

        char label[48];
        const auto& v = verts[idx];
        snprintf( label, sizeof( label ), ICON_MDI_CIRCLE_SMALL " %zu  (%.2f %.2f %.2f)",
                  idx, v.Position.x, v.Position.y, v.Position.z );

        bool clicked = ImGui::Selectable( label, isSel, ImGuiSelectableFlags_SpanAllColumns );

        if ( isSel )
            ImGui::PopStyleColor( 2 );

        if ( clicked )
        {
            if ( ImGui::GetIO().KeyCtrl )
                m_Selection.Toggle( idx );
            else
            {
                m_Selection.VertexIndices.clear();
                m_Selection.VertexIndices.push_back( idx );
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(); // ChildBg
}

// ─── Properties panel ────────────────────────────────────────────────────────
void MeshEditorPanel::DrawPropertiesPanel()
{
    SectionHeader( ICON_MDI_PENCIL_BOX, "Properties" );

    if ( m_Selection.VertexIndices.empty() )
    {
        ImGui::PushStyleColor( ImGuiCol_Text, kColMuted );
        ImGui::TextUnformatted( "  Select vertices to edit" );
        ImGui::PopStyleColor();
        return;
    }

    auto& verts = m_Mesh->GetVertices();

    if ( m_Selection.VertexIndices.size() == 1 )
    {
        const size_t idx = m_Selection.VertexIndices[0];
        if ( idx >= verts.size() ) return;

        Vertex& v = verts[idx];

        ImGui::PushStyleColor( ImGuiCol_Text, kColMuted );
        ImGui::Text( "  Vertex #%zu", idx );
        ImGui::PopStyleColor();
        ImGui::Spacing();

        glm::vec3 prevPos = v.Position;
        glm::vec3 prevN   = v.Normal;
        glm::vec2 prevUV  = v.TexCoord;

        Widgets::DrawVec3Control( "Position", v.Position, 0.0f );
        Widgets::DrawVec3Control( "Normal",   v.Normal,   0.0f );

        ImGui::PushID( "UV" );
        ImGui::Text( "UV" );
        ImGui::SameLine( 80 );
        ImGui::SetNextItemWidth( -1 );
        ImGui::DragFloat2( "##UV", glm::value_ptr( v.TexCoord ), 0.001f );
        ImGui::PopID();

        const glm::vec3 posDelta = v.Position - prevPos;
        if ( posDelta != glm::vec3( 0.f ) )
        {
            // Move coincident twins together (selection is this single vertex).
            v.Position = prevPos;
            MoveSelectedVerticesLocal( posDelta );
        }
        if ( v.Normal != prevN || v.TexCoord != prevUV )
        {
            if ( m_AutoRecalcNormals )
                RecalcNormals();
            m_Mesh->Update( verts, m_Mesh->GetIndices() );
            m_PreviewDirty = true;
        }
    }
    else
    {
        ImGui::PushStyleColor( ImGuiCol_Text, kColMuted );
        ImGui::Text( "  %zu vertices selected", m_Selection.VertexIndices.size() );
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::TextUnformatted( "Use Move tool ( " ICON_MDI_AXIS_ARROW " ) in the" );
        ImGui::TextUnformatted( "viewport to translate all selected vertices." );

        // Bulk offset
        ImGui::Spacing();
        static glm::vec3 bulkOffset{ 0.f };
        Widgets::DrawVec3Control( "Offset", bulkOffset, 0.f );
        ImGui::SetNextItemWidth( -1 );
        if ( ImGui::Button( ICON_MDI_CHECK "  Apply Offset" ) )
        {
            MoveSelectedVerticesLocal( bulkOffset );
            bulkOffset = {};
        }
        if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Apply offset to all selected vertices" );
    }
}

// ─── Topology panel ──────────────────────────────────────────────────────────
void MeshEditorPanel::DrawTopologyPanel()
{
    if ( !ImGui::CollapsingHeader( ICON_MDI_SHAPE "  Topology", ImGuiTreeNodeFlags_DefaultOpen ) )
        return;

    ImGui::Spacing();

    const auto& indices = m_Mesh->GetIndices();
    ImGui::PushStyleColor( ImGuiCol_Text, kColMuted );
    ImGui::Text( "Triangles : " );
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text( "%zu", indices.size() );

    ImGui::Spacing();

    // Weld with custom threshold
    static float weldThreshold = 0.001f;
    ImGui::SetNextItemWidth( 90 );
    ImGui::DragFloat( "##WeldThres", &weldThreshold, 0.0001f, 0.f, 1.f, "%.4f" );
    if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Weld distance threshold" );
    ImGui::SameLine();
    if ( ActionButton( ICON_MDI_VECTOR_SQUARE, "Weld", "Merge vertices within threshold",
                       { 0.28f, 0.20f, 0.48f, 1.f } ) )
        WeldVertices( weldThreshold );

    ImGui::Spacing();
    if ( ActionButton( ICON_MDI_REFRESH, "Recalc Normals",
                       "Recalculate smooth normals from triangle geometry" ) )
    {
        RecalcNormals();
        CommitMeshEdit();
    }
    ImGui::SameLine();
    if ( ActionButton( ICON_MDI_FLIP_HORIZONTAL, "Flip Normals",
                       "Negate all normals", kColRed ) )
    {
        FlipNormals();
        CommitMeshEdit();
    }
}

// ─── Preview viewport ─────────────────────────────────────────────────────────
void MeshEditorPanel::DrawPreviewViewport()
{
    if ( !m_PreviewScene ) InitPreviewScene();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if ( avail.x <= 1.f || avail.y <= 1.f )
        return;

    // Mirror the edited entity's transform onto the preview so what you see (and edit) matches the scene.
    if ( m_TargetEntity && m_TargetEntity.HasComponent<ECS::TransformComponent>() && m_PreviewEntity &&
         m_PreviewEntity.HasComponent<ECS::TransformComponent>() )
    {
        const auto& ttc = m_TargetEntity.GetComponent<ECS::TransformComponent>();
        auto&       ptc = m_PreviewEntity.GetComponent<ECS::TransformComponent>();
        if ( ptc.Translation != ttc.Translation || ptc.Rotation != ttc.Rotation || ptc.Scale != ttc.Scale )
        {
            ptc.Translation = ttc.Translation;
            ptc.Rotation    = ttc.Rotation;
            ptc.Scale       = ttc.Scale;
            m_PreviewDirty  = true;
        }
    }

    // Resize only on actual change (recreating framebuffers every frame was part of the FPS hit).
    const uint32_t w = (uint32_t)avail.x, h = (uint32_t)avail.y;
    if ( w != m_LastPreviewW || h != m_LastPreviewH )
    {
        m_PreviewScene->Resize( w, h );
        m_LastPreviewW = w;
        m_LastPreviewH = h;
        m_PreviewDirty = true;
    }

    // Render ON DEMAND: only re-run the full scene render when something visible changed. Otherwise the
    // previously rendered framebuffer image is simply blitted again (this is the main FPS fix).
    if ( m_PreviewDirty )
    {
        m_PreviewScene->BeginScene();
        m_PreviewScene->OnUpdate( Common::Timestep( 0.016f ) );
        m_PreviewScene->EndScene();
        m_PreviewDirty = false;
    }

    auto         finalImage = m_PreviewScene->GetFinalImage();
    const ImVec2 imagePos   = ImGui::GetCursorScreenPos();
    m_UIHelper->Image( finalImage, avail );

    m_ViewportX = imagePos.x;
    m_ViewportY = imagePos.y;
    m_ViewportW = avail.x;
    m_ViewportH = avail.y;

    // ── Orbit + zoom (each marks the preview dirty so it re-renders that frame) ──
    if ( ImGui::IsItemHovered() )
    {
        if ( ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
            m_OrbitDragging = true;

        const float scrollY = ImGui::GetIO().MouseWheel;
        if ( scrollY != 0.f )
        {
            m_CamDistance = std::max( 0.1f, m_CamDistance - scrollY * m_CamDistance * 0.1f );
            UpdatePreviewCamera();
            m_PreviewDirty = true;
        }
    }

    if ( m_OrbitDragging )
    {
        if ( ImGui::IsMouseDown( ImGuiMouseButton_Right ) )
        {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            if ( delta.x != 0.f || delta.y != 0.f )
            {
                m_CamYaw -= delta.x * 0.005f;
                m_CamPitch = std::clamp( m_CamPitch - delta.y * 0.005f, -1.5f, 1.5f );
                UpdatePreviewCamera();
                m_PreviewDirty = true;
            }
        }
        else
        {
            m_OrbitDragging = false;
        }
    }

    // ── Overlay: camera reset ──
    const ImVec2 overlayPos = { imagePos.x + avail.x - 80.f, imagePos.y + 8.f };
    ImGui::SetCursorScreenPos( overlayPos );
    ImGui::PushStyleColor( ImGuiCol_Button, { 0.15f, 0.15f, 0.18f, 0.80f } );
    if ( ImGui::Button( ICON_MDI_HOME "  Reset##cam", { 72.f, 22.f } ) )
        ResetCamera();
    ImGui::PopStyleColor();

    // ── Gizmo (Move tool) ──
    DrawGizmo();
}

// ─── Gizmo ───────────────────────────────────────────────────────────────────
void MeshEditorPanel::DrawGizmo()
{
    if ( m_CurrentTool != EditorTool::Move || m_Selection.VertexIndices.empty() )
        return;

    const auto& mainCamera = m_PreviewScene->GetMainCamera().lock();
    if ( !mainCamera )
        return;

    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect( m_ViewportX, m_ViewportY, m_ViewportW, m_ViewportH );
    ImGuizmo::SetOrthographic( false );

    const glm::mat4 view  = mainCamera->GetViewMatrix();
    const glm::mat4 proj  = mainCamera->GetProjectionMatrix();
    const glm::mat4 model = GetTargetModelMatrix();

    // Gizmo sits at the selection centroid in WORLD space (local centroid pushed through the entity
    // transform), so it lines up with the transformed mesh shown in the preview.
    const glm::vec3 localCenter = m_Selection.GetCenter( *m_Mesh );
    const glm::vec3 worldCenter = glm::vec3( model * glm::vec4( localCenter, 1.f ) );
    glm::mat4       gizmoWorld  = glm::translate( glm::mat4( 1.f ), worldCenter );

    if ( ImGuizmo::Manipulate( glm::value_ptr( view ), glm::value_ptr( proj ),
                               ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
                               glm::value_ptr( gizmoWorld ) ) )
    {
        glm::vec3 newWorldCenter, scale, skew;
        glm::quat rot;
        glm::vec4 persp;
        glm::decompose( gizmoWorld, scale, rot, newWorldCenter, skew, persp );

        // World delta back into mesh-local space, then move the selection (coincident-aware).
        const glm::vec3 deltaWorld = newWorldCenter - worldCenter;
        const glm::vec3 deltaLocal = glm::vec3( glm::inverse( model ) * glm::vec4( deltaWorld, 0.f ) );
        MoveSelectedVerticesLocal( deltaLocal );
    }
}

// ─── Operations ──────────────────────────────────────────────────────────────
void MeshEditorPanel::CommitMeshEdit()
{
    m_Mesh->Invalidate();
    // Keep preview entity in sync
    if ( m_PreviewEntity && m_PreviewEntity.HasComponent<ECS::StaticMeshComponent>() )
        m_PreviewEntity.GetComponent<ECS::StaticMeshComponent>().RuntimeMesh = m_Mesh;
    m_PreviewDirty = true;
}

void MeshEditorPanel::RecalcNormals()
{
    auto& verts   = m_Mesh->GetVertices();
    const auto& indices = m_Mesh->GetIndices();

    for ( auto& v : verts )
        v.Normal = glm::vec3( 0.f );

    for ( const auto& tri : indices )
    {
        if ( tri.V1 >= verts.size() || tri.V2 >= verts.size() || tri.V3 >= verts.size() )
            continue;
        const glm::vec3 e1 = verts[tri.V2].Position - verts[tri.V1].Position;
        const glm::vec3 e2 = verts[tri.V3].Position - verts[tri.V1].Position;
        const glm::vec3 fn = glm::cross( e1, e2 );
        verts[tri.V1].Normal += fn;
        verts[tri.V2].Normal += fn;
        verts[tri.V3].Normal += fn;
    }

    for ( auto& v : verts )
        if ( glm::length( v.Normal ) > 1e-6f )
            v.Normal = glm::normalize( v.Normal );

    m_Mesh->Update( verts, m_Mesh->GetIndices() );
}

void MeshEditorPanel::WeldVertices( float threshold )
{
    auto& verts   = m_Mesh->GetVertices();
    auto& indices = m_Mesh->GetIndices();

    std::vector<uint32_t> remap( verts.size() );
    std::vector<Vertex>   newVerts;
    newVerts.reserve( verts.size() );

    for ( uint32_t i = 0; i < (uint32_t)verts.size(); ++i )
    {
        bool found = false;
        for ( uint32_t j = 0; j < (uint32_t)newVerts.size(); ++j )
        {
            if ( glm::distance( verts[i].Position, newVerts[j].Position ) <= threshold )
            {
                remap[i] = j;
                found    = true;
                break;
            }
        }
        if ( !found )
        {
            remap[i] = (uint32_t)newVerts.size();
            newVerts.push_back( verts[i] );
        }
    }

    for ( auto& tri : indices )
    {
        tri.V1 = remap[tri.V1];
        tri.V2 = remap[tri.V2];
        tri.V3 = remap[tri.V3];
    }

    verts = std::move( newVerts );
    m_Selection.VertexIndices.clear();
    m_Mesh->Update( verts, indices );
    CommitMeshEdit();
}

void MeshEditorPanel::FlipNormals()
{
    for ( auto& v : m_Mesh->GetVertices() )
        v.Normal = -v.Normal;
    m_Mesh->Update( m_Mesh->GetVertices(), m_Mesh->GetIndices() );
}

void MeshEditorPanel::SelectAll()
{
    m_Selection.VertexIndices.clear();
    const size_t n = m_Mesh->GetVertices().size();
    m_Selection.VertexIndices.resize( n );
    for ( size_t i = 0; i < n; ++i )
        m_Selection.VertexIndices[i] = i;
}

void MeshEditorPanel::ClearSelection()
{
    m_Selection.VertexIndices.clear();
}

glm::mat4 MeshEditorPanel::GetTargetModelMatrix() const
{
    if ( m_TargetEntity && m_TargetEntity.HasComponent<ECS::TransformComponent>() )
        return m_TargetEntity.GetComponent<ECS::TransformComponent>().GetTransform();
    return glm::mat4( 1.f );
}

void MeshEditorPanel::MoveSelectedVerticesLocal( const glm::vec3& deltaLocal )
{
    if ( !m_Mesh || m_Selection.VertexIndices.empty() || deltaLocal == glm::vec3( 0.f ) )
        return;

    auto& verts = m_Mesh->GetVertices();

    // Snapshot the selected positions first; then move EVERY vertex coincident with one of them. Meshes
    // imported/generated with per-face splitting have several vertices at the same position (distinct
    // normals/UVs) — moving only the selected index would tear a seam open ("gap").
    std::vector<glm::vec3> selPositions;
    selPositions.reserve( m_Selection.VertexIndices.size() );
    for ( size_t idx : m_Selection.VertexIndices )
        if ( idx < verts.size() )
            selPositions.push_back( verts[idx].Position );

    constexpr float eps = 1e-5f;
    for ( auto& v : verts )
    {
        for ( const auto& sp : selPositions )
        {
            if ( glm::distance( v.Position, sp ) <= eps )
            {
                v.Position += deltaLocal;
                break;
            }
        }
    }

    if ( m_AutoRecalcNormals )
        RecalcNormals();
    m_Mesh->Update( verts, m_Mesh->GetIndices() );
    m_PreviewDirty = true;
}

// ─── Preview scene ────────────────────────────────────────────────────────────
void MeshEditorPanel::InitPreviewScene()
{
    m_PreviewRenderer = std::make_unique<Graphic::SceneRenderer>();
    m_PreviewScene    = std::make_shared<::Desert::Core::Scene>( "MeshPreview", m_PreviewRenderer.get() );
    m_PreviewScene->Init();

    // Camera
    m_PreviewCamera = m_PreviewScene->CreateNewEntity( "PreviewCamera" );
    m_PreviewCamera.AddComponent<ECS::CameraComponent>().Data.IsMainCamera = true;

    // Light
    auto light = m_PreviewScene->CreateNewEntity( "PreviewLight" );
    light.AddComponent<ECS::DirectionLightComponent>();
    light.GetComponent<ECS::TransformComponent>().Translation = { 2.f, 3.f, 2.f };

    // Mesh entity
    m_PreviewEntity = m_PreviewScene->CreateNewEntity( "EditTarget" );
    m_PreviewEntity.AddComponent<ECS::StaticMeshComponent>().RuntimeMesh = m_Mesh;

    m_PreviewScene->AddSystem<ECS::MeshECSSystem>();

    ResetCamera();
}

void MeshEditorPanel::UpdatePreviewCamera()
{
    if ( !m_PreviewCamera )
        return;

    const float x = m_CamDistance * std::cos( m_CamPitch ) * std::sin( m_CamYaw );
    const float y = m_CamDistance * std::sin( m_CamPitch );
    const float z = m_CamDistance * std::cos( m_CamPitch ) * std::cos( m_CamYaw );

    auto& tc         = m_PreviewCamera.GetComponent<ECS::TransformComponent>();
    tc.Translation   = m_CamTarget + glm::vec3{ x, y, z };
    tc.Rotation      = { -m_CamPitch, m_CamYaw, 0.f };  // Euler radians
}

float MeshEditorPanel::ComputeOrbitDistance() const
{
    if ( !m_Mesh || m_Mesh->GetVertices().empty() )
        return 5.f;

    const glm::mat4 model = GetTargetModelMatrix();
    glm::vec3       mn{ 1e9f }, mx{ -1e9f };
    for ( const auto& v : m_Mesh->GetVertices() )
    {
        const glm::vec3 p = glm::vec3( model * glm::vec4( v.Position, 1.f ) );
        mn = glm::min( mn, p );
        mx = glm::max( mx, p );
    }
    return std::max( 0.5f, glm::length( mx - mn ) * 1.4f );
}

void MeshEditorPanel::ResetCamera()
{
    if ( m_Mesh && !m_Mesh->GetVertices().empty() )
    {
        const glm::mat4 model = GetTargetModelMatrix();
        glm::vec3       mn{ 1e9f }, mx{ -1e9f };
        for ( const auto& v : m_Mesh->GetVertices() )
        {
            const glm::vec3 p = glm::vec3( model * glm::vec4( v.Position, 1.f ) );
            mn = glm::min( mn, p );
            mx = glm::max( mx, p );
        }
        m_CamTarget   = ( mn + mx ) * 0.5f;
        m_CamDistance = ComputeOrbitDistance();
    }
    else
    {
        m_CamTarget   = { 0.f, 0.f, 0.f };
        m_CamDistance = 5.f;
    }
    m_CamYaw   = 0.5f;
    m_CamPitch = 0.35f;
    UpdatePreviewCamera();
    m_PreviewDirty = true;
}

// ─── SetTarget / ClearTarget ─────────────────────────────────────────────────
void MeshEditorPanel::SetTarget( ECS::Entity entity )
{
    m_TargetEntity = entity;
    m_SowPanel     = true;
    m_Selection.VertexIndices.clear();
    m_VertexPageOffset = 0;
    m_SearchBuf[0]     = '\0';

    if ( m_TargetEntity.HasComponent<ECS::StaticMeshComponent>() )
    {
        auto& smc = m_TargetEntity.GetComponent<ECS::StaticMeshComponent>();

        if ( !smc.RuntimeMesh )
        {
            if ( smc.Primitive.has_value() )
            {
                auto dm     = Geometry::PrimitiveMeshFactory::Create( smc.Primitive.value() );
                smc.RuntimeMesh = std::make_shared<DynamicMesh>( dm->GetVertices(),
                                                                  dm->GetIndices(),
                                                                  dm->GetSubmeshes() );
                smc.RuntimeMesh->Invalidate();
            }
            else if ( smc.MeshHandle )
            {
                auto* asset = Runtime::ResourceRegistry::GetMeshService()->GetAsset( smc.MeshHandle );
                if ( asset && !asset->IsSkinned() )
                {
                    auto* sa = static_cast<Assets::StaticMeshAsset*>( asset );
                    smc.RuntimeMesh = std::make_shared<DynamicMesh>( sa->GetVertices(),
                                                                      sa->GetIndices(),
                                                                      sa->GetSubmeshes() );
                    smc.RuntimeMesh->Invalidate();
                }
            }
        }
        m_Mesh = smc.RuntimeMesh;
    }

    if ( m_PreviewScene )
    {
        if ( m_PreviewEntity && m_PreviewEntity.HasComponent<ECS::StaticMeshComponent>() )
            m_PreviewEntity.GetComponent<ECS::StaticMeshComponent>().RuntimeMesh = m_Mesh;
        ResetCamera();
    }
}

void MeshEditorPanel::ClearTarget()
{
    m_TargetEntity = {};
    m_Mesh         = nullptr;
    m_Selection.VertexIndices.clear();
    m_SowPanel = false;
}

} // namespace Desert::Editor
