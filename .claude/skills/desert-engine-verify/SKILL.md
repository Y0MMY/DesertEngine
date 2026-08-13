---
name: desert-engine-verify
description: >
  How to actually VERIFY a change in DesertEngine — render a frame and look at it, run every
  test suite rather than the ones whose name matches, and test the RELATION between two things
  rather than each thing alone. Use this before claiming a rendering, shader, material, sky,
  cloud, lighting or post-processing change works; before committing anything that touches a
  reflected component or a shader's bindings; and whenever tempted to report "builds and tests
  pass" as evidence that a picture is right. Encodes what the project's most expensive defects
  had in common.
---

# Verifying a change in DesertEngine

The engine's four most expensive defects — a tonemapper that reduced algebraically to the
identity, a radiance five thousand times too large, a cloud shadow map marching the antipodal
side of the planet, and an empty sky at 50% coverage — all shipped **built, tested, and unseen**.
None was catchable by a unit test. Each was a minute's work for anyone looking at a frame.

This skill is the counter-measure. It is short on purpose.

## 1. Render it and look

**The editor runs on this machine.** (A project memory said otherwise for a long time; that was
wrong and it cost a lot.) Vulkan works through MoltenVK, and the engine can render a scene to a
PNG with no human present:

```bash
cd Editor && ../build/Bin/Debug/Editor --project Desert.deproj \
    --scene Resources/Assets/Scenes/Clouds/Clouds_PartlyCloudy.desce \
    --shot /tmp/out.png --shot-frames 90 \
    --camera 0,200,0 --look 0,0.9,-1
```

- `--camera x,y,z` world units (1 unit = 1 cm), `--look x,y,z` a direction, need not be normalized.
- **`--shot-frames` matters.** Volumetric clouds accumulate over ~10 frames, so a shot taken on
  frame 1 is a picture of the dither. 90 is a safe default.
- It segfaults during teardown afterwards — a known shutdown bug. The PNG is already written.
- Interactive: `./scripts/MacOS/RunEditor.sh Debug` (sets the ICD, layer path, `DYLD_FALLBACK_LIBRARY_PATH`
  and `cd`s to `Editor/`, which is what makes `Resources/...` resolve).

To capture a **running** editor's window instead, get its id from `CGWindowListCopyWindowInfo` and
`screencapture -x -o -l<id>` — that works even when the window is behind others. Synthetic input
does *not* work (`osascript`/System Events has no assistive access), so the GUI cannot be driven;
drive the render through the flags above.

**Take at least two frames: what the change fixed, and what it could have broken.** These fail in
opposite directions and one alone is not evidence. For sky/clouds the pair is the zenith
(`--look 0,0.9,-1`) and the horizon (`--look 0,0.12,-1`) — shrinking a noise tile fixes the first
and destroys the second.

## 2. What is NOT verification

Do not offer these as evidence that a picture is right:

> "builds cleanly" · "tests pass" · "the logic is correct" · "should work" · "matches the paper"

All four were true of the tonemapper that did nothing. If a change touches what appears on screen
and the report has no frame in it, the change is unverified — say so plainly rather than dressing
it up.

## 3. Run every suite, not the matching one

Test `*.make` files **do not exist** until premake is re-run with the `CI` variable set — the test
projects are only generated then:

```bash
CI=true premake5 gmake
for f in *.make; do t="${f%.make}"
  case "$t" in Desert|Common|Editor|Runtime|GLFW|ImGui*|yaml-cpp|Jolt|Lua|Optick|MeshOptimizer|Dlib|ReflectCpp|DesertHeaderTool|FbxMeshSplitter|ProjectHub|DShaderTool|PakTool|BuildAllTests|RunAllTests) continue;; esac
  make -f "$f" config=debug -j8 >/dev/null 2>&1
  [ -x "build/Bin/Tests/Debug/$t" ] && ./build/Bin/Tests/Debug/$t 2>/dev/null | grep -q FAILED && echo "FAIL $t"
done
```

Three suites fire from changes nowhere near their name. Expect them and fix them with the truth,
not by adjusting the number until it passes:

| suite | fires when |
|---|---|
| `ComponentReflection` | any `PROPERTY` added to a reflected component — it pins an exact field census and per-category counts |
| `SettingConsumers` | same — every field must name the file that reads it or the task that owes a reader |
| `ShaderCacheKey` | a shader gains or loses a binding — and note it **compiles the shader with shaderc and reflects the SPIR-V**, so GLSL edits *are* compile-checked |

Shader maths in `Editor/Resources/Shaders/Common/*.glslh` is compiled **as C++** by the test
references, so a shader edit can break tests without touching a single `.cpp`.

## 4. Test the relation, not the function

The defect taxonomy of this project, from three audits and a render session:

| defect | what actually disagreed |
|---|---|
| flat slabs instead of clouds | noise tile 35 km vs layer thickness 3.5 km |
| empty zenith | weather-map scale vs what a ground camera sees |
| no aerial perspective | ray's shell entry vs the cloud's own distance |
| a preset clipped by a ring | `HorizonFadeEnd` vs `MaxViewDistance` |
| a seam at 30 km | shadow map vs cone march |
| white blown-out sky | tonemapper white point vs the operator it feeds |
| grey clouds | a C++ mirror of a gradient vs the shader's own formula |
| authored tints never loading | preset table vs the saved scenes |

**Not one is a wrong line in isolation.** Each side is individually correct, so a unit test of
either passes. When two values must agree, assert the agreement:

```cpp
EXPECT_LE( fadeEnd, maxViewDistance );                    // a range inside another range
EXPECT_LT( coarseMultiplier, emptySamplesBeforeCoarse );  // or the march stalls in place
EXPECT_NEAR( mapValue, coneValue, tol );                  // two implementations of one quantity
EXPECT_EQ( componentDefaults, presetRow );                // a mirror that must not drift
```

Two properties worth reaching for, because they catch whole classes rather than instances:

- **A bound.** "A step cannot scatter more light toward the eye than the source it integrates" —
  one assertion, and it catches any spurious factor of anything.
- **A monotonicity.** "More cloud lies above a low sample than a high one" — catches inversions
  and off-by-one indexing that a spot value never will.

## 5. Before you commit

1. `make Desert config=debug -j8` and `make Editor config=debug -j8` clean. New files ⇒
   `premake5 gmake` first (the generated makefiles list files explicitly).
2. Formatting with **llvm@18**, not the local toolchain:
   `git add` new files, then
   `/opt/homebrew/opt/llvm@18/bin/git-clang-format --binary /opt/homebrew/opt/llvm@18/bin/clang-format`.
   Local v22 disagrees with CI and passes work CI rejects.
3. The full sweep from §3 — zero failures.
4. Frames from §1 if anything on screen could have changed.
5. Report what you verified and *how*, and what you did not. A defect you name is a discussion; a
   defect found on review is a return.

## Related

- `Docs/Clouds/DEV_CONTRACT.md` §2.3, §2.3.1, §2.4 — the same rules as the project's own contract,
  with the history of why each exists.
- `Docs/Clouds/RESEARCH_REFERENCE.md` §M — what rendering showed that reading the code did not.
- The `desert-engine-dev` skill — how the engine is built (architecture, conventions, footguns).
  This skill is about proving a change works; that one is about writing it.
