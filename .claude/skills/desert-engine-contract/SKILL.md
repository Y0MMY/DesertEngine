---
name: desert-engine-contract
description: >
  The engineering contract this project is reviewed against — what may not ship (TODOs, stubs,
  dead settings, silent fallbacks, legacy paths), what every change must do (interface first,
  layers unmixed, one source of truth per value, centimetre units, per-frame renderer state),
  how migrations are done (immediately, in the files, tested), and how work is delivered and
  reviewed. Use this when starting or finishing a task, deciding whether something is ready to
  hand over, adding a parameter or a component field, replacing an existing subsystem, changing a
  serialized format, or working alongside other people on the same repo.
---

# The delivery contract

From `Docs/Clouds/DEV_CONTRACT.md`, written for the sky-and-clouds programme and applied to the
repository generally. That document is the authority and carries the history of why each rule
exists — usually a specific defect. This is its operative core. The full original (Russian) is
bundled next to this file as `DEV_CONTRACT.md` — read it when a rule's wording or its history
matters.

**How to use it:** the rules in §1 are absolute; a change that breaks one is returned regardless of
how good the rest is. Everything else is judgement, and the contract says which way to lean.

---

## 0. The rule the rest follow from

**Unfinished code does not exist in a branch.** If a task needs a dependency to exist first, write
the dependency and then the task. Not "a stub for now", not "we'll finish it later", not "left a
TODO for whoever comes next". A task ships whole or it does not ship.

That is expensive exactly once — at planning, when the size is counted honestly. After that it
saves weeks, because nobody builds on sand.

---

## 1. Cannot ship (any one of these is an automatic return)

1. **`TODO`, `FIXME`, `XXX`, `HACK` in new code.** None. If something is not done, it is not
   delivered. If something is deliberately out of scope, that is a line in the report, not a
   comment in the source.
2. **Stubs.** A function returning a constant "for now"; an empty body "so it links"; a parameter
   accepted and ignored.
3. **Dead settings.** Every parameter exposed to the outside must be wired end to end: component →
   serialization → editor UI → GPU → a visible effect. A slider that moves nothing is a TODO
   wearing a feature's clothes, and it is how systems end up where "half the settings don't work".
4. **Silent fallbacks.** A resource that did not load, a size that did not match, a shader that did
   not compile — log it with the reason and the actual numbers (`LOG_ERROR` with names and values).
   Never substitute a default quietly. Debugging blind is the most expensive thing in graphics.
5. **New third-party dependencies without written agreement.** OpenVDB at runtime in particular: no.
6. **Editing files another task owns.** Each task owns a file list. Need someone else's file — ask;
   the answer is either to widen the ownership or to sequence the tasks. A conflict in a shared
   header costs more than a minute of coordination.
7. **`git push` and anything touching `dev`** when working as one of several developers —
   integration is the lead's. Commits **inside your own worktree** are how work is handed over and
   are expected. *(This one is conditional: when working solo with the user, they routinely ask for
   commit-and-push directly. Follow what the user asks for now over the multi-developer default.)*
8. **Keeping legacy alive.** See §4 — the old path is deleted by the same change that replaces it.

---

## 2. Required

### Architecture

- **Interface before implementation.** The header first: types, signatures, resource ownership,
  invariants, with comments. Implementation after. If a task changes an agreed public interface,
  say so before changing it.
- **Layers do not mix.** `Engine/Core` knows nothing of Vulkan. `Engine/Graphic` knows nothing of
  ImGui or the editor. The editor does not reach into the renderer's internals. Data (an ECS
  component, serializable and POD-like) is separate from behaviour (a system or renderer).
- **One source of truth per value.** A parameter that lives in a component is not duplicated into
  `SceneSettings` "just in case". Duplicated state is a future desync bug.
- **RAII for resources.** Anything created on the GPU is released deterministically, waiting for
  device idle where that is required. Copy how the existing systems do it; do not invent a third way.
- **1 world unit = 1 centimetre.** Layer altitudes, radii, distances — all centimetres. Numbers from
  papers are almost always metres or kilometres: convert explicitly through `Common::Units::` and
  write the original figure in the comment. Half the mystery bugs in this engine were metre-era
  leftovers.

### Conformance to the engine

- **Per-frame renderer state is mandatory.** Any new uniform/storage buffer, material or descriptor
  set stores its state per (frame × renderer slot) — see `Docs/RENDERER_FRAME_STATE.md`. Breaking it
  looks like "the preview is peeking at the viewport camera" and takes days to find.
- **Errors through `Common::BoolResultStr` / `NO_DISCARD`**, as in the neighbouring code. Not
  exceptions, not a bare `bool`, not an `assert` standing in for handling.
- **Comments say WHY, in English.** What symptom does this solve, and what would happen if it were
  done naively. Not a restatement of the code.
- **Naming and formatting follow the neighbouring code.** No new style per subsystem.

### Verification

