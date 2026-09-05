# Slate/UMG Debugging & Introspection Tools in Unreal Engine — research findings

All PRIMARY unless marked SECONDARY. URL follows each claim.

---

## 1. Widget Reflector (`SWidgetReflector`)

**Opening it**
- UE5: "select Tools > Debug > Widget Reflector"; also "type Ctrl+Shift+W or enter `WidgetReflector` into the console" — Ctrl+Shift+W and the `WidgetReflector` console command are both CONFIRMED. https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-slate-widget-reflector-in-unreal-engine
- UE4 (4.27) menu path is different: **Window > Developer Tools > Widget Reflector**, same Ctrl+Shift+W / `WidgetReflector` console command; also available as a standalone application. https://dev.epicgames.com/documentation/en-us/unreal-engine/widget-reflector?application_version=4.27
- Ctrl+Shift+W is repeated in Epic's UMG Best Practices: "Use the Widget Reflector (Ctrl+Shift+W) to get information and statistics about your Widgets." https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-best-practices-in-unreal-engine
- Slate Clipping doc also points at it: "The Widget Reflector indicates the clipping state for selected Widgets inside the Editor." https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-slate-clipping-system-in-unreal-engine

**Layout of the window** — five areas: main menu (demo modes, atlases, windows); Application Scale + Slate Debug Options; Widget Hierarchy; tab menu (snapshots, widget events, hit test grids); Widget Details. https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-slate-widget-reflector-in-unreal-engine

**Picking modes** (CONFIRMED names)
- **Pick Painted Widgets** — select/inspect rendered widgets. https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-slate-widget-reflector-in-unreal-engine
- **Pick Hit-Testable Widgets** — "useful for developers wanting to visualize and inspect a widget's hitbox"; hover the widget then **press ESC to retain focus** over it. Same URL.
- **Show Focus** — highlights the currently focused widget. Same URL.
- Note the doc's exact plural wording is "Pick Painted Widget**s**" / "Pick Hit-Testable Widget**s**" (not the singular in your brief).

**Widget details columns**: Widget Name, FG (foreground) Visibility, Focus, Clipping, Source (clickable → jumps to code in a connected IDE), Address (memory address, usable for debugger breakpoints). Right-click the property header to show more available properties. https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-slate-widget-reflector-in-unreal-engine and https://dev.epicgames.com/documentation/en-us/unreal-engine/widget-reflector?application_version=4.27

**Slate Debug Options checkboxes** (these are the UI equivalents of the cvars below): Invalidation Debugging, Invalidation Root Debugging, Update Debugging, Paint Debugging, Show Clipping ("displays how a widget was clipped"), Debug Culling ("debug when a widget is culled by another widget"), Widget Caching (driven by the InvalidationBox caching system; disabled under Global Invalidation). https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-slate-widget-reflector-in-unreal-engine
- There is **no option literally named "Visualize Layout Invalidation"** in the documented list — the closest is "Invalidation Debugging" / "Invalidation Root Debugging". UNCONFIRMED as a distinct feature.

**Snapshots ("Snapshot Widget Picker")**
- "Take Snapshot" saves an image plus the current state of all widgets in the application; you can then run Hit Test Grid against the snapshot and the result shows in the Widget Hierarchy. https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-slate-widget-reflector-in-unreal-engine
- Workflow: Options in the Widget Hierarchy area → optionally enable **Navigation Event Simulation** → select the target application → Take Snapshot. https://dev.epicgames.com/documentation/en-us/unreal-engine/widget-reflector?application_version=4.27
- Constraint stated by Epic: "Snapshots can only be taken from the editor's PIE mode and from a Standalone application." Same URLs.
- **`FWidgetSnapshotService` and the `.widgetsnapshot` file extension: UNCONFIRMED — could not verify from any Epic doc or API page.** The docs describe a target-application picker (which is how remote/other-process snapshots are taken) but never name the class or a file format, and the API reference search returned nothing for `FWidgetSnapshotService`. https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-slate-widget-reflector-in-unreal-engine

