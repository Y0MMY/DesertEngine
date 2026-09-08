#pragma once

#include <Editor/Core/FuzzyMatch.hpp>

#include <Common/Core/ResultStr.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
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

    /**
     * @brief WHAT THE PALETTE OFFERS FOR A QUERY, AND IN WHAT ORDER. Free, pure, and testable.
     *
     * ── WHY THIS IS OUT HERE AND NOT INSIDE Draw() ──────────────────────────────────────────────────
     *
     * A6-2 point 3. `CommandPalette.cpp` is compiled by no test suite, which by itself distinguishes it
     * from nothing — 85 % of this repository's translation units are not (scripts/CI/UnreachedSources.sh).
     * What distinguished it was that A6-1 had just put two things worth checking INTO it: when the
     * dictionary is built, and what the ranking does with it. Both were unreachable, so the only
     * evidence either worked was a photograph of the overlay.
     *
     * So the parts that are DECISIONS moved out here, where a suite can drive them with a plain vector
     * and no ImGui, no window and no GPU — the same split ResolveCommand and the state writers already
     * use, and for the same reason. What stays in the .cpp is the part that is genuinely a drawing: the
     * popup's lifecycle, the keyboard focus, the scroll. That half remains unreachable by a test and is
     * checked the only way it can be — by looking at a frame of it (the control channel can now open the
     * palette by name, so an unattended run can take that picture).
     *
     * THE MATCH IS FuzzyMatch's, unchanged: it decides what is OFFERED and in what order, which is the
     * opposite of ResolveCommand, where fuzzy matching decides suggestions and never what RUNS. A person
     * reading a ranked list is present to see what they pick; a client addressing a command is not.
     */
    struct PaletteHit
    {
        const PaletteCommand* Command = nullptr;
        int                   Score   = 0;
    };

    /// The entries whose LABEL matches @p query, best first.
    ///
    /// INLINE, IN THE HEADER, and that is the point rather than a detail. Defined in CommandPalette.cpp
    /// it linked only for targets that compile that file — which is every target EXCEPT a test, because
    /// the .cpp needs ImGui. The first version of this was exactly that, and the suite that exists to
    /// reach the ranking could not link it. A decision that can only be built inside an untestable
    /// translation unit is still untestable.
    [[nodiscard]] inline std::vector<PaletteHit> RankPaletteCommands( const std::vector<PaletteCommand>& commands,
                                                                      std::string_view                   query )
    {
        std::vector<PaletteHit> hits;
        hits.reserve( commands.size() );
        for ( const PaletteCommand& command : commands )
        {
            int score = 0;
            if ( FuzzyMatch( query, command.Label, score ) )
                hits.push_back( PaletteHit{ &command, score } );
        }

        // STABLE, so equal scores keep the dictionary's order. An unstable sort would let two openings
        // that found the same entries offer them in different orders, and the first row is what Enter
        // runs — the palette would obey a different command on a second press of the same keys.
        std::stable_sort( hits.begin(), hits.end(),
                          []( const PaletteHit& a, const PaletteHit& b ) { return a.Score > b.Score; } );
        return hits;
    }

    /// Put @p selected back inside a list of @p hitCount entries. Zero for an empty list — there is no
    /// row to have selected, and any other answer would index one.
    [[nodiscard]] inline int ClampPaletteSelection( int selected, std::size_t hitCount )
    {
        if ( hitCount == 0 )
            return 0;
        const int last = static_cast<int>( hitCount ) - 1;
        return selected < 0 ? 0 : ( selected > last ? last : selected );
    }

    /// Up/Down wrap around the ends, which is what makes Down from the last row reach the first without
    /// a second keystroke. Given a CLAMPED selection moved by one, so the input is in [-1, hitCount].
    [[nodiscard]] inline int WrapPaletteSelection( int selected, std::size_t hitCount )
    {
        if ( hitCount == 0 )
            return 0;
        const int count = static_cast<int>( hitCount );
        return ( ( selected % count ) + count ) % count;
    }

    // A Ctrl+P "go to anything" overlay. The owner calls Open() on the hotkey, fills SetCommands() ONCE
    // per opening with that moment's candidates (panels, entities, actions, openable files), and calls
    // Draw() every frame — Draw only renders while open. Filtering/ranking uses FuzzyMatch; Up/Down move,
    // Enter runs, Esc closes.
    class CommandPalette
    {
    public:
        /// INLINE, so the flag protocol below is reachable by a test. It touches nothing but members —
        /// the ImGui half of opening (the popup, the keyboard focus) is Draw's, and stays there.
        void Open()
        {
            m_Open          = true;
            m_JustOpened    = true;
            m_NeedsCommands = true;
            m_Selected      = 0;
            m_Query[0]      = '\0';
        }
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