Its own skill: **`desert-engine-verify`**. In one line — if the change alters what appears on
screen, render a frame and look at it; "builds and tests pass" is not verification, and the four
most expensive defects in this project all shipped built, tested and unseen.

---

## 3. Definition of done

1. `make Desert config=debug -j8` and `make Editor config=debug -j8` build clean. New files ⇒
   `premake5 gmake` first (the generated makefiles list files explicitly, so new code silently
   misses the build otherwise).
2. No new compiler warnings.
3. Formatting clean on changed lines under **llvm@18**:
   `/opt/homebrew/opt/llvm@18/bin/git-clang-format --binary /opt/homebrew/opt/llvm@18/bin/clang-format`
   (`git add` new files first — untracked files are skipped locally and checked by CI). Local v22
   disagrees with CI. As a developer, check against **your branch's merge-base**, not `dev`;
   checking against `dev` reformats other people's landed lines into your diff.
4. No new TODOs, stubs or dead parameters.
5. Tests on the pure logic, written and passing — **all suites, not the matching one**, and frames
   if the render changed. See `desert-engine-verify`.
6. **A report.** What was done; what was decided and why; **what differs from the requirements and
   for what reason**; what was left out of scope deliberately. A divergence you name is a
   discussion. A divergence found on review is a return.

### A measured refusal counts as done

If the work turns out not to earn its cost, **the deliverable is the measurement and the refusal**,
not a feature nobody should have shipped. That is a completed task, and this project has accepted
several: an SDF step bound that could only ever fire in a sky with no clouds in it; a
quarter-resolution atlas that cost a third of the channel it was meant to preserve; a 3D mip
generator that was **built, measured at six tenths of a grey level, and removed again**; a weighted
metering mask whose own control series moved the frame by one 8-bit level.

The rules that make it a refusal rather than a shrug:

- **Numbers, not impressions.** Say what you measured, on what, and what the noise floor was.
- **Say what would change the answer.** ("This only pays once a stretch of sky can be declared
  voxel-only.") The next person needs to know when to revisit.
- **Record it where it will be found** — the commit message, the requirement entry, or a comment at
  the site. A refusal nobody can find gets re-derived.
- A hypothesis you **disproved** is worth the same treatment. Knowing that the height bands were not
  the horizon fringe, or that shrinking the weather cell makes "clouds look too low" worse, saved
  the next agent hours.

What is still forbidden: shipping something that never fires, a knob that hides a defect instead of
fixing it, or raising a budget constant until one camera angle looks acceptable.

---

## 4. No legacy — migrate immediately, and in the files

Compatibility "just in case" is how a project ends up with two paths forever, one of which nobody
tests.

1. **The old path is deleted by the change that replaces it.** A field moved to a new component is
   gone from the struct, the serialization, the UI and the shader. Not "deprecated but still read",
   not a "use the new system" flag, not an `#ifdef`, not two branches in the renderer.
2. **No double writes.** Never write a value to both the old and the new place "so nothing breaks".
3. **Data migrates once, at load, and is written back in the new form.** A scene has a version;
   the loader raises N to N+1 through an explicit migration and then works only with the new model.
   The runtime knows nothing about the old format.
4. **The migration function is pure and tested.** In: the old property tree. Out: the new one. No
   GPU, files or global state. The test covers missing and malformed fields.
5. **Scenes in the repository are converted by the same task.** Everything under
   `Resources/Assets/Scenes` is saved in the new format and committed. No file is left keeping the
   old path alive.
6. **Migration code has an expiry.** It raises a specific version to a specific version, and says so
   in a comment. It does not "support the old format" — it destroys it.
7. **Silent migration is forbidden.** Log which scene, from which version to which, and how many
   fields moved.

The same applies inside the code: when a new subsystem replaces an old one, the old implementation
is deleted, not kept "in case we roll back". Rolling back is called `git revert`.

---

## 5. Working in parallel

- **The shared backbone goes first, by one person** — component structures, serialization, editor
  registration. Nobody else starts until it exists, or three people edit one header at once.
- **After the backbone, work on non-overlapping files.** Shaders, the render pass, the editor UI.
- **Own your files, ask for anyone else's.**
- **Say which files you are fencing off**, and expect the same. When several people work at once the
  scope fence is what makes a merge mechanical instead of archaeological — and the one conflict this
  project actually hit was a scene file two people edited for unrelated reasons.
- **`git stash` is shared across worktrees on this machine.** One `stash pop` pulled another
  engineer's untracked files into an unrelated tree. Prefer explicit commits; if you must stash,
  apply by name.
- **The machine is shared too.** Rendering and timing while someone else renders produces numbers
  that measure them, not you — see `desert-engine-verify` §5a before quoting a slope.

---

## Related

- `Docs/Clouds/DEV_CONTRACT.md` — the authority, with the history behind each rule.
- `desert-engine-verify` — how to prove a change works (§2.3, §2.3.1, §2.4 of the contract).
- `desert-engine-dev` — how the engine is built: architecture, conventions, footguns.
