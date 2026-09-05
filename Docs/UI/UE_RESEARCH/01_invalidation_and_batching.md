## Source basis

Everything below is quoted from the actual UE source via `gh api` against `EpicGames/UnrealEngine`, branch `release` = **5.8.2** (`Engine/Build/Build.version`). Version history was established by re-fetching the same files at branch refs `4.23`…`5.7`. `github.com/EpicGames/UnrealEngine` via WebFetch failed as you predicted (socket closed) — `gh` works because it's authenticated. **Secondary sources are labelled inline; everything unlabelled is source code.**

---

# A. Invalidation

## A0. Three mechanisms, and which one wins

`Engine/Source/Runtime/Slate/Private/Widgets/SInvalidationPanel.cpp:171-180`:

```cpp
bool SInvalidationPanel::GetCanCache() const
{
    ...
    return bCanCache && !GSlateEnableGlobalInvalidation && bInvalidationPanelsEnabled;
}
```

**Global Invalidation switches every `SInvalidationPanel` off.** They are mutually exclusive by construction — and `SInvalidationPanel::OnPaint` asserts it: `check(!GSlateEnableGlobalInvalidation);` (`SInvalidationPanel.cpp:359`). Under global invalidation the `SWindow` is the only invalidation root (`SWindow.cpp:1116-1119`: `bool SWindow::Advanced_IsInvalidationRoot() const { return bAllowFastUpdate && GSlateEnableGlobalInvalidation; }`).

Both are the *same machinery*: `class SInvalidationPanel : public SCompoundWidget, public FSlateInvalidationRoot` and `SWindow` likewise derives from `FSlateInvalidationRoot`. There is one implementation; the two modes differ only in where the root sits.

## A1. The CVars — exact registration

`Engine/Source/Runtime/SlateCore/Private/SlateCoreClasses.cpp:52-56`:

```cpp
bool GSlateEnableGlobalInvalidation = false;
static FAutoConsoleVariableRef CVarSlateNewUpdateMethod(
	TEXT("Slate.EnableGlobalInvalidation"), 
	GSlateEnableGlobalInvalidation, 
	TEXT("")
);
```

**Default `false`. The help string is literally empty** — Epic ships no documentation on the CVar itself.

`SlateCoreClasses.cpp:41-48`:
```cpp
// Enable fast widget paths outside the editor by default.  Only reason we don't enable them everywhere
// is that the editor is more complex than a game, and there are likely a larger swath of edge cases.
bool GSlateFastWidgetPath = false;
FAutoConsoleVariableRef CVarSlateFastWidgetPath(
	TEXT("Slate.EnableFastWidgetPath"), GSlateFastWidgetPath,
	TEXT("Whether or not we enable fast widget pathing.  This mode relies on parent pointers to work correctly."));
```
Note the comment claims "enable by default outside the editor" but the initializer is `false` — the comment is stale.

Other invalidation CVars, all from source:

| CVar | Default | File |
|---|---|---|
| `Slate.EnableGlobalInvalidation` | `false` | `SlateCoreClasses.cpp:52` |
| `Slate.EnableFastWidgetPath` | `false` | `SlateCoreClasses.cpp:43` |
| `Slate.EnableInvalidationPanels` | `true` — "Whether to attempt to cache any widgets through invalidation panels." | `SInvalidationPanel.cpp:21-27` |
| `Slate.AlwaysInvalidate` | `false`, `WITH_SLATE_DEBUGGING` only — "Forces invalidation panels to cache, but to always invalidate." | `SInvalidationPanel.cpp:30-34` |
| `Slate.DynamicInvalidation.Options` | `2` (4 modes: 0 disable+no caching, 1 act as regular panels, 2 enabled, 3 force on all panels) | `SInvalidationPanel.cpp:42-52` |
| `Slate.InvalidationList.MaxArrayElements` | `64` | `SlateInvalidationRoot.cpp:156` |
| `Slate.InvalidationList.NumberElementLeftBeforeSplitting` | `40` | `SlateInvalidationRoot.cpp:162` |
| `Slate.InvalidationList.EnableReindexLayerId` | `true` | `SlateInvalidationRoot.cpp:168` |
| `Slate.InvalidationRoot.Dump{UpdateList,UpdateListOnce,PreInvalidationList,PrepassInvalidationList,PostInvalidationList}` | `false` | `SlateInvalidationRoot.cpp:24-64` |
| `Slate.InvalidationRoot.Verify{WidgetList,WidgetsIndex,ValidWidgets,HittestGrid,WidgetVisibility,WidgetVolatile,WidgetUpdateList,SlateAttribute,WidgetsAreUpdatedOnce,CachedElementDataList}` | `false` | `SlateInvalidationRoot.cpp:71-147` |
| `Slate.DumpUpdateList` | console *command* | `SlateInvalidationRoot.cpp:40` |
| `Slate.InvalidationDebugging` | deprecated → `SlateDebugger.Invalidate.Enable` | `SlateCoreClasses.cpp` |

The editor exposes the toggle in Widget Reflector: `SSlateOptions.cpp:96` — `AddMenuEntry(MenuBuilder, Icon, LOCTEXT("GlobalInvalidation", "Global Invalidation"), TEXT("Slate.EnableGlobalInvalidation"));`

