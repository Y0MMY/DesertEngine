#pragma once

#include <Common/Core/ResultStr.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Desert::Editor
{
    /**
     * @brief One entry in the command palette. Group is a short category ("View", "Entity", "Action")
     *        shown dimmed beside the label; Run is invoked when the entry is chosen.
     *
     * ── RUN RETURNS ITS OUTCOME, AND THAT IS THE WHOLE OF A6-2 POINT 1 ──────────────────────────────
     *
     * It used to return `void`. The dictionary is also the control channel's vocabulary, so `run` over
     * the socket answered `{"ok":true}` for a command that had FAILED — a document that would not
     * resolve, a scene that would not save — and the only trace was a line in a log the client was not
     * reading. Failure read as success, which is the Ф4/Г13 shape this codebase spent a day removing:
     * a result that exists, is known, and is thrown away at the boundary.
     *
     * `Common::BoolResultStr`, exactly as `SetData` and the rest: the outcome is a VALUE that carries
     * its reason, not a severity in a log. Two consumers need it and neither could have it before —
     * the channel turns it into a refusal a script can stop on, and the palette turns it into a toast
     * the person who clicked can read.
     *
     * A SUCCESS FROM AN ENTRY THAT CANNOT FAIL IS NOT A PRETENCE, provided it says so. Most entries set
     * a flag, flip a preference or select an entity, and for those `PaletteCommandDone()` below is the
     * spelling — it is greppable, it reads as "there is no failure mode here", and it is distinguishable
     * from a genuine result that was tested and passed. What is forbidden is the third thing: an entry
     * that calls something fallible and returns Done anyway.
     */
    struct PaletteCommand
    {
        std::string                            Group;
        std::string                            Label;
        std::function<Common::BoolResultStr()> Run;
    };

    /// "This entry has no failure mode." Named rather than written as a bare MakeSuccess so the census
    /// is greppable: `PaletteCommandDone` marks the entries that CANNOT fail, and every other entry is
    /// expected to return something it actually got back from the thing it called.
    [[nodiscard]] inline Common::BoolResultStr PaletteCommandDone()
    {
        return Common::MakeSuccess( true );
    }

    /// The outcome of a `bool`-returning editor operation, given the words to explain a false.
    ///
    /// EXISTS BECAUSE THREE DOCUMENT OPERATIONS ANSWER `bool` AND NOTHING ELSE. ISubjectDocument's
    /// ApplyEdits, DiscardEdits and SaveDocument each return "did anything move", with no reason
    /// attached — so the palette can report THAT the command did nothing, and cannot report why. That is
    /// a real limit of that interface and it is named here rather than papered over: the message says
    /// what the editor knows, which is that the document declined, and does not invent a cause.
    [[nodiscard]] inline Common::BoolResultStr PaletteCommandOutcome( bool moved, const std::string& whenFalse )
    {
        return moved ? Common::MakeSuccess( true ) : Common::MakeError<bool>( whenFalse );
    }

    // A Ctrl+P "go to anything" overlay. The owner calls Open() on the hotkey, fills SetCommands() ONCE
    // per opening with that moment's candidates (panels, entities, actions, openable files), and calls
    // Draw() every frame — Draw only renders while open. Filtering/ranking uses FuzzyMatch; Up/Down move,
    // Enter runs, Esc closes.
    class CommandPalette
    {
    public:
        void Open();
        bool IsOpen() const
        {
            return m_Open;
        }

        /**
         * @brief "Has this been opened since you last filled it?" — true exactly once per opening.
         *
         * THE OWNER USED TO REBUILD THE DICTIONARY EVERY FRAME the overlay was up, sixty times a second
         * while somebody typed, and the dictionary is not cheap: it enumerates the scene's entities, the
         * open documents, the levels on disk and — since A6-1 — every openable file under the project's
         * content root. Two recursive directory walks per frame, for a list that cannot change while the
         * palette holds the keyboard.
         *
         * A SEPARATE FLAG FROM m_JustOpened, which Draw() consumes for the ImGui popup and the keyboard
         * focus. Sharing one would make the two consumers race: whichever ran first would clear it, and
         * either the palette would open unfocused or it would open showing the PREVIOUS session's list.
         */
        [[nodiscard]] bool TakeJustOpened()
        {
            const bool opened = m_NeedsCommands;
            m_NeedsCommands   = false;
            return opened;
        }

        void SetCommands( std::vector<PaletteCommand> commands )
        {
            m_Commands = std::move( commands );
        }

        /**
         * @brief Draw the overlay, and hand back what the chosen entry answered.
         *
         * SUCCESS ALSO MEANS "NOTHING WAS CHOSEN THIS FRAME", which is the common case sixty times a
         * second, and that conflation is deliberate: the caller's only job with this value is to show a
         * refusal, and "no command ran" has no refusal to show.
         *
         * THE REFUSAL IS HANDED OUT RATHER THAN SHOWN HERE. A toast raised from inside this file would
         * put a second UI dependency in the one class A6-2 point 3 is trying to make testable, and it
         * would decide for every caller how a refusal is presented. The layer that owns the toasts owns
         * that decision; this owns knowing what happened.
         */
        [[nodiscard]] Common::BoolResultStr Draw();

    private:
        bool                        m_Open       = false;
        bool                        m_JustOpened = false;
        // Set by Open(), cleared by TakeJustOpened(). See there for why it is not m_JustOpened.
        bool                        m_NeedsCommands = false;
        int                         m_Selected   = 0;
        char                        m_Query[128] = {};
        std::vector<PaletteCommand> m_Commands;
    };
} // namespace Desert::Editor
