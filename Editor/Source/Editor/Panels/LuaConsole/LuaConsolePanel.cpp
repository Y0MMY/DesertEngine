#include "LuaConsolePanel.hpp"

#include <Editor/Core/IconsMaterialDesignIcons.hpp>

#include <Engine/Scripting/ScriptEngine.hpp>

#include <ImGui/imgui.h>

#include <cctype>
#include <cstring>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    namespace
    {
        int HistoryCallbackThunk( ImGuiInputTextCallbackData* data )
        {
            auto* self = static_cast<LuaConsolePanel*>( data->UserData );
            return self->OnHistoryCallback( data );
        }
    } // namespace

    LuaConsolePanel::LuaConsolePanel( ::Desert::Core::Scene* scene,
                                      ::Desert::Assets::AssetManager* assetManager )
         : IPanel( "Lua Console", /*showPanel=*/false ), m_Scene( scene ), m_AssetManager( assetManager )
    {
        m_Log.push_back( { "Lua console — runs against the live scene. Try: print(1+2)", false, false } );
    }

    LuaConsolePanel::~LuaConsolePanel() = default;

    int LuaConsolePanel::OnHistoryCallback( void* dataPtr )
    {
        auto* data = static_cast<ImGuiInputTextCallbackData*>( dataPtr );
        if ( m_History.empty() )
            return 0;

        const int prev = m_HistoryPos;
        if ( data->EventKey == ImGuiKey_UpArrow )
            m_HistoryPos = m_HistoryPos < 0 ? static_cast<int>( m_History.size() ) - 1
                                            : ( m_HistoryPos > 0 ? m_HistoryPos - 1 : 0 );
        else if ( data->EventKey == ImGuiKey_DownArrow && m_HistoryPos >= 0 )
            m_HistoryPos = m_HistoryPos + 1 < static_cast<int>( m_History.size() ) ? m_HistoryPos + 1 : -1;

        if ( prev != m_HistoryPos )
        {
            const std::string& line = m_HistoryPos >= 0 ? m_History[m_HistoryPos] : std::string();
            data->DeleteChars( 0, data->BufTextLen );
            data->InsertChars( 0, line.c_str() );
        }
        return 0;
    }

    void LuaConsolePanel::Execute()
    {
        std::string code = m_Input;
        // trim
        while ( !code.empty() && std::isspace( static_cast<unsigned char>( code.front() ) ) )
            code.erase( code.begin() );
        while ( !code.empty() && std::isspace( static_cast<unsigned char>( code.back() ) ) )
            code.pop_back();

        m_Input[0]       = '\0';
        m_HistoryPos     = -1;
        m_ReclaimFocus   = true;
        m_ScrollToBottom = true;
        if ( code.empty() )
            return;

        m_History.push_back( code );
        m_Log.push_back( { "> " + code, /*input*/ true, false } );

        if ( !m_Engine )
            m_Engine = std::make_unique<Scripting::ScriptEngine>( m_Scene, m_AssetManager );

        std::string output;
        const auto  result = m_Engine->EvalToString( code, output );

        if ( !output.empty() )
        {
            // Split captured output into lines so long results wrap sensibly.
            std::size_t start = 0;
            while ( start < output.size() )
            {
                const std::size_t nl = output.find( '\n', start );
                const std::size_t end = nl == std::string::npos ? output.size() : nl;
                if ( end > start )
                    m_Log.push_back( { output.substr( start, end - start ), false, false } );
                if ( nl == std::string::npos )
                    break;
                start = nl + 1;
            }
        }
        if ( !result )
            m_Log.push_back( { result.GetError(), false, /*error*/ true } );
    }

    void LuaConsolePanel::OnUIRender()
    {
        if ( ImGui::Button( ICON_MDI_DELETE_SWEEP "  Clear" ) )
            m_Log.clear();
        ImGui::SameLine();
        ImGui::TextDisabled( "Runs against the live scene (Entity / World / Input / Material bindings)." );

        const float inputH = ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild( "##luaOutput", ImVec2( 0.0f, -inputH ), true,
                           ImGuiWindowFlags_HorizontalScrollbar );
        for ( const auto& line : m_Log )
        {
            if ( line.IsError )
                ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 0.45f, 0.4f, 1.0f ) );
            else if ( line.IsInput )
                ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.55f, 0.8f, 1.0f, 1.0f ) );
            else
                ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.85f, 0.85f, 0.85f, 1.0f ) );
            ImGui::TextUnformatted( line.Text.c_str() );
            ImGui::PopStyleColor();
        }
        if ( m_ScrollToBottom )
        {
            ImGui::SetScrollHereY( 1.0f );
            m_ScrollToBottom = false;
        }
        ImGui::EndChild();

        ImGui::SetNextItemWidth( -1.0f );
        const ImGuiInputTextFlags flags =
             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory;
        if ( ImGui::InputText( "##luaInput", m_Input, sizeof( m_Input ), flags, &HistoryCallbackThunk,
                               this ) )
            Execute();

        ImGui::SetItemDefaultFocus();
        if ( m_ReclaimFocus )
        {
            ImGui::SetKeyboardFocusHere( -1 );
            m_ReclaimFocus = false;
        }
    }
} // namespace Desert::Editor