**Version:** `Slate.EnableGlobalInvalidation` is **absent from `SlateCoreClasses.cpp` at ref `4.23` and present at ref `4.24`** (`int32 GSlateEnableGlobalInvalidation = 0;`). `Engine/Source/Runtime/SlateCore/Public/FastUpdate/` also first exists at 4.24 (`SlateInvalidationRoot.h`, `WidgetProxy.h`). **Global invalidation and the whole `FSlateInvalidationRoot` fast path landed in 4.24.** (Secondary corroboration: [Epic's invalidation doc](https://dev.epicgames.com/documentation/en-us/unreal-engine/invalidation-in-slate-and-umg-for-unreal-engine).) It became `bool` (from `int32`) somewhere in UE5.

## A2. `EInvalidateWidgetReason` — complete, verbatim

`Engine/Source/Runtime/SlateCore/Public/Widgets/InvalidateWidgetReason.h` (5.8):

| Value | Bit | Epic's own comment (verbatim) |
|---|---|---|
| `None` | `0` | — |
| `Layout` | `1<<0` (L20) | "Use Layout invalidation if your widget needs to change desired size. **This is an expensive invalidation so do not use if all you need to do is redraw a widget**" |
| `Paint` | `1<<1` (L25) | "Use when the painting of widget has been altered, but nothing affecting sizing." |
| `Volatility` | `1<<2` (L30) | "Use if just the volatility of the widget has been adjusted." |
| `ChildOrder` | `1<<3` (L35) | "A child was added or removed. **(this implies prepass and layout)**" |
| `RenderTransform` | `1<<4` (L40) | "A Widgets render transform changed" |
| `Visibility` | `1<<5` (L45) | "Changing visibility (this implies layout)" |
| `AttributeRegistration` | `1<<6` (L50) | "Attributes got bound or unbound (it's used by the SlateAttributeMetaData)" |
| `Prepass` | `1<<7` (L55) | "Re-cache desired size of all of this widget's children recursively (this implies layout)" |
| `PaintAndVolatility` | `Paint\|Volatility` (L62) | — |
| `LayoutAndVolatility` | `Layout\|Volatility` (L68) | — |

**There is no `All` in 5.8.** It existed as `All UE_DEPRECATED(4.22, "EInvalidateWidget::All has been deprecated. You probably wanted EInvalidateWidget::Layout but if you need more than that then use bitwise or to combine them") = 0xff`. Verified present at refs 4.24, 4.27, 5.0, 5.3, **5.4**; **absent at 5.5** → removed in 5.5.

`AttributeRegistration` and `Prepass` are **absent at 4.27, present at 5.0** → added in UE5.0. 4.24's `ChildOrder` comment said only "(this implies layout)"; the "prepass and" was added later.

Also in that header: `typedef EInvalidateWidgetReason EInvalidateWidget;` with the comment `// This typedefed because EInvalidateWidget will be deprecated soon` (still not deprecated as of 5.8).

### What each reason actually triggers

`SWidget::Invalidate` (`SlateCore/Private/Widgets/SWidget.cpp:1306-1368`) normalises the reason *before* dispatch:

```cpp
if (InvalidateReason == EInvalidateWidgetReason::None || !IsConstructed()) return;

// Backwards compatibility fix:  Its no longer valid to just invalidate volatility since we need to
// repaint to cache elements if a widget becomes non-volatile. So after volatility changes force repaint
if (EnumHasAnyFlags(InvalidateReason, EInvalidateWidgetReason::Volatility))
    InvalidateReason |= EInvalidateWidgetReason::PaintAndVolatility;

if (EnumHasAnyFlags(InvalidateReason, EInvalidateWidgetReason::Prepass))
{ MarkPrepassAsDirty(); InvalidateReason |= EInvalidateWidgetReason::Layout; }

if (EnumHasAnyFlags(InvalidateReason, EInvalidateWidgetReason::ChildOrder) || !bPrepassLayoutScaleMultiplierSet)
{ MarkPrepassAsDirty(); InvalidateReason |= EInvalidateWidgetReason::Prepass; InvalidateReason |= EInvalidateWidgetReason::Layout; }
```

So `ChildOrder` ⟹ `Prepass` ⟹ `Layout`, and `Volatility` ⟹ `Paint|Volatility`. The reason is then routed into one of three phase queues (`SlateInvalidationRoot.cpp:179-199`):

```cpp
constexpr EInvalidateWidgetReason PreInvalidationReason = Layout | AttributeRegistration | Visibility | ChildOrder;
constexpr EInvalidateWidgetReason PostInvalidationReason = Layout | Paint | Volatility | RenderTransform | Prepass;
// post-update flag test: (uint8)Reason & (0xFF & ~(uint8)AttributeRegistration)
```

`FSlateInvalidationRoot::InvalidateWidget` (`SlateInvalidationRoot.cpp:301-342`) pushes the proxy into `WidgetsNeedingPreUpdate` (heap), `WidgetsNeedingPrepassUpdate`, and/or `WidgetsNeedingPostUpdate` accordingly, and **does nothing at all if `bNeedsSlowPath` is already set** (`if (!bNeedsSlowPath)` guard — an invalidation storm during a slow-path frame costs nothing).

The four phases of `ProcessInvalidation()` (`SlateInvalidationRoot.h`):
```cpp
void ProcessPreUpdate();      /** Update child order and slate attribute registration */
void ProcessAttributeUpdate();/** Slate attribute update */
void ProcessPrepassUpdate();  /** Call Slate Prepass. */
bool ProcessPostUpdate();     /** Update paint, tick, timers */
```

Layout invalidation's real cost, `FWidgetProxy::ProcessLayoutInvalidation` (`WidgetProxy.cpp:110-190`): it re-prepasses or re-caches desired size, marks `NeedsRepaint`, and **if the desired size changed, propagates a `Layout` invalidation to the parent** — this is the mechanism by which one leaf layout change walks up the tree; and if the parent is the root of the panel, `Root.InvalidateRootLayout(...)` invalidates the whole thing.

## A3. `SInvalidationPanel`

Header: `Engine/Source/Runtime/Slate/Public/Widgets/SInvalidationPanel.h`.

- `class SInvalidationPanel : public SCompoundWidget, public FSlateInvalidationRoot`, with `TWidgetTypeTraits<SInvalidationPanel>::SupportsInvalidation() = true`.
- Default `_Visibility = EVisibility::SelfHitTestInvisible`.
- **`GetChildren()` returns `FNoChildren::NoChildrenInstance` when caching is on** — that is the mechanism by which cached children are excluded from the normal prepass/paint walk. `Debug_GetChildrenForReflector()` exists purely so Widget Reflector can still see them.
- It owns its own `FHittestGrid` and merges it into the parent grid after painting (`Args.GetHittestGrid().AddGrid(HittestGrid)`).

**What forces a re-cache** — `UpdateCachePrequisites` (`SInvalidationPanel.cpp`, verbatim):
```cpp
// We only need to re-cache if the incoming layer is higher than the maximum layer Id we cached at,
// we do this so that widgets that appear and live behind your invalidated UI don't constantly invalidate everything above it.
if (LayerId > LastIncomingLayerId) { ...bNeedsRecache = true; }
if (AllottedGeometry.GetLocalSize() != LastAllottedGeometry.GetLocalSize()
 || AllottedGeometry.GetAccumulatedRenderTransform() != LastAllottedGeometry.GetAccumulatedRenderTransform()) { ...true; }
// If our clip rect changes size, we've definitely got to invalidate.
if (ClipRectSize != LastClipRectSize) { ...true; }
if (LastClippingState != ClippingState) { ...true; }
if (LastIncomingColorAndOpacity != InWidgetStyle.GetColorAndOpacityTint()) { ...true; }
```
and on a hit: `MutableThis->InvalidateRootLayout(this);` preceded by Epic's own comment **`// @todo: Overly aggressive?`**. So: **moving the panel, resizing it, changing the clip, or changing the inherited colour/opacity tint blows the whole cache.** That is the single most important "when it hurts" fact and it is Epic's own code, not folklore.

**Args removed since 4.23:** `SLATE_ARGUMENT(bool, CacheRelativeTransforms)` and the `bCacheRelativeTransforms` member exist at ref `4.23` and are **gone at ref `4.24`**. Epic's public docs page ["Using the Invalidation Box for UMG"](https://dev.epicgames.com/documentation/unreal-engine/using-the-invalidation-box-for-umg-in-unreal-engine) still documents "Cache Relative Transforms" — **that page describes the pre-4.24 design and is wrong for any modern engine.** Flagging this explicitly since you'd otherwise mirror a dead setting.

**Dynamic Invalidation** (new, guarded by `UE_SLATE_WITH_DYNAMIC_INVALIDATION`; present in 5.8, not in 4.x): a panel with `bUseDynamicInvalidation` only caches "if all descendant widgets support invalidation" (`SupportsInvalidationRecursive`). `CanCacheThisFrame()` is re-evaluated per frame and a widget class opts in via `TWidgetTypeTraits<T>::SupportsInvalidation()`. This is Epic conceding that **not all widgets are safe to cache**, and building a per-frame recursive check for it.

## A4. Fast-path data structures

**`FWidgetProxy`** (`SlateCore/Public/FastUpdate/WidgetProxy.h:112`). Per-widget bookkeeping, with a hard size budget:
```cpp
static_assert(sizeof(FWidgetProxy) <= 32, "FWidgetProxy should be 32 bytes");   // line 228
static_assert(std::is_trivially_destructible_v<FWidgetProxy>, ...);
template <> struct TIsPODType<FWidgetProxy> { enum { Value = true }; };
```
Contents: raw `SWidget*` (a `TWeakPtr` variant exists behind `UE_SLATE_WITH_WIDGETPROXY_WEAKPTR 0` — **off**), `Index`, `ParentIndex`, `LeafMostChildIndex`, `CurrentInvalidateReason`, `FSlateInvalidationWidgetVisibility Visibility` (1 byte, static-asserted), and a bitfield: `bContainedByWidgetPreHeap`, `bContainedByWidgetPostHeap`, `bContainedByWidgetPrepassList`, `bIsInvalidationRoot`, `bIsVolatilePrepass`.

**`FSlateInvalidationWidgetIndex`** (same dir): a `{uint16 ArrayIndex, uint16 ElementIndex}` pair — "Index of the special container to order widget in InvalidateRoot."

**`FSlateInvalidationWidgetSortOrder`** — one `uint32`, with Epic's comment:
> "SlateInvalidationWidgetIndex cannot be used to sort the widget since the ArrayIndex may not be in the expected order. (See the array as a double linked list). SlateInvalidationWidgetSortOrder builds a unique number that represents the order of the widget. The number is padded in a way to keep the order but not necessarily sequential. **It is valid until the next SlateInvalidationRoot::ProcessInvalidation()**"

**`FSlateInvalidationWidgetList`** — `Private/FastUpdate/SlateInvalidationWidgetList.h`. An array-of-arrays (`TArray<FWidgetProxy>` chunks), tuned by `FArguments`: `PreferedElementsNum = 64`, `NumberElementsLeftBeforeSplitting = 40`, `SortOrderPaddingBetweenArray = 1000` ("The sort order is used by the HittestGrid and the LayerId").

**`FWidgetProxyHandle`** — note the naming: the type the caller called `FastPathProxyHandle` is the *member* (`SWidget::FastPathProxyHandle`); the *type* is `FWidgetProxyHandle`. It stores `{FSlateInvalidationRootHandle, FSlateInvalidationWidgetIndex, FSlateInvalidationWidgetSortOrder}`.

**`FSlateInvalidationRoot`** (`Public/FastUpdate/SlateInvalidationRoot.h`) is `public FGCObject, public FNoncopyable`, owns `FastWidgetPathList`, three heaps (`WidgetsNeedingPreUpdate`/`PrepassUpdate`/`PostUpdate`), `FinalUpdateList`, and `FSlateCachedElementData* CachedElementData`. Public API: `InvalidateRootChildOrder()` ("Rebuild the list and request a SlowPath"), `InvalidateRootLayout()`, `InvalidateScreenPosition()` ("This is faster then doing a SlowPath when only the DesktopGeometry changed"), `NeedsSlowPath()`, `PaintInvalidationRoot()`, `virtual bool CanCacheThisFrame()`.

**`EWidgetUpdateFlags`** (`Public/FastUpdate/WidgetUpdateFlags.h`) — the per-widget "what still needs doing" set: `NeedsTick = 1<<2`, `NeedsActiveTimerUpdate = 1<<3`, `NeedsRepaint = 1<<4`, `NeedsVolatilePaint = 1<<6`, `NeedsVolatilePrepass = 1<<7`, `AnyUpdate = 0xff`. (Bits 0, 1, 5 are unused — historical.)

## A5. Volatility — API and exact cost

API is on `SWidget` (`SlateCore/Public/Widgets/SWidget.h`). **There is no `SetVolatile` in Slate**; the setter is `ForceVolatile(bool)`:

```cpp
inline bool IsVolatile() const { return bCachedVolatile; }                                  // L1135
inline bool IsVolatileIndirectly() const { return bInheritedVolatility; }                   // L1141
/** Should this widget always appear as volatile for any layout caching host widget. A volatile
 *  widget's geometry and layout data will never be cached, and neither will any children. */
inline void ForceVolatile(bool bForce) { if (bForceVolatile != bForce) { bForceVolatile = bForce;
        Invalidate(EInvalidateWidgetReason::PaintAndVolatility); } }                        // L1148
inline void CacheVolatility() { bCachedVolatile = bForceVolatile || ComputeVolatility(); }  // L1169
virtual bool ComputeVolatility() const { return false; }                                    // L1737
```
`ComputeVolatility`'s comment: *"Recomputes the volatility of the widget. If you have additional state you automatically want to make the widget volatile, you should sample that information here."* Note it is **cached**, not polled — `CacheVolatility()` must be re-run manually ("Should be called any time anything examined by your implementation of ComputeVolatility is changed").

UMG's exposure is `UWidget::ForceVolatile(bool)` (BlueprintCallable) plus the property `Widget.h:387-388`:
```cpp
/** If true prevents the widget or its child's geometry or layout information from being cached. If this widget
 *  changes every frame, but you want it to still be in an invalidation panel you should make it as volatile
 *  instead of invalidating it every frame, which would prevent the invalidation panel from actually ever caching anything. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Performance")
uint8 bIsVolatile:1;
```

**The exact cost of volatility** — `FSlateWindowElementList::AddUninitialized`, `Public/Rendering/DrawElements.h:269`:
```cpp
const bool bAllowCache = CachedElementDataListStack.Num() > 0 && WidgetDrawStack.Num() && !WidgetDrawStack.Top().bIsVolatile;
if (bAllowCache) return AddCachedElement<ElementType>();
else { /* goes into UncachedDrawElements */ }
```
and `bIsVolatile` on that stack entry is set in `PushPaintingWidget` (`Private/Rendering/DrawElements.cpp:195`):
```cpp
WidgetDrawStack.Emplace(CurrentCacheHandle, CurrentWidget.IsVolatileIndirectly() || CurrentWidget.IsVolatile(), &CurrentWidget);
```
So: **a volatile widget's draw elements bypass the cached element data entirely and are re-created and re-batched every frame — and volatility is inherited, so every descendant does too.** `SWidget::UpdateFastPathVolatility` (`SWidget.cpp:945-964`) recurses `GetAllChildren()->ForEachWidget` propagating `bParentVolatile || IsVolatile()`. Marking a high node volatile silently un-caches its whole subtree.

Per `WidgetProxy.cpp:151-155`, a volatile widget still pays layout: *"Note even if volatile we need to recompute desired size. We do not need to invalidate parents though if they are volatile since they will naturally redraw this widget."*

Volatile prepass is a separate, newer axis: `SetVolatilePrepass(bool)` (`SWidget.h:734`) toggling `EWidgetUpdateFlags::NeedsVolatilePrepass`. In `FWidgetProxy::Repaint` (`WidgetProxy.cpp`) the old path is explicitly marked for retirement: `// Todo: this should be deprecated in favor of NeedsVolatilePrepass`.

## A6. `Invalidate()` API surface across versions

- 4.22: `EInvalidateWidget::All` deprecated in favour of bitwise-or (deprecation message in source).
- 4.24: `FSlateInvalidationRoot`/`FWidgetProxy` fast path + `Slate.EnableGlobalInvalidation` added; `SInvalidationPanel` rewritten onto it; `CacheRelativeTransforms` removed.
- 5.0: `AttributeRegistration` and `Prepass` reasons added (they exist to serve the new `TSlateAttribute<T, EInvalidateWidgetReason>` system, `SWidget.h:185`).
- 5.5: `All` removed outright — **a hard compile break for any code still using it.**
- 5.6–5.8: `UE_SLATE_WITH_DYNAMIC_INVALIDATION`, `SupportsInvalidationRecursive`, `TWidgetTypeTraits<T>::SupportsInvalidation()`, `CanCacheThisFrame()` added.
- 5.8: `FSlateWidgetPersistentState::bIsInGameLayer` deprecated → `IsProjectContent()`/`SetIsProjectContent()`.

## A7. What invalidation saves, and what it costs — direct answer to the new question

**Without any invalidation root (default engine config: both CVars `false`, no `SInvalidationPanel`):**

1. **Full prepass of the whole tree, every frame.** `SWidget::SlatePrepass` (`SWidget.cpp:679-714`):
   ```cpp
   if (!GSlateIsOnFastUpdatePath || bNeedsPrepass) { ... Prepass_Internal(InLayoutScaleMultiplier); }
   ```
   `GSlateIsOnFastUpdatePath` is only true inside a fast-path paint. So off the fast path the `bNeedsPrepass` short-circuit **never applies** and `Prepass_Internal` recurses every non-`Collapsed` child (`Prepass_ChildLoop`), calling `ComputeDesiredSize` on every widget. `FSlateApplication::DrawPrepass` → `PrepassWindowAndChildren` (`SlateApplication.cpp:1345-1377`) does this for every visible window each frame.
2. **Full paint of the whole tree**, producing a brand-new `FSlateDrawElement` for every visible widget.
3. **Full re-batching** of all those elements (§B).
4. **`SWidget::Invalidate()` is a no-op.** `SWidget.cpp:1346-1367`: if `FastPathProxyHandle.IsValid(this)` is false, the entire `else` branch is just a `WITH_SLATE_DEBUGGING` broadcast and a trace event. Nothing is recorded, because nothing needs to be — everything repaints anyway.

**With a root, the fast path skips:** prepass (guarded by `bNeedsPrepass`), `Tick`, active timers, `OnPaint`, and draw-element creation for every widget that is not in `FinalUpdateList`. `FWidgetProxy::Update` (`WidgetProxy.cpp:52`) only repaints when `NeedsRepaint|NeedsVolatilePaint` is set, and otherwise only runs active timers / `Tick` if those flags are set. Cached elements are reused via `FSlateCachedElementData`/`FSlateCachedElementList` (one list per widget) and cached `FSlateRenderBatch`es in a `TSparseArray`.

**What it costs:**

- **Memory:** ≤32 B per widget for `FWidgetProxy` (static-asserted), *plus* `FSlateWidgetPersistentState` per widget — that one is much larger: `TWeakPtr<SWidget> PaintParent`, `TOptional<FSlateClippingState> InitialClipState`, two `FGeometry`s, an `FSlateRect`, an `FWidgetStyle`, an `FSlateCachedElementsHandle`, and ~8 scalars. *Plus* the retained per-widget `FSlateCachedElementList` (its own `FSlateDrawElementMap`, `CachedRenderBatchIndices`, `FSlateCachedFastPathRenderingData`). This is the real memory bill — Epic's docs describe it only as "trading memory consumption for CPU efficiency" ([optimization guidelines](https://dev.epicgames.com/documentation/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine)).
- **Bookkeeping:** three heaps + a final update list, a widget-list rebuild on any `ChildOrder` change, and hit-test-grid maintenance per invalidation root.
- **The slow-path cliff:** any `InvalidateRootChildOrder()` sets `bNeedsSlowPath`; `PaintInvalidationRoot` then calls `ClearAllFastPathData(...)`, `BuildFastPathWidgetList(RootWidget)` and `PaintSlowPath(Context)` — **a full rebuild + full repaint, i.e. strictly worse than not having invalidation at all for that frame.** Under global invalidation the root is the whole window, so one bad `ChildOrder` invalidation costs a full-window rebuild.
- **A known 1-frame hitch, in Epic's own words** (`SlateInvalidationRoot.cpp`, in `PaintInvalidationRoot`):
  > `// Once everything is painted and ticked, new widget (SListView, SRichBox, SScrollBox) might have created new widget in both SlowPath and FastPath`
  > `// It might create a 1 frame hitch because the widget is in the cached and should not.`

**Verdict for your decision:** for a *static-ish* editor UI the saving is prepass+paint+batch of the entire tree per frame — very large. But it is not free and it is not unconditional: the machinery only pays off when the invalidation rate is low, and a single mis-signalled `ChildOrder`/geometry change per frame converts it into a net loss (rebuild + full repaint). The "widgets that must opt out" set is real and Epic has grown a whole per-frame recursive check (`SupportsInvalidationRecursive`, 5.6+) to detect it automatically.

## A8. Known limitations and bugs

From source:
- `SInvalidationPanel.cpp` comment: **"InvalidationPanel cannot be nested in regular mode"** (appears twice, in `ConsoleVariableEnableInvalidationPanelsChanged` and `EnableInvalidationPanels`).
- `UpdateCachePrequisites` re-cache trigger marked `// @todo: Overly aggressive?`.
- `ensureMsgf(bProcessingChildOrderInvalidation == false, TEXT("A widget got invalidated while building the childorder."))` and `ensureMsgf(false, TEXT("An invalid invalidation occurred while processing the widget attributes. That may result in an infinite loop."))` — `SlateInvalidationRoot.cpp:303-311`. Both are live failure modes Epic guards against.
- A commented-out block in `FWidgetProxy::Update`: *"If the widget was invalidated while ticking. In slow mode the widget would be painted right away. For now postpone for the next frame. Enabling this code would cause more issues with the list management."* — a real, unfixed one-frame-latency behaviour.
- `#define UE_SLATE_WITH_WIDGETPROXY_WEAKPTR 0` / `UE_SLATE_VERIFY_WIDGETPROXY_WEAKPTR_STALE 0` — the proxy holds a **raw** `SWidget*`; stale-pointer verification is compiled out. Lifetime is managed manually via `FSlateInvalidationRoot::OnWidgetDestroyed`.

Community-reported, **secondary — Epic Developer Community forums, not verified by me against source**: global invalidation not updating the hit-test grid when collapsing a widget inside a RetainerBox ([5.5.4](https://forums.unrealengine.com/t/5-5-4-slate-global-invalidation-not-updating-hittest-grid-when-collapsing-parent-widget-in-a-retainer-widget/2556279), [still in 5.6.1](https://forums.unrealengine.com/t/5-6-1-slate-global-invalidation-still-not-updating-hittest-grid-correctly-based-on-parent-widget-visibility-changes/2678893)); `SExpandableArea` lacking full global-invalidation support ([forum](https://forums.unrealengine.com/t/expandablearea-widget-seems-to-lack-full-support-for-slate-enableglobalinvalidation/2549463)); an `ensureAlways` in the invalidation root ([forum](https://forums.unrealengine.com/t/global-invalidation-hits-an-ensurealways-in-the-slate-invalidation-root/2524189)); a 5.7 crash in `FSlateCachedElementData::AddCache` / `FSlateInvalidationRoot::ClearAllFastPathData` ([forum](https://forums.unrealengine.com/t/widget-invalidation-ensure-and-crash-in-5-7-update-fslatecachedelementdata-addcache-fslateinvalidationroot-clearallfastpathdata/2699493)). `issues.unrealengine.com` (UE-136046, UE-95748) returned **HTTP 403** to WebFetch — I could not confirm those tickets' contents.

---

# B. Draw elements and batching

## B1. `FSlateDrawElement` and the `Make*` surface

Header moved in UE5: it is now `Engine/Source/Runtime/SlateCore/Public/Rendering/DrawElementTypes.h` (not `DrawElements.h`, which now holds the element *list* and cache types). Class comment: *"FSlateDrawElement is the building block for Slate's rendering interface. Slate describes its visual output as an ordered list of FSlateDrawElement s"*.

**All `Make*` are `static` and all take `FSlateWindowElementList&` as the first parameter** — in 5.8 and in 4.27 alike. The premise in your brief ("deprecated in UE5 in favour of the non-static / `FSlateDrawElement::Make*` with `FSlateWindowElementList&`") does not match the source: the `FSlateWindowElementList&` signature *is* the current one, and there is no non-static variant. What *was* deprecated in UE4 were the older overloads carrying an explicit `const FSlateRect& InClippingRect` (superseded by the clipping-stack `PushClip`/`PopClip` model, `DrawElements.h:302-307`); those are long gone from 5.8.

Full 5.8 static list (`DrawElementTypes.h`, line numbers):

| Function | Line | Note |
|---|---|---|
| `MakeDebugQuad` | 66 | |
| `MakeGeometryOutline` | 79 | |
| `MakeBox` | 102 | |
| `MakeRotatedBox` | 110 | |
| `MakeText` ×3 (`FString`, `FString`+start/end, inline `FText`) | 134/136/138 | |
| `MakeShapedText` | 153 | |
| `MakeRotatedShapedText` | 167 | |
| `MakeGradient` | 180 | takes `FVector4f CornerRadius` |
| `MakeSpline` (Hermite) | 195 | |
| `MakeCubicBezierSpline` | 210 | |
| `MakeDrawSpaceSpline` | 213 | |
| `MakeLines` ×4 (`FVector2d`/`FVector2f` × with/without per-point colours) | 228/230/262/264 | |
| `MakeDashedLines` ×2 | 246/280 | |
| `MakeViewport` | 293 | |
| `MakeCustom` | 303 | |
| `MakeCustomVerts` | 305 | |
| `MakePostProcessPass` | 308 | **`UE_DEPRECATED(5.4, "MakePostProcessPass has been deprecated. If you need to make a blur please call MakePostProcessBlur.")`** |
| `MakePostProcessBlur` | 320 | the 5.4 replacement |
| `MakeBoxInternal` | 378 | private-ish helper returning `FSlateDrawElement&` |

**`MakePostProcessPass` is the only deprecated `Make*` in 5.8.** There is no `MakeGradient` deprecation, no `MakeLines` deprecation.

Element subclasses (`DrawElementTypes.h`): `FSlateBoxElement`, `FSlateRoundedBoxElement`, `FSlateTextElement`, `FSlateShapedTextElement`, `FSlateGradientElement`, `FSlateSplineElement`, `FSlateLineElement`, `FSlateViewportElement`, `FSlateCustomDrawerElement`, `FSlateCustomVertsElement`, `FSlatePostProcessElement`, `FSlateLayerElement`, `FSlateCachedBufferElement`.

Base-class state (`FSlateDrawElement` members): `FSlateRenderTransform RenderTransform`, `FVector2f Position`, `FVector2f LocalSize`, `float Scale`, `int32 LayerId`, `FClipStateHandle ClipStateHandle`, `ESlateDrawEffect DrawEffects`, `ESlateBatchDrawFlag BatchFlags`, `EElementType ElementType`, `int8 SceneIndex`, `uint8 bIsCached`.

## B2. `EElementType` (the type-keyed element map)

`Public/Rendering/DrawElementCoreTypes.h:34-57` — elements are stored **grouped by type** in a `FSlateDrawElementMap`:
```cpp
enum class EElementType : uint8 {
  ET_Box, ET_DebugQuad, ET_Text, ET_ShapedText, ET_Spline, ET_Line, ET_Gradient,
  ET_Viewport, ET_Border, ET_Custom, ET_CustomVerts, ET_PostProcessPass, ET_RoundedBox,
  /** We map draw elements by type on add for better cache coherency if possible,
   *  this type is used when that grouping is not possible.
   *  Grouping is also planned to be used for bulk element type processing. */
  ET_NonMapped,
  ET_Count,
};
```
At ref `4.27` this enum ends at `ET_PostProcessPass` — **`ET_RoundedBox` and `ET_NonMapped` are UE5 additions**, and the per-type grouping/`FSlateDrawElementMap` is a UE5 restructure. Consequence for batching: elements are batched **type-by-type in a fixed order**, not in paint order (see B4).

## B3. The enums, with version history

`Public/Rendering/RenderingCommon.h`.

**`ESlateDrawPrimitive : uint8` (L48):** `None`, `LineList`, `TriangleList`. (UE4 had this as `ESlateDrawPrimitive::Type` namespaced enum; it is a scoped enum class in UE5.)

**`ESlateShader : uint8` (L59)** — header comment: *"Shader types. NOTE: mirrored in the shader file. If you add a type here you must also implement the proper shader type (TSlateElementPS). See SlateShaders.h"*

| Enumerator | Value | Epic's comment | First appears |
|---|---|---|---|
| `Default` | 0 | "The default shader type. Simple texture lookup" | ≤4.27 |
| `Border` | 1 | "Border shader" | ≤4.27 |
| `GrayscaleFont` | 2 | "Grayscale font shader. Uses an alpha only texture" | ≤4.27 |
| `ColorFont` | 3 | "Color font shader. Uses an sRGB texture" (was "Grayscale font shader. Uses an color texture" through 5.0 — a copy-paste bug Epic later fixed) | ≤4.27 |
| `LineSegment` | 4 | "Line segment shader. For drawing anti-aliased lines" | ≤4.27 |
| `Custom` | 5 | "For completely customized materials. Makes no assumptions on use" | ≤4.27 |
| `PostProcess` | 6 | "For post processing passes" | ≤4.27 |
| **`RoundedBox`** | 7 | "Rounded Box shader." | **5.0** — absent at 4.27, present at 5.0 ✅ |
| **`SdfFont`** | 8 | "Signed distance field font shader" | **5.4** — absent at 5.3, present at 5.4 |
| **`MsdfFont`** | 9 | "Multi-channel signed distance field font shader" | **5.4** |
| **`Dynamic`** | 10 | "Dynamic branching version of all the shader types above" | **5.8** — absent at 5.7 |

**`ESlateDrawEffect : uint8` (L90):** `None=0`, `NoBlending=1<<0`, `PreMultipliedAlpha=1<<1`, `NoGamma=1<<2`, `InvertAlpha=1<<3`, `NoPixelSnapping=1<<4`, `DisabledEffect=1<<5`, `IgnoreTextureAlpha=1<<6`, `ReverseGamma=1<<7`. Source comment after `InvertAlpha`: `// ^^ These Match ESlateBatchDrawFlag ^^` — **the low 4 bits are deliberately bit-identical between the two enums** so they can be reinterpreted.

**`ESlateBatchDrawFlag : uint16` (L119):** `None=0`, `NoBlending=1<<0`, `PreMultipliedAlpha=1<<1`, `NoGamma=1<<2`, `InvertAlpha=1<<3`, `Wireframe=1<<4`, `TileU=1<<5`, `TileV=1<<6`, `ReverseGamma=1<<7`, `HDR=1<<8` ("Potentially apply to HDR batch when composition is active"). Note `uint16`, not `uint8` — `HDR` forced the widening.

**`FSlateVertex` (L222)** — the actual vertex layout: `float TexCoords[4]`, `FVector2f MaterialTexCoords`, `FVector2f Position` (*window* space, not local), `FColor Color`, `FColor SecondaryColor` ("Generally used for outlines"), `uint16 PixelSize[2]`. Index type: `SLATE_USE_32BIT_INDICES !PLATFORM_USES_GLES` → `uint32` (else `uint16`).

**`FShaderParams` (L188)** — three `FVector4f`s (`PixelParams`, `PixelParams2`, `PixelParams3`), value-compared in the batch key.

## B4. What merges a batch and what breaks one — the exact code

There are **two distinct batch keys**, applied at two different stages. This is the crux.

### Stage 1 — batch formation, within one element-type run

`FSlateElementBatcher::GenerateIndexedVertexBatches` (`SlateCore/Private/Rendering/ElementBatcher.cpp:594-645`) walks the type-grouped element array and extends the current batch while consecutive elements are compatible:

```cpp
while (DrawElements.IsValidIndex(++BatchIndexEnd)) {
    FSlateRenderBatchParams NextBatchParams;
    InElementBatchParamCreator(NextDrawElement, NextBatchParams);
    if (!NextBatchParams.IsBatchableWith(NewBatchParams)) break;   // <- new batch starts here
}
```

Key = `FSlateRenderBatchParams::IsBatchableWith` (`Public/Rendering/SlateRenderBatch.h:30-42`):
```cpp
return Layer == Other.Layer
    && ShaderParams == Other.ShaderParams
    && Resource == Other.Resource
    && PrimitiveType == Other.PrimitiveType
    && ShaderType == Other.ShaderType
    && DrawEffects == Other.DrawEffects
    && DrawFlags == Other.DrawFlags
    && SceneIndex == Other.SceneIndex
    && ClippingState == Other.ClippingState;
```
**Nine fields, and it only merges *adjacent* elements** — a single non-matching element in the middle of a run splits the batch in two even if the elements either side are identical.

### Stage 2 — cross-batch merging, per layer

`FSlateBatchData::MergeRenderBatches` (`ElementBatcher.cpp:171-277`):
```cpp
// Stable sort because order in the same layer should be preserved
BatchIndices.StableSort([](const TPair<int32,int32>& A, const TPair<int32,int32>& B){ return A.Value < B.Value; }); // Value == LayerId
...
if (CurBatch.bIsMergable) {
  for (int32 TestIndex = BatchIndex + 1; TestIndex < BatchIndices.Num(); ++TestIndex) {
     if (TestBatch.GetLayer() != CurBatch.GetLayer()) {
        // none of the batches will be compatible since we encountered an incompatible layer
        break;
     }
     else if (!TestBatch.bIsMerged && CurBatch.IsBatchableWith(TestBatch)) {
        CombineBatches(CurBatch, TestBatch, FinalVertexData, FinalIndexData);
     }
  }
}
```
Key = `FSlateRenderBatch::IsBatchableWith` (`SlateRenderBatch.h:145-160`) — **13 fields, and `LayerId` is *not* among them** because the enclosing loop already restricts to one layer:
```cpp
ShaderResource == Other.ShaderResource && DrawFlags == Other.DrawFlags && ShaderType == Other.ShaderType
&& DrawPrimitiveType == Other.DrawPrimitiveType && DrawEffects == Other.DrawEffects
&& ShaderParams == Other.ShaderParams && InstanceData == Other.InstanceData
&& InstanceCount == Other.InstanceCount && InstanceOffset == Other.InstanceOffset
&& DynamicOffset == Other.DynamicOffset && CustomDrawer == Other.CustomDrawer
&& SceneIndex == Other.SceneIndex && ClippingState == Other.ClippingState;
```

`bIsMergable` defaults to `true` (`Private/Rendering/SlateRenderBatch.cpp`, constructor init list) and is set `false` in exactly two places: `FSlateElementBatcher::AddCustomElement` (`ElementBatcher.cpp:3043`) and `AddCustomVerts` (`:3064`). **`MakeCustom` and `MakeCustomVerts` are batch-merge barriers.**

Batches with no geometry are skipped: `if (CurBatch.bIsMerged || !CurBatch.IsValidForRendering()) continue;` with the comment *"skip already merged batches or batches with invalid data (e.g text with pure whitespace)"*. `IsValidForRendering()` = `(NumVertices>0 && NumIndices>0) || CustomDrawer != nullptr || ShaderType == ESlateShader::PostProcess`.

### Practical list — what breaks a batch

1. **`LayerId`** — hard wall in both stages. Merging never crosses a layer, and Stage 2 `break`s on the first differing layer.
2. **Clipping state** — compared by **pointer** (`const FSlateClippingState* ClippingState`), resolved by `FSlateElementBatcher::ResolveClippingState` (`ElementBatcher.cpp:3167`). Two *identical-valued* clip states in different slots do not merge. Every `PushClip` (`SWidget::Paint` when `bClipToBounds`) is a batch boundary.
3. **Shader resource** (texture atlas / material) — pointer compare. This is why glyphs spilling to a second font atlas page split text batches: `// Font has a new texture for this glyph. Refresh the batch we use and the index we are currently using` (`ElementBatcher.cpp:1340`, and again at `:3506`).
4. **`ESlateShader`** — a rounded-box next to a plain box cannot merge (different shader). Note that in 5.8 `ESlateShader::Dynamic` exists precisely to collapse this axis via dynamic branching.
5. **`ESlateDrawPrimitive`** — `LineList` never merges with `TriangleList`.
6. **`ESlateBatchDrawFlag`** and **`ESlateDrawEffect`** — any difference (e.g. one widget disabled → `DisabledEffect`, or a tiling brush → `TileU|TileV`) splits.
7. **`FShaderParams`** — three `FVector4f` compared by value. For SDF text Epic notes this explicitly (`ElementBatcher.cpp:3586`): *"Note - it would be much better to pass the SDF shader params as per-vertex attributes instead to avoid having to switch batches too often"*.
8. **`SceneIndex`** — differing scenes never merge.
9. **`bIsMergable == false`** — custom drawers / custom verts.
10. **Element type ordering.** `AddElementsInternal` (`ElementBatcher.cpp:374-513`) processes types in a fixed order: Box → RoundedBox → Border → Text → ShapedText → Line → DebugQuad → Spline → Gradient → Viewport → Custom → CustomVerts → PostProcess. Elements of *different* types are never in the same Stage-1 run; they can only be unified in Stage 2, and only if they landed on the same layer with an identical 13-field key — which across types is essentially impossible.

`FSlateBatchData` then flattens into `FinalVertexData`/`FinalIndexData` via `FillBuffersFromNewBatch` / `CombineBatches` (which re-offsets the second batch's indices by the first's vertex count), and `FillVertexAndIndexBuffer` uploads.

## B5. `FSlateDrawLayer`

**`FSlateDrawLayer` does not exist as a type in 5.8, nor at 4.27.** Grep of both `DrawElements.h` versions finds only `class FSlateDrawLayerHandle;` (a forward declaration) plus `struct FSlateLayerElement : public FSlateDrawElement { class FSlateDrawLayerHandle* LayerHandle; }` (`DrawElementTypes.h:700-710`). It is a vestige of the pre-4.24 deferred-layer design. In modern Slate "layer" means the `int32 LayerId` carried on each `FSlateDrawElement` and each `FSlateRenderBatch` — that's what the sort and the merge barrier use. Flagging this because any design mirroring "FSlateDrawLayer" would be copying something Epic has already removed.

## B6. Measuring it

`stat Slate` — `Engine/Source/Runtime/SlateRHIRenderer/Private/SlateRHIRenderingPolicy.cpp:49-54, 625-627`:
```cpp
DECLARE_DWORD_COUNTER_STAT(TEXT("Num Layers"),       STAT_SlateNumLayers,     STATGROUP_Slate);
DECLARE_DWORD_COUNTER_STAT(TEXT("Num Batches"),      STAT_SlateNumBatches,    STATGROUP_Slate);
DECLARE_DWORD_COUNTER_STAT(TEXT("Num Vertices"),     STAT_SlateVertexCount,   STATGROUP_Slate);
DECLARE_DWORD_COUNTER_STAT(TEXT("Clips (Scissor)"),  STAT_SlateScissorClips,  STATGROUP_Slate);
DECLARE_DWORD_COUNTER_STAT(TEXT("Clips (Stencil)"),  STAT_SlateStencilClips,  STATGROUP_Slate);
```
Also `STAT_SlateNumCachedElementLists` / `STAT_SlateNumCachedElements` (`RenderingCommon.h:31-32`) for the invalidation cache's size, and `FSlateInvalidationRoot::FPerformanceStat` (`WidgetsPreUpdate`, `WidgetsAttribute`, `WidgetsPrepass`, `WidgetsUpdate`, `InvalidationProcessing`) behind `WITH_SLATE_DEBUGGING`.

Scissor vs stencil clipping (`Public/Layout/Clipping.h:185-252`): `enum class EClippingMethod : uint8 { Scissor, Stencil }` and `GetClippingMethod() { return ScissorRect.IsSet() ? Scissor : Stencil; }` — *"the simpler clipping is scissor clipping, but that's only possible if the clipping rect is axis aligned."* A rotated clip forces stencil, which sets `bIsStencilBufferRequired` for the whole batch data (`MergeRenderBatches`).

---

# Things I could not confirm / corrections to the brief

- **`FastPathProxyHandle` is a member name, not a type** — the type is `FWidgetProxyHandle`.
- **`SetVolatile` does not exist in Slate.** It is `SWidget::ForceVolatile(bool)` / `UWidget::ForceVolatile(bool)` + `UWidget::bIsVolatile`.
- **`EInvalidateWidgetReason::All` is gone** (removed 5.5), and `PaintAndVolatility`/`LayoutAndVolatility` are the only composites.
- **No `Make*` deprecation "in favour of the `FSlateWindowElementList&` form"** — that form has always been the form. Only `MakePostProcessPass` is deprecated (5.4 → `MakePostProcessBlur`).
- **`FSlateDrawLayer` no longer exists.**
- **`issues.unrealengine.com` returns HTTP 403** to automated fetch, so I could not verify UE-136046 or UE-95748.
- I did not audit `SRetainerWidget` (the third mechanism) beyond noting it also derives from `FSlateInvalidationRoot`; say the word if you want that dissected too.
- The Epic docs pages I cite as supplementary ([invalidation](https://dev.epicgames.com/documentation/en-us/unreal-engine/invalidation-in-slate-and-umg-for-unreal-engine), [invalidation box](https://dev.epicgames.com/documentation/unreal-engine/using-the-invalidation-box-for-umg-in-unreal-engine), [UMG optimization](https://dev.epicgames.com/documentation/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine)) are **partly stale** — the invalidation-box page documents `Cache Relative Transforms`, removed in 4.24. Treat them as intent, not as spec.