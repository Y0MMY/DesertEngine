#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Desert::Editor::Control
{
    /**
     * @brief THE ORDER "COMMAND -> FRAME -> SNAPSHOT", MADE A PROPERTY INSTEAD OF A COINCIDENCE.
     *
     * The rule the channel owes its callers is one sentence: a reply leaves only after a frame that
     * already reflects the command it answers. Get it wrong and a snapshot overtakes its own action and
     * shows the state BEFORE it — the same class as "the first render after a shader edit is not
     * evidence", and just as convincing to look at.
     *
     * THE OBVIOUS FORMULATION IS FALSE, so it is worth saying why rather than only saying what. "Execute
     * at the top of OnUpdate, answer after that frame" sounds exact and is not: EditorLayer's update runs
     * a block of DEFERRED work — CloseDismissedSceneViews, ServiceDocumentCloses, ServiceSubjectOpenRequests,
     * a pending scene stop — and several commands land in a QUEUE there rather than taking effect where
     * they were issued. Opening a document goes through Core::SubjectOpenRequests; a document refused for
     * want of one of the six renderer slots only raises its dialog on the NEXT ImGui frame. For those, a
     * reply "one frame later" would be a reply about a frame that had not caught up, and it would be right
     * often enough to be trusted and wrong exactly when something interesting happened.
     *
     * SO THE GATE IS NOT COUNTED IN FRAMES, IT IS CONDITIONED ON QUIESCENCE. The reply leaves after a
     * frame that was both PRESENTED and rendered with NOTHING OUTSTANDING. That is a statement about the
     * editor rather than about arithmetic, and it stays true when somebody adds a fourth deferred queue —
     * provided they add it to the census below, which the compiler makes them do.
     *
     * Pure: no ImGui, no renderer, no globals, no clock. The editor samples the flags and drives this;
     * a suite drives it with flags of its own.
     */

    /// EVERY KIND OF WORK THAT CAN STILL BE OUTSTANDING WHEN A FRAME IS DRAWN.
    ///
    /// An enumeration and not a handful of bools, so the census below can be closed at COMPILE TIME. A
    /// bool added to a struct is a bool some `&&` chain forgets; an enumerator added here without a name
    /// beside it does not build. That matters more than it looks: a missing entry does not fail loudly,
    /// it makes the gate open one frame early, which is the exact defect this file exists to prevent and
    /// the hardest kind to see.
    enum class PendingWork : std::size_t
    {
        StartupLoading, ///< the staged boot is still running; the scene is not even rendered yet
        SceneLoad,      ///< a scene load is queued for between frames
        NewScene,       ///< Ctrl+N / File -> New, queued the same way
        SceneView,      ///< a new scene view (viewport + renderer + slot) is being added
        SceneStop,      ///< leaving Play, which tears down and recreates GPU render resources
        DocumentCloses, ///< documents dismissed but not yet destroyed behind the device-idle wait
        AssetOpens,     ///< asset documents requested but not yet built
        OpenRefusal,    ///< an open was refused and its dialog has not been raised yet
        Count
    };

    /// What each kind is CALLED when a refusal has to name it. Same order as the enumerators; the
    /// static_assert below is what keeps it that way.
    inline constexpr const char* kPendingWorkNames[] = {
         "the staged startup is still loading",
         "a scene load is queued",
         "a new scene is queued",
         "a new scene view is being added",
         "leaving Play is queued",
         "documents are waiting to be destroyed",
         "asset documents are waiting to be opened",
         "a refused open has not shown its dialog yet",
    };

    static_assert( std::size( kPendingWorkNames ) == static_cast<std::size_t>( PendingWork::Count ),
                   "Every PendingWork enumerator needs a name beside it. This is the census that keeps the "
                   "reply gate honest: a kind of outstanding work nobody named is a kind nothing waits for, "
                   "and the gate would open on a frame that had not caught up." );

    /// The editor's outstanding work, as one value. Held rather than queried so the SAMPLE that a frame
    /// was rendered under is the sample the gate later judges it by — asking again after the fact would
    /// answer about a different moment.
    class EditorQuiescence
    {
    public:
        void Set( PendingWork kind, bool pending ) noexcept
        {
            m_Pending[static_cast<std::size_t>( kind )] = pending;
        }

        [[nodiscard]] bool Get( PendingWork kind ) const noexcept
        {
            return m_Pending[static_cast<std::size_t>( kind )];
        }

        /// Nothing outstanding at all. A loop over the census rather than a chain of `&&`, which is the
        /// other half of why PendingWork is an enumeration: a chain can omit a term and still compile.
        [[nodiscard]] bool Settled() const noexcept
        {
            for ( bool pending : m_Pending )
                if ( pending )
                    return false;
            return true;
        }

        /// What is outstanding, in words, for a refusal. Empty when Settled().
        [[nodiscard]] std::string Describe() const
        {
            std::string description;
            for ( std::size_t i = 0; i < m_Pending.size(); ++i )
            {
                if ( !m_Pending[i] )
                    continue;
                if ( !description.empty() )
                    description += "; ";
                description += kPendingWorkNames[i];
            }
            return description;
        }

    private:
        std::array<bool, static_cast<std::size_t>( PendingWork::Count )> m_Pending{};
    };

    /// What a presented frame did to the gate.
    enum class GateVerdict
    {
        Idle,       ///< nothing is waiting on this gate
        Waiting,    ///< this frame did not discharge it (too early, or work still outstanding)
        Discharged, ///< THIS frame reflects the command; the reply may leave, the shot may be taken
        TimedOut,   ///< the editor never settled; the caller must REFUSE, not keep waiting
    };

    /**
     * @brief The gate itself: armed when a command is executed, discharged by the first frame that
     *        proves the command landed.
     *
     * One command in flight at a time. That is a decision and not a limitation of the implementation:
     * every command runs on the editor thread between frames, so two of them are sequential anyway, and
     * a queue would only let a client believe otherwise. It also makes the guarantee a client can state
     * in one line — "when the reply arrives, the frame that shows it has already been presented" —
     * instead of one per outstanding request.
     */
    class FrameGate
    {
    public:
        /// How many frames the gate will wait before refusing. Long enough for a document open behind a
        /// device-idle wait, a scene load and a Play/Stop cycle to all finish (four seconds at 60 Hz);
        /// short enough that a wedged editor answers rather than holding the client forever. A refusal
        /// that names what stayed outstanding is a diagnosis; silence is not.
        static constexpr uint32_t kMaxSettleFrames = 240;

        /// A command has just run, during the update of frame @p executedOnFrame. The gate now waits for
        /// that frame or a later one to be presented in a settled state.
        void ArmAfterExecution( uint64_t executedOnFrame ) noexcept
        {
            m_Armed           = true;
            m_ExecutedOnFrame = executedOnFrame;
            m_FramesWaited    = 0;
        }

        [[nodiscard]] bool IsArmed() const noexcept
        {
            return m_Armed;
        }

        [[nodiscard]] uint32_t FramesWaited() const noexcept
        {
            return m_FramesWaited;
        }

        /**
         * @brief A frame has been PRESENTED. Judge it.
         *
         * @param frame       That frame's index, from the same counter ArmAfterExecution was given.
         * @param quiescence  The outstanding work sampled while that frame was being BUILT — after the
         *                    deferred queues drained and before the scene was rendered. Sampled then and
         *                    not now, because "was anything outstanding when this picture was made" is
         *                    the question; by the time the frame is out, the answer has moved on.
         *
         * Discharges AT MOST ONCE per arming: the verdict is Discharged on exactly the frame that proves
         * the command, and Idle from then until the gate is armed again. A gate that could discharge
         * twice would send two replies to one request.
         */
        [[nodiscard]] GateVerdict ObserveFramePresented( uint64_t                frame,
                                                         const EditorQuiescence& quiescence ) noexcept
        {
            if ( !m_Armed )
                return GateVerdict::Idle;

            // A frame that STARTED before the command ran cannot show it, whatever its quiescence says.
            // This is the ordering half of the guarantee, and it is separate from the settling half on
            // purpose: the two failures it prevents are different ones, and a single condition covering
            // both would be right for the wrong reason.
            if ( frame < m_ExecutedOnFrame )
                return GateVerdict::Waiting;

            ++m_FramesWaited;

            if ( quiescence.Settled() )
            {
                m_Armed = false;
                return GateVerdict::Discharged;
            }

            if ( m_FramesWaited >= kMaxSettleFrames )
            {
                m_Armed = false;
                return GateVerdict::TimedOut;
            }

            return GateVerdict::Waiting;
        }

        /**
         * @brief Would THIS frame discharge the gate, asked without discharging it?
         *
         * Exists because a capture of the composited frame has to be RECORDED while that frame is still
         * being built — a swapchain image may only be touched between its acquire and its present, so a
         * copy issued after the present is a spec violation (measured: "vkQueueSubmit(): performs a layout
         * transition on presentable VkImage, but the image has not been acquired"). The editor therefore
         * has to know, before it submits, whether this is the frame the reply is waiting for.
         *
         * Same inputs, same answer as ObserveFramePresented: a frame this returns true for is a frame that
         * discharges. That equality is the point — if the two could disagree, the editor would record a
         * capture for a frame that then did not count, or skip one for a frame that did.
         */
        [[nodiscard]] bool WouldDischarge( uint64_t frame, const EditorQuiescence& quiescence ) const noexcept
        {
            return m_Armed && frame >= m_ExecutedOnFrame && quiescence.Settled();
        }

        /// Abandon the wait — the connection went away, or the editor is shutting down. Distinct from a
        /// discharge so a caller cannot mistake "nobody is listening" for "the frame proved it".
        void Disarm() noexcept
        {
            m_Armed        = false;
            m_FramesWaited = 0;
        }

    private:
        bool     m_Armed           = false;
        uint64_t m_ExecutedOnFrame = 0;
        uint32_t m_FramesWaited    = 0;
    };

    /// The refusal a TimedOut verdict owes its caller. Names what never finished, because "the editor did
    /// not settle" alone tells the reader nothing they can act on.
    [[nodiscard]] inline std::string DescribeSettleTimeout( const EditorQuiescence& quiescence,
                                                            uint32_t                framesWaited )
    {
        const std::string outstanding = quiescence.Describe();
        return "the editor did not reach a settled frame within " + std::to_string( framesWaited ) +
               " frames, so this reply cannot promise that a rendered frame reflects the command. Still "
               "outstanding: " +
               ( outstanding.empty() ? std::string( "nothing — which means the gate and the "
                                                    "census disagree, and that is a defect "
                                                    "in the channel itself" )
                                     : outstanding ) +
               ".";
    }
} // namespace Desert::Editor::Control
