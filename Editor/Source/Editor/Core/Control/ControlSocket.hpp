#pragma once

#include <Common/Core/ResultStr.hpp>

#include <optional>
#include <string>

namespace Desert::Editor::Control
{
    /**
     * @brief The channel's transport: one local socket, one client, polled once a frame.
     *
     * OFF UNLESS ASKED FOR. An editor that listens without being told to is a hole, so this exists only
     * when `--control-socket <path>` names one. The path is an ARGUMENT and not a fixed location because
     * two editors on one machine must not fight over one socket — which they would, silently, with the
     * second one either failing to bind or stealing the first one's clients.
     *
     * A UNIX-DOMAIN SOCKET AND NOT A LOCAL TCP PORT. "Listens only on localhost" is a promise about a
     * configuration; a filesystem socket is a promise about the kernel — there is no network endpoint to
     * find, nothing answers a port scan, and the access control is the access control of a file, which
     * the operating system already enforces and the user already understands.
     *
     * NO THREAD, AND THAT IS DELIBERATE. The socket is non-blocking and drained once per frame from the
     * editor's own update. A listener thread would buy about sixteen milliseconds of latency and cost a
     * mutex around every piece of editor state the channel reads — which is most of it — plus the whole
     * class of defects that comes with a second thread touching ImGui-adjacent data. The editor draws at
     * 60 Hz; a frame of latency on a developer tool is not worth a race.
     *
     * ONE CLIENT AT A TIME. Commands execute on the editor thread between frames, so two clients would
     * be serialised regardless; accepting only one makes that visible instead of pretending otherwise.
     * A second connection is accepted and refused in words, rather than left hanging.
     */
    class ControlSocket
    {
    public:
        ControlSocket() = default;
        ~ControlSocket();

        ControlSocket( const ControlSocket& )            = delete;
        ControlSocket& operator=( const ControlSocket& ) = delete;

        /// Whether this platform has the transport at all. Windows does not, here — see the .cpp for why
        /// that is a refusal rather than an untested implementation.
        [[nodiscard]] static bool SupportedOnThisPlatform() noexcept;

        /// Bind and listen on @p path, or fail naming the reason. Never silently reuses a socket another
        /// editor is holding: an existing path is PROBED, and only a dead one is cleared away.
        [[nodiscard]] Common::BoolResultStr Listen( const std::string& path );

        void Close();

        [[nodiscard]] bool IsListening() const noexcept;
        [[nodiscard]] bool HasClient() const noexcept;

        /**
         * @brief Accept a waiting connection, flush pending output, and return ONE complete request line
         *        if a whole one has arrived. Never blocks.
         *
         * One line per call on purpose: a request may take several frames to answer (see FrameGate), and
         * reading a second while the first is in flight would put the channel's own ordering guarantee in
         * the client's hands. Bytes already received stay buffered and are offered on a later poll.
         */
        [[nodiscard]] std::optional<std::string> PollRequestLine();

        /// Queue one response line. Written on this call if the socket takes it, on a later poll if not —
        /// a full send buffer must not block the editor's frame.
        void SendResponseLine( const std::string& line );

        /// Whether a queued response is still waiting to go out. The editor holds the run open until this
        /// is false before honouring a `quit`, so the last answer is not lost to the exit.
        [[nodiscard]] bool HasUnsentOutput() const noexcept;

        void DropClient();

    private:
        void FlushOutput();
        /// Read whatever the client has sent into the incoming buffer, and NOTICE a client that has gone.
        /// A disconnected peer is only detected by reading from it, which is why this runs before a new
        /// connection is judged — see PollRequestLine.
        void DrainIncoming();

        // Raw descriptors rather than a platform type in the header: this file is included by EditorLayer,
        // and <sys/socket.h> in a header that also sees windows.h is a fight nobody needs.
        int         m_ListenFd = -1;
        int         m_ClientFd = -1;
        std::string m_Path;
        std::string m_Incoming; ///< bytes received, not yet a complete line
        std::string m_Outgoing; ///< bytes to send that the socket has not taken yet
    };
} // namespace Desert::Editor::Control