**Hit Test Grid tab flags**: "Visualize on Navigation" (snapshot mode only) and "Reject Widget Reflector navigation events". https://dev.epicgames.com/documentation/en-us/unreal-engine/widget-reflector?application_version=4.27

**Application Scale** — slider or manual entry, "for presentations, screenshots, or debugging". https://dev.epicgames.com/documentation/en-us/unreal-engine/widget-reflector?application_version=4.27

**Widget Events tab** — "flushes messages to an output log in the Widget Reflector while the user navigates over the UI" (focus, input, navigation events). https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-slate-widget-reflector-in-unreal-engine

**Other menu items**: Demo Mode (Mouse Click / Key visualisation, for presentations); Atlases (Display Texture Atlases, Display Font Atlases). https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-slate-widget-reflector-in-unreal-engine

- **`WidgetReflector.ShowHiddenInDetailedInfo`: UNCONFIRMED — could not verify.** No Epic doc, forum post, or community page returns this string; searches only surface the general Widget Reflector pages. https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-slate-widget-reflector-in-unreal-engine

---

## 2. Console Slate Debugger — the `SlateDebugger.*` family

Primary reference page (this is the authoritative command list): https://dev.epicgames.com/documentation/en-us/unreal-engine/console-slate-debugger-in-unreal-engine

- Purpose/usage: "While running the project in PIE mode, press the tilde (~) key to open the PIE Console, and type `SlateDebugger`. SlateDebugger logs are typically written to a `[ProjectName].txt` log file under `[ProjectName]/Saved/Logs`." Same URL.
- **`SlateDebugger.Start`** — "Alias for `SlateDebugger.Event.Start` that starts the Slate Console Debugger." **`SlateDebugger.Stop`** — alias for `SlateDebugger.Event.Stop`. Same URL. (So `SlateDebugger.Start`/`.Stop` do exist, but only as event-debugger aliases.)

**`SlateDebugger.Event.*`** (all from the same URL): `Start`, `Stop`, `SetInputFilter`, `SetFocusFilter`, `LogWarning`, `LogInputEvent`, `LogFocusEvent`, `LogExecuteNavigationEvent`, `LogAttemptNavigationEvent`, `LogCaptureStateChangeEvent`, `LogCursorChangeEvent`, `InputRoutingModeEnabled` ("If enabled, outputs the route taken by an input event"), `EnableAllInputFilters`, `DisableAllInputFilters`, `EnableAllFocusFilters`, `DisableAllFocusFilters`, `CaptureStack` ("If enabled, captures the stack when there are events").
- Your guessed `SlateDebugger.CaptureStates` does not exist; the real one is **`SlateDebugger.Event.CaptureStack`**. Same URL.

**`SlateDebugger.Invalidate.*`**: `Enable`, `Start`, `Stop`, `SetInvalidateRootReasonFilter` (usage `[None][ChildOrder][Root][ScreenPosition][Any]`), `SetInvalidateWidgetReasonFilter` (usage `[None][Layout][Paint][Volatility][ChildOrder][RenderTransform][Visibility][Any]`), `ToggleLegend`, `ToggleLogInvalidateWidget`, `ToggleWidgetNameList`. Same URL.
- The bare form is also documented: "`SlateDebugger.Invalidate [Disable/Enable]` enables or disables the invalidation debugger visualizer", and `SlateDebugger.Invalidate.ToggleLegend` shows the legend after `SlateDebugger.Invalidate.Enable 1`. https://dev.epicgames.com/documentation/unreal-engine/using-the-invalidation-box-for-umg-in-unreal-engine

