#pragma once

#include <ImGui/imgui.h>

#include <functional>
#include <utility>

namespace Desert
{
    class Window;
}

namespace Desert::Editor::UI
{
    /// WHAT THE OS STOPS DOING once an application asks for a window with no frame.
    ///
    /// The editor draws its own title bar (the main menu bar already carried the project name, the open
    /// level, the menus and the engine stats — the system bar above it was a SECOND title bar over the same
    /// window). Taking the system frame away takes four behaviours with it, and this class is where each
    /// one is either given back or named as lost:
    ///
    ///   given back  move the window by dragging empty space on the bar
    ///   given back  double-click the bar to maximize / restore
    ///   given back  minimize, maximize/restore and close buttons
    ///   given back  resize by dragging an edge or a corner — eight grips, only while not maximized
    ///
    ///   NOT given back, Windows: Aero Snap, snap assist, shake, and Win+Arrow. All four are the shell
    ///       reacting to a window it can resize, and GLFW's undecorated window is `WS_POPUP` without
    ///       `WS_THICKFRAME` or `WS_MAXIMIZEBOX` (ThirdParty/GLFW/src/win32_window.c, getWindowStyle).
    ///       Restoring them means keeping the caption styles and cancelling the non-client area with a
    ///       `WM_NCCALCSIZE` handler installed over GLFW's own window procedure — Win32-only code that
    ///       nothing on this machine can execute, only compile. У9 refused to ship a gesture it could not
    ///       run once; the honest state is that Windows keeps move, resize, the buttons and the
    ///       double-click, and loses the four shell gestures above.
    ///   NOT given back, macOS: the traffic lights and the native full-screen Space. The three buttons are
    ///       reproduced (on the RIGHT, with the other window commands, not as a look-alike traffic light
    ///       on the left — a control that looks native and is not is worse than one that plainly is not),
    ///       but `-toggleFullScreen:` needs `NSWindowStyleMaskTitled`, which a borderless window does not
    ///       have. Getting it back means a titled window with a full-size content view and hidden title,
    ///       which is Objective-C this engine has no compilation unit for.
    ///
    /// Nothing here stores window state. "Is the window maximized" is asked of the OS on every use — see
    /// Window::IsWindowMaximized — because a flag of ours beside the window manager's own answer is two
    /// owners of one fact, and they part company the first time anything else maximizes the window.
    ///
    /// WHAT HAS AND HAS NOT BEEN EXECUTED, because half of this file answers a mouse and this machine has
    /// no mouse to give it. Maximize and Restore have been driven end to end through the control channel
    /// and photographed at OS level in both states (the middle button's glyph follows the OS's answer, one
    /// square restored, two maximized). The resize ARITHMETIC has a suite of its own
    /// (Desert/Tests/Editor/WindowResize). The GESTURES — the drag, the double-click, and grabbing a grip —
    /// have never been performed: synthetic input is unavailable here, and У9 measured a SECOND door shut
    /// rather than assuming the first. `osascript`/System Events was already known to lack assistive
    /// access; `CGEventPost( kCGHIDEventTap, ... )` was tried next and the cursor did not move by one
    /// pixel — before 1028,698, after a posted move to 215,50 still 1028,698. Both doors are the same
    /// accessibility gate. Anyone extending this file should expect to test the gestures by hand.
    class WindowChrome
    {
    public:
        /// @p onCloseRequested is what the close button does. It is a callback and not a Window method
        /// because closing is the APPLICATION's decision, not the window's — the editor ends its run
        /// through the same Application::Close the control channel's `quit` uses, so there is one way to
        /// end a session rather than two that will drift.
        WindowChrome( Desert::Window& window, std::function<void()> onCloseRequested )
             : m_Window( window ), m_OnCloseRequested( std::move( onCloseRequested ) )
        {
        }

        /// The window's commands, drawn at the right-hand end of the main menu bar. Call INSIDE
        /// BeginMainMenuBar()/EndMainMenuBar(). Returns the width it occupied, so whatever is right-aligned
        /// beside it (the engine stats) can leave room.
        float DrawWindowButtons();

        /// The width DrawWindowButtons will take, asked BEFORE anything right-aligned is placed.
        [[nodiscard]] static float WindowButtonsWidth();

        /// Move and maximize/restore by the bar itself. Call INSIDE the main menu bar, after every item, so
        /// that "over the bar and over no item" is a question with an answer.
        void HandleTitleBarGestures();

        /// The eight resize grips, at the edges and corners of the main viewport. Call LAST in the frame,
        /// outside every other window. Does nothing while the window is maximized — an edge you can drag
        /// on a maximized window is a window that silently stops being maximized.
        void DrawResizeBorders();

    private:
        Desert::Window&       m_Window;
        std::function<void()> m_OnCloseRequested;

        // Drag state. The anchors are SCREEN coordinates on both sides, so moving the window under a
        // stationary cursor does not feed its own motion back in: with multi-viewports on, ImGui's
        // io.MousePos is already in OS absolute coordinates (imgui_impl_glfw.cpp adds the window position).
        bool   m_Dragging       = false;
        ImVec2 m_DragMouseStart = ImVec2( 0.0f, 0.0f );
        int    m_DragWindowX    = 0;
        int    m_DragWindowY    = 0;

        // Which grip is being dragged, as a pair of signs: -1 / 0 / +1 per axis, so (-1,-1) is the top-left
        // corner and (0,+1) the bottom edge. Zero on both axes means no resize is in progress.
        int    m_ResizeX          = 0;
        int    m_ResizeY          = 0;
        ImVec2 m_ResizeMouseStart = ImVec2( 0.0f, 0.0f );
        int    m_ResizeStartPosX  = 0;
        int    m_ResizeStartPosY  = 0;
        int    m_ResizeStartW     = 0;
        int    m_ResizeStartH     = 0;
    };
} // namespace Desert::Editor::UI
