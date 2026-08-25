# Per-pass GPU time

Four decisions in the cloud programme were taken on a number that could not see a single pass.

| decision | what was recorded | how it was measured |
|---|---|---|
| shadow ray 6 → 32 samples | ×1.87, 0.230 ms/sample | frame-count slope |
| cloud shadow map on the world | +4.92 ms, ×1.38 | frame-count slope |
| the quality tiers | 17.99 / 14.24 / 8.61 ms | frame-count slope |
| eight hero clouds | 1.39× | frame-count slope |

The slope method is honest and the developers kept its discipline — configurations interleaved in one
session, minimum of N, spread named. But `(t900 - t300) / 600` measures **the whole frame**. It cannot
say whether eighteen milliseconds are the march, the shadow map, the temporal resolve or the composite,
and the tiers were built by distributing a budget nobody had ever seen itemised.

This is the itemisation.

## What it measures, and where to read it

`DESERT_PROFILE_PASS( "name" )` is `DESERT_PROFILE_SCOPE` plus a pair of device timestamps around the
same scope, under the same name. There is no second list of pass names anywhere: the string written at
the site is the string the profiler shows, for both the CPU and the GPU column.

The numbers appear in the editor's **Profiler** panel (View → Profiler), in the same table and the same
row as the CPU time, with a `GPU` checkbox beside `Enabled`. Comparing the two columns on one frame is
half the point of the feature — `PresentFinalImage (Submit)` costing 14 ms of CPU is not work, it is the
CPU waiting on a GPU that is 17 ms deep.

A headless `--shot` run draws no ImGui, so `--gpu-profile` writes the same table to the log at the end of
the run, and `--no-gpu-timing` starts with timestamps off (what the A/B below is measured with).

Use `DESERT_PROFILE_PASS` at **pass** granularity only — a dispatch, a render pass, a stage of the frame.
Inner scopes keep `DESERT_PROFILE_SCOPE`: some of them run hundreds of times a frame, and a timestamp
pair per draw call would cost more than the draw and would say nothing the pass total does not.

## Three decisions that are not obvious

**Keyed by (frame × renderer slot), not by frame.** A query is a per-frame GPU resource written during
recording and read later, so `Docs/RENDERER_FRAME_STATE.md` applies to it in full. The editor runs
several live `SceneRenderer`s — viewport, asset thumbnails, previews — recording into **one** command
buffer in one frame. A pool keyed by frame alone would have the preview's `VolumetricClouds` land on top
of the viewport's, which is the exact shape of the bug that document exists because of, and which shows
up as a plausible wrong number rather than an error. The slot comes from
`EngineContext::GetActiveRendererSlot()`, the same ambient read every other per-frame resource uses.

**Results are read `MaxFramesInFlight` frames late, and never waited on.** `VulkanQueue::Present()`
already calls `vkWaitForFences` on the frame index it is about to reuse, so by the time frame index *f*
begins recording again, everything the GPU was asked to do the last time *f* was used has completed and
its queries are readable. Resolving right there costs nothing and cannot stall — the wait has already
happened, for reasons that have nothing to do with profiling. Reading in the same frame would mean
idling the GPU, which would make the profiler the most expensive pass in the frame it is measuring. On
this swapchain that is **three frames**; the display averages over a 0.5 s window, which is hundreds of
frames, so the lag is invisible in the number.

**Both timestamps at `BOTTOM_OF_PIPE`.** A begin marker at `TOP_OF_PIPE` fires as soon as prior work has
*started*, so every pass would appear to begin at once and the parts would sum to far more than the
whole. At `BOTTOM_OF_PIPE` a scope measures "from when everything before me had finished, to when I had
finished" — the parts then tile the frame and the sum is checkable against it, which is the entire point
of a breakdown.

## The inclusive column cannot be summed

Passes nest: `VolumetricClouds` contains `Clouds: ExecuteInFrame` contains `Clouds: March`. The first
breakdown this feature ever printed added the inclusive times and came to **159 % of its own frame**.

So every row carries a **self** time as well — its own interval with its direct children subtracted. For
any tree whose children lie inside their parents the self times partition the root exactly, and that is
the only column that may be added up. The arithmetic and the query-index layout are pure functions of
integers in `Engine/Graphic/GpuTimestampLayout.hpp`, asserted by `Desert/Tests/Engine/GpuTimestampLayout`
— including the relation that no two `(frame, slot)` pairs can ever reach the same query.

## Timestamp period

`VkPhysicalDeviceLimits::timestampPeriod` is nanoseconds per tick and differs between devices — it is
**1.0 on MoltenVK**, because Metal counts in nanoseconds already, and tens of nanoseconds on some AMD
parts. It is read into `DeviceCapabilities::TimestampPeriodNs` and logged at startup. A period of 1.0 is
correct here, not a missing conversion; the check that it is *right* is that the whole-frame GPU bracket
agrees with the wall-clock frame time, which it does to well under a per cent.