**`SlateDebugger.Paint.*`**: `Enable`, `Start`, `Stop`, `LogOnce` ("Logs widgets painted during the last update"), **`LogWarningIfWidgetIsPaintedMoreThanOnce`** (CONFIRMED — warns if a widget paints multiple times per frame), `MaxNumberOfWidgetDisplayedInList`, `ToggleWidgetNameList`. https://dev.epicgames.com/documentation/en-us/unreal-engine/console-slate-debugger-in-unreal-engine
- Known visual side effect reported on Epic's forums: enabling `SlateDebugger.Paint.Enable` can render the screen as white-filled wireframe. https://forums.unrealengine.com/t/when-enables-slatedebugger-paint-enable-the-screen-shows-wireframe-filled-white/661956

**`SlateDebugger.Update.*`**: `Enable`, `Start`, `Stop`, `SetInvalidationRootIdFilter`, `SetWidgetUpdateFlagsFilter` (usage `[None][Tick][ActiveTimer][Repaint][VolatilePaint][Any]`), `ToggleLegend`, `ToggleUpdateFromPaint`, `ToggleWidgetNameList`. Same URL.

**`SlateDebugger.InvalidationRoot.*`** — a fifth family, NOT listed on the Console Slate Debugger page but confirmed elsewhere by Epic: `SlateDebugger.InvalidationRoot.Enable` is documented for debugging InvalidationBox behaviour. https://dev.epicgames.com/documentation/unreal-engine/using-the-invalidation-box-for-umg-in-unreal-engine — `SlateDebugger.InvalidationRoot.Start` ("shows when invalidation roots are using the slow or the fast path"), `.ToggleLegend`, `.ToggleWidgetNameList` appear in the community compendium (SECONDARY). https://github.com/YawLighthouse/UMG-Slate-Compendium

- **`SlateDebugger.bDisplayWidgetsNameList`: UNCONFIRMED — could not verify.** The real commands are the per-family `SlateDebugger.<Family>.ToggleWidgetNameList`. https://dev.epicgames.com/documentation/en-us/unreal-engine/console-slate-debugger-in-unreal-engine

---

## 3. Other Slate console variables

