#pragma once

#include <cstdint>
#include <optional>

namespace Desert::Editor
{
    // How an open scene view is NAMED once scene views can be closed.
    //
    // WHY THIS IS NOT AN INDEX. Every extra viewport carries an activation callback that has to say "make MY
    // document the active one". It used to capture the document's position in the vector by value. That is
    // correct exactly as long as nothing is ever removed, which was true only because closing a scene view
    // was not implemented: the document and its panel were appended and never erased, so the editor kept
    // rendering a full scene per closed view and kept one of the six renderer slots per closed view, for the
    // rest of the session.
    //
    // Adding the removal without changing the naming would have been WORSE than the leak it fixes. Erase
    // document 1 of 3 and document 2 slides into position 1: every surviving callback still holds a number
    // that is still in range and still resolves — to the wrong document. Clicking the third viewport would
    // silently bind the Outliner, Details, gizmo, undo stack and Play button to the second one's scene. No
    // crash, no log line, no assertion: an index is only ever wrong about WHICH, never about WHETHER.
    //
    // WHY AN ID AND NOT A POINTER. A raw SceneDocument* is stable across vector growth (the documents are
    // held by unique_ptr) and needs no lookup, which makes it the tempting answer. Its failure mode is the
    // problem: a callback that outlives its document dereferences freed memory — undefined behaviour, and
    // the one shape of defect this codebase cannot observe from a frame or a log. An id that outlives its
    // document resolves to nothing. Both are bugs; only one of them is a bug that ANSWERS. IndexOfSceneView
    // returning an empty optional is a state the caller can handle, log and — this is the point — assert on
    // in a test with no Vulkan device anywhere near it.
    //
    // WHY IDS ARE NEVER REUSED. A recycled id resurrects the exact defect the id was introduced to kill: a
    // stale callback would find a live document under its old name and activate a stranger, which is the
    // dangling index again wearing different clothes. So the source only ever counts up. At one view per
    // millisecond a 64-bit counter needs half a billion years to wrap, which is the honest reason there is
    // no wrap handling here rather than an unstated assumption.

    // The primary document — the main viewport, which always exists and cannot be closed. Zero is reserved
    // for it so that "no extra view is active" and "the primary is active" are the same state rather than
    // two that can disagree.
    inline constexpr uint64_t kPrimarySceneViewId = 0;

    // Hands out scene-view names. Monotonic, never reused; see the note above.
    class SceneViewIdSource
    {
    public:
        [[nodiscard]] uint64_t Next() noexcept
        {
            return ++m_Last;
        }

    private:
        uint64_t m_Last = kPrimarySceneViewId;
    };

    // Where the document named @p id currently sits in @p docs, or nothing if it is gone.
    //
    // A range + a projection rather than a container of ids, so that the id lives in the document and
    // nowhere else. A parallel vector of ids would be a second answer to "which documents exist", and the
    // two would disagree the first frame a close was handled halfway.
    template <typename Range, typename IdOf>
    [[nodiscard]] std::optional<size_t> IndexOfSceneView( const Range& docs, IdOf idOf, uint64_t id )
    {
        if ( id == kPrimarySceneViewId )
            return std::nullopt; // the primary is not IN the extra-document list at all

        size_t index = 0;
        for ( const auto& doc : docs )
        {
            if ( idOf( doc ) == id )
                return index;
            ++index;
        }
        return std::nullopt;
    }

    // Which document is active once @p closedId has been closed.
    //
    // Closing the view you are working in has to leave the editor bound to SOMETHING, and the primary is the
    // only document guaranteed to exist. Closing any other view must not move the user's focus at all —
    // tidying up a spare viewport is not a request to be teleported into a different scene, and because the
    // answer is an id rather than a position it stays correct while the documents behind it shuffle down.
    [[nodiscard]] inline uint64_t ActiveSceneViewAfterClose( uint64_t activeId, uint64_t closedId ) noexcept
    {
        return activeId == closedId ? kPrimarySceneViewId : activeId;
    }
} // namespace Desert::Editor
