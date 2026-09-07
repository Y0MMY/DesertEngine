#pragma once

#include <Engine/Graphic/DebugViewState.hpp>

namespace Desert::Editor::Core
{
    // WHAT ONE VIEWPORT IS DOING RIGHT NOW — and the whole point of this type is that it is NOT a
    // preference and never reaches a file.
    //
    // The distinction it exists to hold is the one К10 was opened for. Two different things can make the
    // ground grid absent from a viewport, and they have different owners and different lifetimes:
    //
    //   * THE USER'S ANSWER to "do I want a grid" — EditorPreferences::DebugView.ShowGrid. It is written
    //     when the user clicks the Show popup's Grid checkbox, it goes to ~/.desertengine/editor.json,
    //     and it outlives the session, the project and the scene.
    //   * A MODE'S SUPPRESSION — "this viewport is laying out a screen-space canvas, so the 3D ground
    //     grid is noise in it". It belongs to the viewport, it is true for exactly as long as the toggle
    //     is on, and nobody should ever see it again after the toggle goes off.
    //
    // BEFORE К10 THE SECOND WAS STORED IN THE FIRST. Entering 2D UI mode wrote `false` straight into the
    // preference struct and parked the user's real answer in a member of the panel, so any save of the
    // preference file — and there were twenty-six call sites — wrote "this user does not want a grid".
    // К2 fenced the two save sites that existed then by restoring the true value around `Save()`; К8 took
    // the count of unfenced ones to twenty-four. A fence per call site is a fence somebody forgets, and the
    // answer is not a twenty-fifth fence: it is that a mode must not have a field in the saved struct at all.
    //
    // So the suppression is applied HERE, to a COPY, on the view's way to the renderer, and the store is
    // never written. That is the same shape the corner orientation triad has always had in this viewport
    // (`if ( !m_Modes.UI2D ) DrawViewAxisGizmo(...)`) — a mode decides what it draws at the moment it
    // draws it, and stores nothing.
    struct ViewportModes
    {
        // The toolbar's "2D" toggle: in-scene UI editing. Hides the ground grid (below) and the corner
        // orientation triad (ViewportPanel::OnUIRender), because both are aids for reading a 3D world and
        // this mode is not looking at one.
        bool UI2D = false;
    };

    // THE VIEW A RENDERER MUST BE GIVEN: the user's answer, minus what the modes hide. Pure — in a state
    // and a set of modes, out a copy; no globals, no file, no GPU, so Desert/Tests/Editor/
    // PreferenceOwnership can assert the relation directly instead of inferring it from a rendered frame.
    //
    // It takes `user` by value-returning copy rather than mutating in place ON PURPOSE: an in-place
    // version would have exactly one plausible argument at the call site, the preference struct itself,
    // and that is the defect this function exists to make unspellable.
    Graphic::DebugViewState ApplyViewportModes( const Graphic::DebugViewState& user, const ViewportModes& modes );
} // namespace Desert::Editor::Core