- **`Slate.ShowClipping`** — CONFIRMED, with Epic's exact description: "When enabled, this shows yellow outlines for all axis-aligned clipping rects (Scissor Rects) and red outlines for all stencil clipping quads." https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-slate-clipping-system-in-unreal-engine
- **`Slate.EnableGlobalInvalidation`** — CONFIRMED: "Enable Global Invalidation by setting `Slate.EnableGlobalInvalidation` to true"; it "activates invalidation features in `SWindow`", wrapping the whole UI in an Invalidation Box and deactivating contained Invalidation Boxes. https://dev.epicgames.com/documentation/en-us/unreal-engine/invalidation-in-slate-and-umg-for-unreal-engine — note the name is `Slate.EnableGlobalInvalidation`; **`Slate.GlobalInvalidation` is UNCONFIRMED / not the documented name.**
- **`Slate.ShowBatching`** — CONFIRMED by Epic staff (Cody Albert, Epic Games) on the Epic Developer Community forums: used in-editor to spot widgets that are not batching properly and diagnose draw-call count. https://forums.unrealengine.com/t/umg-slate-batching-rules/2514764
- **`Slate.ShowOverdraw`** — SECONDARY only: described as the command to view pixel overdraw (paired with `Slate.ShowBatching`), reportedly added for Battle Breakers and merged around 4.17. https://topic.alibabacloud.com/a/ui-optimization-tips-in-unreal-engine-4_8_8_10274886.html — no Epic page found naming it; treat name as likely-correct but not primary-sourced.
- **`Slate.DebugCulling`** — the *feature* "Debug Culling" is documented as a Widget Reflector flag ("debug when a widget is culled by another widget, such as a panel"), but the **exact cvar spelling `Slate.DebugCulling` is UNCONFIRMED from a primary source.** https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-slate-widget-reflector-in-unreal-engine
- **`Slate.AlwaysInvalidate`** — SECONDARY: forces the Invalidation Box to rebuild its cache every frame, used to test whether invalidation is behaving. https://github.com/YawLighthouse/UMG-Slate-Compendium
- **`Slate.EnableTooltips [0/1]`** — SECONDARY: "Whether to allow tooltips to spawn at all"; default true on platforms that need UI tooltips. Note the spelling is `Slate.EnableTooltips`, **not** `Slate.EnableToolTips`. https://github.com/YawLighthouse/UMG-Slate-Compendium
- **`Slate.HitTestGridDebugging [0/1]`** — SECONDARY: "Flag for showing UMG/Slate focusing hit test grid." https://github.com/YawLighthouse/UMG-Slate-Compendium
- Other Slate cvars documented in that same compendium section (SECONDARY, all https://github.com/YawLighthouse/UMG-Slate-Compendium): `Slate.ThrottleWhenMouseIsMoving [0/1]`, `Slate.TargetFrameRateForResponsiveness [int]` (default 35), `Slate.AllowSlateToSleep [0/1]`, `Slate.SleepBufferPostInput`, `Slate.GlobalScrollAmount [float]`; plus the safe-zone simulation set `r.DebugSafeZone.TitleRatio`, `r.DebugActionZone.ActionRatio`, `r.DebugSafeZone.Mode [0-2]`.
- **`Slate.InvalidationDebugging`: UNCONFIRMED — could not verify.** The documented equivalent is the Widget Reflector "Invalidation Debugging" checkbox and `SlateDebugger.Invalidate.*`. https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-slate-widget-reflector-in-unreal-engine
- **`Slate.InvalidationRoot.VerifyWidgetVisibility` / `VerifyWidgetHitTest` / `VerifyWidgetVolatile`: UNCONFIRMED — could not verify any of these names.** Only an internal `FSlateInvalidationRoot::VerifyWidgetList()` debug path is alluded to in community discussion; no cvar names surface in Epic docs. https://dev.epicgames.com/documentation/en-us/unreal-engine/invalidation-in-slate-and-umg-for-unreal-engine
- **`Slate.EnableInvalidationPanels`: UNCONFIRMED — could not verify.** No Epic source names it; the documented switch is `Slate.EnableGlobalInvalidation`. https://dev.epicgames.com/documentation/en-us/unreal-engine/invalidation-in-slate-and-umg-for-unreal-engine

---

## 4. `stat` commands

From Epic's Stat Commands reference (https://dev.epicgames.com/documentation/en-us/unreal-engine/stat-commands-in-unreal-engine):
- **`stat Slate` / `stat SlateVerbose`** — "Displays Slate performance statistics."
- **`stat SlateMemory`** — "Shows Slate memory counters."
- **`stat UI`** — "Shows UI performance information." (So `STAT UI` DOES exist.)
- **`stat Canvas`** — "Canvas statistics, showing performance information for Canvas user interface items, such as tiles, borders, and text."
- No `stat UMG` group is documented. UNCONFIRMED.

Deep Slate profiling recipe (from Epic's SlateCore `STATCAT_Advanced` API page): set `WITH_VERY_VERBOSE_SLATE_STATS` to 1, then, running outside the editor, use `stat group enable slateverbose`, `stat group enable slateveryverbose`, `stat dumpave -root=stat_slate -num=120 -ms=0`. https://docs.unrealengine.com/4.26/en-US/API/Runtime/SlateCore/STATCAT_Advanced/ (corroborated SECONDARY: https://getperfguard.com/tutorials/slate-umg-performance)

Named Slate stat counters visible in those groups (SECONDARY): `STAT_SlateRenderingGTTime`, `STAT_SlateNumPaintedWidgets`, `STAT_SlateNumTickedWidgets`, `STAT_SlateNumWidgets`. https://getperfguard.com/tutorials/slate-umg-performance

---

## 5. Unreal Insights / Slate Insights

- Plugin name is **`SlateInsights`**, display name "Slate Insights"; enable via **Editor > Plugins > Built-In > Slate Insights**, or in the `.uproject` with `{"Name": "SlateInsights", "Enabled": true}`, then restart. https://dev.epicgames.com/documentation/en-us/unreal-engine/slate-insights-in-unreal-engine
- Trace channel flag is **`-trace=slate`**, entered in Editor Preferences > Level Editor > Play > Play in Standalone Game > "Additional Launch Parameters". Same URL, and https://dev.epicgames.com/documentation/en-us/unreal-engine/slate-insights-overview?application_version=4.27
- The **`Slate` trace channel** is listed in Epic's Trace channel table with the description "Slate Insights Plugin". https://dev.epicgames.com/documentation/en-us/unreal-engine/trace-in-unreal-engine-5
- The view it adds is **Slate Frame View** (Unreal Insights > Menu > Slate Frame View), listing widgets painted / invalidated / updated per frame, with Invalidation trace flags **LPUCRV** (Layout, Paint, volatility, Child order, Render transform, Visibility) and Update trace flags **UTPV** (Tick, active Timer, rePaint, Volatile). https://dev.epicgames.com/documentation/en-us/unreal-engine/slate-insights-overview?application_version=4.27
- Related trace console commands that do exist: `Trace.Status`, `trace.bookmark`, `trace.screenshot`, `trace.send [ip]`, `trace.start [filename]`; command-line `-trace.start`, `-tracefile=`, `-tracetailmb=`. https://dev.epicgames.com/documentation/en-us/unreal-engine/trace-in-unreal-engine-5
- **`Slate.EnableTrace`: UNCONFIRMED — could not verify.** Tracing is enabled by the `slate` trace channel, not a `Slate.*` cvar. https://dev.epicgames.com/documentation/en-us/unreal-engine/trace-in-unreal-engine-5

---

## 6. UMG-specific

- **There is no Epic-shipped tool called "UMG Widget Debugger" or "Widget Debugger" in any UE5.x release that I could confirm.** Searches of dev.epicgames.com docs for 5.4–5.8 return only the Widget Reflector as the UI debugging tool, reachable at Tools > Debug > Widget Reflector. Treat "UMG Widget Debugger" as **not existing** in stock UE. https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-slate-widget-reflector-in-unreal-engine and the docs hub https://dev.epicgames.com/documentation/en-us/unreal-engine/testing-and-debugging-user-interfaces-in-unreal-engine
  - The likely source of the rumour: Epic's public roadmap card **"UMG Input Debugging Tools"** — a tool to debug UI navigation, triggerable from the Widget Reflector or the UMG Designer, letting you visualise the static behaviour of navigation events. This is a roadmap item, not a shipped, documented tool. https://portal.productboard.com/epicgames/1-unreal-engine-public-roadmap/c/243-umg-input-debugging-tools
  - The shipped partial of that idea is the Widget Reflector's **Navigation Event Simulation** option on snapshots. https://dev.epicgames.com/documentation/en-us/unreal-engine/widget-reflector?application_version=4.27
  - A third-party marketplace plugin named **"UMG Runtime Debugger"** does exist (shows UserWidget layout at runtime) — SECONDARY / not Epic. https://www.unrealengine.com/marketplace/en-US/product/umg-runtime-debugger
  - **A "Debug UMG" plugin: UNCONFIRMED — could not verify.** No such Epic plugin found in the Plugin Index. https://dev.epicgames.com/documentation/unreal-engine/API/PluginIndex
- **UMG Widget Preview** — a real, built-in UE5 plugin (`UMGWidgetPreview`) that previews/tests widgets without entering PIE; companion plugin `ModelViewViewModelPreview` ("UMG Viewmodel for UMG Preview"). https://dev.epicgames.com/documentation/en-us/unreal-engine/API/PluginIndex/UMGWidgetPreview and https://dev.epicgames.com/documentation/unreal-engine/API/PluginIndex/ModelViewViewModelPreview
- **Widget Blueprint graph debugging** uses the ordinary Blueprint Debugger — breakpoints via right-click > Add Breakpoint on a node, Blueprint Debugger window from the Tools menu or the Blueprint Editor's Debug menu, breakpoints persist in the project `.ini`. https://dev.epicgames.com/documentation/unreal-engine/blueprint-debugger-in-unreal-engine?lang=en-US
- **`ShowDebug` for UMG: UNCONFIRMED — could not verify** any UMG/Slate `ShowDebug` category.
- The UMG Designer has no dedicated "Debug" panel documented; debugging is the Details panel + Widget Reflector. https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-best-practices-in-unreal-engine

---

## 7. Epic's optimization docs that reference these tools

- **Optimization Guidelines for UMG in Unreal Engine** — points at Unreal Insights and Slate Insights; names no console commands itself. https://dev.epicgames.com/documentation/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine
- **Slate UI Optimization and Performance in Unreal Engine** (hub page). https://dev.epicgames.com/documentation/en-us/unreal-engine/slate-ui-optimization-and-performance-in-unreal-engine
- **Invalidation in Slate and UMG** — the Global Invalidation reference. https://dev.epicgames.com/documentation/en-us/unreal-engine/invalidation-in-slate-and-umg-for-unreal-engine
- **Using the Invalidation Box for UMG** — the best source for `SlateDebugger.Invalidate.*` and `SlateDebugger.InvalidationRoot.Enable` in practice, incl. the colour code (yellow = paint, gray = volatility, cyan = child order). https://dev.epicgames.com/documentation/unreal-engine/using-the-invalidation-box-for-umg-in-unreal-engine
- **Testing and Debugging User Interfaces** — the UI debugging docs hub. https://dev.epicgames.com/documentation/en-us/unreal-engine/testing-and-debugging-user-interfaces-in-unreal-engine
- Epic knowledge-base articles by Ryan B. (Epic): **"Slate Debugging Tips"** https://dev.epicgames.com/community/learning/knowledge-base/P88X/unreal-engine-slate-debugging-tips (forum mirror https://forums.unrealengine.com/t/knowledge-base-slate-debugging-tips/265069) and **"Slate — General Optimization Guidelines"** https://dev.epicgames.com/community/learning/knowledge-base/VZZD/unreal-engine-slate-general-optimization-guidelines (forum mirror https://forums.unrealengine.com/t/slate-general-optimization-guidelines/265070). Caveat: both community pages are JS-rendered and their body text could not be retrieved by fetch — I could only confirm authorship and topic, not their command lists.

---

## Summary of things I could NOT verify (do not treat as real)

`FWidgetSnapshotService`, `.widgetsnapshot` file format, `WidgetReflector.ShowHiddenInDetailedInfo`, `SlateDebugger.CaptureStates` (real name: `SlateDebugger.Event.CaptureStack`), `SlateDebugger.bDisplayWidgetsNameList` (real: per-family `ToggleWidgetNameList`), `Slate.InvalidationDebugging`, `Slate.GlobalInvalidation` (real: `Slate.EnableGlobalInvalidation`), `Slate.InvalidationRoot.VerifyWidgetVisibility` / `VerifyWidgetHitTest` / `VerifyWidgetVolatile`, `Slate.EnableInvalidationPanels`, `Slate.EnableTrace`, `stat UMG`, a "Debug UMG" plugin, a UE5 "Widget Debugger" tool, and `ShowDebug` for UMG. `Slate.DebugCulling` and `Slate.ShowOverdraw` have the feature confirmed but the exact cvar string only from secondary sources; `Slate.EnableTooltips` (not `EnableToolTips`), `Slate.AlwaysInvalidate`, `Slate.HitTestGridDebugging` are secondary-sourced only.