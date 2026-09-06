#pragma once

#include <Common/Core/ResultStr.hpp>

#include <string>
#include <string_view>

namespace Desert::Editor::Core::Rules
{
    /**
     * @brief What the editor is allowed to do once a scene save has ANSWERED. Pure: a result and two
     *        names in, a verdict out — no ImGui, no disk, no globals.
     *
     * WHY THIS IS A FUNCTION AND NOT FOUR COPIES OF AN `if`. There are four places that save the open
     * scene — Ctrl+S, File -> Save, the command palette's "Save Scene", and the "Save and Open" button
     * of the unsaved-changes modal — and before this existed all four wrote the same three statements
     * in a row: save, clear the saved-revision mark, announce success. Unconditionally. The save chain
     * returned void, so "announce success" was the only thing any of them could do.
     *
     * The cost was not evenly spread. Ctrl+S on a read-only file or a full disk put the amber "unsaved
     * changes" star out and raised a green "Saved 'X'" toast over a scene that was still only in
     * memory. "Save and Open" did the same and then called LoadScene, which clears the command history
     * and the scene itself — so the user was asked "save before opening the other one?", said yes, and
     * the work they said yes FOR was destroyed in memory after failing to reach the disk. There was
     * nowhere left to recover it from.
     *
     * So the two permissions below are deliberately separate fields even though today they carry the
     * same value: they answer different questions ("is the file on disk current?" and "may I throw the
     * scene in memory away?"), and a future policy that clears the mark for, say, an autosave without
     * ever permitting a discard should have to change this function rather than one call site.
     */
    struct SaveVerdict
    {
        /// May the "unsaved changes" mark be cleared (s_SavedRevision advanced, the amber star put out)?
        /// FALSE on failure — the scene in memory is still the only current copy, and the star is the
        /// only thing telling the user so.
        bool MarkSceneSaved = false;

        /// May a step that DESTROYS the in-memory scene now run (LoadScene over it, New Scene, quit)?
        /// FALSE on failure, and this is the field the "Save and Open" button exists to read.
        bool MayDiscardScene = false;

        /// Is `Message` a failure? Drives the toast level and whether the log line is an error.
        bool IsError = false;

        /// What the user is shown. On failure it names the scene AND carries the reason the save chain
        /// gave, which already contains the destination path — a message that says only "save failed"
        /// sends the user to the log to find out which file, and the log is exactly what a user of a
        /// GUI editor does not have open.
        std::string Message;
    };

    /**
     * @param save      what Scene::Serialize answered.
     * @param sceneName the scene's name, for the message. Not used to decide anything.
     */
    [[nodiscard]] inline SaveVerdict DecideAfterSceneSave( const Common::BoolResultStr& save,
                                                           std::string_view             sceneName )
    {
        SaveVerdict verdict;
        if ( save.IsSuccess() )
        {
            verdict.MarkSceneSaved  = true;
            verdict.MayDiscardScene = true;
            verdict.IsError         = false;
            verdict.Message         = "Saved '" + std::string( sceneName ) + "'";
            return verdict;
        }

        verdict.MarkSceneSaved  = false;
        verdict.MayDiscardScene = false;
        verdict.IsError         = true;
        verdict.Message         = "'" + std::string( sceneName ) + "' was NOT saved — " + save.GetError() +
                          ". The scene is still open and still unsaved.";
        return verdict;
    }
} // namespace Desert::Editor::Core::Rules
