#include "HistoryPanel.hpp"

#include <Editor/Core/CommandHistory.hpp>

#include <ImGui/imgui.h>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    void HistoryPanel::OnUIRender()
    {
        auto&       history = CommandHistory::Get();
        const auto& undo    = history.UndoStack();
        const auto& redo    = history.RedoStack();

        ImGui::Text( "Revision %llu", static_cast<unsigned long long>( history.Revision() ) );
        ImGui::SameLine();
        ImGui::TextDisabled( "(%zu undo / %zu redo)", undo.size(), redo.size() );

        ImGui::BeginDisabled( undo.empty() );
        if ( ImGui::Button( "Undo" ) )
            history.Undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( redo.empty() );
        if ( ImGui::Button( "Redo" ) )
            history.Redo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if ( ImGui::Button( "Clear" ) )
            history.Clear();

        ImGui::Separator();

        // The stacks mutate as we undo/redo, which would invalidate the iteration — so record the requested
        // jump during the draw and apply it once, after the child.
        int applyUndo = 0; // number of Undo() steps to run
        int applyRedo = 0; // number of Redo() steps to run

        ImGui::BeginChild( "##historyList" );

        // Undo stack: index 0 = oldest, back() = the next Undo target. Clicking entry i reverts everything
        // from the top down to (and including) i -> undo (count - i) times.
        for ( size_t i = 0; i < undo.size(); ++i )
        {
            ImGui::PushID( static_cast<int>( i ) );
            ImGui::Bullet();
            ImGui::SameLine();
            if ( ImGui::Selectable( undo[i]->GetLabel().c_str() ) )
                applyUndo = static_cast<int>( undo.size() - i );
            ImGui::PopID();
        }

        // Current-state marker sits between the applied (undo) and undone (redo) entries.
        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 0.7f, 0.3f, 1.0f ) );
        ImGui::TextUnformatted( undo.empty() ? "* (initial state)" : "* current state" );
        ImGui::PopStyleColor();

        // Redo stack: back() = the next Redo target. Listed next-first (top) so clicking replays forward.
        for ( size_t k = 0; k < redo.size(); ++k )
        {
            const size_t idx = redo.size() - 1 - k; // back()..front()
            ImGui::PushID( static_cast<int>( 1000 + k ) );
            ImGui::TextDisabled( " " );
            ImGui::SameLine();
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.6f, 0.6f, 0.6f, 1.0f ) );
            if ( ImGui::Selectable( redo[idx]->GetLabel().c_str() ) )
                applyRedo = static_cast<int>( k + 1 );
            ImGui::PopStyleColor();
            ImGui::PopID();
        }

        ImGui::EndChild();

        for ( int s = 0; s < applyUndo; ++s )
            history.Undo();
        for ( int s = 0; s < applyRedo; ++s )
            history.Redo();
    }
} // namespace Desert::Editor
