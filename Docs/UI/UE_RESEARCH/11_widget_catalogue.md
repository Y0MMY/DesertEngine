# 11 — The complete catalogue of author-facing UMG widgets

Source of truth: **Unreal Engine `release` branch (5.8.2)**, read directly from the private
`EpicGames/UnrealEngine` repo. Every claim below is cited as `path:line` into that tree. Nothing
here comes from the public docs unless explicitly marked **SECONDARY**.

Scope of this document:

* the **whole** of `Engine/Source/Runtime/UMG/Public/Components/` — that directory *is* the
  author-facing widget catalogue;
* the **CommonUI plugin** (`Engine/Plugins/Runtime/CommonUI/`) and its input-routing model;
* the **focus / navigation / hit-test / reply / drag-drop / tooltip** model that all of the above
  sits on.

Panels (layout containers) are covered by a sibling report; here they are only named, with their
Slate counterpart and slot class, so the census is complete.

---

## 0. Ground rules of the UMG object model

### 0.1 Two objects per widget

Every UMG widget is a `UObject` wrapper (`U...`) that *builds* and *synchronises* a Slate widget
(`S...`). The wrapper owns serialised designer data; the Slate widget owns layout and paint.

* `UVisual` → `UWidget` → (`UPanelWidget` → `UContentWidget`) → concrete widgets.
* `Engine/Source/Runtime/UMG/Public/Components/Widget.h:215` — `UCLASS(Abstract, BlueprintType, Blueprintable, CustomFieldNotify)` `class UWidget : public UVisual, public INotifyFieldValueChanged`.
* `Widget.h:1148` `virtual TSharedRef<SWidget> RebuildWidget()` — the one function every widget overrides to construct its `SWidget`.
* `Widget.h:938` `virtual void SynchronizeProperties()` — pushes every UPROPERTY onto the live `SWidget`. Called after construction and again by the editor on every property edit.
* `Widget.h:823` `TSharedRef<SWidget> TakeWidget()` / `Widget.h:845` `TakeDerivedWidget<T>(ConstructMethod)` — the GC-rooted wrapper (`SObjectWidget`) is inserted here.
* `Widget.h:1195-1201` cached weak pointers: `MyWidget`, `ComponentWrapperWidget`, `MyGCWidget`.

**Parity note.** The wrapper/Slate split is the single most consequential design decision in UMG.
It buys you: serialisable designer data separate from runtime state, hot property edit without
rebuilding the tree, and a Blueprint-visible reflection surface. It costs you: two objects, a
`SynchronizeProperties` funnel that is easy to forget (a property added to the header but not to
`SynchronizeProperties` silently does nothing), and `Init*()` methods for properties that can only
be set *before* the `SWidget` exists (see `Button.h:206 InitIsFocusable`, `ComboBoxString.h:237-246`,
`RetainerBox.h:191-200`, `MenuAnchor.h:151-157`, `DynamicEntryBoxBase.h:159-171`, `ScrollBox.h:368-372`).

### 0.2 The property-binding system

UMG exposes a "bind this property to a function" system distinct from the setter API. Each bindable
property gets a companion `F...Delegate` UPROPERTY.

`Widget.h:236-246` declares the canonical binding delegate types:

| Delegate | Returns |
|---|---|
| `FGetBool` | `bool` |
| `FGetFloat` | `float` |
| `FGetInt32` | `int32` |
| `FGetText` | `FText` |
| `FGetSlateColor` | `FSlateColor` |
| `FGetLinearColor` | `FLinearColor` |
| `FGetSlateBrush` | `FSlateBrush` |
| `FGetSlateVisibility` | `ESlateVisibility` |
| `FGetMouseCursor` | `EMouseCursor::Type` |
| `FGetCheckBoxState` | `ECheckBoxState` |
| `FGetWidget` | `UWidget*` |

Plus event-shaped ones: `FGenerateWidgetForString` / `FGenerateWidgetForObject` (`Widget.h:249-250`),
`FOnReply` returning `FEventReply` (`Widget.h:253`), and
`FOnPointerEvent(FGeometry, const FPointerEvent&) -> FEventReply` (`Widget.h:254`).

The `PROPERTY_BINDING(ReturnType, MemberName)` macro (`Widget.h:109-114` editor / `:137-142` shipping)
decides at construct time whether to hand Slate a `TAttribute` bound to the delegate or a constant.
`PROPERTY_BINDING_IMPLEMENTATION` (`Widget.h:123-133`) generates an editor-only caching gate so that
Blueprint debugging does not re-enter the VM mid-paint.

**Parity note.** The important semantic is that *bindings are polled every frame by Slate's
`TAttribute`*, not pushed. That is why UE later added `FieldNotify` (below) — polling every bound
property every frame is the dominant cost in binding-heavy UI.

### 0.3 FieldNotify (push-based change notification)

`Widget.h:223-231` declares field-notification IDs for `ToolTipText`, `Visibility`, `bIsEnabled`;
`Widget.h:798-813` is the `INotifyFieldValueChanged` implementation plus the Blueprint-callable
`K2_AddFieldValueChangedDelegate` / `K2_RemoveFieldValueChangedDelegate` /
`K2_BroadcastFieldValueChanged`. Individual widgets opt properties in with the `FieldNotify`
UPROPERTY specifier — e.g. `Slider.h:33` `Value`, `ProgressBar.h:33` `Percent`,
`CheckBox.h:38` `CheckedState`, `Image.h:38` `Brush`, `WidgetSwitcher.h:23` `ActiveWidgetIndex`,
`ComboBoxString.h:35` `SelectedOption`, `ExpandableArea.h:45` `bIsExpanded`,
`SpinBox.h:32` `Value`, `InputKeySelector.h:42` `SelectedKey`.

### 0.4 Widget state bitfield

`Widget.h:256` `DECLARE_MULTICAST_DELEGATE_TwoParams(FOnWidgetStateBroadcast, UWidget*, const FWidgetStateBitfield&)`;
`Widget.h:1020` `RegisterPostStateListener(...)`, `:1027` `UnregisterPostStateListener`,
`:1134` `BroadcastBinaryPostStateChange`. Concrete enum-state registrations live next to the widget —
e.g. `CheckBox.h:183-211` `UWidgetCheckedStateRegistration` with static bitfields
`Unchecked` / `Checked` / `Undetermined` and `StateName = "CheckedState"` (`CheckBox.h:195`).
`Widget.h:1141` marks `BroadcastEnumPostStateChange` **deprecated in 5.5** ("FWidgetStateBitfield
currently no longer supports enum states").

### 0.5 Every property `UWidget` gives you for free

These exist on **every** widget in the catalogue and are not repeated per entry.

| Property | Type | Default / note | Line |
|---|---|---|---|
| `Slot` | `UPanelSlot*` (Instanced) | parent-slot layout data, `ShowOnlyInnerProperties` | `Widget.h:264` |
| `ToolTipText` | `FText` | `MultiLine`; **direct access deprecated 5.1** | `Widget.h:277` |
| `ToolTipTextDelegate` | `FGetText` | | `Widget.h:272` |
| `ToolTipWidget` | `UWidget*` | `VisibleAnywhere`, `AdvancedDisplay`; **deprecated 5.1** | `Widget.h:282` |
| `ToolTipWidgetDelegate` | `FGetWidget` | | `Widget.h:286` |
| `VisibilityDelegate` | `FGetSlateVisibility` | | `Widget.h:291` |
| `RenderTransform` | `FWidgetTransform` | DisplayName "Transform"; **deprecated 5.1** | `Widget.h:298` |
| `RenderTransformPivot` | `FVector2D` | normalised; **deprecated 5.1** | `Widget.h:306` |
| `FlowDirectionPreference` | `EFlowDirectionPreference` | **deprecated 5.1** | `Widget.h:311` |
| `bIsVariable` | `uint8:1` | exposes as BP variable | `Widget.h:318` |
| `bCreatedByConstructionScript` | `uint8:1` (Transient) | | `Widget.h:322` |
| `bIsEnabled` | `uint8:1` | FieldNotify; **deprecated 5.1** | `Widget.h:327` |
| `bOverride_Cursor` | `uint8:1` | `InlineEditConditionToggle` | `Widget.h:331` |
| `bIsVolatile` | `uint8:1` | Category "Performance", BlueprintReadOnly | `Widget.h:388` |
| `bWrappedByComponent` | `uint8:1` (Transient) | | `Widget.h:402` |
| `Cursor` | `TEnumAsByte<EMouseCursor::Type>` | `AdvancedDisplay`, editcondition `bOverride_Cursor`; **deprecated 5.1** | `Widget.h:422` |
| `Clipping` | `EWidgetClipping` | Category "Rendering"; **deprecated 5.1** | `Widget.h:435` |
| `Visibility` | `ESlateVisibility` | FieldNotify; **deprecated 5.1** | `Widget.h:440` |
| `PixelSnapping` | `EWidgetPixelSnapping` | private w/ `AllowPrivateAccess` | `Widget.h:445` |
| `RenderOpacity` | `float` | `UIMin 0 / UIMax 1`; **deprecated 5.1** | `Widget.h:452` |
| `AccessibleWidgetData` | `USlateAccessibleWidgetData*` (Instanced) | private | `Widget.h:457` |
| `Navigation` | `UWidgetNavigation*` (Instanced) | Category "Navigation" | `Widget.h:466` |

Editor-only accessibility block (`Widget.h:346-376`): `bOverrideAccessibleDefaults`,
`bCanChildrenBeAccessible`, `AccessibleBehavior`, `AccessibleSummaryBehavior` (`AdvancedDisplay`),
`AccessibleText` (MultiLine), `AccessibleTextDelegate`, `AccessibleSummaryText`,
`AccessibleSummaryTextDelegate`. Editor designer flags `bHiddenInDesigner`, `bExpandedInDesigner`,
`bLockedInDesigner` at `Widget.h:408-416`.

**`UWidget` Blueprint API** (all `UFUNCTION(BlueprintCallable)` unless noted):
`SetRenderTransform` `:504`, `SetRenderScale` `:508`, `SetRenderShear` `:512`,
`SetRenderTransformAngle` `:516`, `GetRenderTransformAngle` `:520`, `SetRenderTranslation` `:524`,
`SetRenderTransformPivot` `:531`, `GetIsEnabled` `:541`, `SetIsEnabled` `:545`,
`IsInViewport` (BlueprintPure) `:549`, `SetToolTipText` `:556`, `SetToolTip` `:563`,
`SetCursor` `:570`, `ResetCursor` `:574`, `IsRendered` `:578`, `IsVisible` `:582`,
`GetVisibility` `:586`, `SetVisibility` `:590`, `GetRenderOpacity` `:598`, `SetRenderOpacity` `:602`,
`GetClipping` `:606`, `SetClipping` `:610`, `ForceVolatile` `:620`, `IsHovered` `:624`,
`HasKeyboardFocus` `:632`, `SupportsKeyboardFocus` `:640`, `HasMouseCapture` `:647`,
`HasMouseCaptureByUser` `:656`, `SetKeyboardFocus` `:660`, `HasUserFocus` `:664`,
`HasAnyUserFocus` `:668`, `HasFocusedDescendants` `:672`, `HasUserFocusedDescendants` `:676`,
`SetFocus` `:680`, `SetUserFocus` `:684`, `ForceLayoutPrepass` `:692`,
`InvalidateLayoutAndVolatility` `:699`, `GetDesiredSize` `:709`, `SetAllNavigationRules` `:717`,
`SetNavigationRule` (**deprecated 4.23**) `:727`, `SetNavigationRuleBase` `:735`,
`SetNavigationRuleExplicit` `:743`, `SetNavigationRuleCustom` `:751`,
`SetNavigationRuleCustomBoundary` `:759`, `GetParent` `:769`, `RemoveFromParent` `:776`,
`GetCachedGeometry` `:788`, `GetTickSpaceGeometry` `:791`, `GetPaintSpaceGeometry` `:794`,
`GetGameInstance` `:871`, `GetOwningPlayer` `:888`, `GetOwningLocalPlayer` `:905`,
`GetAccessibleText` `:923`, `GetAccessibleSummaryText` `:931`.
Non-BP: `SetPixelSnapping` `:616`, `SetFlowDirectionPreference` `:537`,
`SetNavigationMethod(TInstancedStruct<FNavigationMethod>)` `:765`, `BuildNavigation()` `:945`.

### 0.6 `ESlateVisibility` — five values, not two

`Engine/Source/Runtime/UMG/Public/Components/SlateWrapperTypes.h:21-33`:

| Value | Occupies layout? | Painted? | Hit-testable? | Children hit-testable? |
|---|---|---|---|---|
| `Visible` | yes | yes | yes | yes |
| `Collapsed` | **no** | no | no | n/a |
| `Hidden` | yes | no | no | n/a |
| `HitTestInvisible` ("Not Hit-Testable (Self & All Children)") | yes | yes | **no** | **no** |
| `SelfHitTestInvisible` ("Not Hit-Testable (Self Only)") | yes | yes | **no** | **yes** |

`Widget.h:1108-1109` `ConvertSerializedVisibilityToRuntime` / `ConvertRuntimeToSerializedVisibility`
map this onto Slate's `EVisibility`. **This five-value visibility is load-bearing**: most
non-interactive containers default themselves to `SelfHitTestInvisible` in their constructor so they
do not eat clicks (`Spacer.cpp:20`, `RichTextBlock.cpp:53`, `UniformGridPanel.cpp:19`,
`WidgetSwitcher.cpp:18`, `BackgroundBlur.cpp:30`, `InvalidationBox.cpp:21`). A parity
implementation with only visible/hidden will produce a UI where every decorative panel blocks input.

Other types in that header: `ESlateAccessibleBehavior` (`:37`) `NotAccessible|Auto|Summary|Custom|ToolTip`;
`USlateAccessibleWidgetData` (`:61`); `ESlateSizeRule` (`:113`) `Automatic|Fill`;
`FEventReply` (`:128`, wraps `FReply NativeReply`); `FSlateChildSize` (`:155`) `{ float Value = 1.0f; ESlateSizeRule::Type SizeRule = Fill; }`;
`EVirtualKeyboardType` (`:182`) `Default|Number|Web|Email|Password|AlphaNumeric`.

### 0.7 Designer / compiler metadata

`Widget.h:53-96` documents the UPROPERTY/UCLASS meta keywords a parity system would need an
equivalent of:

* `EntryClass`, `EntryInterface` — constrain the entry-widget class picker on `DynamicEntryBox`/`ListViewBase`.
* `DesignerRebuild` — editing this property rebuilds the designer preview (as opposed to going through `SynchronizeProperties`).
* `BindWidget`, `BindWidgetOptional`, `OptionalWidget` — a native `UPROPERTY` that *must* / *may* be satisfied by a widget of that name in the designer tree. This is UMG's native↔designer binding mechanism; the compile fails if a required `BindWidget` is unsatisfied.
* `BindWidgetAnim`, `BindWidgetAnimOptional` — same, for animations.
* `IsBindableEvent` — surfaces a dynamic delegate property in the Details panel as an event row.

`EWidgetDesignFlags` (`Widget.h:175-182`): `None`, `Designing = 1<<0`, `ShowOutline = 1<<1`,
`ExecutePreConstruct = 1<<2`, `Previewing = 1<<3`. `IsDesignTime()` `:949` / `IsPreviewTime()` `:969`.
`ValidateCompiledDefaults(IWidgetCompilerLog&)` `:994` is the per-widget compile-time validation hook
(used by `URichTextBlock`, `UDynamicEntryBox`, `UListViewBase`).

### 0.8 `UUserWidget` — the authored widget

`Engine/Source/Runtime/UMG/Public/Blueprint/UserWidget.h`. This is the class users subclass in
Blueprint; it owns a `WidgetTree` of the widgets catalogued below.

Author-exposed properties: `ColorAndOpacity` `:996` (+ `ColorAndOpacityDelegate` `:999`),
`ForegroundColor` `:1007` (+ delegate `:1010`), `OnVisibilityChanged` (BlueprintAssignable) `:1014`,
`Padding` `:1024`, `Priority` (input-action priority) `:1028`, `bIsFocusable:1` `:1033`,
`bStopAction:1` (input-action blocking) `:1037`, `bAutomaticallyRegisterInputOnConstruction:1` `:1046`,
`TickFrequency` (`EWidgetTickFrequency`, `EditDefaultsOnly`) `:1725`,
`DesiredFocusWidget` (`FWidgetChild`, `EditDefaultsOnly`) `:1728`.
Editor-only: `DesignTimeSize` `:1532`, `DesignSizeMode` `:1535`, `PaletteCategory` `:1539`,
`PreviewBackground` `:1546`.

Lifecycle events (`BlueprintImplementableEvent`): `OnInitialized` `:508`, `PreConstruct(bool IsDesignTime)` `:523`,
`Construct` `:531`, `Destruct` `:538`, `Tick(FGeometry, float)` `:547`, `OnPaint(FPaintContext&)` `:553`,
`IsInteractable` `:559`.

Input events, all returning `FEventReply` unless noted:
`OnFocusReceived` `:569`, `OnFocusLost` (void) `:577`, `OnAddedToFocusPath` (void) `:586`,
`OnRemovedFromFocusPath` (void) `:595`, `OnKeyChar` `:605`, `OnPreviewKeyDown` `:619`, `OnKeyDown` `:629`,
`OnKeyUp` `:639`, `OnAnalogValueChanged` `:649`, `OnMouseButtonDown` `:659`,
`OnPreviewMouseButtonDown` `:673`, `OnMouseButtonUp` `:683`, `OnMouseMove` `:693`,
`OnMouseEnter` (void) `:702`, `OnMouseLeave` (void) `:710`, `OnMouseWheel` `:719`,
`OnMouseButtonDoubleClick` `:729`, `OnTouchGesture` `:809`, `OnTouchStarted` `:818`,
`OnTouchMoved` `:827`, `OnTouchEnded` `:836`, `OnMotionDetected` `:846`, `OnMouseCaptureLost` (void) `:852`,
`OnTouchForceChanged` `:873`, `OnTouchFirstMove` `:882`.

Drag-drop events: `OnDragDetected(FGeometry, const FPointerEvent&, UDragDropOperation*& Operation)` `:746`,
`OnDragCancelled` `:756`, `OnDragEnter` `:766`, `OnDragLeave` `:775`, `OnDragOver` → `bool` `:787`,
`OnDrop` → `bool` `:799`.

Viewport/screen API: `AddToViewport(int32 ZOrder = 0)` `:342`, `AddToPlayerScreen(int32 ZOrder = 0)` `:351`,
`RemoveFromViewport` `:358`, `SetPositionInViewport(FVector2D, bool bRemoveDPIScale = true)` `:368`,
`SetDesiredSizeInViewport` `:372`, `SetAnchorsInViewport` `:376`, `SetAlignmentInViewport` `:380`,
`GetAnchorsInViewport` `:384`, `GetAlignmentInViewport` `:388`, `GetIsVisible` `:393`,
`SetOwningPlayer` `:447`, `GetOwningPlayerPawn` `:454`, `GetOwningPlayerCameraManager` `:489`,
`SetDesiredFocusWidget(UWidget*)` `:1408`.

Animation API (a full sequencer runtime, not a tween helper): `PlayAnimation` `:1193`,
`PlayAnimationTimeRange` `:1207`, `PlayAnimationForward` `:1220`, `PlayAnimationReverse` `:1233`,
`StopAnimation` `:1241`, `StopAllAnimations` `:1249`, `PauseAnimation` `:1258`,
`GetAnimationCurrentTime` `:1267`, `SetAnimationCurrentTime` `:1276`, `IsAnimationPlaying` `:1285`,
`IsAnyAnimationPlaying` `:1291`, `SetNumLoopsToPlay` `:1300`, `SetPlaybackSpeed` `:1309`,
`ReverseAnimation` `:1317`, `IsAnimationPlayingForward` `:1325`, `FlushAnimations` `:1331`,
`IsPlayingAnimation` `:1435`; queued variants `QueuePlayAnimation` `:1117`,
`QueuePlayAnimationTimeRange` `:1131`, `QueuePlayAnimationForward` `:1144`,
`QueuePlayAnimationReverse` `:1157`, `QueueStopAnimation` `:1165`, `QueueStopAllAnimations` `:1171`,
`QueuePauseAnimation` `:1180`; binding `BindToAnimationStarted` `:892`, `BindToAnimationFinished` `:911`,
`BindToAnimationEvent(..., EWidgetAnimationEvent, FName UserTag)` `:932`; native events
`OnAnimationStarted` `:945`, `OnAnimationFinished` `:955` (both `BlueprintNativeEvent`).

Extensions: `GetExtension` `:1342`, `GetExtensions` `:1346`, `AddExtension` `:1357`,
`GetOrAddExtension` `:1368`, `RemoveExtension` `:1372`, `RemoveExtensions` `:1383`
(`UUserWidgetExtension`, `:1519`).

Legacy input-action API: `ListenForInputAction(FName, EInputEvent, bool bConsume, FOnInputAction)` `:1669`,
`StopListeningForInputAction` `:1675`, `StopListeningForAllInputActions` `:1681`,
`RegisterInputComponent` `:1689`, `UnregisterInputComponent` `:1697`,
`IsListeningForInputAction` `:1703`, `SetInputActionPriority` `:1706`, `SetInputActionBlocking` `:1709`.

---

## 1. Common widgets

### 1.1 `UTextBlock` → `STextBlock`

`Engine/Source/Runtime/UMG/Public/Components/TextBlock.h:23`, `UCLASS(meta=(DisplayName="Text"))`,
parent `UTextLayoutWidget`. Static text. No children.

**Shared text base — `UTextLayoutWidget`** (`Components/TextWidgetTypes.h:72`), inherited by
`UTextBlock`, `URichTextBlock`, `UMultiLineEditableText`, `UMultiLineEditableTextBox`:

| Property | Type | Default | Line |
|---|---|---|---|
| `ShapedTextOptions` | `FShapedTextOptions` | see below | `TextWidgetTypes.h:128` |
| `Justification` | `TEnumAsByte<ETextJustify::Type>` | `Left` | `:136` / default `TextWidgetTypes.cpp:21` |
| `WrappingPolicy` | `ETextWrappingPolicy` | `DefaultWrapping` | `:140` / `.cpp:24` |
| `AutoWrapText` | `uint8:1` | `false` | `:144` / `.cpp:22` |
| `ApplyLineHeightToBottomLine` | `bool` | `true` | `:148` / `.cpp:27` |
| `FontFacesLoadingPaintPolicy` | `EFontFacesLoadingPaintPolicy` | `DoNotPaint` | `:152` / `.cpp:28` |
| `WrapTextAt` | `float` | `0.0f` (0 or negative = no wrap) | `:156` / `.cpp:23` |
| `Margin` | `FMargin` | `FMargin(0.0f)` | `:160` / `.cpp:25` |
| `LineHeightPercentage` | `float` | `1.0f` | `:164` / `.cpp:26` |

Event: `OnAllFontFacesFinishLoading` (`BlueprintAssignable`, sparse) `TextWidgetTypes.h:132`.
`FShapedTextOptions` (`TextWidgetTypes.h:20`): `bOverride_TextShapingMethod:1` (false),
`bOverride_TextFlowDirection:1` (false), `TextShapingMethod` (`ETextShapingMethod::Auto`),
`TextFlowDirection` (`ETextFlowDirection::Auto`) — defaults `TextWidgetTypes.cpp:11-15`.

**`UTextBlock`'s own properties:**

| Property | Type | Default | Line |
|---|---|---|---|
| `Text` | `FText` | empty; MultiLine; **deprecated 5.1** | `TextBlock.h:31` |
| `TextDelegate` | `FGetText` | | `:35` |
| `ColorAndOpacity` | `FSlateColor` | `FLinearColor::White` | `:40` / `TextBlock.cpp:28` |
| `ColorAndOpacityDelegate` | `FGetSlateColor` | | `:44` |
| `MinDesiredWidth` | `float` | `0` | `:49` |
| `Font` | `FSlateFontInfo` | Roboto, size **24**, `"Bold"` | `:54` / `TextBlock.cpp:36` |
| `StrikeBrush` | `FSlateBrush` | empty | `:59` |
| `ShadowOffset` | `FVector2D` | `(1.0f, 1.0f)` | `:64` / `.cpp:27` |
| `ShadowColorAndOpacity` | `FLinearColor` | `FLinearColor::Transparent` | `:69` / `.cpp:29` |
| `ShadowColorAndOpacityDelegate` | `FGetLinearColor` | | `:73` |
| `bWrapWithInvalidationPanel` | `bool` | `false`; Category "Performance", `AdvancedDisplay` | `:77` / `.cpp:25` |
| `TextTransformPolicy` | `ETextTransformPolicy` | `None` | `:82` / `.cpp:30` |
| `TextOverflowPolicy` | `ETextOverflowPolicy` | `Clip`; Category "Clipping", `AdvancedDisplay` | `:87` / `.cpp:31` |
| `bSimpleTextMode` | `bool` | `false`; protected, `DesignerRebuild` | `:288` |

`bSimpleTextMode` (`:283-287`) disables shaping/wrapping/justification for a large speed win on
ASCII-only text. Accessibility default: `AccessibleBehavior = Auto`, `bCanChildrenBeAccessible = false`
(`TextBlock.cpp:41-42`).

API: `GetText` `:95`, `SetText` `:103`, `GetColorAndOpacity` `:106`, `SetColorAndOpacity` `:114`,
`SetOpacity` `:122`, `GetShadowColorAndOpacity` `:125`, `SetShadowColorAndOpacity` `:134`,
`GetShadowOffset` `:137`, `SetShadowOffset` `:145`, `GetFont` `:148`, `SetFont` `:156`,
`GetStrikeBrush` `:159`, `SetStrikeBrush` `:167`, `GetMinDesiredWidth` `:170`, `SetMinDesiredWidth` `:178`,
`SetAutoWrapText` `:186`, `GetTextTransformPolicy` `:189`, `SetTextTransformPolicy` `:197`,
`GetTextOverflowPolicy` `:200`, `SetTextOverflowPolicy` `:208`, `SetFontMaterial` `:211`,
`SetFontOutlineMaterial` `:214`, `SetFontSize(float DisplayFontSize)` `:222` (DPI-aware),
`GetFontSize` `:230`, `GetDynamicFontMaterial` `:234`, `GetDynamicOutlineMaterial` `:238`.

**No style struct** — `UTextBlock` exposes font/colour/shadow *flat*, unlike every other widget.
(`URichTextBlock` and `UMultiLineEditableText` use `FTextBlockStyle` instead.)

### 1.2 `URichTextBlock` → `SRichTextBlock`

`Components/RichTextBlock.h:39`, parent `UTextLayoutWidget`. Markup-decorated text.

| Property | Type | Default | Line |
|---|---|---|---|
| `Text` | `FText` | MultiLine; **deprecated 5.2** | `:46` |
| `TextStyleSet` | `UDataTable*` | null; must have `RowStructure=/Script/UMG.RichTextStyleRow` | `:51` |
| `DecoratorClasses` | `TArray<TSubclassOf<URichTextBlockDecorator>>` | empty | `:55` |
| `DefaultTextStyleOverride` | `FTextBlockStyle` | editcondition `bOverrideDefaultStyle` | `:60` |
| `MinDesiredWidth` | `float` | 0; **deprecated 5.2** | `:65` |
| `bOverrideDefaultStyle` | `bool` | `false` | `:69` |
| `TextTransformPolicy` | `ETextTransformPolicy` | `None` | `:74` / `RichTextBlock.cpp:54` |
| `TextOverflowPolicy` | `ETextOverflowPolicy` | `Clip` | `:79` / `.cpp:55` |

Constructor sets visibility to `SelfHitTestInvisible` (`RichTextBlock.cpp:53`).
`FRichTextStyleRow : FTableRowBase { FTextBlockStyle TextStyle; }` (`:22-30`) — styles come from a
**DataTable**, keyed by row name, referenced from markup as `<RowName>text</>`.

API: `SetDefaultColorAndOpacity` `:87`, `SetDefaultShadowColorAndOpacity` `:95`,
`SetDefaultShadowOffset` `:102`, `SetDefaultFont` `:109`, `SetDefaultStrikeBrush` `:116`,
`SetMinDesiredWidth` `:123`, `SetAutoWrapText` `:130`, `SetTextTransformPolicy` `:137`,
`SetTextOverflowPolicy` `:144`, `SetDefaultTextStyle(const FTextBlockStyle&)` `:151`,
`SetDefaultMaterial` `:154`, `ClearAllDefaultStyleOverrides` `:158`, `GetDefaultDynamicMaterial` `:165`,
`SetDecorators(TArray<TSubclassOf<URichTextBlockDecorator>>)` `:171`, `GetText` `:196`, `SetText` `:204`,
`GetTextStyleSet` `:207`, `SetTextStyleSet` `:210`, `GetDecoratorByClass` `:217`, `RefreshTextLayout` `:223`.
Extension points: `CreateDecorators` `:241`, `CreateMarkupParser` `:242`, `CreateMarkupWriter` `:243`,
`UpdateStyleData` `:239`, `ApplyUpdatedDefaultTextStyle` `:246`.
`ValidateCompiledDefaults` `:188` — compile-time validation of the style set.

**Decorators.** `Components/RichTextBlockDecorator.h`:
`FRichTextDecorator : ITextDecorator` (`:20`) with `Supports(const FTextRunParseResults&, const FString&)` `:27`,
`CreateDecoratorWidget(const FTextRunInfo&, const FTextBlockStyle&)` `:36`,
`CreateDecoratorText(const FTextRunInfo&, FTextBlockStyle&, FString&)` `:39`;
`URichTextBlockDecorator : UObject` (`:45`, `Abstract, Blueprintable`) with
`CreateDecorator(URichTextBlock*)` `:52`.
`Components/RichTextBlockImageDecorator.h`: `FRichImageRow : FTableRowBase { FSlateBrush Brush; }` (`:21-29`),
`URichTextBlockImageDecorator` (`:39`, `Abstract, Blueprintable`) with
`ImageSet` (`UDataTable*`, requires `RowStructure=/Script/UMG.RichImageRow`) `:55`,
`FindImageBrush(FName TagOrId, bool bWarnIfMissing)` `:48`. Markup form: `<img id="NameOfBrushInTable"></>` (`:36`).

**Parity note.** Rich text in UE is: a markup parser → runs → per-run `FTextBlockStyle` looked up from
a DataTable → optional decorator widgets inlined into the text layout. The DataTable indirection is
what makes styles authorable without touching each text block.

### 1.3 `UImage` → `SImage`

`Components/Image.h:30`. Draws an `FSlateBrush` (texture, material, atlas region, or soft reference).

| Property | Type | Default | Line |
|---|---|---|---|
| `Brush` | `FSlateBrush` | empty; FieldNotify; **deprecated 5.2** | `:39` |
| `BrushDelegate` | `FGetSlateBrush` | | `:43` |
| `ColorAndOpacity` | `FLinearColor` | `FLinearColor::White`; `meta=(sRGB="true")` | `:48` / `Image.cpp:27` |
| `ColorAndOpacityDelegate` | `FGetLinearColor` | | `:52` |
| `bFlipForRightToLeftFlowDirection` | `bool` | `false`; Category "Localization" | `:57` |
| `OnMouseButtonDownEvent` | `FOnPointerEvent` | `EditAnywhere, meta=(IsBindableEvent="True")` | `:62` |

API: `SetColorAndOpacity` `:68`, `GetColorAndOpacity` `:70`, `SetOpacity` `:74`,
`SetBrushSize` (**deprecated 5.0**, use `SetDesiredSizeOverride`) `:78`, `SetDesiredSizeOverride` `:81`,
`SetBrushTintColor` `:85`, `SetBrushResourceObject` `:89`, `SetBrush` `:93`, `GetBrush` `:95`,
`SetBrushFromAsset(USlateBrushAsset*)` `:99`, `SetBrushFromTexture(UTexture2D*, bool bMatchSize=false)` `:108`,
`SetBrushFromAtlasInterface(TScriptInterface<ISlateTextureAtlasInterface>, bool bMatchSize=false)` `:117`,
`SetBrushFromTextureDynamic(UTexture2DDynamic*, bool bMatchSize=false)` `:126`,
`SetBrushFromMaterial(UMaterialInterface*)` `:130`,
`SetBrushFromSoftTexture(TSoftObjectPtr<UTexture2D>, bool bMatchSize=false)` `:139`,
`SetBrushFromSoftMaterial(TSoftObjectPtr<UMaterialInterface>)` `:143`, `GetDynamicMaterial` `:147`,
`SetFlipForRightToLeftFlowDirection` `:149`, `ShouldFlipForRightToLeftFlowDirection` `:151`.

**Async streaming is built in**: `RequestAsyncLoad` `:176/:177`, `CancelImageStreaming` `:180`,
`OnImageStreamingStarted` `:183`, `OnImageStreamingComplete` `:186`, with
`TSharedPtr<FStreamableHandle> StreamingHandle` / `FSoftObjectPath StreamingObjectPath` `:198-199`.
This is how `SetBrushFromSoftTexture` works without a hitch.

**Materials on Image** is the "Media & 3D" path — `SetBrushFromMaterial` + `GetDynamicMaterial`
returns a `UMaterialInstanceDynamic` whose parameters you drive per frame. There is no separate
"video widget" in base UMG; media playback is a material with a media texture.

### 1.4 `UButton` → `SButton`

`Components/Button.h:32`, parent `UContentWidget` (single child).

| Property | Type | Default | Line |
|---|---|---|---|
| `WidgetStyle` (DisplayName "Style") | `FButtonStyle` | `FDefaultStyleCache::GetRuntime().GetButtonStyle()` (editor variant if `IsEditorWidget()`) | `:40` / `Button.cpp:26,33` |
| `ColorAndOpacity` | `FLinearColor` | `White`, sRGB | `:45` / `.cpp:39` |
| `BackgroundColor` | `FLinearColor` | `White`, sRGB | `:50` / `.cpp:40` |
| `ClickMethod` | `TEnumAsByte<EButtonClickMethod::Type>` | `DownAndUp`; `AdvancedDisplay` | `:55` / `.cpp:42` |
| `TouchMethod` | `TEnumAsByte<EButtonTouchMethod::Type>` | `DownAndUp`; `AdvancedDisplay` | `:60` / `.cpp:43` |
| `PressMethod` | `TEnumAsByte<EButtonPressMethod::Type>` | (unset in ctor → enum 0 = `DownAndUp`); `AdvancedDisplay` | `:65` |
| `IsFocusable` | `bool` | `true`; **read-only, deprecated 5.2** | `:70` / `.cpp:48` |
| `bAllowDragDrop` | `bool` | `false`; protected but `EditAnywhere` | `:225` / `.cpp:46` |

Accessibility defaults `Summary` / children not accessible (`Button.cpp:52-53`).

**Events (BlueprintAssignable):** `OnClicked` (`FOnButtonClickedEvent`) `:76`, `OnPressed` `:80`,
`OnReleased` `:84`, `OnHovered` `:87`, `OnUnhovered` `:90`.
**Native-only delegates:** `OnButtonDragDetected` (`FOnDragDetected`) `:93`, `OnButtonDragEnter` `:96`,
`OnButtonDragLeave` `:99`, `OnButtonDragOver` `:102`, `OnButtonDrop` `:105`,
`OnReceivedFocus` (`FSimpleDelegate`) `:108`, `OnLostFocus` `:111`.

API: `SetStyle` `:117`, `GetStyle` `:119`, `SetColorAndOpacity` `:123`, `GetColorAndOpacity` `:125`,
`SetBackgroundColor` `:129`, `GetBackgroundColor` `:131`, `IsPressed` `:139`, `SetClickMethod` `:142`,
`GetClickMethod` `:144`, `SetTouchMethod` `:147`, `GetTouchMethod` `:149`, `SetPressMethod` `:152`,
`GetPressMethod` `:154`, `GetIsFocusable` `:156`, `SetAllowDragDrop` `:159`.

**`FButtonStyle`** — `Engine/Source/Runtime/SlateCore/Public/Styling/SlateTypes.h:508`:
`Normal` `:526`, `Hovered` `:531`, `Pressed` `:536`, `Disabled` `:541` (all `FSlateBrush`);
`NormalForeground` `:546`, `HoveredForeground` `:551`, `PressedForeground` `:556`,
`DisabledForeground` `:561` (all `FSlateColor`, `AdvancedDisplay`);
`NormalPadding` `:571`, `PressedPadding` `:579` (`FMargin`);
`PressedSlateSound` `:586`, `ClickedSlateSound` `:593`, `HoveredSlateSound` `:600` (`FSlateSound`);
deprecated `PressedSound_DEPRECATED` `:605`, `HoveredSound_DEPRECATED` `:607`.

**Three separate activation-method enums** (`EButtonClickMethod` / `EButtonTouchMethod` /
`EButtonPressMethod`) is a parity detail worth copying: mouse, touch and key/gamepad each need their
own down/up semantics, and the `PressedPadding` shift is what makes a button feel "pressed" without
a separate pressed layout.

### 1.5 `UCheckBox` → `SCheckBox`

`Components/CheckBox.h:31`, parent `UContentWidget`. Tri-state: also serves as toggle button and
radio button (`:23-25`).

| Property | Type | Default | Line |
|---|---|---|---|
| `CheckedState` | `ECheckBoxState` | `Unchecked`; FieldNotify | `:39` / `CheckBox.cpp:33` |
| `CheckedStateDelegate` | `FGetCheckBoxState` | **deprecated 5.2** → `InitCheckedStateDelegate()` | `:44` |
| `WidgetStyle` (DisplayName "Style") | `FCheckBoxStyle` | style cache | `:49` / `.cpp:24,29` |
| `HorizontalAlignment` | `TEnumAsByte<EHorizontalAlignment>` | `HAlign_Fill` | `:53` / `.cpp:35` |
| `ClickMethod` | `EButtonClickMethod::Type` | `DownAndUp` | `:58` / `.cpp:37` |
| `TouchMethod` | `EButtonTouchMethod::Type` | `DownAndUp` | `:63` / `.cpp:38` |
| `PressMethod` | `EButtonPressMethod::Type` | `DownAndUp` | `:68` / `.cpp:39` |
| `IsFocusable` | `bool` | `true`; construction-only, **deprecated 5.2** | `:73` / `.cpp:41` |

Event: `OnCheckStateChanged(bool bIsChecked)` (`FOnCheckBoxComponentStateChanged`, BlueprintAssignable) `:79`.
API: `IsPressed` `:85`, `IsChecked` `:89`, `GetCheckedState` `:93`, `SetIsChecked(bool)` `:97`,
`SetCheckedState(ECheckBoxState)` `:101`, `GetWidgetStyle` `:104`, `SetWidgetStyle` `:107`,
`GetClickMethod` `:110`, `SetClickMethod` `:114`, `GetTouchMethod` `:117`, `SetTouchMethod` `:121`,
`GetPressMethod` `:124`, `SetPressMethod` `:128`, `GetIsFocusable` `:131`.

**`FCheckBoxStyle`** — `SlateTypes.h:104`: `CheckBoxType` (`ESlateCheckBoxType::Type`) `:121`;
nine state brushes `UncheckedImage` `:126`, `UncheckedHoveredImage` `:131`, `UncheckedPressedImage` `:136`,
`CheckedImage` `:141`, `CheckedHoveredImage` `:146`, `CheckedPressedImage` `:151`,
`UndeterminedImage` `:156`, `UndeterminedHoveredImage` `:161`, `UndeterminedPressedImage` `:166`;
`Padding` `:171`; `BackgroundImage` `:176`, `BackgroundHoveredImage` `:181`, `BackgroundPressedImage` `:186`;
`ForegroundColor` `:191`, `HoveredForeground` `:196`, `PressedForeground` `:201`.
(Sounds also exist on this style further down the struct.)

**3 states × 3 interaction states = 9 brushes** plus a separate background triple. A parity
implementation that models a checkbox as "on/off image" will not be able to express UE's toggle-button
mode.

### 1.6 `USlider` → `SSlider`

`Components/Slider.h:26`, parent `UWidget`. No children.

| Property | Type | Default | Line |
|---|---|---|---|
| `Value` | `float` | 0; FieldNotify; `UIMin 0 / UIMax 1` | `:34` |
| `ValueDelegate` | `FGetFloat` | | `:38` |
| `MinValue` | `float` | `0.0f` | `:43` / `Slider.cpp:20` |
| `MaxValue` | `float` | `1.0f` | `:48` / `.cpp:21` |
| `WidgetStyle` (DisplayName "Style") | `FSliderStyle` | style cache | `:54` / `.cpp:30,35` |
| `Orientation` | `TEnumAsByte<EOrientation>` | `Orient_Horizontal` | `:59` / `.cpp:22` |
| `SliderBarColor` | `FLinearColor` | `White` | `:64` / `.cpp:23` |
| `SliderHandleColor` | `FLinearColor` | `White` | `:69` / `.cpp:24` |
| `IndentHandle` | `bool` | (unset → false); `AdvancedDisplay` | `:74` |
| `Locked` | `bool` | (unset → false); `AdvancedDisplay` | `:79` |
| `MouseUsesStep` | `bool` | `false`; `AdvancedDisplay`, read-only | `:83` / `.cpp:27` |
| `RequiresControllerLock` | `bool` | **`true`**; `AdvancedDisplay`, read-only | `:87` / `.cpp:28` |
| `StepSize` | `float` | `0.01f`; `UIMin 0 / UIMax 1` | `:92` / `.cpp:25` |
| `IsFocusable` | `bool` | `true` | `:96` / `.cpp:26` |
| `bPreventThrottling` | `bool` | `false` | `:100` |

Events (all BlueprintAssignable): `OnMouseCaptureBegin` `:106`, `OnMouseCaptureEnd` `:110`,
`OnControllerCaptureBegin` `:114`, `OnControllerCaptureEnd` `:118`, `OnValueChanged(float Value)` `:122`.
API: `GetValue` `:126`, `GetNormalizedValue` `:130`, `SetValue` `:134`, `GetMinValue` `:137`,
`SetMinValue` `:141`, `GetMaxValue` `:144`, `SetMaxValue` `:148`, `GetWidgetStyle` `:151`,
`SetWidgetStyle` `:154`, `GetOrientation` `:157`, `SetOrientation` `:160`, `HasIndentHandle` `:163`,
`SetIndentHandle` `:167`, `IsLocked` `:170`, `SetLocked` `:174`, `GetStepSize` `:177`, `SetStepSize` `:181`,
`GetSliderBarColor` `:184`, `SetSliderBarColor` `:188`, `GetSliderHandleColor` `:191`,
`SetSliderHandleColor` `:195`.

**`FSliderStyle`** — `SlateTypes.h:1339`: `NormalBarImage` `:1356`, `HoveredBarImage` `:1361`,
`DisabledBarImage` `:1366`, `NormalThumbImage` `:1371`, `HoveredThumbImage` `:1376`,
`DisabledThumbImage` `:1381`, `BarThickness` (`float`) `:1385`.

`RequiresControllerLock = true` by default is the gamepad affordance: on a pad you must "enter" the
slider before D-pad left/right changes its value, otherwise directional input navigates away.
`OnControllerCaptureBegin/End` exist purely for that mode.

### 1.7 `UProgressBar` → `SProgressBar`

`Components/ProgressBar.h:21`, parent `UWidget`.

| Property | Type | Default | Line |
|---|---|---|---|
| `WidgetStyle` (DisplayName "Style") | `FProgressBarStyle` | style cache; ctor forces `WidgetStyle.FillImage.TintColor = White` | `:29` / `ProgressBar.cpp:19,28` |
| `Percent` | `float` | `0`; FieldNotify; `UIMin 0 / UIMax 1` | `:34` / `.cpp:33` |
| `BarFillType` | `TEnumAsByte<EProgressBarFillType::Type>` | `LeftToRight` | `:39` / `.cpp:30` |
| `BarFillStyle` | `TEnumAsByte<EProgressBarFillStyle::Type>` | `Mask` | `:44` / `.cpp:31` |
| `bIsMarquee` | `bool` | `false`; FieldNotify | `:49` / `.cpp:32` |
| `BorderPadding` | `FVector2D` | `(0, 0)` | `:54` / `.cpp:35` |
| `PercentDelegate` | `FGetFloat` | | `:58` |
| `FillColorAndOpacity` | `FLinearColor` | `White`; FieldNotify | `:63` / `.cpp:34` |
| `FillColorAndOpacityDelegate` | `FGetLinearColor` | | `:67` |

No events. API: `GetWidgetStyle` `:72`, `SetWidgetStyle` `:75`, `GetPercent` `:78`, `SetPercent` `:82`,
`GetBarFillType` `:85`, `SetBarFillType` `:88`, `GetBarFillStyle` `:91`, `SetBarFillStyle` `:94`,
`UseMarquee` `:97`, `SetIsMarquee` `:101`, `GetBorderPadding` `:104`, `SetBorderPadding` `:107`,
`GetFillColorAndOpacity` `:110`, `SetFillColorAndOpacity` `:114`.

**`FProgressBarStyle`** — `SlateTypes.h:1164`: `BackgroundImage` `:1181`, `FillImage` `:1186`,
`MarqueeImage` `:1191`, `EnableFillAnimation` (`bool`) `:1197`.

`EProgressBarFillStyle` `Mask` vs `Scale` is the difference between clipping the fill brush and
stretching it — a genuine parity requirement for 9-slice fill bars.

### 1.8 `USpacer` → `SSpacer`

`Components/Spacer.h:19`. Invisible padding. One property: `Size` (`FVector2D`, default `(1.0f, 1.0f)`)
`:28` / `Spacer.cpp:17`. API `GetSize` `:33`, `SetSize` `:37`. Constructor sets
`bIsVariable = false` and visibility `SelfHitTestInvisible` (`Spacer.cpp:19-20`).

### 1.9 `UBorder` → `SBorder`

`Components/Border.h:29`, parent `UContentWidget`. Single child + background brush + padding.

| Property | Type | Default | Line |
|---|---|---|---|
| `HorizontalAlignment` | `TEnumAsByte<EHorizontalAlignment>` | `HAlign_Fill` | `:37` / `Border.cpp:33` |
| `VerticalAlignment` | `TEnumAsByte<EVerticalAlignment>` | `VAlign_Fill` | `:42` / `.cpp:34` |
| `bShowEffectWhenDisabled` | `uint8:1` | `true`; `AdvancedDisplay` | `:47` / `.cpp:38` |
| `ContentColorAndOpacity` | `FLinearColor` | `White`, sRGB | `:52` / `.cpp:28` |
| `ContentColorAndOpacityDelegate` | `FGetLinearColor` | | `:56` |
| `Padding` | `FMargin` | **`FMargin(4, 2)`** | `:61` / `.cpp:31` |
| `Background` (DisplayName "Brush") | `FSlateBrush` | empty; `BlueprintReadOnly` | `:65` |
| `BackgroundDelegate` | `FGetSlateBrush` | | `:69` |
| `BrushColor` | `FLinearColor` | `White`, sRGB | `:74` / `.cpp:29` |
| `BrushColorDelegate` | `FGetLinearColor` | | `:78` |
| `DesiredSizeScale` | `FVector2D` | `(1, 1)` | `:88` / `.cpp:36` |
| `bFlipForRightToLeftFlowDirection` | `bool` | `false` | `:92` |

Bindable pointer events (`EditAnywhere, meta=(IsBindableEvent="True")`, type `FOnPointerEvent`):
`OnMouseButtonDownEvent` `:97`, `OnMouseButtonUpEvent` `:100`, `OnMouseMoveEvent` `:103`,
`OnMouseDoubleClickEvent` `:106`.

API: `GetContentColorAndOpacity` `:110`, `SetContentColorAndOpacity` `:114`, `GetPadding` `:116`,
`SetPadding` `:119`, `GetHorizontalAlignment` `:121`, `SetHorizontalAlignment` `:124`,
`GetVerticalAlignment` `:126`, `SetVerticalAlignment` `:129`, `GetBrushColor` `:132`,
`SetBrushColor` `:136`, `SetBrush` `:140`, `SetBrushFromAsset` `:144`, `SetBrushFromTexture` `:148`,
`SetBrushFromMaterial` `:152`, `GetShowEffectWhenDisabled` `:155`, `SetShowEffectWhenDisabled` `:159`,
`GetDynamicMaterial` `:163`, `GetDesiredSizeScale` `:167`, `SetDesiredSizeScale` `:175`.

**No style struct** — the border is a bare brush. `DesiredSizeScale` (`:82-86`) is the "slide open
without hard-coding a size" trick.

### 1.10 `UThrobber` → `SThrobber` / `UCircularThrobber` → `SCircularThrobber`

`UThrobber` (`Components/Throbber.h:19`): `NumberOfPieces` (`int32`, default **3**, clamp 1..25) `:28` / `Throbber.cpp:21`;
`bAnimateHorizontally` (`bool`, `true`) `:33` / `.cpp:24`; `bAnimateVertically` (`bool`, `true`) `:38` / `.cpp:23`;
`bAnimateOpacity` (`bool`, `true`) `:43` / `.cpp:25`; `Image` (`FSlateBrush`, from style cache) `:48` / `.cpp:27`.
API `SetNumberOfPieces` `:54`, `GetNumberOfPieces` `:55`, `SetAnimateHorizontally` `:59`,
`IsAnimateHorizontally` `:60`, `SetAnimateVertically` `:64`, `IsAnimateVertically` `:65`,
`SetAnimateOpacity` `:69`, `IsAnimateOpacity` `:70`, `SetImage` `:72`, `GetImage` `:73`.
The three bools are combined into one `SThrobber::EAnimation` flag set (`:94`).

`UCircularThrobber` (`Components/CircularThrobber.h:22`): `NumberOfPieces` (`int32`, **6**, clamp 1..25) `:31` / `CircularThrobber.cpp:32`;
`Period` (`float` seconds, **0.75f**) `:36` / `.cpp:33`; `Radius` (`float`, **16.f**, editcondition `bEnableRadius`) `:41` / `.cpp:34`;
`Image` (`FSlateBrush`, style cache) `:46` / `.cpp:23`; private `bEnableRadius` (`bool`, `true`, `Transient`, `InlineEditConditionToggle`) `:84` / `.cpp:20`.
API `SetNumberOfPieces` `:52`, `GetNumberOfPieces` `:53`, `SetPeriod` `:57`, `GetPeriod` `:58`,
`SetRadius` `:62`, `GetRadius` `:63`, `SetImage` `:66`, `GetImage` `:67`.
Note `:39` — "If the throbber is a child of Canvas Panel, the 'Size to Content' option must be enabled
in order to set Radius."

There is **no** `USpinningImage` in UE 5.8 `Components/`; the spinning-image effect is
`UCircularThrobber`, or an `UImage` with a `RenderTransformAngle` animation.

---

## 2. Input widgets

### 2.1 `UEditableText` → `SEditableText`

`Components/EditableText.h:26`, parent `UWidget` (**not** `UTextLayoutWidget`). Single-line entry
with no chrome.

| Property | Type | Default | Line |
|---|---|---|---|
| `Text` | `FText` | empty; FieldNotify; **deprecated 5.1** | `:39` |
| `TextDelegate` | `FGetText` | | `:43` |
| `HintText` | `FText` | empty; MultiLine | `:50` |
| `HintTextDelegate` | `FGetText` | | `:54` |
| `WidgetStyle` (DisplayName "Style", `ShowOnlyInnerProperties`) | `FEditableTextStyle` | style cache; font forced to Roboto **24** `"Regular"` | `:58` / `EditableText.cpp:28,33-37` |
| `IsReadOnly` | `bool` | `false` | `:63` / `.cpp:47` |
| `IsPassword` | `bool` | `false` | `:68` / `.cpp:48` |
| `MinimumDesiredWidth` | `float` | `0.0f` | `:73` / `.cpp:49` |
| `IsCaretMovedWhenGainFocus` | `bool` | **`true`**; `AdvancedDisplay` | `:78` / `.cpp:50` |
| `SelectAllTextWhenFocused` | `bool` | `false`; `AdvancedDisplay` | `:83` / `.cpp:51` |
| `RevertTextOnEscape` | `bool` | `false`; `AdvancedDisplay` | `:88` / `.cpp:52` |
| `ClearKeyboardFocusOnCommit` | `bool` | **`true`**; `AdvancedDisplay` | `:93` / `.cpp:53` |
| `SelectAllTextOnCommit` | `bool` | `false`; `AdvancedDisplay` | `:98` / `.cpp:54` |
| `AllowContextMenu` | `bool` | `true`; `AdvancedDisplay` | `:102` / `.cpp:55` |
| `KeyboardType` | `TEnumAsByte<EVirtualKeyboardType::Type>` | `Default`; `AdvancedDisplay` | `:106` |
| `VirtualKeyboardOptions` | `FVirtualKeyboardOptions` | | `:110` |
| `VirtualKeyboardTrigger` | `EVirtualKeyboardTrigger` | `OnFocusByPointer` | `:113` / `.cpp:57` |
| `VirtualKeyboardDismissAction` | `EVirtualKeyboardDismissAction` | `TextChangeOnDismiss` | `:117` / `.cpp:58` |
| `Justification` | `TEnumAsByte<ETextJustify::Type>` | (enum 0 = `Left`) | `:122` |
| `OverflowPolicy` | `ETextOverflowPolicy` | `Clip`; Category "Clipping", `AdvancedDisplay` | `:127` / `.cpp:60` |
| `ShapedTextOptions` | `FShapedTextOptions` | | `:131` |
| `EnableIntegratedKeyboard` | `bool` | `false`; private, Category "Mobile", `AdvancedDisplay` | `:296` / `.cpp:56` |
| `FontFacesLoadingPaintPolicy` | `EFontFacesLoadingPaintPolicy` | `DoNotPaint` | `:301` / `.cpp:61` |

Constructor also sets `Clipping = ClipToBounds` (`EditableText.cpp:59`) and accessibility `Auto`.

Events: `OnTextChanged(const FText&)` `:137`, `OnTextCommitted(const FText&, ETextCommit::Type)` `:141`,
`OnAllFontFacesFinishLoading` `:305`.
API: `GetText` `:148`, `SetText` `:156`, `GetIsPassword` `:158`, `SetIsPassword` `:161`,
`GetHintText` `:165`, `SetHintText` `:168`, `GetMinimumDesiredWidth` `:171`, `SetMinimumDesiredWidth` `:179`,
`SetIsCaretMovedWhenGainFocus` `:182`, `GetIsCaretMovedWhenGainFocus` `:185`,
`SetSelectAllTextWhenFocused` `:188`, `GetSelectAllTextWhenFocused` `:191`, `SetRevertTextOnEscape` `:194`,
`GetRevertTextOnEscape` `:197`, `GetClearKeyboardFocusOnCommit` `:200`, `SetSelectAllTextOnCommit` `:203`,
`GetSelectAllTextOnCommit` `:206`, `SetWidgetStyle` `:209`, `GetIsReadOnly` `:211`, `SetIsReadOnly` `:214`,
`GetJustification` `:217`, `SetJustification` `:220`, `GetTextOverflowPolicy` `:223`,
`SetTextOverflowPolicy` `:226`, `SetClearKeyboardFocusOnCommit` `:229`, `SetKeyboardType` `:231`,
`GetFont` `:234`, `SetFont` `:237`, `SetFontMaterial` `:240`, `SetFontOutlineMaterial` `:243`,
`GetEnableIntegratedKeyboard` `:245`, `SetEnableIntegratedKeyboard` `:246`,
`ToggleVirtualKeyboard(bool bShow)` `:250`, `SetFontFacesLoadingPaintPolicy` `:252`.

**`FEditableTextStyle`** — `SlateTypes.h:867`: `Font` `:884`, `ColorAndOpacity` `:895`,
`BackgroundImageSelected` `:900`, `BackgroundImageComposing` `:905`, `CaretImage` `:910`.
(`BackgroundImageComposing` is the IME composition underline — needed for CJK input.)

### 2.2 `UEditableTextBox` → `SEditableTextBox`

`Components/EditableTextBox.h:27`, `UCLASS(meta=(DisplayName="Text Box"))`, parent `UWidget`.
`UEditableText` + a background/border + an error-reporting slot.

Properties are the same list as `UEditableText` with these differences:
`WidgetStyle` is `FEditableTextBoxStyle` `:52`; `MinimumDesiredWidth` has `Setter=SetMinDesiredWidth` `:76`;
there is **no** `EnableIntegratedKeyboard`; `VirtualKeyboardTrigger` `:117`;
plus editor-only `bIsFontDeprecationDone` `:311`.
Defaults from `EditableTextBox.cpp:28-39`: `IsReadOnly=false`, `IsPassword=false`,
`MinimumDesiredWidth=0.0f`, `IsCaretMovedWhenGainFocus=true`, `SelectAllTextWhenFocused=false`,
`RevertTextOnEscape=false`, `ClearKeyboardFocusOnCommit=true`, `SelectAllTextOnCommit=false`,
`AllowContextMenu=true`, `VirtualKeyboardDismissAction=TextChangeOnDismiss`, `OverflowPolicy=Clip`,
`FontFacesLoadingPaintPolicy=DoNotPaint`; font Roboto **24** `"Regular"` (`.cpp:46`).

Events: `OnTextChanged` `:142`, `OnTextCommitted` `:146`, and native-only
`OnCursorMovedWithSelectionEvent(const FTextLocation&, const FTextSelection&)` `:149`.
Error API: `SetError(FText)` `:225`, `ClearError()` `:240`, `HasError()` `:243`.
Also `IsAnyTextSelected()` `:222`, `SetForegroundColor(FLinearColor)` `:266`,
`SetWidgetStyle` `:261`, `GetWidgetStyle` `:263`.

**`FEditableTextBoxStyle`** — `SlateTypes.h:1018`: `BackgroundImageNormal` `:1036`,
`BackgroundImageHovered` `:1041`, `BackgroundImageFocused` `:1046`, `BackgroundImageReadOnly` `:1051`,
`Padding` `:1056`, `Font_DEPRECATED` `:1063`, **`TextStyle` (`FTextBlockStyle`)** `:1072`,
`ForegroundColor` `:1077`, `BackgroundColor` `:1082`, `ReadOnlyForegroundColor` `:1087`,
`FocusedForegroundColor` `:1092`, `HScrollBarPadding` `:1097`, `VScrollBarPadding` `:1102`,
`ScrollBarStyle` (`FScrollBarStyle`) `:1107`.

### 2.3 `UMultiLineEditableText` → `SMultiLineEditableText`

`Components/MultiLineEditableText.h:21`, `UCLASS(meta=(DisplayName="Editable Text (Multi-Line)"))`,
parent **`UTextLayoutWidget`** (so it also has Justification/WrapTextAt/Margin/LineHeightPercentage/…).

| Property | Type | Default | Line |
|---|---|---|---|
| `Text` | `FText` | MultiLine; FieldNotify | `:34` |
| `HintText` | `FText` | MultiLine | `:39` |
| `HintTextDelegate` | `FGetText` | | `:43` |
| `WidgetStyle` (`ShowOnlyInnerProperties`) | **`FTextBlockStyle`** | style cache; font Roboto **12** `"Bold"` | `:48` / `MultiLineEditableText.cpp:23,45` |
| `bIsReadOnly` | `bool` | `false` | `:53` / `.cpp:32` |
| `SelectAllTextWhenFocused` | `bool` | `false`; `AdvancedDisplay` | `:58` / `.cpp:33` |
| `ClearTextSelectionOnFocusLoss` | `bool` | **`true`**; `AdvancedDisplay` | `:63` / `.cpp:34` |
| `RevertTextOnEscape` | `bool` | `false`; `AdvancedDisplay` | `:68` / `.cpp:35` |
| `ClearKeyboardFocusOnCommit` | `bool` | **`true`**; `AdvancedDisplay` | `:73` / `.cpp:36` |
| `AllowContextMenu` | `bool` | `true`; `AdvancedDisplay` | `:77` / `.cpp:37` |
| `VirtualKeyboardOptions` | `FVirtualKeyboardOptions` | | `:81` |
| `VirtualKeyboardDismissAction` | `EVirtualKeyboardDismissAction` | `TextChangeOnDismiss` | `:85` / `.cpp:39` |

Constructor sets `Clipping = ClipToBounds` and **`AutoWrapText = true`** (`.cpp:38,40`) — note the
multi-line variants default to wrapping on, unlike `UTextBlock`.
Events: `OnTextChanged` `:89`, `OnTextCommitted` `:93`.
API: `GetText` `:102`, `SetText` `:109`, `GetHintText` `:113`, `SetHintText` `:120`,
`SetSelectAllTextWhenFocused` `:123`, `GetSelectAllTextWhenFocused` `:126`,
`SetClearTextSelectionOnFocusLoss` `:129`, `GetClearTextSelectionOnFocusLoss` `:132`,
`SetRevertTextOnEscape` `:135`, `GetRevertTextOnEscape` `:138`, `SetClearKeyboardFocusOnCommit` `:141`,
`GetClearKeyboardFocusOnCommit` `:144`, `GetIsReadOnly` `:147`, `SetIsReadOnly` `:151`,
`SetWidgetStyle` `:154`, `GetFont` `:157`, `SetFont` `:160`, `SetFontMaterial` `:163`,
`SetFontOutlineMaterial` `:166`.

**`FTextBlockStyle`** — `SlateTypes.h:325`: `Font` `:342`, `ColorAndOpacity` (DisplayName "Color") `:371`,
`ShadowOffset` `:376`, `ShadowColorAndOpacity` `:381`, `SelectedBackgroundColor` `:386`,
`HighlightColor` `:391`, `HighlightShape` `:396`, `StrikeBrush` `:401`, `UnderlineBrush` `:406`.

### 2.4 `UMultiLineEditableTextBox` → `SMultiLineEditableTextBox`

`Components/MultiLineEditableTextBox.h:21`, `UCLASS(meta=(DisplayName="Text Box (Multi-Line)"))`,
parent `UTextLayoutWidget`.

`Text` `:35`, `HintText` `:40`, `HintTextDelegate` `:44`,
`WidgetStyle` (**`FEditableTextBoxStyle`**) `:49`,
`TextStyle_DEPRECATED` (`FTextBlockStyle`, **deprecated 5.1** — "mainly duplicated information already
available inside WidgetStyle") `:55`, `bIsReadOnly` (`false`) `:61` / `.cpp:48`,
`AllowContextMenu` (`true`) `:65` / `.cpp:49`, `VirtualKeyboardOptions` `:69`,
`VirtualKeyboardDismissAction` (`TextChangeOnDismiss`) `:73` / `.cpp:50`.
`AutoWrapText = true` in the constructor (`.cpp:51`); font Roboto **24** `"Regular"` (`.cpp:32`).

Events `OnTextChanged` `:77`, `OnTextCommitted` `:81`.
API: `GetText` `:93`, `SetText` `:101`, `GetHintText` `:105`, `SetHintText` `:112`,
`SetError(FText)` `:115`, `GetIsReadOnly` `:118`, `SetIsReadOnly` `:122`,
`SetTextStyle(const FTextBlockStyle&)` `:125`, `SetForegroundColor(FLinearColor)` `:128`.
`:130-132` carries three open TODOs (no setter for `ReadOnlyForegroundColor`, `BackgroundColor`, `Font`).

### 2.5 `USpinBox` → `SSpinBox<float>`

`Components/SpinBox.h:19`, parent `UWidget`. Numeric entry that doubles as a drag-slider.

| Property | Type | Default | Line |
|---|---|---|---|
| `Value` | `float` | `0`; FieldNotify | `:33` / `SpinBox.cpp:26` |
| `ValueDelegate` | `FGetFloat` | | `:37` |
| `WidgetStyle` (DisplayName "Style") | `FSpinBoxStyle` | style cache | `:43` / `.cpp:43,48` |
| `MinFractionalDigits` | `int32` | **`1`**; ClampMin 0 | `:48` / `.cpp:31` |
| `MaxFractionalDigits` | `int32` | **`6`**; ClampMin 0 | `:53` / `.cpp:32` |
| `bAlwaysUsesDeltaSnap` | `bool` | `false` | `:58` / `.cpp:33` |
| `bEnableSlider` | `bool` | `true` | `:63` / `.cpp:34` |
| `Delta` | `float` | `0` | `:68` / `.cpp:35` |
| `SliderExponent` | `float` | `1` (constant delta) | `:73` / `.cpp:36` |
| `Font` | `FSlateFontInfo` | Roboto **12** `"Bold"` | `:78` / `.cpp:23` |
| `Justification` | `TEnumAsByte<ETextJustify::Type>` | (enum 0) | `:83` |
| `MinDesiredWidth` | `float` | `0`; `AdvancedDisplay` | `:88` / `.cpp:37` |
| `KeyboardType` | `TEnumAsByte<EVirtualKeyboardType::Type>` | **`Number`** | `:92` / `.cpp:40` |
| `VirtualKeyboardDismissAction` | `EVirtualKeyboardDismissAction` | `TextChangeOnDismiss` | `:96` / `.cpp:41` |
| `ClearKeyboardFocusOnCommit` | `bool` | **`false`** | `:101` / `.cpp:38` |
| `SelectAllTextOnCommit` | `bool` | **`true`** | `:106` / `.cpp:39` |
| `ForegroundColor` | `FSlateColor` | `WidgetStyle.ForegroundColor` | `:110` / `.cpp:52` |
| `bOverride_MinValue` / `MaxValue` / `MinSliderValue` / `MaxSliderValue` | `uint32:1` | `InlineEditConditionToggle` | `:304, :308, :312, :316` |
| `MinValue` | `float` | `0` | `:321` / `.cpp:27` |
| `MaxValue` | `float` | `0` | `:326` / `.cpp:28` |
| `MinSliderValue` | `float` | `0` | `:331` / `.cpp:29` |
| `MaxSliderValue` | `float` | `0` | `:336` / `.cpp:30` |

**Four independent bounds** (typed min/max vs slider min/max), each individually overridable — that
is the design worth copying.

Events: `OnValueChanged(float)` `:115`, `OnValueCommitted(float, ETextCommit::Type)` `:119`,
`OnBeginSliderMovement` `:123`, `OnEndSliderMovement(float)` `:127`.
API: `GetValue` `:133`, `SetValue` `:137`, `GetWidgetStyle` `:142`, `SetWidgetStyle` `:145`,
`GetMinFractionalDigits` `:149`, `SetMinFractionalDigits` `:153`, `GetMaxFractionalDigits` `:157`,
`SetMaxFractionalDigits` `:161`, `GetAlwaysUsesDeltaSnap` `:165`, `SetAlwaysUsesDeltaSnap` `:169`,
`GetEnableSlider` `:172`, `SetEnableSlider` `:175`, `GetDelta` `:179`, `SetDelta` `:183`,
`GetSliderExponent` `:186`, `SetSliderExponent` `:189`, `GetFont` `:192`, `SetFont` `:195`,
`GetJustification` `:198`, `SetJustification` `:201`, `GetMinDesiredWidth` `:204`, `SetMinDesiredWidth` `:207`,
`GetClearKeyboardFocusOnCommit` `:210`, `SetClearKeyboardFocusOnCommit` `:213`,
`GetSelectAllTextOnCommit` `:216`, `SetSelectAllTextOnCommit` `:219`,
`GetMinValue` `:223` / `SetMinValue` `:227` / `ClearMinValue` `:231`,
`GetMaxValue` `:235` / `SetMaxValue` `:239` / `ClearMaxValue` `:243`,
`GetMinSliderValue` `:247` / `SetMinSliderValue` `:251` / `ClearMinSliderValue` `:255`,
`GetMaxSliderValue` `:259` / `SetMaxSliderValue` `:263` / `ClearMaxSliderValue` `:267`,
`SetForegroundColor` `:271`, `GetForegroundColor` `:274`.

**`FSpinBoxStyle`** — `SlateTypes.h:1488`: `BackgroundBrush` `:1505`, `ActiveBackgroundBrush` `:1509`,
`HoveredBackgroundBrush` `:1514`, `ActiveFillBrush` `:1519`, `HoveredFillBrush` `:1524`,
`InactiveFillBrush` `:1529`, `ArrowsImage` `:1534`, `ForegroundColor` `:1539`, `TextPadding` `:1544`,
`InsetPadding` `:1549`.

### 2.6 `UComboBoxString` → `SComboBox<TSharedPtr<FString>>`

`Components/ComboBoxString.h:21`, `UCLASS(meta=(DisplayName="ComboBox (String)"))`.

| Property | Type | Default | Line |
|---|---|---|---|
| `DefaultOptions` | `TArray<FString>` | empty; private | `:32` |
| `SelectedOption` | `FString` | empty; FieldNotify; private | `:36` |
| `WidgetStyle` (DisplayName "Style") | `FComboBoxStyle` | style cache | `:43` / `ComboBoxString.cpp:23,30` |
| `ItemStyle` | `FTableRowStyle` | style cache | `:48` / `.cpp:24,31` |
| `ScrollBarStyle` | `FScrollBarStyle` | style cache; **read-only, construction-only** | `:53` / `.cpp:25,32` |
| `ContentPadding` | `FMargin` | **`FMargin(4.0, 2.0)`** | `:57` / `.cpp:39` |
| `MaxListHeight` | `float` | **`450.0f`**; `AdvancedDisplay` | `:62` / `.cpp:40` |
| `HasDownArrow` | `bool` | `true`; `AdvancedDisplay` | `:70` / `.cpp:41` |
| `EnableGamepadNavigationMode` | `bool` | **`true`**; `AdvancedDisplay` | `:78` / `.cpp:42` |
| `Font` | `FSlateFontInfo` | Roboto **16** `"Bold"`; read-only, construction-only | `:86` / `.cpp:47` |
| `ForegroundColor` | `FSlateColor` | `ItemStyle.TextColor`; read-only, `DesignerRebuild` | `:91` / `.cpp:36` |
| `bIsFocusable` | `bool` | `true`; read-only, construction-only | `:95` / `.cpp:37` |

`EnableGamepadNavigationMode` (`:73-77`): "When false, directional keys will change the selection.
When true, ComboBox must be activated and will only capture arrow input while activated." Same
activation model as `USlider::RequiresControllerLock`.

Events: `OnGenerateWidgetEvent` (`FGenerateWidgetForString`, `IsBindableEvent`) `:101`,
`OnSelectionChanged(FString SelectedItem, ESelectInfo::Type)` `:105`, `OnOpening()` `:109`.
API: `AddOption` `:114`, `RemoveOption` `:117`, `FindOptionIndex` `:120`, `GetOptionAtIndex` `:123`,
`ClearOptions` `:126`, `ClearSelection` `:129`, `RefreshOptions` `:136`, `SetSelectedOption` `:139`,
`SetSelectedIndex` `:142`, `GetSelectedOption` `:145`, `GetSelectedIndex` `:148`, `GetOptionCount` `:152`,
`IsOpen` `:155`, `SetContentPadding` `:162`, `GetContentPadding` `:165`,
`IsEnableGamepadNavigationMode` `:168`, `SetEnableGamepadNavigationMode` `:171`, `IsHasDownArrow` `:174`,
`SetHasDownArrow` `:177`, `GetMaxListHeight` `:180`, `SetMaxListHeight` `:183`, `GetFont` `:186`,
`GetWidgetStyle` `:189`, `SetWidgetStyle` `:192`, `GetItemStyle` `:195`, `SetItemStyle` `:198`,
`GetScrollBarStyle` `:201`, `IsFocusable` `:204`, `GetForegroundColor` `:207`.

**`FComboBoxStyle`** — `SlateTypes.h:741`: `ComboButtonStyle` (`FComboButtonStyle`) `:760`,
`PressedSlateSound` `:767`, `SelectionChangeSlateSound` `:774`, `ContentPadding` `:781`,
`MenuRowPadding` `:788`, + deprecated `PressedSound_DEPRECATED` `:795` / `SelectionChangeSound_DEPRECATED` `:797`.
**`FComboButtonStyle`** — `SlateTypes.h:644`: `ButtonStyle` (`FButtonStyle`) `:663`,
`DownArrowImage` `:670`, `ShadowOffset` `:678`, `ShadowColorAndOpacity` `:686`, `MenuBorderBrush` `:693`,
`MenuBorderPadding` `:700`, `ContentPadding` `:707`, `DownArrowPadding` `:714`,
`DownArrowAlign` (`TEnumAsByte<EVerticalAlignment>`) `:721`.
**`FTableRowStyle`** — `SlateTypes.h:1640`: `SelectorFocusedBrush` `:1657`, `ActiveHoveredBrush` `:1662`,
`ActiveBrush` `:1667`, `InactiveHoveredBrush` `:1672`, `InactiveBrush` `:1677`,
`bUseParentRowBrush` `:1682`, `ParentRowBackgroundBrush` `:1687`, `ParentRowBackgroundHoveredBrush` `:1692`,
`EvenRowBackgroundHoveredBrush` `:1697`, `EvenRowBackgroundBrush` `:1702`,
`OddRowBackgroundHoveredBrush` `:1707`, `OddRowBackgroundBrush` `:1712`, `TextColor` `:1717`,
`SelectedTextColor` `:1722`, `DropIndicator_Above` `:1727`, `DropIndicator_Onto` `:1732`,
`DropIndicator_Below` `:1737`, `ActiveHighlightedBrush` `:1742`, `InactiveHighlightedBrush` `:1747`.
(Note the **active/inactive** distinction — "active" means the owning list has focus. That is a
19-brush row style; the three `DropIndicator_*` brushes are the drag-drop insertion affordances.)

### 2.7 `UComboBoxKey` → `SComboBox<FName>`

`Components/ComboBoxKey.h:18`, `UCLASS(meta=(DisplayName="ComboBox (Key)"))`. Same shape as
`UComboBoxString` but keyed by `FName` and with **two** generator delegates (content vs item).

`Options` (`TArray<FName>`, private) `:32`; `SelectedOption` (`FName`, FieldNotify, private) `:36`;
`WidgetStyle` (`FComboBoxStyle`) `:42`; `ItemStyle` (`FTableRowStyle`) `:47`;
`ScrollBarStyle` (read-only) `:52`; `ForegroundColor` (read-only, `DesignerRebuild`) `:57`;
`ContentPadding` (`FMargin(4.0, 2.0)`) `:61` / `ComboBoxKey.cpp:40`;
`MaxListHeight` (`450.0f`) `:66` / `.cpp:43`; `bHasDownArrow` (`true`) `:74` / `.cpp:44`;
`bEnableGamepadNavigationMode` (`true`) `:82` / `.cpp:45`; `bIsFocusable` (`true`, read-only) `:87` / `.cpp:46`.

Events: `OnGenerateContentWidget` (`FGenerateWidgetEvent`, `IsBindableEvent`) `:93`,
`OnGenerateItemWidget` `:97`, `OnSelectionChanged(FName, ESelectInfo::Type)` `:101`, `OnOpening` `:105`.
API: `AddOption` `:113`, `RemoveOption` `:117`, `ClearOptions` `:121`, `ClearSelection` `:125`,
`SetSelectedOption` `:129`, `GetSelectedOption` `:133`, `IsOpen` `:137`, plus the same
padding/arrow/height/style/focus getters `:144-186`.

### 2.8 `UComboBox` → `SComboBox<UObject*>` — **Experimental**

`Components/ComboBox.h:16`, `UCLASS(Experimental, meta=(DisplayName="ComboBox (Object)"))`.
Only four properties: `ScrollBarStyle` (`FScrollBarStyle`) `:23`, `Items` (`TArray<UObject*>`) `:27`,
`OnGenerateWidgetEvent` (`FGenerateWidgetForObject`, `IsBindableEvent`) `:31`, `bIsFocusable` `:40`.
No selection API, no events beyond the generator. **Do not use as a parity reference** — it is
unfinished; `UComboBoxKey` is the current pattern for non-string combo boxes.

### 2.9 `UInputKeySelector` → `SInputKeySelector`

`Components/InputKeySelector.h:21`. Key-rebinding widget.

| Property | Type | Default | Line |
|---|---|---|---|
| `WidgetStyle` (DisplayName "Style") | `FButtonStyle` | style cache | `:33` / `InputKeySelector.cpp:21,27` |
| `TextStyle` (DisplayName "Text Style") | `FTextBlockStyle` | style cache; font Roboto **24** `"Bold"` | `:38` / `.cpp:22,44` |
| `SelectedKey` | `FInputChord` | `FInputChord(EKeys::Invalid)`; FieldNotify; **no `EditAnywhere`** | `:43` / `.cpp:34` |
| `Margin` | `FMargin` | default-constructed | `:48` |
| `KeySelectionText` | `FText` | `"..."` | `:53` / `.cpp:32` |
| `NoKeySpecifiedText` | `FText` | `"Empty"` | `:58` / `.cpp:33` |
| `bAllowModifierKeys` | `bool` | **`true`** | `:66` / `.cpp:35` |
| `bAllowGamepadKeys` | `bool` | **`false`** | `:71` / `.cpp:36` |
| `EscapeKeys` | `TArray<FKey>` | contains `EKeys::Gamepad_Special_Right` | `:75` / `.cpp:38` |

Events: `OnKeySelected(FInputChord SelectedKey)` `:80`, `OnIsSelectingKeyChanged()` `:84`.
API: `SetSelectedKey` `:88`, `GetSelectedKey` `:91`, `SetKeySelectionText` `:95`,
`GetKeySelectionText` `:98`, `SetNoKeySpecifiedText` `:102`, `GetNoKeySpecifiedText` `:105`,
`SetAllowModifierKeys` `:109`, `AllowModifierKeys` `:112`, `SetAllowGamepadKeys` `:116`,
`AllowGamepadKeys` `:119`, `GetIsSelectingKey` `:123`, `SetTextBlockVisibility(ESlateVisibility)` `:127`,
`SetButtonStyle(const FButtonStyle&)` `:130` (pointer overload **deprecated 5.2** `:134`),
`GetButtonStyle` `:137`, `SetTextStyle` `:140`, `GetTextStyle` `:143`, `SetMargin` `:146`,
`GetMargin` `:149`, `SetEscapeKeys(const TArray<FKey>&)` `:153`.

### 2.10 `UScrollBar` → `SScrollBar` — **Experimental**

`Components/ScrollBar.h:15`, `UCLASS(Experimental)`. A standalone scrollbar you drive yourself.

`WidgetStyle` (`FScrollBarStyle`, style cache) `:24` / `ScrollBar.cpp:27,32`;
`bAlwaysShowScrollbar` (`bool`, **`true`**) `:29` / `.cpp:21`;
`bAlwaysShowScrollbarTrack` (`bool`, **`true`**) `:34` / `.cpp:22`;
`Orientation` (`Orient_Vertical`, read-only/construction-only) `:39` / `.cpp:23`;
`Thickness` (`FVector2D`, **`(16.0f, 16.0f)`**) `:44` / `.cpp:24`;
`Padding` (`FMargin`, **`FMargin(2.0f)`**) `:49` / `.cpp:25`.
Single API entry point: `SetState(float InOffsetFraction, float InThumbSizeFraction)` `:62` —
"the maximum offset is 1.0-ThumbSizeFraction" (`:55-57`).
Getters/setters `:85-110`; `InitOrientation` `:122`.

**`FScrollBarStyle`** — `SlateTypes.h:931`: `HorizontalBackgroundImage` `:948`,
`VerticalBackgroundImage` `:953`, `VerticalTopSlotImage` `:958`, `HorizontalTopSlotImage` `:963`,
`VerticalBottomSlotImage` `:968`, `HorizontalBottomSlotImage` `:973`, `NormalThumbImage` `:978`,
`HoveredThumbImage` `:983`, `DraggedThumbImage` `:988`, `Thickness` (`float`) `:992`.

---

## 3. Panels (covered by a sibling report — named only)

All in `Engine/Source/Runtime/UMG/Public/Components/`. Each has a matching `U...Slot` that carries
per-child layout data.

| UMG class | Slate widget | Slot class |
|---|---|---|
| `UCanvasPanel` | `SConstraintCanvas` | `UCanvasPanelSlot` (anchors, offsets, alignment, ZOrder, SizeToContent, AutoSize) |
| `UGridPanel` | `SGridPanel` | `UGridSlot` (Row/Column/RowSpan/ColumnSpan/Layer/Nudge) |
| `UUniformGridPanel` | `SUniformGridPanel` | `UUniformGridSlot` |
| `UHorizontalBox` | `SHorizontalBox` | `UHorizontalBoxSlot` (Size `FSlateChildSize`, Padding, HAlign/VAlign) |
| `UVerticalBox` | `SVerticalBox` | `UVerticalBoxSlot` |
| `UStackBox` | `SStackBox` | `UStackBoxSlot`; has `Orientation` (`StackBox.h:31`) — the runtime-switchable H/V box |
| `UOverlay` | `SOverlay` | `UOverlaySlot` |
| `UWrapBox` | `SWrapBox` | `UWrapBoxSlot`; `InnerSlotPadding` `WrapBox.h:30`, `WrapSize` `:35`, `bExplicitWrapSize` `:40`, `HorizontalAlignment` `:45`, `Orientation` (`Orient_Horizontal`) `:50` |
| `UScrollBox` | `SScrollBox` | `UScrollBoxSlot` — see §3.1, it is far more than a panel |
| `USizeBox` | `SBox` | `USizeBoxSlot` |
| `UScaleBox` | `SScaleBox` | `UScaleBoxSlot` |
| `USafeZone` | `SSafeZone` | `USafeZoneSlot`; `PadLeft` `SafeZone.h:54`, `PadRight` `:59`, `PadTop` `:64`, `PadBottom` `:69` |
| `UWidgetSwitcher` | `SWidgetSwitcher` | `UWidgetSwitcherSlot` — see §6.1 |
| `UBorder` | `SBorder` | `UBorderSlot` (content widget, §1.9) |
| `UButton` | `SButton` | `UButtonSlot` (content widget, §1.4) |
| `UBackgroundBlur` | `SBackgroundBlur` | `UBackgroundBlurSlot` (§5.3) |
| `UWindowTitleBarArea` | `SWindowTitleBarArea` | `UWindowTitleBarAreaSlot` (§6.5) |
| `UMenuAnchor` | `SMenuAnchor` | (content widget, §6.2) |
| `URetainerBox` | `SRetainerWidget` | (content widget, §5.2) |
| `UInvalidationBox` | `SInvalidationPanel` | (content widget, §6.4) |
| `UNamedSlot` | `SBox` | (content widget, §6.3) |

`UPanelWidget` (`Components/PanelWidget.h:14`) is the shared base: `Slots` array `:22`,
`GetChildrenCount` `:28`, `GetChildAt` `:36`, `GetAllChildren` `:40`, `GetChildIndex` `:44`,
`HasChild` `:48`, `RemoveChildAt` `:52`, `AddChild` `:59` (+ template-slot overload `:66`),
`InsertChildAt` `:72` (+ overload `:79`), `ShiftChild` `:84`, `RemoveChild` `:114`,
`HasAnyChildren` `:118`, `ClearChildren` `:122`, `GetSlots` `:125`, `CanHaveMultipleChildren` `:128`,
`CanAddMoreChildren` `:134`, `GetSlotClass` `:167`, `bCanHaveMultipleChildren` `:190`;
editor-only `ReplaceChildAt` `:95`, `ReplaceChild` `:103`, `LockToPanelOnDrag` `:142`.
`UContentWidget` (`Components/ContentWidget.h:12`) narrows it to one child:
`GetContentSlot` `:19`, `SetContent` `:23`, `GetContent` `:27`.

### 3.1 `UScrollBox` → `SScrollBox` (listed here but genuinely a control)

`Components/ScrollBox.h:25`. Non-virtualised scrolling container ("Great for presenting 10-100
widgets… Doesn't support virtualization" `:22`).

| Property | Type | Default | Line |
|---|---|---|---|
| `ScrollAnimationInterpolationSpeed` | `float` | `15.f` | `:32` |
| `bEnableTouchScrolling` | `bool` | `true` | `:36` |
| `bConsumePointerInput` | `bool` | `true` | `:40` |
| `AnalogMouseWheelKey` | `FKey` | none — **must be set for gamepad scrolling** | `:44` |
| `bIsFocusable` | `bool` | (ctor-unset → false) | `:48` |
| `WidgetStyle` | `FScrollBoxStyle` | style cache | `:55` / `ScrollBox.cpp:39,47` |
| `WidgetBarStyle` | `FScrollBarStyle` | style cache | `:60` / `.cpp:40,48` |
| `Orientation` | `TEnumAsByte<EOrientation>` | `Orient_Vertical` | `:65` / `.cpp:19` |
| `ScrollBarVisibility` | `ESlateVisibility` | `Visible` | `:70` / `.cpp:20` |
| `ConsumeMouseWheel` | `EConsumeMouseWheel` | `WhenScrollingPossible` | `:75` / `.cpp:21` |
| `ScrollbarThickness` | `FVector2D` | `(9.0f, 9.0f)` | `:80` / `.cpp:22` |
| `ScrollbarPadding` | `FMargin` | `FMargin(2.0f)` | `:85` / `.cpp:23` |
| `AlwaysShowScrollbar` | `bool` | `false` | `:90` / `.cpp:24` |
| `AlwaysShowScrollbarTrack` | `bool` | `false` | `:95` / `.cpp:25` |
| `AllowOverscroll` | `bool` | `true` | `:100` / `.cpp:26` |
| `BackPadScrolling` | `bool` | `false`; read-only, construction-only | `:105` / `.cpp:27` |
| `FrontPadScrolling` | `bool` | `false`; read-only, construction-only | `:110` / `.cpp:28` |
| `bAnimateWheelScrolling` | `bool` | `false` | `:115` |
| `NavigationDestination` | `EDescendantScrollDestination` | `IntoView` | `:120` / `.cpp:29` |
| `NavigationScrollPadding` | `float` | `0.0f`; read-only | `:128` / `.cpp:30` |
| `ScrollWhenFocusChanges` | `EScrollWhenFocusChanges` | **`NoScroll`** | `:133` / `.cpp:31` |
| `bAllowRightClickDragScrolling` | `bool` | `true` | `:138` / `.cpp:53` |
| `WheelScrollMultiplier` | `float` | `1.f` | `:143` |

Constructor also sets `Clipping = ClipToBounds` and visibility `Visible` (`.cpp:35-36`).
Events: `OnUserScrolled(float CurrentOffset)` `:258`,
`OnScrollBarVisibilityChanged(ESlateVisibility)` `:262`, `OnFocusReceived` `:266`, `OnFocusLost` `:270`,
`OnFocusUpdated(const UWidget* ScrollBox, bool bFocused)` `:274`.
API: `SetScrollOffset` `:281`, `GetScrollOffset` `:287`, `GetOverscrollOffset` `:293`,
`GetOverscrollPercentage` `:298`, `GetScrollOffsetOfEnd` `:302`, `GetViewFraction` `:306`,
`GetViewOffsetFraction` `:309`, `ScrollToStart` `:313`, `ScrollToEnd` `:317`,
`ScrollWidgetIntoView(UWidget*, bool AnimateScroll = true, EDescendantScrollDestination = IntoView, float Padding = 0)` `:321`,
`GetIsScrolling` `:325`, `EndInertialScrolling` `:240`, plus the full getter/setter set `:145-252`.

**`FScrollBoxStyle`** — `SlateTypes.h:2022`: `BarThickness` `:2038`, `TopShadowBrush` `:2043`,
`BottomShadowBrush` `:2048`, `LeftShadowBrush` `:2053`, `RightShadowBrush` `:2062`,
`HorizontalScrolledContentPadding` (`FMargin(0,0,1,0)`) `:2071`,
`VerticalScrolledContentPadding` (`FMargin(0,0,0,1)`) `:2080`.

`ScrollWhenFocusChanges` + `NavigationDestination` + `NavigationScrollPadding` together are the
gamepad story: when focus moves to an offscreen child, scroll it into view with N pixels of the next
item peeking. A parity implementation that only scrolls on wheel/drag will feel broken on a pad.

---

## 4. Lists and data

The list family is UMG's virtualised-collection story. Three concepts: **items** (your data),
**entries** (pooled widgets, only as many as fit on screen), and an **interface** the entry widget
must implement.

### 4.1 `UListViewBase` (abstract)

`Components/ListViewBase.h:587`,
`UCLASS(Abstract, NotBlueprintable, hidedropdown, meta = (EntryInterface = UserListEntry))`.
Constructor sets `bIsVariable = true` and `Clipping = ClipToBounds` (`ListViewBase.cpp:19-20`).

| Property | Type | Default | Line |
|---|---|---|---|
| `EntryWidgetClass` | `TSubclassOf<UUserWidget>` | null; `DesignerRebuild`, `MustImplement = "/Script/UMG.UserListEntry"` | `:816` |
| `WheelScrollMultiplier` | `float` | `1.f` | `:820` |
| `bEnableScrollAnimation` | `bool` | `false` | `:824` |
| `ScrollingAnimationInterpolationSpeed` | `float` | `12.f` | `:828` |
| `bInEnableTouchAnimatedScrolling` | `bool` | `false`; DisplayName "Enable Touch Animated Scrolling" | `:832` |
| `AllowOverscroll` | `bool` | `true` | `:836` |
| `bEnableRightClickScrolling` | `bool` | `true` | `:840` |
| `bEnableTouchScrolling` | `bool` | `true` | `:844` |
| `bIsPointerScrollingEnabled` | `bool` | `true`; `EditDefaultsOnly` | `:848` |
| `bIsGamepadScrollingEnabled` | `bool` | `true`; `EditDefaultsOnly` | `:852` |
| `bEnableFixedLineOffset` | `bool` | `false` | `:855` |
| `FixedLineScrollOffset` | `float` | `0.f`; ClampMin 0.0 / ClampMax 0.5 | `:863` |
| `bAllowDragging` | `bool` | `true` | `:867` |
| `bAllowDragDrop` | `bool` | `false` | `:871` |
| `DragDropVisualPivot` | `EDragPivot` | `CenterCenter` | `:878` |
| `DragDropVisualOffset` | `FVector2D` | `(0,0)` | `:882` |
| `DragDropVisualEntryClass` | `TSubclassOf<UUserWidget>` | null; falls back to `EntryWidgetClass` | `:886` |
| `DragDropOperationClass` | `TSubclassOf<UDragDropOperation>` | null — **if unset, no drag-drop ops are created** | `:890` |
| `DragVisualWidget` | `UWidget*` | null; `BlueprintReadWrite, Transient` | `:894` |
| `bIsDragging` | `bool` | `false`; `BlueprintReadOnly, Transient` | `:898` |
| `bSelectItemOnNavigation` | `bool` | `true` | `:902` |
| `bAllowKeepPreselectedItems` | `bool` | `true` (Multi-select mouse behaviour) | `:906` |
| `NumDesignerPreviewEntries` | `int32` | `5`; editor-only, Clamp 0..20 | `:922` |

Events: `BP_OnEntryGenerated` (`FOnListEntryGeneratedDynamic(UUserWidget*)`) `:714`,
`BP_OnEntriesGenerated` (`FOnListEntriesGeneratedDynamic(int32 NumEntries)`) `:719`,
`BP_OnEntryReleased` (`FOnListEntryReleasedDynamic(UUserWidget*)`) `:910`.
Native events: `OnEntryWidgetGenerated()` `:687`, `OnEntryWidgetReleased()` `:690`.
Native virtual hooks: `NativeOnEntryGenerated` `:715`, `NativeOnEntriesGenerated` `:720`,
`NativeOnEntryReleased` `:911`.

API: `GetEntryWidgetClass` `:599`, `GetDisplayedEntryWidgets` `:603`, `GetScrollOffset` `:607`,
`GetListObjectFromEntry` `:610`, `GetIsDraggingListItem` `:614`, `RegenerateAllEntries` `:621`,
`ScrollToTop` `:625`, `ScrollToBottom` `:629`, `SetScrollOffset` `:633`, `EndInertialScrolling` `:637`,
`SetWheelScrollMultiplier` `:640`, `SetScrollbarVisibility` `:643`, `GetScrollbarVisibility` `:646`,
`SetAllowOverScroll` `:649`, `GetOverscroll` `:652`, `SetIsPointerScrollingEnabled` `:656`,
`SetIsTouchScrollingEnabled` `:660`, `SetIsGamepadScrollingEnabled` `:664`,
`CancelListViewDragDrop` `:668`, `CreateDragDropOperation(UObject* Item)` `:672`, `RequestRefresh` `:684`.

`RequestRefresh` vs `RegenerateAllEntries` is documented at `:617-618` and `:676-681` and matters:
*refresh* only reconciles items↔entries (releasing entries for gone items, generating for new ones);
*regenerate* releases and re-generates every entry — entry widgets do **not** receive
`Destruct`/`Construct`, only `OnEntryReleased` and `OnListItemObjectSet`.

Entry pooling: `FUserWidgetPool EntryWidgetPool` `:926`, used by
`GenerateTypedEntry<WidgetEntryT, ObjectTableRowT>` `:729-759`, which wraps the pooled `UUserWidget`
in an `SObjectTableRow` carrying `bAllowDragging`, `bAllowDragDrop`, `bAllowKeepPreselectedItems`
and eight drag/hover callbacks (`:739-749`).
Drag-drop hooks a subclass overrides: `HandleListEntryHovered` `:701`, `HandleListEntryUnhovered` `:702`,
`HandleListEntryDragDetected` `:703`, `HandleListEntryCanAcceptDrop` `:704`,
`HandleListEntryAcceptDrop` `:705`, `HandleListEntryDragEnter` `:706`, `HandleListEntryDragLeave` `:707`,
`HandleListEntryDragCancelled` `:708`.

### 4.2 `ITypedUMGListView<ItemType>` — the typed mirror of `SListView`

`Components/ListViewBase.h:39`. Not a `UCLASS` (template), so not Blueprint-visible; child classes
propagate what they need. `IMPLEMENT_TYPED_UMG_LIST(ItemType, ListPropertyName)` (`:939-990`) generates
the boilerplate.

Native multicast events it exposes (`:47-97`):
`OnItemClicked()`, `OnItemDoubleClicked()`, `OnItemDragDetected()`, `OnItemAcceptDrop()`,
`OnItemDragEnter()`, `OnItemDragLeave()` (all `FSimpleListItemEvent(ItemType)`);
`OnItemDragCancelled()` (`const FDragDropEvent&`);
`OnItemCanAcceptDrop()` (`ItemType, bool`);
`OnItemIsHoveredChanged()` (`ItemType, bool`);
`OnItemSelectionChanged()` (`NullableItemType`);
`OnListViewScrolled()` (`float, float`);
`OnFinishedScrolling()`; `OnTouchStart()`; `OnTouchMove()`; `OnTouchEnd()`;
`OnItemScrolledIntoView()` (`ItemType, UUserWidget&`);
`OnItemExpansionChanged()` (`ItemType, bool`);
`OnGetEntryClassForItem()` (`DECLARE_DELEGATE_RetVal_OneParam(TSubclassOf<UUserWidget>, …)`) — **per-item
entry class**, the heterogeneous-list mechanism (`:91-92`, resolved in `GetDesiredEntryClassForItem` `:110-123`);
`OnIsItemSelectableOrNavigable()` (`RetVal bool`) `:96-97`.

Mirrored `SListView` API (`:129-249`): `GetSelectedItem`, `ItemFromEntryWidget(const UUserWidget&)`,
`GetEntryWidgetFromItem<RowWidgetT>(ItemType)`, `GetIndexInList`, `GetSelectedItems(TArray&)`,
`GetNumItemsSelected`, `SetSelectedItem(item, ESelectInfo::Type = Direct)`,
`SetItemSelection(item, bool, ESelectInfo::Type = Direct)`, `ClearSelection`, `IsItemVisible`,
`IsItemSelected`, `RequestNavigateToItem`, `RequestScrollItemIntoView`, `CancelScrollIntoView`.

**Construction-arg structs** — these are the real parameter surface of the underlying Slate views:

`FListViewConstructArgs` (`:258-275`): `bAllowFocus = true`, `SelectionMode = ESelectionMode::Single`,
`bClearSelectionOnClick = false`, `ConsumeMouseWheel = WhenScrollingPossible`,
`bReturnFocusToSelection = false`, `bEnableProximateEntryNavigation = false`,
`bClearScrollVelocityOnSelection = true`, `Orientation = Orient_Vertical`,
`ScrollIntoViewAlignment = CenterAligned`, `ListViewStyle` (`FTableViewStyle` "ListView"),
`ScrollBarStyle`, `ShadowBoxStyle` (`FScrollBoxStyle` "ScrollBox"), `bEnableShadowBoxStyle = false`,
`ScrollBarPadding = FMargin(0.0f)`, `bPreventThrottling = false`.

`FTileViewConstructArgs : FListViewConstructArgs` (`:318-326`): `TileAlignment = EvenlyDistributed`,
`EntryHeight` / `EntryWidth` (`TAttribute<float>`), `bWrapDirectionalNavigation = false`,
`ScrollBarStyle`, `ScrollbarDisabledVisibility = EVisibility::Collapsed`.

`FTreeViewConstructArgs` (`:366-374`): `SelectionMode = Single`, `bClearSelectionOnClick = false`,
`ConsumeMouseWheel = WhenScrollingPossible`, `bReturnFocusToSelection = false`,
`TreeViewStyle` (`FTableViewStyle` "TreeView"), `ScrollBarStyle`.

The `SNew` blocks at `:283-313` / `:334-360` / `:382-404` enumerate exactly which Slate arguments UMG
drives — including `HandleGamepadEvents(true)` on all three.

### 4.3 `UListView` → `SListView<UObject*>`

`Components/ListView.h:41`, `UCLASS(meta = (EntryInterface = "/Script/UMG.UserObjectListEntry"))`,
`public UListViewBase, public ITypedUMGListView<UObject*>`.

| Property | Type | Default | Line |
|---|---|---|---|
| `WidgetStyle` (DisplayName "Style") | `FTableViewStyle` | | `:239` |
| `ScrollBarStyle` | `FScrollBarStyle` | | `:242` |
| `bEnableShadowBrush` | `bool` | `false` | `:245` |
| `ShadowBrushStyle` | `FScrollBoxStyle` | editcondition `bEnableShadowBrush` | `:248` |
| `Orientation` | `TEnumAsByte<EOrientation>` | `Orient_Vertical` (enum 0) | `:256` |
| `SelectionMode` | `TEnumAsByte<ESelectionMode::Type>` | `Single` | `:259` |
| `ConsumeMouseWheel` | `EConsumeMouseWheel` | `WhenScrollingPossible` | `:262` |
| `bClearSelectionOnClick` | `bool` | `false` | `:265` |
| `bIsFocusable` | `bool` | `true` | `:268` |
| `bClearScrollVelocityOnSelection` | `bool` | `true` | `:271` |
| `bReturnFocusToSelection` | `bool` | `false` | `:274` |
| `bEnableProximateEntryNavigation` | `bool` | `false`; **`UPROPERTY(Experimental)`** | `:281` |
| `ScrollIntoViewAlignment` | `EScrollIntoViewAlignment` | `CenterAligned` | `:285` |
| `EntrySpacing` | `float` | `0.f`; **deprecated**, editor-only → use H/V spacing | `:298` |
| `HorizontalEntrySpacing` | `float` | `0.f`; ClampMin 0 | `:303` |
| `VerticalEntrySpacing` | `float` | `0.f`; ClampMin 0 | `:306` |
| `ScrollBarPadding` | `FMargin` | default | `:309` |

`Orientation` doc at `:250-254`: "Vertical will scroll vertically and arrange tiles into rows.
Horizontal will scroll horizontally and arrange tiles into columns."

**`FTableViewStyle`** — `SlateTypes.h:1605`: a single field, `BackgroundBrush` `:1622`.

Items API: `SetListItems<T>` (template) `:55`, `GetListItems` `:94`, `AddItem` `:98`, `AddItemAt` `:102`,
`AddItems` `:106`, `AddItemsAt` `:110`, `RemoveItem` `:114`, `RemoveItems` `:118`, `GetItemAt` `:122`,
`GetNumItems` `:126`, `GetIndexForItem` `:130`, `ClearListItems` `:134`, `SetSelectionMode` `:138`,
`SetScrollIntoViewAlignment` `:142`, `SetEnableProximateEntryNavigation` `:146`, `SetScrollBarPadding` `:150`,
`IsRefreshPending` `:154`, `ScrollIndexIntoView` `:158`, `SetSelectedIndex` `:162`, `NavigateToIndex` `:166`,
`GetHorizontalEntrySpacing` `:315`, `GetVerticalEntrySpacing` `:319`, `GetScrollBarPadding` `:323`,
`SetReturnFocusToSelection` `:327`, `SetVerticalEntrySpacing` `:330`, `SetHorizontalEntrySpacing` `:333`,
`SetShadowBrushStyle` `:336`, `GetShadowBrushStyle` `:339`.
(`InitHorizontalEntrySpacing` / `InitVerticalEntrySpacing` **deprecated 5.6** `:343-347`.)

BP-exposed `ITypedUMGListView` wrappers: `BP_SetSelectedItem` `:354`, `BP_SetItemSelection` `:358`,
`BP_ClearSelection` `:362`, `BP_GetNumItemsSelected` `:366`, `BP_GetSelectedItems` `:370`,
`BP_IsItemVisible` `:374`, `BP_NavigateToItem` `:378`, `BP_ScrollItemIntoView` `:382`,
`BP_CancelScrollIntoView` `:386`, `BP_SetListItems` `:390`, `BP_GetSelectedItem` `:394`.

BP events (all `BlueprintAssignable`): `On Entry Initialized` (`UObject* Item, UUserWidget* Widget`) `:399`,
`On Item Clicked` `:402`, `On Item Double Clicked` `:405`,
`On Item Drag Detected` (`UObject*, const FGeometry&`) `:408`,
`On Item Drag Enter` (`UObject*, UDragDropOperation*`) `:411`, `On Item Drag Leave` `:414`,
`On Item Accept Drop` (`UObject*, EUMGItemDropZone, UDragDropOperation*`) `:417`,
`On Item Drag Cancelled` (`UDragDropOperation*`) `:420`,
`On List View Dragging State Changed` (`bool`) `:423`,
`On Item Is Hovered Changed` (`UObject*, bool`) `:426`,
`On Item Selection Changed` (`UObject*, bool`) `:429`,
`On Item Scrolled Into View` (`UObject*, UUserWidget*`) `:432`,
`On List View Scrolled` (`float ItemOffset, float DistanceRemaining`) `:435`,
`On List View Finished Scrolling` `:438`, `On List View Touch Start` `:441`,
`On List View Touch Move` `:444`, `On List View Touch End` `:447`;
plus `BlueprintReadWrite` `On Is Item Selectable Or Navigable` (`FOnIsItemSelectableOrNavigableDynamic`) `:450`.

### 4.4 `UTileView : UListView` → `STileView<UObject*>`

`Components/TileView.h:15`. Adds:
`EntryHeight` (`float`, **`128.f`**) `:92`, `EntryWidth` (`float`, **`128.f`**) `:96`,
`TileAlignment` (`EListItemAlignment`) `:100`,
`bWrapHorizontalNavigation` (`bool`, `false`) `:104`,
`ScrollbarDisabledVisibility` (`ESlateVisibility`, `Collapsed`; `ValidEnumValues="Collapsed, Hidden, Visible"`) `:108`,
`bEntrySizeIncludesEntrySpacing` (`bool`, **`true`**, private, editcondition `IsAligned`) `:118`.
API: `SetEntryHeight` `:26`, `SetEntryWidth` `:30`, `GetEntryHeight` `:34`, `GetEntryWidth` `:38`;
protected `GetTotalEntryHeight` `:44`, `GetTotalEntryWidth` `:45`, `IsAligned` `:87`.
The `bEntrySizeIncludesEntrySpacing` semantics are documented at `:113-117` and implemented at
`:64-77`: when true the tile *cell* is `EntryWidth`, and the widget shrinks to leave room for spacing;
when false the *widget* is `EntryWidth` and spacing is added on top.

### 4.5 `UTreeView : UListView` → `STreeView<UObject*>`

`Components/TreeView.h:20`. Adds:
`BP_OnGetItemChildren` (`FOnGetItemChildrenDynamic(UObject* Item, TArray<UObject*>& Children)`,
`IsBindableEvent`, DisplayName "On Get Item Children") `:82`;
`BP_OnItemExpansionChanged` (`FOnItemExpansionChangedDynamic(UObject*, bool)`, `BlueprintAssignable`) `:85`;
native `TSlateDelegates<UObject*>::FOnGetChildren OnGetItemChildren` `:87`.
API: `SetItemExpansion(UObject*, bool)` `:30`, `ExpandAll()` `:34`, `CollapseAll()` `:38`,
`SetOnGetItemChildren` (UObject and SharedRef overloads) `:41` / `:47`.
Note the BP delegate is only consulted "if the native C++ version of the event is not bound" (`:80`).

### 4.6 The entry-widget interfaces

**`IUserListEntry`** — `Engine/Source/Runtime/UMG/Public/Blueprint/IUserListEntry.h:27`
(`UINTERFACE(BlueprintType)` `UUserListEntry` at `:22`). Required for **any** `UListViewBase` entry.
Queries: `IsListItemSelected()` `:32`, `IsListItemExpanded()` (tree only) `:35`,
`GetOwningListView()` `:38`, `IsListItemSelectable()` (native-only, default `true`; "intended primarily
for category separators") `:45`.
Native virtuals: `NativeOnItemSelectionChanged(bool)` `:63`, `NativeOnItemExpansionChanged(bool)` `:64`,
`NativeOnEntryReleased()` `:65`.
`BlueprintImplementableEvent`s: `BP_OnItemSelectionChanged(bool)` `:69`,
`BP_OnItemExpansionChanged(bool)` `:73`, `BP_OnEntryReleased()` `:77`,
`BP_OnUpdateEntryDropIndicator(EUMGItemDropZone)` `:81`, `BP_OnEntryDragOverChanged(bool)` `:85`,
`BP_OnEndEntryDropOperation(bool bSuccess)` `:89`, `BP_OnEntryDropped(UDragDropOperation*)` `:93`,
`BP_OnEntryDragged(UDragDropOperation*)` `:97`.
Plumbing statics: `ReleaseEntry` `:49`, `UpdateItemSelection` `:50`, `UpdateItemExpansion` `:51`,
`UpdateEntryDropIndicator` `:52`, `UpdateEntryDragOverState` `:53`, `EndEntryDropOperation` `:54`,
`HandleEntryDropped` `:55`, `HandleEntryDragged` `:56`.
Blueprint library `UUserListEntryLibrary` `:103`: `IsListItemSelected` `:113`, `IsListItemExpanded` `:120`,
`GetOwningListView` `:127` (all `DefaultToSelf`).

**`IUserObjectListEntry : IUserListEntry`** — `Blueprint/IUserObjectListEntry.h:18`. Required for
`UListView` / `UTileView` / `UTreeView` specifically.
`GetListItem<ItemObjectT>()` (template) `:25`; `NativeOnListItemObjectSet(UObject*)` `:33`;
`OnListItemObjectSet(UObject*)` (`BlueprintImplementableEvent`) `:37`;
private `GetListItemObjectInternal` `:40`, `SetListItemObject` `:43` (friend of `SObjectTableRow`).
Library `UUserObjectListEntryLibrary` `:48`: `GetListItemObject` `:58`, `GetListItemIndex` `:62`,
`IsFirstWidget` `:66`, `IsLastWidget` `:70`.

**Parity note.** The interface-not-baseclass choice is deliberate: your entry widget can inherit from
whatever `UUserWidget` subclass your project uses and still be list-usable. The pooled entry gets
`OnListItemObjectSet` instead of `Construct` — a parity implementation must not assume a fresh
widget per item.

### 4.7 `UDynamicEntryBoxBase` (abstract) and `UDynamicEntryBox`

The non-virtualised alternative: N auto-generated entry widgets in a box panel, at design time *and*
runtime.

`UDynamicEntryBoxBase` — `Components/DynamicEntryBoxBase.h:32`, `UCLASS(Abstract)`.
`EDynamicBoxType` (`:14-22`): `Horizontal`, `Vertical`, `Wrap`, `VerticalWrap`, `Radial`, `Overlay`.

| Property | Type | Default | Line |
|---|---|---|---|
| `EntrySpacing` | `FVector2D` | `(0,0)`; H boxes use X only, V boxes Y only, Wrap/Overlay both; first entry ignored | `:45` |
| `SpacingPattern` | `TArray<FVector2D>` | empty; **Overlay only**; overrides `EntrySpacing` when non-empty | `:50` |
| `EntryBoxType` | `EDynamicBoxType` | `Horizontal` (enum 0); read-only, `DesignerRebuild` | `:55` |
| `EntrySizeRule` | `FSlateChildSize` | `{1.0f, Fill}`; H/V boxes only; read-only | `:60` |
| `EntryHorizontalAlignment` | `TEnumAsByte<EHorizontalAlignment>` | H/V/Wrap only; read-only | `:65` |
| `EntryVerticalAlignment` | `TEnumAsByte<EVerticalAlignment>` | H/V/Wrap only; read-only | `:70` |
| `MaxElementSize` | `int32` | `0`; V/H boxes only; read-only | `:75` |
| `RadialBoxSettings` | `FRadialBoxSettings` | RadialBox only | `:80` |

API: `GetBoxType` `:86`, `GetEntrySpacing` `:87`, `GetAllEntries` `:90`, `GetEntrySizeRule` `:92`,
`GetRadialBoxSettings` `:94`, `GetTypedEntries<T>` `:97`, `GetNumEntries` `:111`, `SetEntrySpacing` `:114`,
`SetRadialSettings` `:117`, `GetEntryVerticalAlignment` `:119`, `GetEntryHorizontalAlignment` `:121`,
`GetMaxElementSize` `:123`.
Protected creation/pooling: `AddEntryChild` `:128`, `IsEntryClassValid` `:130`, `CreateEntryInternal` `:131`,
`RemoveEntryInternal` `:132`, `BuildEntryPadding` `:133`, `ResetInternal(bool bDeleteWidgets = false)` `:136`,
templated `ResetInternal(TFunctionRef<void(WidgetT&)>, bool)` `:140`;
`FUserWidgetPool EntryWidgetPool` `:179`.

`UDynamicEntryBox` — `Components/DynamicEntryBox.h:17`. "No children can be manually added in the
designer — all are auto-generated based on the given entry class" (`:14`).
`EntryWidgetClass` (`TSubclassOf<UUserWidget>`, private) `:89`;
editor-only `NumDesignerPreviewEntries` (`int32`, **3**, Clamp 0..20) `:64` and
`OnPreviewEntryCreatedFunc` (`TFunction<void(UUserWidget*)>`) `:70`.
API: `GetEntryWidgetClass` `:22`, `CreateEntry<WidgetT>(ExplicitEntryClass)` `:25`,
`Reset<WidgetT>(ResetEntryFunc, bDeleteWidgets)` `:36`, `Reset(bool bDeleteWidgets = false)` `:51`,
`RemoveEntry(UUserWidget*)` `:54`, `BP_CreateEntry()` `:78`,
`BP_CreateEntryOfClass(TSubclassOf<UUserWidget>)` `:82`; `ValidateCompiledDefaults` `:58`.

`UUniformGridPanel` (`Components/UniformGridPanel.h:19`) belongs to the same "data grid" story:
`SlotPadding` (`FMargin`) `:28`, `MinDesiredSlotWidth` (`float`) `:33`, `MinDesiredSlotHeight` (`float`) `:38`;
`AddChildToUniformGrid(UWidget*, int32 InRow = 0, int32 InColumn = 0)` `:67`. Constructor sets
`bIsVariable = false`, visibility `SelfHitTestInvisible` (`UniformGridPanel.cpp:18-19`).

---

## 5. Media, 3D and render-target widgets

### 5.1 World-space UI — `UWidgetComponent`

`Engine/Source/Runtime/UMG/Public/Components/WidgetComponent.h:95`, `class UWidgetComponent : public UMeshComponent`,
`UCLASS(Blueprintable, ClassGroup="UserInterface", editinlinenew, meta=(BlueprintSpawnableComponent))`.
Renders a `UUserWidget` to a render target and displays it in the world.

Enums: `EWidgetSpace` (`:25`) `World|Screen`; `EWidgetTimingPolicy` (`:34`) `RealTime|GameTime`;
`EWidgetBlendMode` (`:43`) `Opaque|Masked|Transparent`; `EWidgetGeometryMode` (`:51`) `Plane|Cylinder`;
`EWindowVisibility` (`:61`) `Visible|SelfHitTestInvisible`; `ETickMode` (`:71`) `Disabled|Enabled|Automatic`.

| Property | Type | Default | Line |
|---|---|---|---|
| `Space` | `EWidgetSpace` | `World` | `:448` / `WidgetComponent.cpp:677` |
| `TimingPolicy` | `EWidgetTimingPolicy` | `RealTime` | `:452` / `.cpp:678` |
| `WidgetClass` | `TSubclassOf<UUserWidget>` | null | `:456` |
| `DrawSize` | `FIntPoint` | **`(500, 500)`** | `:460` / `.cpp:621` |
| `bManuallyRedraw` | `bool` | `false` | `:464` / `.cpp:622` |
| `RedrawTime` | `float` | `0.0f` | `:476` / `.cpp:624` |
| `bUseInvalidationInWorldSpace` | `bool` | `false`; editcondition `Space==World` | `:496` / `.cpp:627` |
| `bDrawAtDesiredSize` | `bool` | (unset → false) | `:506` |
| `Pivot` | `FVector2D` | **`(0.5f, 0.5f)`** | `:510` / `.cpp:679` |
| `bReceiveHardwareInput` | `bool` | `false` | `:523` / `.cpp:628` |
| `bWindowFocusable` | `bool` | **`true`** | `:527` / `.cpp:629` |
| `WindowVisibility` | `EWindowVisibility` | **`SelfHitTestInvisible`** | `:531` / `.cpp:630` |
| `bApplyGammaCorrection` | `bool` | `false`; `AdvancedDisplay` | `:538` / `.cpp:631` |
| `BackgroundColor` | `FLinearColor` | `Transparent` | `:549` / `.cpp:632` |
| `TintColorAndOpacity` | `FLinearColor` | `White` | `:553` / `.cpp:633` |
| `OpacityFromTexture` | `float` | `1.0f`; Clamp 0..1 | `:557` / `.cpp:634` |
| `BlendMode` | `EWidgetBlendMode` | **`Masked`** | `:561` / `.cpp:635` |
| `bOverrideRenderTargetFormat` | `bool` | `false`; `AdvancedDisplay` | `:565` / `.cpp:636` |
| `RenderTargetFormatOverride` | `TEnumAsByte<ETextureRenderTargetFormat>` | `RTF_RGBA8` | `:569` / `.cpp:637` |
| `bIsTwoSided` | `bool` | `false` | `:573` / `.cpp:638` |
| `TickWhenOffscreen` | `bool` | `false`; Category "Animation" | `:577` / `.cpp:639` |
| `SharedLayerName` | `FName` | `"WidgetComponentScreenLayer"`; `EditDefaultsOnly` | `:628` / `.cpp:640` |
| `LayerZOrder` | `int32` | **`-100`**; `EditDefaultsOnly` | `:632` / `.cpp:641` |
| `GeometryMode` | `EWidgetGeometryMode` | `Plane` | `:636` / `.cpp:642` |
| `CylinderArcAngle` | `double` | **`180.0f`**; Clamp 1..180 | `:640` / `.cpp:643` |
| `TickMode` | `ETickMode` | `Automatic` or `Enabled` per `bUseAutomaticTickModeByDefault` cvar | `:643` / `.cpp:644` |

Collision profile is forced to `"UI"` (`.cpp:653`); pass-through materials come from
`/Engine/EngineMaterials/Widget3DPassThrough_{Translucent,Opaque,Masked}[_OneSided]` (`.cpp:656-671`).
Material parameters the component drives, documented at `:88-92`:
`SlateUI` (Texture), `BackColor` (Vector), `TintColorAndOpacity` (Vector), `OpacityFromTexture` (Scalar).

API: `GetLocalHitLocation(FVector WorldHitLocation, FVector2D& OutLocalHitLocation)` `:160`,
`GetCylinderHitLocation` `:168`, `GetUserWidgetObject` `:181`, `GetSlateWidget` `:184`,
`GetHitWidgetPath(FVector WorldHitLocation, bool bIgnoreEnabledStatus, float CursorRadius = 0.0f)` `:187`
and the 2D overload `:190`, `GetRenderTarget` `:194`, `GetMaterialInstance` `:198`, `GetSlateWindow` `:201`,
`GetWidget` `:207`, `SetWidget(UUserWidget*)` `:214`, `SetSlateWidget(TSharedPtr<SWidget>)` `:220`,
`SetOwnerPlayer(ULocalPlayer*)` `:228`, `SetManuallyRedraw` `:239`, `GetOwnerPlayer` `:243`,
`GetDrawSize` `:247`, `GetCurrentDrawSize` `:251`, `SetDrawSize` `:255`, `RequestRedraw` `:259`,
`RequestRenderUpdate` `:263`, `SetBlendMode` `:269`, `SetTwoSided` `:280`, `SetBackgroundColor` `:298`,
`SetTintColorAndOpacity` `:302`, `SetOpacityFromTexture` `:305`, `GetVirtualWindow` `:332`,
`UpdateMaterialInstanceParameters` `:335`, `SetWidgetClass` `:338`, `SetWindowFocusable` `:392`,
`SetWindowVisibility` `:403`, `SetTickMode` `:407`, `IsWidgetVisible` `:411`,
`CanReceiveHardwareInput` `:423`, `RegisterHitTesterWithViewport` `:425`, `RegisterWindow` `:428`.
Native event `OnMaterialInstanceUpdated` (`FOnMaterialInstanceUpdated`) `:417`.
`static TSharedPtr<FWidget3DHitTester> WidgetHitTester` `:652` — one shared 3D hit tester registered
with the viewport; this is how a world-space widget participates in the normal Slate hit path.

### 5.2 `URetainerBox` → `SRetainerWidget`

`Components/RetainerBox.h:26`, parent `UContentWidget`. Renders children to a render target, then
composites that target — decoupling UI redraw rate from frame rate and allowing a post-process
material on the result (`:16-21`).

| Property | Type | Default | Line |
|---|---|---|---|
| `bRetainRender` | `bool` | `true` | `:33` |
| `RenderOnInvalidation` | `bool` | **`false`**; read-only, construction-only, editcondition `bRetainRender` | `:42` / `RetainerBox.cpp:26` |
| `RenderOnPhase` | `bool` | **`true`**; read-only, construction-only | `:49` / `.cpp:25` |
| `Phase` | `int32` | `0`; UIMin/ClampMin 0 | `:60` / `.cpp:23` |
| `PhaseCount` | `int32` | `1`; UIMin/ClampMin 1 | `:72` / `.cpp:24` |
| `EffectMaterial` | `UMaterialInterface*` | null | `:157` |
| `TextureParameter` | `FName` | `DefaultTextureParameterName` | `:164` / `.cpp:27` |
| `bShowEffectsInDesigner` | `bool` | `true`; editor-only | `:171` / `.cpp:29` |

Constructor sets visibility `Visible` (`.cpp:21`).
Phase semantics (`:55-57`, `:67-69`): `Phase 0 / PhaseCount 1` = redraw every frame;
`Phase 0 / PhaseCount 2` = redraw every other frame (60 Hz game → 30 Hz UI).
API: `SetRenderingPhase(int32 RenderPhase, int32 TotalPhases)` `:80`, `RequestRender()` `:86`,
`GetEffectMaterial()` `:92` (returns `UMaterialInstanceDynamic*`), `SetEffectMaterial` `:98`,
`SetTextureParameter(FName)` `:104`, `SetRetainRendering` `:110`, `IsRetainRendering` `:115`,
`GetPhase` `:120`, `GetPhaseCount` `:125`, `IsRenderOnInvalidation` `:130`, `IsRenderOnPhase` `:135`,
`GetCachedAllottedGeometry()` `:143`, `GetTextureParameter` `:205`, `GetEffectMaterialInterface` `:210`.
`:152-155` warns: for transparency the effect material must be `AlphaComposite (Pre-Multiplied Alpha)`
and must multiply alpha into both colour and alpha.

### 5.3 `UBackgroundBlur` → `SBackgroundBlur`

`Components/BackgroundBlur.h:16`, parent `UContentWidget`. Gaussian blur of everything drawn beneath.

| Property | Type | Default | Line |
|---|---|---|---|
| `Padding` | `FMargin` | `(0, 0)` | `:24` / `BackgroundBlur.cpp:21` |
| `HorizontalAlignment` | `TEnumAsByte<EHorizontalAlignment>` | (enum 0 = `HAlign_Fill`) | `:29` |
| `VerticalAlignment` | `TEnumAsByte<EVerticalAlignment>` | (enum 0 = `VAlign_Fill`) | `:34` |
| `bApplyAlphaToBlur` | `bool` | **`true`** | `:39` / `.cpp:22` |
| `BlurStrength` | `float` | `0.f`; Clamp 0..100 | `:46` / `.cpp:23` |
| `bOverrideAutoRadiusCalculation` | `bool` | `false`; **no `EditAnywhere`** (getter/setter only) | `:51` / `.cpp:24` |
| `BlurRadius` | `int32` | `0`; Clamp 0..255, `AdvancedDisplay`, editcondition `bOverrideAutoRadiusCalculation` | `:59` / `.cpp:25` |
| `CornerRadius` | `FVector4` | `(0,0,0,0)`; X=TL, Y=TR, Z=BR, W=BL | `:64` / `.cpp:26` |
| `LowQualityFallbackBrush` | `FSlateBrush` | `FSlateNoResource()` | `:73` / `.cpp:27` |

Constructor sets `bIsVariable = false`, visibility `SelfHitTestInvisible` (`.cpp:29-30`).
Low-quality override is driven by the cvar **`Slate.ForceBackgroundBlurLowQualityOverride`** (`:68-71`) —
i.e. blur is a scalability setting, and the fallback is a flat brush.
API: `SetPadding` `:84`, `GetPadding` `:86`, `SetHorizontalAlignment` `:89`, `GetHorizontalAlignment` `:91`,
`SetVerticalAlignment` `:94`, `GetVerticalAlignment` `:96`, `SetApplyAlphaToBlur` `:99`,
`GetApplyAlphaToBlur` `:101`, `SetOverrideAutoRadiusCalculation` `:103`,
`GetOverrideAutoRadiusCalculation` `:105`, `SetBlurRadius` `:108`, `GetBlurRadius` `:110`,
`SetBlurStrength` `:113`, `GetBlurStrength` `:115`, `SetCornerRadius` `:118`, `GetCornerRadius` `:120`,
`SetLowQualityFallbackBrush` `:123`, `GetLowQualityFallbackBrush` `:125`.

### 5.4 `UPostBufferUpdate` → `SPostBufferUpdate`

`Components/PostBufferUpdate.h:44`. Draws nothing; when painted, triggers a Slate post-process buffer
update so material functions (`GetSlatePost*`) can sample the UI drawn *before* this widget (`:36-41`).

`FSlatePostBufferUpdateInfo` (`:23`): `BufferToUpdate` (`ESlatePostRT`, `None`) `:29`,
`PostParamUpdater` (`USlatePostBufferProcessorUpdater*`, Instanced) `:33`.
`UpdateBufferInfos` (`TArray<FSlatePostBufferUpdateInfo>`) `:75`.
Deprecated: `bUpdateOnlyPaintArea` (**5.7**, "Now always true") `:58`,
`bPerformDefaultPostBufferUpdate` (**5.7**, "Default post buffer updates are no longer performed under
any circumstances") `:66`, `BuffersToUpdate` (**5.5** → `UpdateBufferInfos`) `:71`.
`USlatePostBufferProcessorUpdater` (`:103`, `Abstract, Blueprintable, EditInlineNew, CollapseCategories`)
with deprecated `bSkipBufferUpdate` (**5.7**) `:118` and `GetRenderThreadProxy()` `:121`.

### 5.5 `UViewport` → `SAutoRefreshViewport` — **Experimental**

`Components/Viewport.h:234`, `UCLASS(Experimental)`, parent `UContentWidget`. Renders a live 3D
preview scene inside UI.
`BackgroundColor` (`FLinearColor`, `Black`) `:240` / `Viewport.cpp:377`;
`bIsEditorPreview` (`bool`, `false`) `:247`.
API: `GetViewportWorld` `:250`, `GetViewLocation` `:253`, `SetViewLocation` `:256`, `GetViewRotation` `:259`,
`SetViewRotation` `:262`, `Spawn(TSubclassOf<AActor>)` `:265`, `SetBackgroundColor` `:267`,
`GetBackgroundColor` `:269`, `SetEnableAdvancedFeatures` `:272`, `SetLightIntensity` `:275`,
`SetSkyIntensity` `:278`, `SetShowFlag(FString, bool)` `:281`, `GetViewProjectionMatrix` `:284`.
Supporting types in the same header: `FUMGViewportCameraTransform` `:27` (`SetLocation` `:33`,
`SetRotation` `:36`, `SetLookAt` `:41`, `SetOrthoZoom` `:48`, `TransitionToLocation` `:72`,
`UpdateTransition` `:79`, `ComputeOrbitMatrix` `:84`) and `FUMGViewportClient` `:103`.
Constructor: `ShowFlags(ESFIM_Game)` then `ShowFlags.DisableAdvancedFeatures()` (`Viewport.cpp:373,379`).

### 5.6 `UWidgetInteractionComponent`

`Components/WidgetInteractionComponent.h:56`, `class UWidgetInteractionComponent : public USceneComponent`.
A virtual pointer device — "simulate a sort of laser pointer" (`:51`) — that drives
`UWidgetComponent`s via a **virtual Slate user**.

`EWidgetInteractionSource` (`:24`): `World`, `Mouse`, `CenterScreen`, `Custom`.

| Property | Type | Line |
|---|---|---|
| `VirtualUserIndex` | `int32` (ClampMin 0, `ExposeOnSpawn`) | `:202` |
| `PointerIndex` | `int32` (Clamp 0, UIMin 0/UIMax 9, `ExposeOnSpawn`) | `:208` |
| `TraceChannel` | `TEnumAsByte<ECollisionChannel>` | `:216` |
| `InteractionDistance` | `float` | `:222` |
| `InteractionSource` | `EWidgetInteractionSource` | `:230` |
| `bEnableHitTesting` | `bool` | `:239` |
| `bShowDebug` | `bool` | `:247` |
| `DebugSphereLineThickness` | `float` (ClampMin 0.001) | `:253` |
| `DebugLineThickness` | `float` (Clamp 0.001..50) | `:259` |
| `DebugColor` | `FLinearColor` | `:265` |

Event: `OnHoveredWidgetChanged(UWidgetComponent* WidgetComponent, UWidgetComponent* PreviousWidgetComponent)` `:66`.
API: `PressPointerKey(FKey)` `:84`, `ReleasePointerKey(FKey)` `:92`, `PressKey(FKey, bool bRepeat = false)` `:100`,
`ReleaseKey(FKey)` `:106`, `PressAndReleaseKey(FKey)` `:112`, `SendKeyChar(FString, bool bRepeat = false)` `:119`,
`ScrollWheel(float ScrollDelta)` `:125`, `GetHoveredWidgetComponent` `:131`,
`IsOverInteractableWidget` `:138`, `IsOverFocusableWidget` `:145`, `IsOverHitTestVisibleWidget` `:152`,
`GetHoveredWidgetPath` `:157`, `GetLastHitResult` `:163`, `Get2DHitLocation` `:169`,
`SetCustomHitResult(const FHitResult&)` `:175`, `SetFocus(UWidget*)` `:181`.
`HoveredWidgetComponent` is **deprecated 5.6** in favour of `WeakHoveredWidgetComponent` (`:339-345`).
The virtual-user rationale is at `:184-191`: virtual users start at a slate index (≈8) chosen so they
never collide with real hardware users, keeping focus/capture states separate.

---

## 6. Navigation and miscellaneous

### 6.1 `UWidgetSwitcher` → `SWidgetSwitcher`

`Components/WidgetSwitcher.h:16`, parent `UPanelWidget`. "Like a tab control, but without tabs" (`:13`).
One property: `ActiveWidgetIndex` (`int32`, FieldNotify, UIMin/ClampMin 0) `:24`.
API: `GetNumWidgets` `:30`, `GetActiveWidgetIndex` `:34`, `SetActiveWidgetIndex(int32)` `:38`,
`SetActiveWidget(UWidget*)` `:42`, `GetWidgetAtIndex(int32)` `:46`, `GetActiveWidget()` `:50`.
Constructor: `bIsVariable = true`, visibility `SelfHitTestInvisible` (`WidgetSwitcher.cpp:17-18`).

### 6.2 `UMenuAnchor` → `SMenuAnchor`

`Components/MenuAnchor.h:23`, parent `UContentWidget`. Anchors a popup menu to a widget.

| Property | Type | Default | Line |
|---|---|---|---|
| `MenuClass` | `TSubclassOf<UUserWidget>` | null; freshly created each time | `:37` |
| `OnGetMenuContentEvent` | `FGetWidget` | **deprecated 4.26** → `OnGetUserMenuContentEvent` | `:42` |
| `OnGetUserMenuContentEvent` | `FGetUserWidget` | | `:46` |
| `Placement` | `TEnumAsByte<EMenuPlacement>` | **`MenuPlacement_ComboBox`** | `:51` / `MenuAnchor.cpp:23` |
| `bFitInWindow` | `bool` | **`true`** | `:56` / `.cpp:24` |
| `ShouldDeferPaintingAfterWindowContent` | `bool` | **`true`**; read-only, `AdvancedDisplay` | `:60` / `.cpp:18` |
| `UseApplicationMenuStack` | `bool` | **`true`**; read-only, `AdvancedDisplay` | `:65` / `.cpp:19` |
| `ShowMenuBackground` | `bool` | **`true`**; read-only, editcondition `UseApplicationMenuStack` (`EditConditionHides`) | `:69` / `.cpp:20` |

Event: `OnMenuOpenChanged(bool bIsOpen)` `:74`.
API: `SetPlacement` `:80`, `GetPlacement` `:82`, `FitInWindow(bool)` `:85`, `IsFitInWindow` `:87`,
`IsDeferPaintingAfterWindowContent` `:89`, `IsUseApplicationMenuStack` `:91`, `IsShowMenuBackground` `:93`,
`ToggleOpen(bool bFocusOnOpen)` `:103`, `Open(bool bFocusMenu)` `:107`, `Close()` `:111`, `IsOpen()` `:115`,
`ShouldOpenDueToClick()` `:123`, `GetMenuPosition()` `:127`, `HasOpenSubMenus()` `:131`.

`ShouldOpenDueToClick` (`:117-121`) is the fix for the classic bug where the mouse-down that dismisses
an open menu immediately re-opens it because it landed on the summoning button.
`UseApplicationMenuStack = false` means "control the menu's lifetime yourself" (`:63`).

### 6.3 `UNamedSlot` → `SBox`

`Components/NamedSlot.h:18`, parent `UContentWidget`. "Allows you to expose an external slot for your
user widget" (`:14`). Editor-only `bExposeOnInstanceOnly` (`bool`, `false`) `:47` — instance-only named
slots can carry replaceable default content but **cannot be inherited/filled by a subclass** (`:40-44`).
Editor `GetSlotGUID()` `:35`; persisted `SlotGuid` (`FGuid::NewGuid()`) `:65`.

`INamedSlotInterface` — `Components/NamedSlotInterface.h:22`,
`UINTERFACE(meta=(CannotImplementInterfaceInBlueprint))` at `:17`.
Pure virtuals: `GetSlotNames(TArray<FName>&)` `:29`, `GetContentForSlot(FName)` `:32`,
`SetContentForSlot(FName, UWidget*)` `:35`. Provided: `ContainsContent(UWidget*)` `:38`,
`FindSlotForContent(UWidget*)` `:41`, `ReleaseNamedSlotSlateResources(bool)` `:44`,
editor `SetNamedSlotDesignerFlags(EWidgetDesignFlags)` `:48`.
Implemented by `UExpandableArea` (Header/Body slots) and `UUserWidget`
(`UserWidget.h:1515 NamedSlotBindings`, `FNamedSlotBinding { FName Name; FGuid Guid; UWidget* Content; }`
at `UserWidget.h:231-240`).

### 6.4 `UInvalidationBox` → `SInvalidationPanel`

`Components/InvalidationBox.h:19`, parent `UContentWidget`.
One property: `bCanCache` (`bool`, `true`) `:70` / `InvalidationBox.cpp:19`. Constructor sets
visibility `SelfHitTestInvisible` (`.cpp:21`).
API: `GetCanCache()` `:37`, `SetCanCache(bool)` `:44`; `InvalidateCache()` **deprecated 4.27**
("InvalidationCache is not used") `:30`. When `bCanCache` is false the panel "stops acting like an
invalidation panel, just becomes a simple container widget" (`:66-68`).

### 6.5 `UWindowTitleBarArea` → `SWindowTitleBarArea`

`Components/WindowTitleBarArea.h:21`, parent `UContentWidget`. Marks a UI region as OS window-drag
handle on desktop (`:17`).
`bWindowButtonsEnabled` (`bool`, DisplayName "Window Buttons Enabled") `:30`;
`bDoubleClickTogglesFullscreen` (`bool`, **`false`**, DisplayName "Toggle Fullscreen") `:35` /
`WindowTitleBarArea.cpp:35`. Constructor: `bIsVariable = false`, visibility `Visible` (`.cpp:32-33`).
API: `SetPadding` `:40`, `SetHorizontalAlignment` `:43`, `SetVerticalAlignment` `:46`,
`SetWindowButtonsEnabled` `:48`, `IsWindowButtonsEnabled` `:50`, `SetDoubleClickTogglesFullscreen` `:52`,
`IsDoubleClickTogglesFullscreen` `:54`. Private `HandleWindowAction(const TSharedRef<FGenericWindow>&, EWindowAction::Type)` `:91`,
`RequestToggleFullscreen()` `:92`.

### 6.6 `UNativeWidgetHost`

`Components/NativeWidgetHost.h:16`, parent `UWidget`. Escape hatch: nest a raw `SWidget` inside a UMG
tree. No UPROPERTYs. `SetContent(TSharedRef<SWidget>)` `:20`, `GetContent()` `:21`,
`GetDefaultContent()` `:34`.

### 6.7 `UExpandableArea` → `SExpandableArea`

`Components/ExpandableArea.h:25`, `class UExpandableArea : public UWidget, public INamedSlotInterface`.
Named slots for **Header** and **Body** (`HeaderContent` `:131`, `BodyContent` `:134`).

| Property | Type | Default | Line |
|---|---|---|---|
| `Style` | `FExpandableAreaStyle` | style cache; **direct access deprecated 5.2** | `:33` / `ExpandableArea.cpp:28,34` |
| `BorderBrush` | `FSlateBrush` | style cache | `:37` / `.cpp:29,35` |
| `BorderColor` | `FSlateColor` | `FLinearColor::White` | `:41` / `.cpp:39` |
| `bIsExpanded` | `bool` | `false`; FieldNotify | `:46` / `.cpp:23` |
| `MaxHeight` | `float` | (unset → 0) | `:51` |
| `HeaderPadding` | `FMargin` | **`FMargin(4.0f, 2.0f)`** | `:55` / `.cpp:41` |
| `AreaPadding` | `FMargin` | **`FMargin(1)`** | `:60` / `.cpp:40` |

Event: `OnExpansionChanged(UExpandableArea* Area, bool bIsExpanded)` `:64`.
API: `GetIsExpanded` `:69`, `SetIsExpanded(bool)` `:72`, `SetIsExpanded_Animated(bool)` `:75`,
`GetStyle` `:77`, `SetStyle` `:79`, `GetBorderBrush` `:81`, `SetBorderBrush` `:83`, `GetBorderColor` `:85`,
`SetBorderColor` `:87`, `GetMaxHeight` `:89`, `SetMaxHeight` `:91`, `GetHeaderPadding` `:93`,
`SetHeaderPadding` `:95`, `GetAreaPadding` `:97`, `SetAreaPadding` `:98`,
plus `INamedSlotInterface` `:101-103`.

**`FExpandableAreaStyle`** — `SlateTypes.h:1217`: `CollapsedImage` `:1234`, `ExpandedImage` `:1239`,
`RolloutAnimationSeconds` (`float`) `:1244`.

### 6.8 `UMouseHoverComponent` (UIComponent)

`Components/MouseHoverComponent.h:13`, `class UMouseHoverComponent : public UUIComponent`.
Exposes its owner widget's hover state as a FieldNotify property:
`bIsHovered` (`bool`, `false`, `VisibleAnywhere, BlueprintReadOnly, FieldNotify`) `:26`;
`GetIsHovered()` `:19`, `OnMouseHoverChanged(bool)` `:21`,
`RebuildWidgetWithContent(TSharedRef<SWidget>)` `:28`.

**UIComponents** are a newer composition mechanism distinct from widgets: they *wrap* the owner
widget's content in an extra `SWidget` at build time. Two more ship in `Components/`:

* `USizeBoxComponent` — `Components/SizeBoxComponent.h:16`, `UCLASS(Experimental)`. Wraps the owner in
  an `SBox`. Slot props: `Padding` (`FMargin(0,0)`) `:146`, `HorizontalAlignment` (`HAlign_Fill`) `:150`,
  `VerticalAlignment` (`VAlign_Fill`) `:154`. Box props, each with an `InlineEditConditionToggle` override
  bit (`:192-220`): `WidthOverride` (0.f) `:160`, `HeightOverride` (0.f) `:164`, `MinDesiredWidth` (0.f) `:168`,
  `MinDesiredHeight` (0.f) `:172`, `MaxDesiredWidth` (0.f) `:176`, `MaxDesiredHeight` (0.f) `:180`,
  `MinAspectRatio` (1.f) `:184`, `MaxAspectRatio` (1.f) `:188`. Each has `Get`/`Is…Override`/`Set`/`Clear…`
  (`:23-114`).
* `UScaleBoxComponent` — `Components/ScaleBoxComponent.h:18`, `UCLASS(Experimental)`. Wraps in an
  `SScaleBox`. `HorizontalAlignment` (`HAlign_Center`) `:74`, `VerticalAlignment` (`VAlign_Center`) `:78`,
  `Stretch` (`EStretch::ScaleToFit`) `:84`, `StretchDirection` (`EStretchDirection::Both`) `:88`,
  `UserSpecifiedScale` (`1.0f`) `:92`, `IgnoreInheritedScale` (`false`) `:96`.

---

## A. The CommonUI plugin

Path shorthand used throughout (expand before quoting):

* **`CUI/`** = `Engine/Plugins/Runtime/CommonUI/Source/CommonUI/`
* **`CIN/`** = `Engine/Plugins/Runtime/CommonUI/Source/CommonInput/`

Two runtime modules. `CommonInput` is the lower layer (input-type detection, controller glyph data,
action domains, settings) and has **no dependency on CommonUI**; `CommonUI` is the widget +
action-routing layer on top. `CUI/Public/Input/CommonInputMode.h:7` is now only a deprecated
forwarding stub (`UE_DEPRECATED_HEADER(5.1, …)`); the real enum lives in
`CIN/Public/CommonInputModeTypes.h`.

### A.1 Core widget bases

#### `UCommonUserWidget : UUserWidget` — `CUI/Public/CommonUserWidget.h:33`

The root of every CommonUI widget. No custom `S...` counterpart (plain `SObjectWidget`).
`meta = (DisableNativeTick)` (`:32`).

| Property | Type | Default | Spec |
|---|---|---|---|
| `bDisplayInActionBar` | `bool` | `false` (`:129`) | EditAnywhere, BlueprintReadOnly, Cat=Input |
| `bConsumePointerInput` | `bool` | `false` (`:135`) | EditAnywhere, BlueprintReadOnly, private w/ `AllowPrivateAccess` |

* `RegisterUIActionBinding(const FBindUIActionArgs&) -> FUIActionBindingHandle` `:63` — native entry into action routing.
* Enhanced-Input BP variants: `RegisterUIAction(UInputAction*, bool ShouldDisplayInActionBar)` `:68`, `RegisterUIActionsFromMappingContext(UInputMappingContext*, bool)` `:72`, `RemoveUIAction` `:76`, `RemoveAllUIActionBinding` `:80`.
* `RegisterScrollRecipientExternal` / `Unregister…` `:44`, `:48`; native `RegisterScrollRecipient(const UWidget&, ECommonUIScrollRecipientOwningNodeSource)` `:120`; enum `ECommonUIScrollRecipientOwningNodeSource { ScrollRecipient, RegisteringUserWidget }` `:23` — this is what lets the analog cursor's right stick scroll a specific box.
* Overrides every pointer/touch `Native*` handler `:91-:98` to implement `bConsumePointerInput`.
* State: `TArray<FUIActionBindingHandle> ActionBindings` `:139`, `TArray<TWeakObjectPtr<const UWidget>> ScrollRecipients` `:140`.

#### `UCommonActivatableWidget : UCommonUserWidget` — `CUI/Public/CommonActivatableWidget.h:43`

**The central concept of CommonUI.** A widget that can be activated/deactivated without being
constructed/destructed; each one is a node in the input-routing tree. `meta = (DisableNativeTick)`.

| Property | Type | Default | Category / meta |
|---|---|---|---|
| `bIsBackHandler` | `bool` | `false` (`:189`) | Back |
| `bIsBackActionDisplayedInActionBar` | `bool` | `false` (`:193`) | Back |
| `OverrideBackActionDisplayName` | `FText` | empty (`:197`) | Back |
| `bAutoActivate` | `bool` | `false` (`:201`) | Activation |
| `bSupportsActivationFocus` | `bool` | **`true`** (`:209`) | Activation |
| `bIsModal` | `bool` | `false` (`:216`) | `EditCondition=bSupportsActivationFocus`; treats this node as a routing **root** regardless of parentage |
| `bAutoRestoreFocus` | `bool` | `false` (`:224`) | `EditCondition=bSupportsActivationFocus` |
| `bOverrideActionDomain` | `bool` | `false` (`:227`) | Input, `InlineEditConditionToggle` |
| `InputMapping` | `TObjectPtr<UInputMappingContext>` | null (`:231`) | `EditCondition="CommonInput.CommonInputSettings.IsEnhancedInputSupportEnabled"`, `EditConditionHides` |
| `InputMappingPriority` | `int32` | `0` (`:235`) | same EditCondition |
| `ActionDomainOverride` | `TSoftObjectPtr<UCommonInputActionDomain>` | null (`:241`) | BlueprintReadOnly, `EditCondition=bOverrideActionDomain` |
| `bSetVisibilityOnActivated` | `bool` | `false` (`:287`) | `InlineEditConditionToggle="ActivatedVisibility"` |
| `ActivatedVisibility` | `ESlateVisibility` | `SelfHitTestInvisible` (`:290`) | Activation |
| `bSetVisibilityOnDeactivated` | `bool` | `false` (`:293`) | Activation |
| `DeactivatedVisibility` | `ESlateVisibility` | `Collapsed` (`:296`) | Activation |
| `bIsActive` | `bool` | `false` (`:256`) | BlueprintReadOnly only, private `AllowPrivateAccess` |

Delegates: `FOnWidgetActivationChanged` (dynamic multicast, no params) `:15`; BlueprintAssignable
`BP_OnWidgetActivated` `:249` / `BP_OnWidgetDeactivated` `:253`.
Native: `FSimpleMulticastDelegate OnActivated()` `:94` / `OnDeactivated()` `:95`,
`OnSlateReleased()` `:117`, `OnRequestRefreshFocus()` `:119`, and static
`FActivatableWidgetRebuildEvent OnRebuilding` `:115` — the hook the action router uses to (re)assemble
the tree.

BlueprintImplementableEvent hooks: `BP_GetDesiredFocusTarget` `:157`,
**`BP_GetDesiredInputConfig` -> `FUIInputConfig`** `:165`, `BP_OnActivated` `:168`,
`BP_OnDeactivated` `:173`, `BP_OnHandleBackAction -> bool` `:182`.
Native counterparts `NativeGetDesiredFocusTarget` `:150`, `NativeOnActivated/Deactivated` `:169`/`:174`,
`NativeOnHandleBackAction` `:183`. C++ virtuals:
**`GetDesiredInputConfig() -> TOptional<FUIInputConfig>`** `:108` and
`GetActivationMetadata() -> TOptional<FActivationMetadata>` `:102`.

Other API: `ActivateWidget` `:52` / `DeactivateWidget` `:55`,
`BindVisibilityToActivation(UCommonActivatableWidget*)` `:74` + `SetBindVisibilities(…)` `:65`,
`RequestRefreshFocus()` `:92`, `GetCalculatedActionDomain()` `:131`,
`ResetCalculatedActionDomainCache()` `:137`, `ClearFocusRestorationTarget()` `:82`,
`GetInputTreeNode()` `:124` / `RegisterInputTreeNode()` `:125`. The Slate side is tagged with
`FCommonActivatableSlateMetaData` `:20` so Slate→UMG lookup works.

#### `UCommonUIVisibilitySubsystem : ULocalPlayerSubsystem` — `CUI/Public/CommonUIVisibilitySubsystem.h:22`

Computes an `FGameplayTagContainer` of visibility tags = current input type tag + platform traits +
user tags. `GetVisibilityTags()` `:44`, `HasVisibilityTag()` `:49`,
`AddUserVisibilityCondition` / `Remove` `:51`/`:52`, event `OnVisibilityTagsChanged` `:38`, dynamic
`FHardwareVisibilityTagsChangedDynamicEvent` `:19`. Editor-only
`SetDebugVisibilityConditions(TagsToEnable, TagsToSuppress)` `:55`.
Implementation `CUI/Private/CommonUIVisibilitySubsystem.cpp:106-131`: adds
`TAG_INPUT_MOUSEANDKEYBOARD` / `TAG_INPUT_GAMEPAD` / `TAG_INPUT_TOUCH` per `GetCurrentInputType()`,
then appends `ICommonUIModule::GetSettings().GetPlatformTraits()`.

#### `UCommonUISubsystemBase : UGameInstanceSubsystem` — `CUI/Public/CommonUISubsystemBase.h:21`

Glyph lookup + analytics. `GetInputActionButtonIcon(FDataTableRowHandle, ECommonInputType, FName GamepadName) -> FSlateBrush` `:36`,
`GetEnhancedInputActionButtonIcon(UInputAction*, ULocalPlayer*)` `:40`, `SetAnalyticProvider` `:32`,
`FireEvent_ButtonClicked` `:45` / `FireEvent_PanelPushed` `:48`,
`SetInputAllowed(bool, FName Reason, const ULocalPlayer&)` `:50` / `IsInputAllowed` `:51`.

### A.2 Styled widgets — styles as UObject assets

CommonUI's styling model: a style is an **abstract Blueprintable `UObject` subclass**, referenced by
widgets as `TSubclassOf<…>`; the CDO is handed to Blueprint. Hence the comment repeated in every
style header — *"All properties must be EditDefaultsOnly, BlueprintReadOnly !!! … we return the CDO
to blueprints"* (`CUI/Public/CommonButtonBase.h:65`, `CUI/Public/CommonTextBlock.h:18`,
`CUI/Public/CommonBorder.h:13`).

#### `UCommonButtonStyle : UObject` — `CUI/Public/CommonButtonBase.h:69`

`UCLASS(Abstract, Blueprintable, MinimalAPI, ClassGroup=UI)`; `NeedsLoadForServer()` -> false `:73`.
All fields `EditDefaultsOnly, BlueprintReadOnly`:

* `bool bSingleMaterial` `:79`; `FSlateBrush SingleMaterialBrush` `:83` (`EditCondition="bSingleMaterial"`).
* Normal set (`EditCondition="!bSingleMaterial"`): `NormalBase` `:87`, `NormalHovered` `:91`, `NormalPressed` `:95`.
* Selected set: `SelectedBase` `:99`, `SelectedHovered` `:103`, `SelectedPressed` `:107`.
* `Disabled` `:111`.
* Layout: `FMargin ButtonPadding` `:115`, `FMargin CustomPadding` `:119`, `int32 MinWidth` `:123` / `MinHeight` `:127` / `MaxWidth` `:131` / `MaxHeight` `:135`.
* Text styles (`TSubclassOf<UCommonTextStyle>`): `NormalTextStyle` `:139`, `NormalHoveredTextStyle` `:143`, `SelectedTextStyle` `:147`, `SelectedHoveredTextStyle` `:151`, `DisabledTextStyle` `:155`.
* Sounds: `FSlateSound PressedSlateSound` `:159`, `ClickedSlateSound` `:163`, `HoveredSlateSound` `:183`; plus optional-wrapped `FCommonButtonStyleOptionalSlateSound` for `SelectedPressed` `:167`, `SelectedClicked` `:171`, `LockedPressed` `:175`, `LockedClicked` `:179`, `SelectedHovered` `:187`, `LockedHovered` `:191`.
* `FCommonButtonStyleOptionalSlateSound` `:47` = `{ bool bHasSound = false; FSlateSound Sound; }` with `explicit operator bool`.
* ~17 `UFUNCTION(BlueprintCallable)` getters `:193-:236`.

#### `UCommonButtonBase : UCommonUserWidget` — `CUI/Public/CommonButtonBase.h:330`

`UCLASS(Abstract, Blueprintable, ClassGroup=UI, meta=(Category="Common UI", DisableNativeTick))`.
Slate counterpart **`SCommonButton : SButton`** (`CUI/Private/CommonButtonTypes.h:17`) wrapped by
`UCommonButtonInternalBase : UButton` (`CUI/Public/CommonButtonBase.h:243`, marked
`UCLASS(Experimental)` *specifically to hide it from the designer*, `:242`).

Defaults from the ctor at `CUI/Private/CommonButtonBase.cpp:404-430` where not inline.

| Property | Type | Default | Notes |
|---|---|---|---|
| `MinWidth`/`MinHeight`/`MaxWidth`/`MaxHeight` | `int32` | `0` each (cpp `:406-409`) | Cat=Layout, `ClampMin=0` (`:809-822`) |
| `Style` | `TSubclassOf<UCommonButtonStyle>` | null | `ExposeOnSpawn` (`:826`) |
| `bHideInputAction` | `bool` | `false` | Cat=Style (`:830`) |
| 9 × `…SlateSoundOverride` | `FSlateSound` | empty | EditAnywhere/BPRW with **`Setter`**, Cat=Sound (`:837,:843,:850,:854,:858,:862,:866,:870,:874`) |
| `bApplyAlphaOnDisable:1` | bitfield | **`true`** (cpp `:410`) | `ExposeOnSpawn` (`:878`) |
| `bLocked:1` | bitfield | `false` (cpp `:411`) | Cat=Locked (`:886`) — hoverable/focusable/pressable but Click never fires |
| `bSelectable:1` | bitfield | `false` (cpp `:412`) | Cat=Selection (`:890`) |
| `bShouldSelectUponReceivingFocus:1` | bitfield | `false` (cpp `:413`) | `EditCondition=bSelectable` (`:894`) |
| `bInteractableWhenSelected:1` | bitfield | false | `EditCondition=bSelectable` (`:898`) |
| `bToggleable:1` | bitfield | `false` (cpp `:414`) | `EditCondition=bSelectable` (`:902`) |
| `bTriggerClickedAfterSelection:1` | bitfield | `false` (cpp `:415`) | (`:905`) |
| `bDisplayInputActionWhenNotInteractable:1` | bitfield | **`true`** (cpp `:416`) | Cat=Input (`:909`) |
| `bHideInputActionWithKeyboard:1` | bitfield | false | Cat=Input (`:913`) |
| `bShouldUseFallbackDefaultInputAction:1` | bitfield | **`true`** (cpp `:417`) | Cat=Input (`:917`) |
| `bRequiresHold:1` | bitfield | `false` (cpp `:418`) | Cat="Input\|Hold" (`:921`) |
| `HoldData` | `TSubclassOf<UCommonUIHoldData>` | null | `EditCondition="bRequiresHold"` (`:925`) |
| `bSimulateHoverOnTouchInput` | `bool` | **`true`** (`:929`) | AdvancedDisplay, `EditCondition="IsHoverSimulationOnTouchAvailable()"`, `EditConditionHides` |
| `ClickMethod` | `TEnumAsByte<EButtonClickMethod::Type>` | engine default | `ExposeOnSpawn` (`:945`) |
| `TouchMethod` | `TEnumAsByte<EButtonTouchMethod::Type>` | engine default | (`:948`) |
| `PressMethod` | `TEnumAsByte<EButtonPressMethod::Type>` | engine default | (`:951`) |
| `InputPriority` | `int32` | 0 | **legacy CommonUI, "should be removed"** (`:957-960`) |
| `TriggeringInputAction` | `FDataTableRowHandle` | empty | `RowType="/Script/CommonUI.CommonInputActionDataBase"` (`:966`) |
| `TriggeringEnhancedInputAction` | `TObjectPtr<UInputAction>` | null | EI EditCondition (`:973`) |
| `bNavigateToNextWidgetOnDisable:1` | bitfield | `false` (cpp `:423`) | Cat=Input (`:992`) |
| `bIsPersistentBinding` | `bool` | `false` (`:1078`) | private, AdvancedDisplay, doc'd **"DANGER!"** (`:1071-1075`) |
| `InputModeOverride` | `ECommonInputMode` | **`Menu`** (`:1082`) | private, AdvancedDisplay |
| `InputActionWidget` | `TObjectPtr<UCommonActionWidget>` | null | BlueprintReadOnly, `meta=(BindWidget, OptionalWidget=true)` (`:1157`) — the glyph slot |
| `bStyleNoLongerNeedsConversion` | `bool` | — | `WITH_EDITORONLY_DATA` migration flag (`:987`) |

Non-editable but load-bearing: `TriggeredInputAction` (legacy) `:982`,
`FUIActionBindingHandle TriggeringBindingHandle` `:1043`, hold state
`HoldTime`/`HoldRollbackTime`/`CurrentHoldTime`/`CurrentHoldProgress` + two `FTSTicker::FDelegateHandle`
`:1046-:1065`, internally-built `FButtonStyle NormalStyle/SelectedStyle/DisabledStyle/LockedStyle` `:1107-:1119`.

BlueprintAssignable delegates (Cat="Events", `AllowPrivateAccess`): `OnSelectedChangedBase`
(`FCommonSelectedStateChangedBase`) `:996`; `OnButtonBaseClicked` / `…DoubleClicked` / `…Hovered` /
`…Unhovered` / `…Focused` / `…Unfocused` / `…LockClicked` / `…LockDoubleClicked` / `…Selected` /
`…Unselected` (all `FCommonButtonBaseClicked`) `:999-:1026`; drag/drop `OnButtonBaseDragDetected` /
`DragEnter` / `DragOver` / `Drop` (`FOnButtonBaseGeoOperationDynamic`) + `DragLeave`
(`FOnButtonBaseOperationDynamic`) `:1029-:1041`. Native `FCommonButtonEvent` accessors for the same
set `:544-:560`, plus `FOnIsSelectedChanged` `:565` and `FWidgetEventField ClickEvent` with
`FieldNotify` `:563`.

23 BlueprintImplementableEvent hooks: `BP_OnSelected` `:668` / `BP_OnDeselected` `:672`,
`BP_OnHovered` `:676` / `BP_OnUnhovered` `:680`, `BP_OnFocusReceived` `:684` / `BP_OnFocusLost` `:687`,
`BP_OnLockedChanged(bool)` `:690`, `BP_OnLockClicked` `:693` / `BP_OnLockDoubleClicked` `:696`,
`BP_OnClicked` `:699` / `BP_OnDoubleClicked` `:703`, `BP_OnInputActionTriggered` `:707`,
`BP_OnPressed` `:714` / `BP_OnReleased` `:718`, `BP_OnEnabled` `:732` / `BP_OnDisabled` `:736`,
`BP_OnInputMethodChanged(ECommonInputType)` `:740`, `OnCurrentTextStyleChanged` `:744`,
`BP_OnRequiresHoldChanged` `:748`, `OnTriggeredInputActionChanged` `:756` /
`OnTriggeringInputActionChanged` `:760` / `OnTriggeringEnhancedInputActionChanged` `:764`,
**`OnActionProgress(float HeldPercent)`** `:768`, **`OnActionComplete()`** `:791`.

Hold machinery on the button: `GetRequiresHold()` `:488`, `SetRequiresHold` `:492`,
`GetRequiredHoldTime()` `:496`, `GetConvertInputActionToHold()` `:775`,
`NativeOnActionProgress(float)` `:779`, `NativeOnHoldProgress(float)` `:783` /
`NativeOnHoldProgressRollback(float)` `:787`, `HoldReset()` `:797`,
`SetInputActionProgressMaterial(FSlateBrush, FName)` `:515`.

Also here: `UWidgetLockedStateRegistration : UWidgetBinaryStateRegistration` `:1162` exposing the
button's `"Locked"` state `:1171` to the engine widget-state binding system (§0.4).

`UCommonButtonInternalBase` `:243` adds `SetButtonEnabled` / `SetInteractionEnabled` /
`SetButtonFocusable` `:248-:252`, min/max desired-size setters `:256-:263`,
`FOnButtonDoubleClickedEvent HandleDoubleClicked` `:268`, BlueprintAssignable `OnDoubleClicked` `:272`;
private `MinWidth`/`MinHeight`/`MaxWidth`/`MaxHeight`/`bButtonEnabled`/`bInteractionEnabled`
`:292-:312` (ctor defaults `bButtonEnabled=true, bInteractionEnabled=true`, cpp `:181-192`, and the
transparent `"NoBorder"` `FButtonStyle`).

#### `UCommonTextStyle : UObject` — `CUI/Public/CommonTextBlock.h:22`

`UCLASS(Abstract, Blueprintable)`. Fields `EditDefaultsOnly, BlueprintReadOnly`; defaults from
`CUI/Private/CommonTextBlock.cpp:29-34`: `FSlateFontInfo Font` `:31`,
`FLinearColor Color` = **`FLinearColor::Black`** `:35`, `bool bUsesDropShadow` `:39`,
`FVector2D ShadowOffset` `:43` (EditCondition), `FLinearColor ShadowColor` `:47` (EditCondition),
`FMargin Margin` `:51`, `FSlateBrush StrikeBrush` `:55`,
`float LineHeightPercentage` = **`1.0f`** `:59`, `bool ApplyLineHeightToBottomLine` = **`true`** `:63`.
Converters `ToTextBlockStyle(FTextBlockStyle&)` `:89` / `ApplyToTextBlock(TSharedRef<STextBlock>)` `:91`
+ 7 BP getters `:66-:87`.

#### `UCommonTextScrollStyle : UObject` — `CUI/Public/CommonTextBlock.h:99`

`float Speed`, `StartDelay`, `EndDelay`, `FadeInDelay`, `FadeOutDelay` `:107-:119` (no inline
defaults), `EWidgetClipping Clipping = OnDemand` `:122`. `ToScrollOptions() -> FTextScrollerOptions` `:104`.

#### `UCommonTextBlock : UTextBlock` — `CUI/Public/CommonTextBlock.h:129`

`Config=CommonUI, DefaultConfig`, `DisplayName="Common Text"`, `PrioritizeCategories="Content"`.
Slate: `STextBlock` wrapped in **`STextScroller`** `:227`.

| Property | Type | Default | Spec |
|---|---|---|---|
| `MobileFontSizeMultiplier` | `float` | `1.0f` (`:184`) | Getter/Setter, `ClampMin=0.01, ClampMax=5.0` |
| `bIsScrollingEnabled` | `bool` | `true` (`:195`) | private, `ExposeOnSpawn` |
| `bDisplayAllCaps_DEPRECATED` | `bool` | `false` (`:199`) | **`DeprecatedProperty`** → use `TextTransformPolicy` |
| `bAutoCollapseWithEmptyText` | `bool` | `false` (`:203`) | private |
| `Style` | `TSubclassOf<UCommonTextStyle>` | null (`:207`) | `ExposeOnSpawn` |
| `ScrollStyle` | `TSubclassOf<UCommonTextScrollStyle>` | null (`:211`) | null disables scrolling |
| `ScrollOrientation` | `TEnumAsByte<EOrientation>` | `Orient_Horizontal` (`:215`) | |
| `bStyleNoLongerNeedsConversion` | `bool` | — | `WITH_EDITORONLY_DATA` (`:220`) |

Ctor forces `SelfHitTestInvisible` + `Clipping = Inherit` (cpp `:108-113`).

#### `UCommonBorderStyle` / `UCommonBorder` — `CUI/Public/CommonBorder.h:17` / `:36`

Style: one field `FSlateBrush Background` `:26` + `GetBackgroundBrush` `:29`.
`UCommonBorder : UBorder`, `Config=CommonUI, DefaultConfig`:
`TSubclassOf<UCommonBorderStyle> Style` `:46` (`ExposeOnSpawn`),
`bool bReducePaddingBySafezone = false` `:50` (ctor `CUI/Private/CommonBorder.cpp:26`),
`FMargin MinimumPadding = 0` `:54` (EditCondition; cpp `:27`), editor-only
`bStyleNoLongerNeedsConversion` `:59`. Ctor sets `Background.DrawAs = NoDrawType` and
`SelfHitTestInvisible` (cpp `:29-31`). Reacts to safe-area changes via `SafeAreaUpdated()` `:71`.

#### `UCommonRichTextBlock : URichTextBlock` — `CUI/Public/CommonRichTextBlock.h:35`

Enum `ERichTextInlineIconDisplayMode { IconOnly, TextOnly, IconAndText, MAX }` `:20`.
Props: `InlineIconDisplayMode` `:61`, `bool bTintInlineIcon = false` `:65`,
`float MobileTextBlockScale = 1.0f` `:82` (`ClampMin=0.01 ClampMax=5`),
`TSubclassOf<UCommonTextStyle> DefaultTextStyleOverrideClass` `:85` (`EditCondition=bOverrideDefaultStyle`),
`TSubclassOf<UCommonTextScrollStyle> ScrollStyle` `:89`,
`TEnumAsByte<EOrientation> ScrollOrientation = Orient_Horizontal` `:93`,
`bool bIsScrollingEnabled = true` `:97`, `bDisplayAllCaps_DEPRECATED` `:101`,
`bool bAutoCollapseWithEmptyText = false` `:105`. Static `EscapeStringForRichText(FString)` `:66`.

#### `UCommonUIRichTextData : UObject` — `CUI/Public/CommonUIRichTextData.h:31`

`UCLASS(Abstract, Blueprintable)`. `TObjectPtr<UDataTable> InlineIconSet` `:43`
(`RowType="/Script/CommonUI.RichTextIconData"`). Row struct `FRichTextIconData : FTableRowBase` `:12`:
`FText DisplayName` `:17`, `TSoftObjectPtr<UObject> ResourceObject` `:20` (allows
Texture2D/MaterialInterface/SlateTextureAtlasInterface, **disallows MediaTexture**),
`FVector2D ImageSize = (64,64)` `:23`. Static `Get()` `:36`, `FindIcon(FName)` `:38`.

#### `UCommonNumericTextBlock : UCommonTextBlock` — `CUI/Public/CommonNumericTextBlock.h:67`

`FCommonNumberFormattingOptions` `:11`, ctor defaults `:15-23`: `RoundingMode = HalfFromZero`,
`AlwaysSign = false`, `UseGrouping = true`, `Minimum/MaximumIntegralDigits` from
`FNumberFormattingOptions::DefaultNoGrouping()`, `MinimumFractionalDigits = 0`,
`MaximumFractionalDigits = 0`. Enum `ECommonNumericType { Number, Percentage, Seconds, Distance }` `:55`.
Widget props: `float CurrentNumericValue` `:132`, `ECommonNumericType NumericType` `:138`,
`FCommonNumberFormattingOptions FormattingSpecification` `:142`,
`float EaseOutInterpolationExponent` `:146` (`ClampMin=1.0`),
`float InterpolationUpdateInterval` `:150` (`ClampMin=0.0`),
`float PostInterpolationShrinkDuration` `:158`, `bool PerformSizeInterpolation` `:161`;
deprecated `IsPercentage_DEPRECATED` `:189`.
BlueprintAssignable: `OnInterpolationStartedEvent` `:113`,
`OnInterpolationUpdatedEvent(NumericTextBlock, LastValue, NewValue)` `:118`, `OnOutroEvent` `:123`,
`OnInterpolationEndedEvent(NumericTextBlock, HadCompleted)` `:128`.
`InterpolateToValue(Target, MaxDuration = 3.0, MinChangeRate = 1.0, OutroOffset = 0.0)` `:105`.

#### `UCommonDateTimeTextBlock : UCommonTextBlock` — `CUI/Public/CommonDateTimeTextBlock.h:11`

`FText CustomTimespanFormat` `:57` (supports `{Days}{Hours}{Minutes}{Seconds}`),
`bool bCustomTimespanLeadingZeros = false` `:63`.
API `SetDateTimeValue(FDateTime, bShowAsCountdown, InRefreshDelay = 1.0f)` `:28`,
`SetTimespanValue` `:31`, `SetCountDownCompletionText` `:34`. Native event
`FOnTimeCountDownCompletion` `:18`. Virtuals `FormatTimespan` `:46` / `FormatDateTime` `:47` /
`ShouldClearTimer` `:44`.

### A.3 Switchers, containers, stacks

#### `SCommonAnimatedSwitcher : SWidgetSwitcher` — `CUI/Public/Slate/SCommonAnimatedSwitcher.h:75`

Enums declared here and used across the plugin:

* `ECommonSwitcherTransition { FadeOnly, Horizontal, Vertical, Zoom }` `:13`
* `ETransitionCurve { Linear, QuadIn, QuadOut, QuadInOut, CubicIn, CubicOut, CubicInOut }` `:26`
* `ECommonSwitcherTransitionFallbackStrategy { None, Previous, Next, First, Last }` `:46`

SLATE_ARGS defaults `:81-89`: `InitialIndex=0`, `TransitionType=FadeOnly`,
`TransitionCurveType=CubicInOut`, `TransitionDuration=0.4f`, `TransitionFallbackStrategy=Previous`,
`Visibility=SelfHitTestInvisible`. Events `OnActiveIndexChanged(int32)` `:97`,
`OnIsTransitioningChanged(bool)` `:98`. `TransitionToIndex(int32, bool bInstantTransition = false)` `:106`;
the transition sequence plays **twice per transition** (out then in) `:135`, `:148`.

#### `UCommonAnimatedSwitcher : UWidgetSwitcher` — `CUI/Public/CommonAnimatedSwitcher.h:22`

Props (ctor defaults `CUI/Private/CommonAnimatedSwitcher.cpp:12-19`):
`ECommonSwitcherTransition TransitionType = FadeOnly` `:85`,
`ETransitionCurve TransitionCurveType = CubicInOut` `:89`, `float TransitionDuration = 0.4f` `:93`,
`ECommonSwitcherTransitionFallbackStrategy TransitionFallbackStrategy = None` `:97`.
BlueprintAssignable `FOnActiveIndexChangedDelegate OnActiveWidgetIndexChangedBP(UWidget*, int32)` `:16`,`:80`;
native `OnActiveWidgetIndexChanged` `:72`, `OnTransitioningChanged(bool)` `:76`.
BP API: `ActivateNextWidget(bCanWrap)` `:39` / `ActivatePreviousWidget(bCanWrap)` `:42`,
`HasWidgets` `:45`, `SetDisableTransitionAnimation` `:48`, `IsCurrentlySwitching` `:51`,
`IsTransitionPlaying` `:55`.

#### `UCommonActivatableWidgetSwitcher : UCommonAnimatedSwitcher` — `CUI/Public/CommonActivatableWidgetSwitcher.h:18`

Activates/deactivates the `UCommonActivatableWidget` in the active slot and follows the owning
activatable's activation. One property: `bool bClearFocusRestorationTargetOfDeactivatedWidgets = false` `:33`.

#### `UCommonActivatableWidgetContainerBase : UWidget` — `CUI/Public/Widgets/CommonActivatableWidgetContainer.h:24`

`UCLASS(Abstract)`. Deliberately **not** a panel — "intentionally meant to be black boxes that do not
expose child/slot modification" `:21`. Slate: an `SOverlay` containing `SCommonAnimatedSwitcher` plus
an `SSpacer` **input guard** consumed during transitions `:142-:144`.

| Property | Type | Default | Category |
|---|---|---|---|
| `TransitionType` | `ECommonSwitcherTransition` | enum 0 = FadeOnly (`:108`) | Transition |
| `TransitionCurveType` | `ETransitionCurve` | enum 0 = Linear (`:112`) | Transition |
| `TransitionDuration` | `float` | `0.4f` (`:116`) | Transition |
| `bResetPoolWhenReleasingSlateResources` | `bool` | `false` (`:124`) | Performance |
| `TransitionFallbackStrategy` | `ECommonSwitcherTransitionFallbackStrategy` | `None` (`:131`) | Transition, BPRO |

Pooling: `FUserWidgetPool GeneratedWidgetsPool` `:140` — **every widget added by class is pooled**, so
callers must not cache instances `:147-155`. API: templated `AddWidget<T>(TSubclassOf<…>)` `:33` and
`AddWidget<T>(class, InstanceInitFunc)` `:49`,
`AddWidgetInstance(UCommonActivatableWidget&)` (legacy, unpooled) `:68`, `RemoveWidget` `:70`,
`GetActiveWidget` `:73`, `GetWidgetList` `:75`, `ClearWidgets` `:80`,
`SetTransitionDuration` `:83` / `GetTransitionDuration` `:85`. The BP node is `BP_AddWidget` displayed
as **"Push Widget"** `:156`. Events `FOnDisplayedWidgetChanged` `:87`,
`FTransitioningChanged OnTransitioningChanged` `:90`. A one-frame `ReleasedWidgets` retention hack
keeps the hittest grid honest `:169-183`.

#### `UCommonActivatableWidgetStack` — `…/CommonActivatableWidgetContainer.h:202`

Top of stack is displayed + activated; when it deactivates it is auto-removed and the preceding entry
activates `:196-199`. Property `TSubclassOf<UCommonActivatableWidget> RootContentWidgetClass` `:217` —
an auto-generated permanent root that can never be removed; accessor `GetRootContent()` `:208`.

#### `UCommonActivatableWidgetQueue` — `…/CommonActivatableWidgetContainer.h:235`

Same pooling; one active at a time; on deactivation the entry is removed, released to the pool, and
the next queued entry displays `:230-233`. No properties of its own.

#### `UCommonVisibilitySwitcher : UOverlay` — `CUI/Public/CommonVisibilitySwitcher.h:15`

Toggles child visibility rather than reparenting; activates the visible child if it is activatable.
`meta=(DisableNativeTick)`. Props: `ESlateVisibility ShownVisibility = SelfHitTestInvisible` `:88`,
`int32 ActiveWidgetIndex = 0` `:91` (`ClampMin=-1`), `bool bAutoActivateSlot = true` `:95`,
`bool bActivateFirstSlotOnAdding = false` `:99`.
BP API: `SetActiveWidgetIndex` `:22`, `GetActiveWidget` `:28`, `SetActiveWidget` `:31`,
`IncrementActiveWidgetIndex` / `DecrementActiveWidgetIndex(bAllowWrapping = true)` `:34`/`:37`,
`ActivateVisibleSlot` / `DeactivateVisibleSlot` `:40`/`:43`, `IsCurrentlySwitching` `:46`.
Native `FOnActiveWidgetIndexChanged(int32)` `:54`.
Uses `UCommonVisibilitySwitcherSlot : UOverlaySlot` (`CUI/Public/CommonVisibilitySwitcherSlot.h:17`)
which wraps each child in an `SBox` `:35` so visibility is per-slot (`SetSlotVisibility` `:29`).

#### `UCommonWidgetCarousel : UPanelWidget` — `CUI/Public/CommonWidgetCarousel.h:23`

Slate: `SWidgetCarousel<UPanelSlot*>` `:125`. Props: `int32 ActiveWidgetIndex` `:30`
(`UIMin/ClampMin=0`), `float MoveSpeed` `:36` (Getter/Setter + BlueprintGetter/Setter),
`bool bCacheChildren` `:40` (Getter/Setter). BlueprintAssignable
`FOnCurrentPageIndexChanged(UCommonWidgetCarousel*, int32)` `:17`,`:89`.
API `NextPage` `:67` / `PreviousPage` `:70`, `BeginAutoScrolling(ScrollInterval = 10)` `:61` /
`EndAutoScrolling` `:64`.

#### `UCommonWidgetCarouselNavBar : UWidget` — `CUI/Public/CommonWidgetCarouselNavBar.h:19`

`TSubclassOf<UCommonButtonBase> ButtonWidgetType` `:26`, `FMargin ButtonPadding` `:29`,
`SetLinkedCarousel(UCommonWidgetCarousel*)` `:36`. Builds an `SHorizontalBox` of buttons managed by an
internal `UCommonButtonGroupBase` `:61`,`:67`,`:70`.

#### `UCommonTabListWidgetBase : UCommonUserWidget` — `CUI/Public/CommonTabListWidgetBase.h:51`

`UCLASS(Abstract, Blueprintable, DisableNativeTick)`. Drives a linked `UCommonAnimatedSwitcher`.
`FCommonRegisteredTabInfo` `:21`: `int32 TabIndex`, `TSubclassOf<UCommonButtonBase> TabButtonClass`,
`TObjectPtr<UCommonButtonBase> TabButton`, `TObjectPtr<UWidget> ContentInstance` `:28-:40`.

| Property | Type | Default | Notes |
|---|---|---|---|
| `NextTabInputActionData` | `FDataTableRowHandle` | empty (`:196`) | `RowType=CommonInputActionDataBase` |
| `PreviousTabInputActionData` | `FDataTableRowHandle` | empty (`:200`) | |
| `NextTabEnhancedInputAction` | `TObjectPtr<UInputAction>` | null (`:204`) | EI EditCondition |
| `PreviousTabEnhancedInputAction` | `TObjectPtr<UInputAction>` | null (`:208`) | EI EditCondition |
| `bAutoListenForInput` | `bool` | `false` (cpp `CUI/Private/CommonTabListWidgetBase.cpp:16`) | `ExposeOnSpawn` (`:212`) |
| `bShouldWrapNavigation` | `bool` | `true` (`:216`) | `ExposeOnSpawn` |
| `bDeferRebuildingTabList` | `bool` | `false` (cpp `:17`) | (`:223`) |

BlueprintAssignable: `OnTabSelected(FName)` `:61`, `OnTabButtonCreation(FName, UCommonButtonBase*)` `:68`,
`OnTabButtonRemoval(FName, UCommonButtonBase*)` `:75`, `OnTabListRebuilt()` `:82`.
BlueprintImplementableEvent: `HandlePreLinkedSwitcherChanged_BP` `:179`,
`HandlePostLinkedSwitcherChanged_BP` `:184`. **BlueprintNativeEvent**:
`HandleTabCreation(FName, UCommonButtonBase*)` `:189`, `HandleTabRemoval(FName, UCommonButtonBase*)` `:192`.
API: `SetLinkedSwitcher` `:93`, `RegisterTab(TabNameID, ButtonWidgetType, ContentWidget, TabIndex = -1)` `:108`,
`RemoveTab` `:111` / `RemoveAllTabs` `:114`, `SelectTabByID(FName, bSuppressClickFeedback = false)` `:124`,
`SetTabVisibility` `:134` / `SetTabEnabled` `:138` / `SetTabInteractionEnabled` `:142` /
`DisableTabWithReason` `:146`, `SetListeningForInput(bool)` `:149`, `RegisterTabContentWidget` `:161`,
`SetSelectionRequired` `:164`. Internals: `FUserWidgetPool TabButtonWidgetPool` `:262`,
`UCommonButtonGroupBase TabButtonGroup` `:243`, two `FUIActionBindingHandle` for next/prev tab `:270`,`:271`.

### A.4 Lists and scrolling

* **`UCommonListView : UListView`** — `CUI/Public/CommonListView.h:435`. The only extra BP API is `SetEntrySpacing(float)` `:443`. The value is the templated **`SCommonListView<ItemType> : SListView<ItemType>`** `:22`: gamepad-first list behaviour — `OnFocusReceived` selects item 0 (or the remembered selection) and `RequestNavigateToItem` `:25-97`; proximate-entry spatial navigation (`bEnableProximateEntryNavigation` `:33`); **intra-entry** navigation routing focus between focusable sub-widgets inside one row via the hittest grid before advancing selection `:213-254`; cross-entry proximate focus incl. deferral to `NotifyItemScrolledIntoView` when the target row is culled `:271-380`; touch fixes (`OnMouseLeave` keeps `bStartedTouchInteraction` `:388-398`, `OnTouchMoved` steals user focus `:400-411`).
* **`UCommonTileView : UTileView`** — `CUI/Public/CommonTileView.h:16` (`DisableNativeTick`); value `SCommonTileView<ItemType> : STileView<ItemType>` (`CUI/Public/SCommonTileView.h:12`), same proximate/intra-entry machinery `:23`,`:191`,`:399`.
* **`UCommonTreeView : UTreeView`** — `CUI/Public/CommonTreeView.h:92`; `SCommonTreeView<ItemType> : STreeView<ItemType>` `:17` with the same focus-selects-first-item behaviour `:20-49` and touch scroll fixes `:51-78`.
* **`SCommonButtonTableRow<ItemType> : SObjectTableRow<ItemType>`** — `CUI/Public/SCommonButtonTableRow.h:15`. Assumes its entry **is** a `UCommonButtonBase` and delegates all pointer handling to it `:12`,`:72-77`. On construct it *overrides* the entry button's own settings from the list's `ESelectionMode`: `SetIsToggleable(SingleToggle||Multi)`, `SetIsSelectable(≠None)`, `SetIsInteractableWhenSelected(≠None)`, `SetAllowDragDrop`, `SetTouchMethod(PreciseTap)` `:63-69`. SLATE args `bAllowDragging=true`, `bAllowKeepPreselectedItems=true`, `bAllowDragDrop`, `OnHovered`/`OnUnhovered`, six drag-drop events `:18-36`.
* **`FCommonNativeListItem`** — `CUI/Public/CommonNativeListItem.h:13`. Non-UObject list items with hand-rolled RTTI (`IsDerivedFrom<T>()` `:18`, `AsTypedItem<T>()` `:26`) and the `DERIVED_LIST_ITEM(ItemType, ParentItemType)` macro `:46` — a `dynamic_cast` substitute that does not need C++ RTTI `:59`.
* **`UCommonHierarchicalScrollBox : UScrollBox`** — `CUI/Public/CommonHierarchicalScrollBox.h:14`; value `SCommonHierarchicalScrollBox : SScrollBox` (`CUI/Public/SCommonHierarchicalScrollBox.h:8`) — a near-copy of `SScrollBox::OnNavigation` with a recursive `AppendFocusableChildren` so navigation descends into nested hierarchies `:13-20`.
* **`UCommonCustomNavigation : UBorder`** — `CUI/Public/CommonCustomNavigation.h:14`. One bindable event `FOnCustomNavigationEvent OnNavigationEvent(EUINavigation) -> bool` `:19`,`:23` (`meta=(IsBindableEvent="True")`) that stomps default border navigation.

### A.5 Loading, lazy and misc widgets

* **`SLoadGuard : SCompoundWidget`** — `CUI/Public/CommonLoadGuard.h:28`. Shows a throbber + message in place of content until an asset loads. Args `ThrobberHAlign=HAlign_Center`, `Throbber`, `GuardText`, `GuardTextStyle`, `GuardBackgroundBrush`, event `OnLoadingStateChanged` `:30-45`. `GuardAndLoadAsset(TSoftObjectPtr<UObject>, FOnLoadGuardAssetLoaded)` `:66` + typed template `:69`.
* **`ULoadGuardSlot : UPanelSlot`** — `:101`: `FMargin Padding` `:122`, `HorizontalAlignment = HAlign_Fill` `:125`, `VerticalAlignment = VAlign_Fill` `:128`.
* **`UCommonLoadGuard : UContentWidget`** — `:151`, `Config=Game, DefaultConfig`. Props: `FSlateBrush LoadingBackgroundBrush` `:223`, `FSlateBrush LoadingThrobberBrush` `:227`, `TEnumAsByte<EHorizontalAlignment> ThrobberAlignment` `:231`, `FMargin ThrobberPadding` `:235`, `FText LoadingText` `:239`, `TSubclassOf<UCommonTextStyle> TextStyle` `:243`, config `FSoftObjectPath SpinnerMaterialPath` `:249`, editor-only `bShowLoading = false` `:212` and `bStyleNoLongerNeedsConversion` `:254`. BlueprintAssignable `FOnLoadGuardStateChangedDynamic BP_OnLoadingStateChanged(bool)` `:246`.
* **`UCommonLazyImage : UImage`** — `CUI/Public/CommonLazyImage.h:25`. Wraps `SLoadGuard` `:111` for image-only async loading. Props: editor-only `bShowLoading = false` `:91`, `FSlateBrush LoadingBackgroundBrush` `:95`, `FSlateBrush LoadingThrobberBrush` `:98`, `FName MaterialTextureParamName` `:106` (AdvancedDisplay). BlueprintAssignable `BP_OnLoadingStateChanged` `:109`. API `SetBrushFromLazyTexture` `:38`, `SetBrushFromLazyMaterial` `:42`, `SetBrushFromLazyDisplayAsset` `:46`, `IsLoading` `:49`, `SetMaterialTextureParamName` `:59`.
* **`UCommonLazyWidget : UWidget`** — `CUI/Public/CommonLazyWidget.h:21`. Async-loads and instantiates a `UUserWidget` class. Props `TSoftClassPtr<UUserWidget> WidgetClass` `:125`, `FSlateBrush LoadingThrobberBrush` `:129`, `FSlateBrush LoadingBackgroundBrush` `:132`; BlueprintAssignable `BP_OnLoadingStateChanged` `:141`; native `FOnLazyContentChangedEvent(UUserWidget*)` `:15`,`:62`. API `SetLazyContent` `:30`, `SetLazyContentWithCallback(…, FOnWidgetCreated)` `:34`, `LoadLazyContent()` `:38` + templated init-func overload `:41`, `GetContent()` `:51`, `IsLoading()` `:60`.
* **`UCommonVideoPlayer : UWidget`** — `CUI/Public/CommonVideoPlayer.h:22`. Props `TObjectPtr<UMediaSource> Video` `:94`, `bool bMatchSize = false` `:98`. Full transport API `SetVideo`/`Seek`/`Close`/`SetPlaybackRate`/`SetLooping`/`SetIsMuted`/`SetShouldMatchSize`/`Play`/`Reverse`/`Pause`/`PlayFromStart`/`GetVideoDuration`/`GetPlaybackTime`/`GetPlaybackRate`/`IsLooping`/`IsPaused`/`IsPlaying`/`IsMuted` `:31-:69`. Native `OnPlaybackResumed`/`Paused`/`Complete` `:71-:73`. Renders through an internal `UMediaPlayer` + `UMediaTexture` + `UMaterial` + `UMediaSoundComponent` into an `SImage` `:101-:121`. **This is the only true "media widget" in the UE UI stack** — base UMG has none.
* **`UCommonVisualAttachment : USizeBox`** — `CUI/Public/CommonVisualAttachment.h:15`. Zero-size attachment (icon left of a label without changing the label's computed size, `:12`). Prop `FVector2D ContentAnchor` `:25` (Getter/Setter; direct access **`UE_DEPRECATED(5.4)`** `:22`). Slate `SVisualAttachmentBox : SBox` (`CUI/Public/SVisualAttachmentBox.h:12`) overriding `ComputeDesiredSize`/`OnArrangeChildren`/`OnPaint` `:17-19`.
* **`UAnalogSlider : USlider`** — `CUI/Public/AnalogSlider.h:22`. Adds BlueprintAssignable `FOnFloatValueChangedEvent OnAnalogCapture` `:29` and reacts to input-method changes `:40`. Slate `SAnalogSlider : SSlider` (`CUI/Public/SAnalogSlider.h:14`) — SLATE arg defaults `IndentHandle=true, Locked=false, Orientation=Orient_Horizontal, SliderBarColor=White, SliderHandleColor=White, Style=CoreStyle "Slider", StepSize=0.01f, Value=1.f, IsFocusable=true` `:18-32`; overrides `OnAnalogValueChanged` `:91` and `OnNavigation` `:92` to step by analog stick, rate-limited by `LastAnalogStepTime` `:103`.
* **`UCommonRotator : UCommonButtonBase`** — `CUI/Public/CommonRotator.h:29`, `DisableNativeTick`. Left/right cycling text selector. Enum `ERotatorDirection { Right, Left }` `:16`. BlueprintAssignable `FOnRotatedWithDirection(int32, ERotatorDirection)` `:70` and **`UE_DEPRECATED(5.4)` `FOnRotated(int32)`** `:73-75`. Native `FRotationEvent(int32, bool bUserInitiated)` `:77`. BP hooks `BP_OnOptionsPopulated(int32)` `:86`, `BP_OnOptionSelected(int32)` `:89`. Requires a bound `UCommonTextBlock MyText` `:95` (`BindWidget`). API `PopulateTextLabels(TArray<FText>)` `:44`, `GetSelectedText` `:48`, `SetSelectedItem` `:52`, `ShiftTextLeft` `:60` / `ShiftTextRight` `:64`.
* **`ICommonPoolableWidgetInterface`** — `CUI/Public/CommonPoolableWidgetInterface.h:19`. Two BlueprintNativeEvent hooks: `OnAcquireFromPool()` `:25`, `OnReleaseToPool()` `:28`. Honoured by `TWidgetFactory` (`CUI/Public/WidgetFactory.h:163`,`:174`,`:202`).
* **`TWidgetFactory<WidgetType> : FGCObject`** — `CUI/Public/WidgetFactory.h:14`. CommonUI's widget pool: `PreConstruct(int32)` `:86`, `Acquire()` `:122`, `Release(WidgetType*)` `:172`, `Reset(bReleaseSlate, bMoveToInactive)` `:194`, `TakeAndCacheWidget` `:242` / `TakeAndCacheRow` `:256`. Gated by `#define ENABLE_WIDGET_FACTORY_POOLING 1` `:11`. Alias `FUserWidgetFactory` `:284`.
* **`UCommonUILibrary : UBlueprintFunctionLibrary`** — `CUI/Public/CommonUILibrary.h:15`: `RefreshFocusIfLeafmostDescendant(UWidget*)` `:30`, `FindParentWidgetOfType` `:36`, `FindParentWidgetImplementingInterface` `:42`, autocasts `Conv_UITagToGameplayTag` `:48` / `Conv_UIActionTagToGameplayTag` `:54`.
* **`CommonUIUtils` namespace** — `CUI/Public/CommonUIUtils.h:12`: `UISmallTextScaling` `:14`, `ShouldDisplayMobileUISizes()` `:27`, `ShouldScaleText()` `:29`, templated `GetOwningUserWidget<T>()` walking `UWidgetTree` outers `:37`, `PrintAllOwningUserWidgets` `:70`, editor-only `ValidateBoundWidgetHierarchy(…, ECollisionPolicy { Allow, Require, Forbid }, …)` for compile-time widget-tree validation `:17`,`:89`.
* **`DEFINE_PASSKEY(FriendClass, PropertyType, PropertyName)`** — `CUI/Public/Misc/Passkey.h:12`. Single-function friend-access idiom, explicitly documented as last-resort `:9-10`.
* **`ICommonUIModule`** — `CUI/Public/ICommonUIModule.h:16`: `GetSettings() -> UCommonUISettings&` `:45`, editor-only `GetEditorSettings()` `:57`, `GetStreamableManager()` `:67`, `GetLazyLoadPriority()` `:70`.

### A.6 Groups

* **`UCommonWidgetGroupBase : UObject`** — `CUI/Public/Groups/CommonWidgetGroupBase.h:14`, `UCLASS(Abstract, BlueprintType)`. `GetWidgetType()` `:21`, `AddWidget`/`AddWidgets`/`RemoveWidget`/`RemoveAll` `:24-:33`, pure virtuals `OnWidgetAdded`/`OnWidgetRemoved`/`OnRemoveAll` `:45-47`.
* **`UCommonButtonGroupBase : UCommonWidgetGroupBase`** — `CUI/Public/Groups/CommonButtonGroupBase.h:21`. Enforces ≤1 (optionally exactly 1) selected button. Property `bool bSelectionRequired` `:141` (`ExposeOnSpawn`).
  BlueprintAssignable + matching native `FNativeSimpleButtonBaseGroupDelegate` pairs:
  `OnSelectedButtonLostSelection` `:94`, `OnSelectedButtonBaseChanged` `:97`, `OnHoveredButtonBaseChanged` `:101`,
  `OnButtonBaseClicked` `:105`, `OnButtonBaseDoubleClicked` `:109`, `OnSelectionCleared` `:115`,
  `OnButtonBaseLockClicked` `:119`, `OnButtonBaseLockDoubleClicked` `:123`.
  API: `SetSelectionRequired` `:35`, `DeselectAll` `:41`,
  `SelectNextButton(bAllowWrap = true, bAllowSound = true)` `:49` / `SelectPreviousButton` `:57`,
  `SelectButtonAtIndex(idx, bAllowSound = true)` `:65`, `GetSelectedButtonIndex` `:72` /
  `GetHoveredButtonIndex` `:79`, `FindButtonIndex` `:87`, `ForEach` `:89`,
  `GetButtonBaseAtIndex` / `GetSelectedButtonBase` / `HasAnyButtons` / `GetButtonCount` `:127-:136`.

### A.7 Hardware / platform-conditional visibility

* **`UCommonHardwareVisibilityBorder : UCommonBorder`** — `CUI/Public/CommonHardwareVisibilityBorder.h:19`. Props `FGameplayTagQuery VisibilityQuery` `:29` (`meta=(Categories="Input,Platform.Trait")`), `ESlateVisibility VisibleType` = **`SelfHitTestInvisible`** `:32` (ctor `CUI/Private/CommonHardwareVisibilityBorder.cpp:14`), `ESlateVisibility HiddenType` = **`Collapsed`** `:35` (cpp `:15`). Ctor also zeroes padding (cpp `:17`). `UpdateVisibility` matches the query against `UCommonUIVisibilitySubsystem::GetVisibilityTags()`, falling back to `ICommonUIModule::GetSettings().GetPlatformTraits()` when there is no local player (cpp `:27-47`).
* **`UDEPRECATED_UCommonVisibilityWidgetBase : UCommonBorder`** — `CUI/Public/CommonVisibilityWidgetBase.h:18`, `UCLASS(Deprecated)`. The pre-tag-query version: `TMap<FName,bool> VisibilityControls` `:24` (`EditFixedSize`, `GetOptions=GetRegisteredPlatforms`), `bShowForGamepad` `:27`, `bShowForMouseAndKeyboard` `:30`, `bShowForTouch` `:33`, `VisibleType` `:36`, `HiddenType` `:39`.
* **`UCommonUISettings : UObject`** — `CUI/Public/CommonUISettings.h:33`, `config=Game, defaultconfig`. Enum `ECommonButtonAcceptKeyHandling { Ignore, TriggerClick }` `:19` — `Ignore` = pre-5.6 default (CommonButton ignores Slate Accept keys so input actions can bind them); `TriggerClick` = default for new 5.6+ projects `:21-29`.
  Props (all `config, EditAnywhere`): `bool bAutoLoadData = true` `:68` (ctor `CUI/Private/CommonUISettings.cpp:15`), `TSoftObjectPtr<UObject> DefaultImageResourceObject` `:72` (AllowedClasses Texture2D/MaterialInterface), `TSoftObjectPtr<UMaterialInterface> DefaultThrobberMaterial` `:76`, `TSoftClassPtr<UCommonUIRichTextData> DefaultRichTextDataClass` `:80`, **`TArray<FGameplayTag> PlatformTraits`** `:84` (`Categories="Platform.Trait"`, `ConfigHierarchyEditable` — an array so the ini hierarchy can inherit), `ECommonButtonAcceptKeyHandling CommonButtonAcceptKeyHandling = Ignore` `:90` (cpp `:16`). Native tag `TAG_PlatformTrait_PlayInEditor` `:16`. Getters `GetRichTextData`, `GetDefaultThrobberBrush`, `GetDefaultImageResourceObject`, `GetPlatformTraits`, `GetCommonButtonAcceptKeyHandling` `:56-:60`.
* **`UCommonUIEditorSettings : UObject`** — `CUI/Public/CommonUIEditorSettings.h:14`, `config=Editor, defaultconfig`. `TSoftClassPtr<UCommonTextStyle> TemplateTextStyle` `:40`, `TSoftClassPtr<UCommonButtonStyle> TemplateButtonStyle` `:44`, `TSoftClassPtr<UCommonBorderStyle> TemplateBorderStyle` `:48` — what newly-palette-dropped widgets get in `OnCreationFromPalette()` (`CUI/Private/CommonButtonBase.cpp:446-449`).

### A.8 Tags

`CUI/Public/UITag.h` — a typed-`FGameplayTag` mechanism. `TTypedTagStaticImpl<TagT>` `:17` +
macro `END_UI_TAG_DECL(TagType, TagRoot)` `:97` generating
`GetRootTag`/`TryConvert`/`ConvertChecked`/`AddNativeTag`/`ExportTextItem` and the
`TStructOpsTypeTraits` (net serializer, export/import text). Concrete types:
**`FUITag : FGameplayTag`** rooted at `"UI"` `:128`, **`FUIActionTag : FUITag`** rooted at
`"UI.Action"` `:135`. Native global actions in `FGlobalUITags` `:156`: `UIAction_Cancel`,
`UIAction_Confirm`, `UIAction_PreviousTab`, `UIAction_NextTab` `:158-170`.

### A.9 The input-routing model

#### `ECommonInputType` — `CIN/Public/CommonInputTypeEnum.h:9`

`{ MouseAndKeyboard, Gamepad, Touch, Count }`, `UENUM(BlueprintType)` with `ENUM_RANGE_BY_COUNT` `:17`.

#### `ECommonInputMode` — `CIN/Public/CommonInputModeTypes.h:11`

`Menu` ("Input is received by the UI only"), `Game` ("…by the Game only"),
`All` ("…by UI and the Game"), `MAX` hidden `:13-17`. `LexToString` `:20`.

#### `UCommonInputSubsystem : ULocalPlayerSubsystem` — `CIN/Public/CommonInputSubsystem.h:25`

Owns "which input device is the player using right now".

* **Query**: `GetCurrentInputType()` `:53`, `GetDefaultInputType()` `:57`, `IsInputMethodActive(ECommonInputType)` `:49`, `GetCurrentGamepadName()` `:63`, `IsUsingPointerInput()` `:69`, `ShouldShowInputKeys()` `:73` (suppresses on-screen prompts, e.g. for video capture), `PlatformSupportsHardwareCursor()` `:83`, `GetIsGamepadSimulatedClick()` `:90`, `HadAnyChangeOfInputMethodInTheLastThrashingWindow()` `:106`.
* **Mutate**: `SetCurrentInputType(ECommonInputType)` `:60`, `SetGamepadInputType(FName)` `:66`, `SetInputTypeFilter(type, Reason, Filter)` `:42`, `AddOrRemoveInputTypeLock(FName Reason, ECommonInputType, bool)` `:46`, `SetCursorPosition` `:85` / `UpdateCursorPosition` `:87`.
* **Broadcast on change**: two delegates fired together in `BroadcastInputMethodChanged()` (`CIN/Private/CommonInputSubsystem.cpp:220-231`) — native `FInputMethodChangedEvent OnInputMethodChangedNative` `:38` and dynamic `FInputMethodChangedDelegate OnInputMethodChanged(ECommonInputType)` `:20`,`:139` (private BlueprintAssignable). Also `FGamepadChangeDetectedEvent GetOnGamepadChangeDetected()` `:40`.
* **State**: `RawInputType` (last raw device) vs `CurrentInputType` (after locks + thrashing) `:152`,`:156`; `TMap<FName, ECommonInputType> CurrentInputLocks` `:168`; `bInputMethodLockedByThrashing` `:164`.
* **Switching flow**: `SetCurrentInputType` → `CheckForInputMethodThrashing` → `RecalculateCurrentInputType` → `LockInput` → broadcast (cpp `:354-366`, `:283-330`, `:233-250`). Thrash protection auto-adds/removes an `"InputMethodThrashing"` lock (cpp `:246-250`).
* **Override hook**: static `FPlatformInputSupportOverrideDelegate GetOnPlatformInputSupportOverride()` `:103`,`:187` lets a game veto an input type per platform.
* Holds the `UCommonInputActionDomainTable` `:75`,`:77`.

**`FCommonInputPreprocessor : IInputProcessor`** — `CIN/Public/CommonInputPreprocessor.h:18`. Runs
**before any UI sees input** so the current input type is right first `:15-17`. Handles
KeyDown/Analog/MouseMove/MouseButtonDown/DoubleClick/Wheel `:25-:30`, `GetDebugName()` = `"CommonInput"` `:31`,
per-type filter reasons `TMap<FName,bool> FilterInputTypeWithReasons[Count]` `:56`, gamepad-hardware
sniffing via `LastSeenGamepadInputDeviceName` `:58` / `LastSeenGamepadHardwareDeviceIdentifier` `:59`.

#### Button-prompt / glyph system

**`FCommonInputTypeInfo`** — `CUI/Public/CommonUITypes.h:40`. Per-input-method binding record.
`FKey Key` (private) `:48` + `TArray<FKey> AdditionalKeys` ("additional keys that also trigger this
action; the primary Key is still used for display") `:52`; accessors `GetKey()` (remaps via
`FPlatformInput::RemapKey`, `CUI/Private/CommonUITypes.cpp:36-39`), `GetAllKeys()` `:60`,
`IsKeyBound(FKey)` `:63`. `EInputActionState OverrrideState` [sic] `:73` — enum
`EInputActionState { Enabled, Disabled, Hidden, HiddenAndDisabled }` `:25-36`.
`bool bActionRequiresHold` `:77`, `float HoldTime` `:81` (EditCondition, ClampMin 0),
`float HoldRollbackTime` `:89` (ClampMin 0 / ClampMax 10), `FSlateBrush OverrideBrush` `:93`
(per-binding glyph override). Ctor defaults (`CUI/Private/CommonUITypes.cpp:27-34`):
`OverrrideState = Enabled`, `OverrideBrush.DrawAs = NoDrawType`, `bActionRequiresHold = false`,
**`HoldTime = 0.5f`**, `HoldRollbackTime = 0.0f`.

**`FCommonInputActionDataBase : FTableRowBase`** — `CUI/Public/CommonUITypes.h:112`. The
legacy-but-still-primary data-table row for a UI action.
`FText DisplayName` `:120`, `FText HoldDisplayName` `:124`, `int32 NavBarPriority = 0` `:128`.
Protected per-method bindings: `FCommonInputTypeInfo KeyboardInputTypeInfo` `:135`,
`DefaultGamepadInputTypeInfo` `:141`, **`TMap<FName, FCommonInputTypeInfo> GamepadInputOverrides`** `:147`
(`GetOptions="CommonInput.CommonInputBaseControllerData.GetRegisteredGamepads"`),
`TouchInputTypeInfo` `:153`.
API: `GetCurrentInputTypeInfo(const UCommonInputSubsystem*)` `:160`,
`GetInputTypeInfo(ECommonInputType, FName GamepadName)` `:162`,
`GetCurrentInputActionIcon(…) -> FSlateBrush` `:168`, `IsKeyBoundToInputActionData` `:164`,`:166`,
`HasHoldBindings()` `:172`, `CanDisplayInReflector` `:158`, `AddGamepadInputOverride` `:178`,
`OnPostDataImport` `:170`. Custom `Serialize`/`PostSerialize` traits `:196`,`:200-208`.

**`UCommonInputBaseControllerData : UObject`** — `CIN/Public/CommonInputBaseTypes.h:157`,
`UCLASS(Abstract, Blueprintable, ClassGroup=Input)`. **This is the key→brush table.**
`ECommonInputType InputType` `:177`, `FName GamepadName` `:180` (`GetOptions=GetRegisteredGamepads`),
`FText GamepadDisplayName` `:183`, `FText GamepadCategory` `:186`, `FText GamepadPlatformName` `:189`,
`TArray<FInputDeviceIdentifierPair> GamepadHardwareIdMapping` `:192` — all
`EditCondition="InputType == ECommonInputType::Gamepad"`.
`TSoftObjectPtr<UTexture2D> ControllerTexture` `:195`, `ControllerButtonMaskTexture` `:198`.
**`TArray<FCommonInputKeyBrushConfiguration> InputBrushDataMap`** `:201` — `{FKey Key; FSlateBrush KeyBrush;}` `:36-51`.
**`TArray<FCommonInputKeySetBrushConfiguration> InputBrushKeySets`** `:204` — `{TArray<FKey> Keys; FSlateBrush KeyBrush;}` `:54-69`,
i.e. one glyph for a key *combination*.
Lookup `TryGetInputBrush(FSlateBrush&, FKey)` `:163` / `(…, TArray<FKey>)` `:164`; free helpers
`CommonUIUtils::TryGetInputBrushFromDataMap` `:97` / `…FromKeySets` `:98`. `NeedsLoadForServer()` -> false `:162`.
Editor-only `SetButtonImageHeightTo` bulk-resize helper `:172`. `static GetRegisteredGamepads()` `:207`.
`FInputDeviceIdentifierPair = {FName InputDeviceName; FString HardwareDeviceIdentifier;}` `:72-81`.

**`UCommonInputPlatformSettings : UPlatformSettings`** — `CIN/Public/CommonInputBaseTypes.h:216`,
`config=Game, defaultconfig`. Per-platform capability + controller-data set.
`ECommonInputType DefaultInputType` `:272`, `bool bSupportsMouseAndKeyboard` `:275`,
`bSupportsTouch` `:278`, `bSupportsGamepad` `:281`, `FName DefaultGamepadName` `:284`,
`bool bCanChangeGamepadType` `:287`,
`TArray<TSoftClassPtr<UCommonInputBaseControllerData>> ControllerData` `:290`. Defaults come from
`InitializePlatformDefaults()` `:269` per platform ini, not inline.
`Get()` via `UPlatformSettingsManager` `:227`, `TryGetInputBrush(brush, key|keys, InputType, GamepadName)` `:232`,`:233`,
`GetControllerDataForInputType` `:239`, `Add/RemoveControllerDataEntry` `:240`,`:241`,
**`GetBestGamepadNameForHardware(CurrentGamepadName, InputDeviceName, HardwareDeviceIdentifier)`** `:243`
— automatic controller-brand detection — `SupportsInputType` `:250`.
Legacy `FCommonInputPlatformBaseData` kept for migration `:298` (marked `/* DEPRECATED Legacy! */` `:296`);
its ctor defaults are `DefaultInputType=Gamepad, bSupportsMouseAndKeyboard=false, bSupportsGamepad=true,
bCanChangeGamepadType=true, bSupportsTouch=false, DefaultGamepadName=GamepadGeneric` `:305-313`.
`FCommonInputDefaults::PlatformPC` / `GamepadGeneric` `:31`,`:32`;
`FCommonInputBase::GetCurrentPlatformName()` / `GetInputSettings()` / `GetCurrentPlatformDefaults()` `:394-:398`.

**`UCommonUIInputData : UObject`** — `CIN/Public/CommonInputBaseTypes.h:103`,
`UCLASS(Abstract, Blueprintable)`. Project-level defaults: `FDataTableRowHandle DefaultClickAction` `:112`,
`DefaultBackAction` `:115`, `TSoftClassPtr<UCommonUIHoldData> DefaultHoldData` `:122`,
`TObjectPtr<UInputAction> EnhancedInputClickAction` `:125` / `EnhancedInputBackAction` `:128` (EI EditCondition).

**`UCommonUIHoldData : UObject`** — `CIN/Public/CommonInputBaseTypes.h:133`,
`UCLASS(Abstract, Blueprintable)`. Per-input-method hold timings:
`FInputHoldData KeyboardAndMouse` `:148` / `Gamepad` `:150` / `Touch` `:152`, each
`{float HoldTime = 0.0; float HoldRollbackTime = 0.0;}` `:85-93` — **but the ctor sets all three
`HoldTime = 0.75f`, `HoldRollbackTime = 0.0f`** `:137-145`.

**`UCommonActionWidget : UWidget`** — `CUI/Public/CommonActionWidget.h:25`. The glyph widget;
"shows a platform-specific icon for the given input action" `:22`.
Props: `FSlateBrush ProgressMaterialBrush` `:84` (the 0..1 hold-progress material),
`FName ProgressMaterialParam` `:88`, `FSlateBrush IconRimBrush` `:91`,
`TArray<FDataTableRowHandle> InputActions` `:109` (`RowType=CommonInputActionDataBase`,
`TitleProperty="RowName"` — multiple rows collapse to one glyph, `:104-107`),
`TObjectPtr<UInputAction> EnhancedInputAction` `:115` (`Getter`, EI EditCondition),
editor-only `FKey DesignTimeKey` `:124` and `InputActionDataRow_DEPRECATED` `:121`.
BlueprintAssignable `FOnInputMethodChanged(bool bUsingGamepad)` `:71`,`:73`,
`FOnInputIconUpdated()` `:75`,`:77`.
API: `GetIcon()` `:42`, `GetDisplayText()` `:45`, `GetIconDynamicMaterial()` `:48`,
`SetInputAction(FDataTableRowHandle)` `:57`, **`SetInputActionBinding(FUIActionBindingHandle)`** `:60`,
`SetInputActions(TArray<…>)` `:63`, `SetEnhancedInputAction(UInputAction*)` `:51`,
`SetIconRimBrush` `:66`, `IsHeldAction()` `:69`, `OnActionProgress(float)` `:97` /
`OnActionComplete()` `:98`, `SetHidden(bool)` `:100`.
Resolution chain (`CUI/Private/CommonActionWidget.cpp:116-156`): Enhanced-Input action →
`CommonUI::GetIconForEnhancedInputAction`, else `CommonUI::GetIconForInputActions(subsystem, InputActions)`;
falls back to `FStyleDefaults::GetNoBrush()`. Crucially it resolves the subsystem from **the bound
binding's local player**, not the widget's owner, so split-screen prompts show each player's own
device (cpp `:158-165`). Design-time path uses `UCommonInputPlatformSettings::Get()->TryGetInputBrush(…)`
with `DesignTimeKey` (cpp `:147`).

**Action bar — `UCommonBoundActionBar : UDynamicEntryBoxBase`** — `CUI/Public/Input/CommonBoundActionBar.h:24`,
`Blueprintable, ClassGroup=UI`. "A box populated with current actions available per CommonUI's Input
Handler" `:21`.
Props: `TSubclassOf<UCommonButtonBase> ActionButtonClass` `:68`
(`MustImplement="/Script/CommonUI.CommonBoundActionButtonInterface"`),
`bool bDisplayOwningPlayerActionsOnly = true` `:71`, `bool bIgnoreDuplicateActions = true` `:74` (AdvancedDisplay).
BlueprintAssignable `FActionBarUpdated OnActionBarUpdated` `:18`,`:77`.
Hooks `NativeOnActionButtonCreated(ICommonBoundActionButtonInterface*, const FUIActionBindingHandle&)` `:37`,
`ActionBarUpdateBeginImpl` `:39` / `ActionBarUpdateEndImpl` `:40`,
`CreateActionButton(const FUIActionBindingHandle&)` `:42`, `SetDisplayOwningPlayerActionsOnly` `:30`.
Refresh is deferred through a ticker and blocked while any action button holds mouse capture
(`CUI/Private/Input/CommonBoundActionBar.cpp:124-145`); it gathers
`ActionRouter->GatherActiveBindings()` per local player (owner last), filtering out
`!bDisplayInActionBar` and actions with no key valid for the current input type unless the type is in
`InputTypesExemptFromValidKeyCheck` (cpp `:167-195`).
**`ICommonBoundActionButtonInterface`** — `CUI/Public/Input/CommonBoundActionButtonInterface.h:20`:
one pure virtual `SetRepresentedAction(FUIActionBindingHandle)` `:25`.
**`UCommonBoundActionButton : UCommonButtonBase` + interface** — `CUI/Public/Input/CommonBoundActionButton.h:15`,
`UCLASS(Abstract, DisableNativeTick)`. Props `TObjectPtr<UCommonTextBlock> Text_ActionName` `:36` (`BindWidget`),
`bool bLinkRequiresHoldToBindingHold = false` `:40`. BlueprintImplementableEvent `OnUpdateInputAction()` `:32`.

#### `UCommonUIInputSettings` — `CUI/Public/Input/CommonUIInputSettings.h:115`

`UCLASS(config=Input, defaultconfig)`. **Project-wide table of UI actions.**

| Property | Type | Default | Notes |
|---|---|---|---|
| `bLinkCursorToGamepadFocus` | `bool` | **`true`** (`:134`) | moves the mouse pointer to the centre of the focused widget while on gamepad |
| `UIActionProcessingPriority` | `int32` | **`10000`** (`:147`) | input-component priority; **when active mode is `Menu`, ALL components below this are fully blocked** (`:143-144`) |
| `InputActions` | `TArray<FUIInputAction>` | empty (`:151`) | `TitleProperty="ActionTag"` |
| `ActionOverrides` | `TArray<FUIInputAction>` | empty (`:155`) | `UPROPERTY(Config)` only — a matching entry here wins completely (`:153`) |
| `AnalogCursorSettings` | `FCommonAnalogCursorSettings` | see below (`:158`) | |
| `DefaultVirtualPointerClass` | `TSoftClassPtr<UUserWidget>` | `/CommonUI/WBP_VirtualPointer.WBP_VirtualPointer_C` (plugin `Config/Input.ini`) | (`:162`) |

Accessors `Get()` `:120`, `FindAction(FUIActionTag)` `:125`, `GetUIInputActions()` `:126`,
`GetUIActionProcessingPriority()` `:124`, `GetAnalogCursorSettings()` `:127`,
`ShouldLinkCursorToGamepadFocus()` `:129`.

**`FUIActionKeyMapping`** `:16`: `FKey Key` `:30`, `float HoldTime = 0.f` `:34`,
`float HoldRollbackTime = 0.f` `:38` — all `EditAnywhere, Config`.
**`FUIInputAction`** `:42`: `FUIActionTag ActionTag` ("universal identifier of this action") `:49`,
`FText DefaultDisplayName` `:56`, `TArray<FUIActionKeyMapping> KeyMappings` `:60` (`TitleProperty="Key"`),
`HasAnyHoldMappings()` `:62`.
**`FCommonAnalogCursorSettings`** `:66`: `PreprocessorPriority = 2` (**`UE_DEPRECATED(5.5)`**) `:72-74`,
`FInputPreprocessorRegistrationKey PreprocessorRegistrationInfo = {EInputPreProcessorType::Game, 2}` `:77`,
`bEnableCursorAcceleration = true` `:80`, `CursorAcceleration = 1500.f` `:83`,
`CursorMaxSpeed = 2200.f` `:86`, `CursorDeadZone = 0.25f` `:89` (clamp 0–0.9),
`HoverSlowdownFactor = 0.4f` `:92`, `ScrollDeadZone = 0.2f` `:95`, `ScrollUpdatePeriod = 0.1f` `:98`,
`ScrollMultiplier = 2.5f` `:101`, `MaxHoldDuration = 1.0f` `:105`.

#### `FBindUIActionArgs` — `CUI/Public/Input/CommonUIInputTypes.h:15`

The registration payload. Six ctors: by `FUIActionTag` `:16`,`:21`; by
`FDataTableRowHandle LegacyActionTableRow` `:28`,`:34`; by `const UInputAction*` `:40`,`:45` — the
three coexisting action-identity systems.

| Field | Type | Default |
|---|---|---|
| `ActionTag` | `FUIActionTag` | — (`:55`) |
| `LegacyActionTableRow` | `FDataTableRowHandle` | — (`:58`) |
| `InputAction` | `TWeakObjectPtr<const UInputAction>` | — (`:60`) |
| `InputMode` | `ECommonInputMode` | **`Menu`** (`:62`) |
| `KeyEvent` | `EInputEvent` | `IE_Pressed` (`:63`) |
| `InputTypesExemptFromValidKeyCheck` | `TSet<ECommonInputType>` | `{MouseAndKeyboard, Touch}` (`:69`) |
| `bIsPersistent` | `bool` | `false` (`:75`) — always live regardless of activation; persistent bindings never stomp each other (`:72-74`) |
| `bConsumeInput` | `bool` | **`true`** (`:82`) |
| `bDisplayInActionBar` | `bool` | **`true`** (`:85`) |
| `bForceHold` | `bool` | `false` (`:88`) |
| `OverrideDisplayName` | `FText` | empty (`:91`) |
| `PriorityWithinCollection` | `int32` | `0` (`:97`) — 0 = registration order (`:94-96`) |
| `OnExecuteAction` | `FSimpleDelegate` | — (`:99`) |
| `OnHoldActionProgressed` | `DECLARE_DELEGATE_OneParam(float)` | — (`:102`,`:103`) |
| `OnHoldActionPressed` | `DECLARE_DELEGATE()` | — (`:106`,`:107`) |
| `OnHoldActionReleased` | `DECLARE_DELEGATE()` | — (`:110`,`:111`) |

#### `FUIActionBinding` — `CUI/Public/Input/UIActionBinding.h:29`

Non-copyable, non-movable `:31-33`; created only via
`TryCreate(const UWidget&, const FBindUIActionArgs&, int32 UserIndex)` `:37` (the no-UserIndex
overload is `UE_DEPRECATED(5.6)` `:35`). Global registry
`static TMap<FUIActionBindingHandle, TSharedPtr<FUIActionBinding>> AllRegistrationsByHandle` `:118`,
`static TMap<FKey, FUIActionBindingHandle> CurrentHoldActionKeys` `:121`, `IdCounter` `:117`;
`FindBinding(Handle)` `:39`, `CleanRegistrations()` `:40`.
Fields mirror `FBindUIActionArgs` plus `TArray<FUIActionKeyMapping> NormalMappings` `:79` /
`HoldMappings` `:80` — the ctor at `CUI/Private/Input/UIActionRouterTypes.cpp:147-217` splits
mappings by `HoldTime > 0.f` into hold vs normal `:167-181` and pulls the display name from the
`FUIInputAction` / `UInputAction::ActionDescription` / `FCommonInputActionDataBase::DisplayName` in
that order `:183-217`.
Hold state machine: `ProcessHoldInput(mode, key, event) -> EProcessHoldActionResult { Handled, GeneratePress, Unhandled }` `:47`
(enum `:22-27`), `BeginHold` `:51`, `UpdateHold(TargetHoldTime)` `:52`, `CancelHold` `:53`,
`BeginRollback(TargetHoldRollbackTime, HoldTime, Handle)` `:54`, `GetSecondsHeld` `:55`,
`IsHoldActive` `:56`, `ResetHold` `:57`; private `HoldStartTime`, `HoldStartSecond`,
`CurrentHoldSecond`, `HoldRollbackMultiplier`, `HoldTime`, `HoldProgressRollbackTickerHandle` `:100-115`.
Multicast `OnHoldActionProgressed(float)` `:83` / `OnHoldActionPressed` `:86` / `OnHoldActionReleased` `:89`.

#### `FUIActionBindingHandle` — `CUI/Public/Input/UIActionBindingHandle.h:19`

`USTRUCT(BlueprintType, DisplayName="UI Action Binding Handle")`. Opaque
`int32 RegistrationId = INDEX_NONE` `:67` + non-shipping `FString CachedDebugActionName` `:60`.
API `IsValid` `:24`, `Unregister` `:25`, `ResetHold` `:28`, `GetActionName` `:30`,
`GetDisplayName` `:32` / `SetDisplayName` `:35` (the setter broadcasts `OnBoundActionsUpdated`, so
"should not be called often" `:34`), `GetDisplayInActionBar` `:37` / `SetDisplayInActionBar` `:40`,
`GetBoundWidget` `:42`, **`GetBoundLocalPlayer`** `:44`, `GetTypeHash` `:50`.

#### `FUIInputConfig` — `CUI/Public/Input/UIActionBindingHandle.h:94`

`USTRUCT(BlueprintType)`. "Allows for input setup (Mouse capture, UI-only input, move/look ignore,
etc.) to be controlled by widget activation" `:90-91`.

| Field | Type | Default | Access |
|---|---|---|---|
| `bIgnoreMoveInput` | `bool` | `false` (`:123`) | public, EditAnywhere/BPRW |
| `bIgnoreLookInput` | `bool` | `false` (`:126`) | public |
| `InputMode` | `ECommonInputMode` | **`Menu`** (ctor `UIActionRouterTypes.cpp:685`) | protected + `GetInputMode()` (`:98`) |
| `MouseCaptureMode` | `EMouseCaptureMode` | **`NoCapture`** (cpp `:686`) | `GetMouseCaptureMode()` (`:99`) |
| `MouseLockMode` | `EMouseLockMode` | **`DoNotLock`** (cpp `:687`) | `GetMouseLockMode()` (`:100`) |
| `bHideCursorDuringViewportCapture` | `bool` | **`true`** (`:145`) | getter (`:101`) |

The 3-arg ctor **derives** `MouseLockMode`: `LockOnCapture` for `CapturePermanently` /
`CapturePermanently_IncludingInitialMouseDown`, else `DoNotLock` (cpp `:690-705`). `ToString()` `:129`.

**`FActivationMetadata`** `:78`: an opaque `TOptional<uint8> MetadataEnum` `:86` set by
`UCommonActivatableWidget::GetActivationMetadata()` and observed via `OnActivationMetadataChanged` —
game-specific side effects (e.g. camera config) keyed off which widget activated `:72-76`.

#### `UCommonUIActionRouterBase : ULocalPlayerSubsystem` — `CUI/Public/Input/CommonUIActionRouterBase.h:66`

"The nucleus of the CommonUI input routing system" `:60`. One per local player.

* **Result enum**: `ERouteUIInputResult { Handled, BlockGameInput, Unhandled }` `:52`.
* **Tree**: `TArray<FActivatableTreeRootRef> RootNodes` `:246`, `FActivatableTreeRootPtr ActiveRootNode` `:247`, `TSharedPtr<FPersistentActionCollection> PersistentActions` `:250`, `TMap<UCommonInputActionDomain*, FActionDomainSortedRootList> ActionDomainRootNodes` `:299` with a paint-layer-sorted root list `:270-297`. Node types live in `CUI/Private/Input/UIActionRouterTypes.h`: `FActionRouterBindingCollection` `:48` → `FActivatableTreeNode` `:96` → `FActivatableTreeRoot` `:189`.
* **Registration**: `RegisterUIActionBinding(const UWidget&, const FBindUIActionArgs&)` `:86`, `AddBinding` `:138` / `RemoveBinding` `:139`, `NotifyUserWidgetConstructed/Destructed` `:135`,`:136`, scroll recipients `:103`,`:104`,`:105`, `RegisterLinkedPreprocessor(widget, processor, FInputPreprocessorRegistrationKey)` `:90` (the `int32 DesiredIndex` overload is `UE_DEPRECATED(5.5)` `:87`).
* **Dispatch**: `ProcessInput(FKey, EInputEvent) -> ERouteUIInputResult` `:112` — implementation `CUI/Private/Input/CommonUIActionRouterBase.cpp:531-606`. Order: (1) PIE `StopPlaySession` chord escape `:535-550`; (2) track held keys `:554-561`; (3) **hold pass first**, so a higher-priority press binding cannot pre-empt a hold on the same key `:566-586`; (4) normal pass. Each pass goes `PersistentActions` → `ActiveRootNode` (recursing leaf-first: `FActivatableTreeNode::ProcessNormalInput` walks children before its own collection, `UIActionRouterTypes.cpp:1107-1118`) → **action domains** `:588-606`.
* **Per-binding gate** (`UIActionRouterTypes.cpp:846-891`): matches on `Binding->UserIndex == UserIndex && (ActiveInputMode == All || ActiveInputMode == Binding->InputMode)`, then resolved key + `InputEvent` + widget reachability (persistent + displayed bindings skip the reachability rule `:856-858`); `bConsumesInput` decides whether the key stops there `:886-889`. For non-generic Enhanced-Input actions it **injects** the input instead of firing `OnExecuteAction` `:867-884`.
* **Input mode / capture**: `GetActiveInputMode(Default = All)` `:94` = `ActiveInputConfig->GetInputMode()` (cpp `:876-879`); `GetActiveMouseCaptureMode(Default = NoCapture)` `:95` — upgrades `CapturePermanently` → `CapturePermanently_IncludingInitialMouseDown` while the virtual pointer is on (cpp `:881-894`). **`CanProcessNormalGameInput()`** `:113`: false in `Menu` mode *unless* the game viewport widget holds cursor capture (cpp `:1540-1554`).
* **Applying a config**: `SetActiveUIInputConfig(const FUIInputConfig&, const UObject* InConfigSource = nullptr)` `:132` → `ApplyUIInputConfig(NewConfig, bForceRefresh)` `:158` (cpp `:1866+`): sets `PC->SetIgnoreMoveInput`/`SetIgnoreLookInput` only on transitions, flushes pressed keys next tick when entering `Menu` (cpp `:1901-1906`), then `GameViewportClient->SetMouseCaptureMode`/`SetHideCursorDuringCapture`/`SetMouseLockMode` (cpp `:1910-1912`), and pushes the corresponding `FReply` Slate ops (`UseHighPrecisionMouseMovement`/`SetUserFocus`/`CaptureMouse`/`LockMouseToWidget` vs `ReleaseMouseCapture`/`ReleaseMouseLock`) (cpp `:1916-1956`), re-centring the cursor when it becomes visible again (cpp `:1958-1988`). A 5-deep `TCircularBuffer<FString> InputConfigSources` records who last changed it `:252`.
* **Fallback config**: when the tree goes dormant/disabled the router resets to `FUIInputConfig(ECommonInputMode::All, EMouseCaptureMode::NoCapture)` (cpp `:1631`); with no activatable roots but an action-domain table present it applies `FUIInputConfig(Table->InputMode, Table->MouseCaptureMode, Table->bHideCursorDuringViewportCapture)` (cpp `:1861`).
* **Events**: `FOnActiveInputModeChanged(ECommonInputMode)` `:92`, `FOnActiveInputConfigChanged(const FUIInputConfig)` `:97`, `FOnActivationMetadataChanged(FActivationMetadata)` `:100`, `FSimpleMulticastDelegate OnBoundActionsUpdated()` `:108`.
* **Queries**: `GatherActiveBindings()` `:107`, `GatherActiveAnalogScrollRecipients()` `:105`, `IsWidgetInActiveRoot` `:121`, `IsWidgetInLeafmostNodeHierarchy` `:122`, `GetLeafmostActivatableWidget()` `:124`, `IsPendingTreeChange()` `:115`, `ShouldAlwaysShowCursor()` `:147`, static `FindOwningActivatable` `:74` / `FindActivatable(TSharedPtr<SWidget>, ULocalPlayer*)` `:76` — the Slate→activatable walk-up.
* **Misc**: `SetIsActivatableTreeEnabled(bool)` `:84` ("when disabled, all we really do is process Persistent input actions" `:83`), `FlushInput()` `:119`, `RefreshActiveRootFocus()` `:144` / `RefreshUIInputConfig()` `:145`, `MakeAnalogCursor()` virtual factory `:150`, debug `OnShowDebugInfo` `:175` + autocomplete registration `:176`.

#### `UCommonInputActionDomain : UDataAsset` — `CIN/Public/CommonInputActionDomain.h:44`

A second, parallel routing space to the activatable tree — an ordered set of independent "layers"
(HUD, chat, popups) each with its own event-flow policy.
`ECommonInputEventFlowBehavior { BlockIfActive, BlockIfHandled, NeverBlock }` `:19`.
`Behavior = BlockIfActive` `:51` — how an event flows to the *next* domain.
`InnerBehavior = BlockIfHandled` `:56` — how it flows to a lower-ZOrder root *within* the domain.
`bool bUseActionDomainDesiredInputConfig` `:59`, `ECommonInputMode InputMode = Game` `:62`,
`EMouseCaptureMode MouseCaptureMode = CapturePermanently` `:65` — both `EditCondition`'d.
Predicates `ShouldBreakInnerEventFlow(bool bHandled)` `:67`,
`ShouldBreakEventFlow(bool bDomainHadActiveRoots, bool bHandledAtLeastOnce)` `:69`.
`ICommonInputActionDomainMetaData` Slate metadata carries the owning domain down to the SWidget `:26-36`.
**`UCommonInputActionDomainTable : UDataAsset`** `:76`:
`TArray<TObjectPtr<UCommonInputActionDomain>> ActionDomains` — "domains will receive events in
ascending index order" `:81`,`:83`; `ECommonInputMode InputMode = Game` `:86`,
`EMouseCaptureMode MouseCaptureMode = CapturePermanently` `:89`,
`bool bHideCursorDuringViewportCapture = true` `:92`.

#### `UCommonInputSettings : UDeveloperSettings` — `CIN/Public/CommonInputSettings.h:25`

`config=Game, defaultconfig`.

| Property | Type | Default |
|---|---|---|
| `InputData` | `TSoftClassPtr<UCommonUIInputData>` | null (`:82`), `AllowAbstract=false` |
| `PlatformInput` | `FPerPlatformSettings` | (`:85`) |
| `CommonInputPlatformData_DEPRECATED` | `TMap<FName, FCommonInputPlatformBaseData>` | config-only (`:88`) |
| `bEnableInputMethodThrashingProtection` | `bool` | **`true`** (`:91`) |
| `InputMethodThrashingLimit` | `int32` | **`30`** (`:94`) |
| `InputMethodThrashingWindowInSeconds` | `double` | **`3.0`** (`:97`) |
| `InputMethodThrashingCooldownInSeconds` | `double` | **`1.0`** (`:100`) |
| `bAllowOutOfFocusDeviceInput` | `bool` | `false` (`:103`) |
| `bEnableDefaultInputConfig` | `bool` | **`true`** (`:110`) — "a default Input Config will be set when the active CommonActivatableWidgets do not specify a desired one" (`:106-107`) |
| `bEnableEnhancedInputSupport` | `bool` | **`false`** (`:114`), `ConfigRestartRequired` |
| `bEnableAutomaticGamepadTypeDetection` | `bool` | **`true`** (`:121`) |
| `ActionDomainTable` | `TSoftObjectPtr<UCommonInputActionDomainTable>` | null (`:125`) |
| `PlatformNameUpgrades` | `TMap<FName,FName>` | empty (`:133`), `ConfigRestartRequired` |

Static `IsEnhancedInputSupportEnabled()` `:75` — the function every
`EditCondition="CommonInput.CommonInputSettings.IsEnhancedInputSupportEnabled"` in the plugin points
at. `ICommonInputModule::GetSettings()` — `CIN/Public/ICommonInputModule.h:40`.

#### `FCommonAnalogCursor : FAnalogCursor, FGCObject` — `CUI/Public/Input/CommonAnalogCursor.h:35`

The trick that makes gamepad navigation work over a mouse-driven Slate: it "tastefully hijacks things
… by moving a **hidden** cursor around based on focus" — the cursor is invisible and auto-centred on
the focused widget `:29-34`.
Factory `CreateAnalogCursor<T>(const UCommonUIActionRouterBase&)` `:39`; `Deinitialize()` `:46`.
`SetCursorMovementStick(EAnalogStick)` `:59`, `ShouldHandleRightAnalog(bool)` `:63`,
`IsAnalogMovementEnabled()` `:65`, `DetermineScrollOrientation(const UWidget&)` `:91`.
**Virtual pointer** (software cursor): `IsVirtualPointerEnabled` / `IsUsingVirtualPointer` /
`SetVirtualPointerVisibility` `:74-:76`, `FVirtualPointerEnabledChanged` event `:27`,`:78`,
`DefaultVirtualPointerWidget` `:147` / `VirtualPointerWidget` `:148` async-loaded from
`UCommonUIInputSettings::DefaultVirtualPointerClass`.
**Virtual accept + hold**: `ShouldVirtualAcceptSimulateMouseButton(FKeyEvent, EInputEvent)` `:67`,
`OnVirtualAcceptHoldCanceled()` `:69`, `bVirtualAcceptDown` `:168` / `HoldElapsedTime` `:169` driven
against `FCommonAnalogCursorSettings::MaxHoldDuration`.
`IsGameViewportInFocusPathWithoutCapture()` `:109` ("relevant, but not exclusive" `:103-108`),
`IsUsingFakeTouch()` `:73`, `CanReleaseMouseCapture()` `:52`.
`CUI/Public/Input/CommonAnalogCursorTypes.h`: bitflag enum
`ECursorVisualState { Default = 0 (Hidden), Hover = 1, Pressed = 2, Drag = 4, Hold = 8 }` `:14-21`,
and `IVirtualPointerVisualStateInterface` with two BlueprintImplementableEvents —
`OnVirtualPointerVisualStateChanged(Previous, Active)` `:37`, `OnVirtualPointerHoldProgress(float)` `:41`.

#### `UCommonGameViewportClient : UGameViewportClient` — `CUI/Public/CommonGameViewportClient.h:24`

`UCLASS(Within=Engine, transient, config=Engine)`. **Required for CommonUI to route input at all** —
"CommonUI Viewport to reroute input to UI first" `:21`. Overrides `InputKey` / `InputAxis` /
`InputTouch` / `MouseMove` / `CapturedMouseMove` / `MapCursor` `:33-:38`.
Delegates `FOnRerouteInputDelegate OnRerouteInput()` `:44`, `OnRerouteAxis()` `:45`,
`GetRerouteTouchRegistration()` `:55`, `OnRerouteBlockedInput()` `:60`.
`OnRerouteTouchInput()` / `FOnRerouteTouchInputDelegate` /
`HandleRerouteTouch(FInputDeviceId, uint32 TouchId, …)` are all **`UE_DEPRECATED(5.8)`** in favour of
`FTouchId` variants `:15`,`:47`,`:68`,`:87`.
`IsKeyPriorityAboveUI(const FInputKeyEventArgs&)` `:82` — console/fullscreen chords outrank UI.
`SetUseVirtualPointerCursor(bool)` `:42`.

#### Enhanced-Input metadata bridge — `CUI/Public/CommonUITypes.h`

* `UCommonInputMetadata : UObject` `:223` (`Blueprintable, EditInlineNew, CollapseCategories`): `int32 NavBarPriority = 0` `:231`, `bool bIsGenericInputAction = true` `:243` — generic actions (Accept, face-button-top) *do not* broadcast EI Triggered/Ongoing/Canceled/Completed because many widgets subscribe; non-generic actions fire EI events but **not** CommonUI action bindings `:233-241`.
* `ICommonMappingContextMetadataInterface::GetCommonInputMetadata(const UInputAction*)` `:275` and default impl `UCommonMappingContextMetadata : UDataAsset` with `EnhancedInputMetadata` `:292` + `PerActionEnhancedInputMetadata` map `:296`.
* `class CommonUI` statics `:302`: `SetupStyles()` `:305`, `EmptyScrollBoxStyle` `:306`, `GetInputActionData(FDataTableRowHandle)` `:308`, `GetIconForInputActions(subsystem, TArray<FDataTableRowHandle>)` `:309`, `IsEnhancedInputSupportEnabled()` `:311`, `GetEnhancedInputActionMetadata` `:313`, `GetEnhancedInputActionKeys` `:314`, `InjectEnhancedInputForAction` `:315`, `GetIconForEnhancedInputAction` `:316`, `ActionValidForInputType(…)` ×2 `:317`,`:318`, `IsKeyValidForInputType` `:319`, `GetFirstKeyForInputType` `:320`.
* `UCommonGenericInputActionDataTable : UDataTable` (`CUI/Public/Input/CommonGenericInputActionDataTable.h:18`) + `UCommonInputActionDataProcessor : UObject` `:35` (Transient) — a `PostLoad` hook to mutate action rows in code (per-platform edits without checking out the asset `:14-15`).

#### Legacy action-handler layer — `CUI/Public/CommonActionHandlerInterface.h`

The entire file is marked *"related to legacy CommonUI and should be removed in 5.3 - UE-164871"*
`:13`,`:46`,`:119`. Declares `FCommonActionCommited(bool& bPassThrough)` `:25`,
`FCommonActionComplete` `:34`, `FCommonActionProgress(float HeldPercent)` `:43` and native twins;
`FCommonInputActionHandlerData` `:48` with `FDataTableRowHandle InputActionRow` `:71` and private
`EInputActionState State` `:83`; `ICommonActionHandlerInterface` with `HandleHoldInput` /
`HandlePressInput` / `HandleTouchInput` / `UpdateCurrentlyHeldAction` /
`TriggerFirstMatchingInputAction` `:135-:147`.

### A.10 What CommonUI adds that base UMG lacks

Base UMG gives you a widget tree, a Slate backend, and per-widget `FButtonStyle` / `FTextBlockStyle`
structs embedded in each widget *instance*. That is essentially all. Everything below is CommonUI,
and none of it exists in UMG:

1. **Activation as a first-class widget state, separate from construction.** `UCommonActivatableWidget` can be turned on/off without destroying its SWidgets, has back-action handling built in (`bIsBackHandler`), and defines a *node* in a routing tree. UMG has Construct/Destruct and nothing else.
2. **A per-local-player input router.** `UCommonUIActionRouterBase` builds a tree of activatable nodes plus a parallel set of action domains, and dispatches every key to persistent bindings → active root (leaf-first) → domains, with consume/hold/priority semantics. UMG routes input purely through Slate bubbling.
3. **Input configs driven by activation.** `GetDesiredInputConfig()` returning `FUIInputConfig` lets a screen declare "Menu mode, no mouse capture, ignore look input" and have the router apply mouse capture mode, mouse lock, cursor visibility, `SetIgnoreMoveInput`/`SetIgnoreLookInput` and key flushing — then restore the previous config on deactivation. In UMG you call `SetInputMode*` by hand from the player controller and hope no one else does too.
4. **`ECommonInputMode` Game/Menu/All as a routing gate** that also *blocks every input component below `UIActionProcessingPriority` (default 10000)* while in Menu mode.
5. **Gamepad-first focus and navigation.** The hidden `FCommonAnalogCursor` moves an invisible mouse to the focused widget (`bLinkCursorToGamepadFocus`, default true), plus proximate-entry and intra-entry spatial navigation inside list/tile views, focus-restore-on-activation (`bAutoRestoreFocus`), `GetDesiredFocusTarget`, `RequestRefreshFocus`, and a modal flag that hard-stops parent action processing.
6. **Styles as assets.** `UCommonButtonStyle` / `UCommonTextStyle` / `UCommonBorderStyle` / `UCommonTextScrollStyle` are Blueprintable UObjects referenced by `TSubclassOf`, so a project's whole button look (8 state brushes, 5 text styles, 12 sound slots, min/max dimensions and padding) is one shared asset — with editor "template style" settings applied to newly created widgets.
7. **Action bindings + platform-correct button prompts.** `FUIActionTag`-identified actions in project settings, bound per widget, rendered as glyphs by `UCommonActionWidget` resolving key → `FSlateBrush` through `UCommonInputPlatformSettings` → `UCommonInputBaseControllerData` (per-key *and* per-key-set brush maps, per-gamepad-brand, auto-detected from hardware ID), auto-collected into a `UCommonBoundActionBar` that rebuilds whenever bindings or input method change.
8. **Hold-to-activate as infrastructure**, not per-widget code: hold time/rollback per input method (`UCommonUIHoldData`, 0.75 s default), a hold pass that runs *before* the press pass so holds cannot be stolen, progress delegates (`OnActionProgress(float)`), and a progress material parameter driven 0..1 on the prompt glyph.
9. **Runtime input-method detection with hysteresis.** `UCommonInputSubsystem` tracks MouseAndKeyboard/Gamepad/Touch, applies named locks and thrashing protection (30 changes / 3 s window / 1 s cooldown by default) and broadcasts both a native and a dynamic delegate.
10. **Platform-conditional visibility as data.** `UCommonUIVisibilitySubsystem` computes a gameplay-tag set from current input type + `UCommonUISettings::PlatformTraits`; `UCommonHardwareVisibilityBorder` shows/hides via an `FGameplayTagQuery`.
11. **Pooled widget containers.** `FUserWidgetPool`-backed activatable stack and queue with animated transitions and an input guard during the transition; `TWidgetFactory` + `ICommonPoolableWidgetInterface` for acquire/release hooks; `UCommonListView`/`TileView`/`TreeView` with `SCommonButtonTableRow` reconciling row selection mode against the entry button.
12. **Async-loading widgets** (`UCommonLazyImage`, `UCommonLazyWidget`, `UCommonLoadGuard`/`SLoadGuard`) that show a throbber until soft references resolve.
13. Assorted: safe-zone-aware padding on `UCommonBorder`, mobile font scaling on text blocks, scrolling text, rich-text inline icon data tables, tab lists bound to switchers, button groups with enforced single selection, `UCommonVideoPlayer` (the only real media widget in the stack), and `UCommonGameViewportClient` to get input to the UI before the game at all.

**A parity implementation would need, minimally:** (a) an activation lifecycle on widgets plus a
per-player tree assembled from it; (b) a tagged action table (project settings) with per-key mappings
incl. hold times; (c) a binding registry with handles, consume flags, priority, persistence,
input-mode filter, and a leaf-first dispatch that runs holds before presses; (d) an input-config
struct applied to whatever your engine's equivalents of mouse capture / cursor / look-ignore are,
pushed and restored on activation change; (e) an input-device tracker with a pre-UI input hook, locks
and thrashing hysteresis, plus a change broadcast; (f) a per-platform, per-gamepad key→brush asset
and a glyph widget that resolves it from the *binding's* player; (g) an action bar that queries "all
currently active bindings"; (h) a hidden analog cursor (or an equivalent focus-follows-navigation
scheme) so pointer-oriented hit-testing works on a stick; (i) styles as shared assets rather than
per-instance structs; (j) a widget pool and pooled stack/queue containers; (k) a tag-driven
platform-trait visibility filter. Items (a)–(d) are the irreducible core — everything else is
comfort on top.

---

## B. Focus, navigation, hit-testing, replies, drag-drop and tooltips

Everything above sits on this layer. Enum values and defaults are quoted verbatim.

### B.1 Navigation enums and replies

#### `EUINavigation` — `Engine/Source/Runtime/SlateCore/Public/Types/SlateEnums.h:98`

```cpp
UENUM(BlueprintType)
enum class EUINavigation : uint8
{
    Left,      // 0
    Right,     // 1
    Up,        // 2
    Down,      // 3
    Next,      // 4
    Previous,  // 5
    Num UMETA(Hidden),   // 6 — array size, NOT a direction
    Invalid,             // 7 — "no navigation specified"
};
ENUM_RANGE_BY_COUNT(EUINavigation, EUINavigation::Num);   // :117
```

`Num == 6` is the array dimension for per-direction rule tables
(`FNavigationMetaData::Rules[(uint8)EUINavigation::Num]`, `NavigationMetaData.h:159`). `Invalid` is
the "nothing matched" sentinel returned by `FNavigationConfig`.

Companion enums in the same header:

* **`EUINavigationAction`** `:123`: `Accept` (0, `Virtual_Gamepad_Accept`), `Back` (1, `Virtual_Gamepad_Back`), `Num` (hidden), `Invalid`.
* **`ENavigationSource`** `:144`: `FocusedWidget` (0, default), `WidgetUnderCursor` (1) — which path a `FReply::SetNavigation` navigates *from*.
* **`ENavigationGenesis`** `:157`: `Keyboard` (0), `Controller` (1), `User` (2, code/widget-generated). Used by the editor gate `GSlateEnableGamepadEditorNavigation` (`SlateApplication.cpp:3664`).
* Also relevant to §1.4/§1.5 above: `EButtonClickMethod::{DownAndUp, MouseDown, MouseUp, PreciseClick}` `:12`, `EButtonTouchMethod::{DownAndUp, Down, PreciseTap}` `:46`, `EButtonPressMethod::{DownAndUp, ButtonPress, ButtonRelease}` `:68`, `ETextCommit::{Default, OnEnter, OnUserMovedFocus, OnCleared}` `:288`, `ESelectInfo::{OnKeyPress, OnNavigation, OnMouseClick, Direct}` `:308`.

#### `EUINavigationRule` — `Engine/Source/Runtime/SlateCore/Public/Input/NavigationReply.h:14`

```cpp
UENUM(BlueprintType)
enum class EUINavigationRule : uint8
{
    Escape,          // 0 — allow movement to continue past this widget, keep searching
    Explicit,        // 1 — move to a specific widget
    Wrap,            // 2 — cycle around from the opposite side if navigation would escape
    Stop,            // 3 — stop movement in this direction
    Custom,          // 4 — user code decides (delegate always consulted)
    CustomBoundary,  // 5 — user code decides, but only when the boundary is hit
    Invalid          // 6
};
```

**Note the ordering** — `Wrap` sits *between* `Explicit` and `Stop`. Do not assume the doc-order
Escape/Explicit/Custom/CustomBoundary/Stop, and do not serialise this by integer against a different
order.

#### `FNavigationDelegate` and `FNavigationReply` — `NavigationReply.h:35`, `:42`

```cpp
DECLARE_DELEGATE_RetVal_OneParam(TSharedPtr<SWidget>, FNavigationDelegate, EUINavigation);
```

`FNavigationReply` is an immutable value object built by static factories, with **no public
constructor**; the private default ctor sets `BoundaryRule = Escape`, everything else null `:135-140`.
Members `EventHandler`, `FocusRecipient`, `FocusDelegate`, `BoundaryRule` `:154-157`.

| Factory | Line | Sets |
|---|---|---|
| `FNavigationReply::Escape()` | `:116` | rule = `Escape` |
| `FNavigationReply::Explicit(TSharedPtr<SWidget>)` | `:63` | rule = `Explicit`, `FocusRecipient` |
| `FNavigationReply::Custom(const FNavigationDelegate&)` | `:74` | rule = `Custom`, `FocusDelegate` |
| `FNavigationReply::CustomBoundary(const FNavigationDelegate&)` | `:85` | rule = `CustomBoundary`, `FocusDelegate` |
| `FNavigationReply::Wrap()` | `:96` | rule = `Wrap` |
| `FNavigationReply::Stop()` | `:106` | rule = `Stop` |

Accessors `GetHandler()` `:47`, `GetBoundaryRule()` `:50`, `GetFocusRecipient()` `:53`,
`GetFocusDelegate()` `:56`. `SetHandler()` is **private** `:126` and friended to `FSlateApplication`,
`FSlateNavigationEventSimulator`, `SWidget` — the framework stamps the handler onto the reply as it
bubbles.

There is **no `FOnNavigation` delegate type** in 5.8. The navigation hook is the virtual
`SWidget::OnNavigation`; on the UMG side it is `UWidgetNavigation`'s per-direction
`FCustomWidgetNavigationDelegate`.

#### The navigation bubble — `Engine/Source/Runtime/Slate/Private/Framework/Application/SlateApplication.cpp:3679-3695`

When a reply requests navigation, Slate walks the source path **leaf → root** calling `OnNavigation`
on each *enabled* widget and stops at the first non-`Escape` answer (or at the window / index 0):

```cpp
FNavigationReply NavigationReply = FNavigationReply::Escape();
for (int32 WidgetIndex = NavigationSource.Widgets.Num() - 1; WidgetIndex >= 0; --WidgetIndex)
{
    FArrangedWidget& SomeWidgetGettingEvent = NavigationSource.Widgets[WidgetIndex];
    if (SomeWidgetGettingEvent.Widget->IsEnabled())
    {
        NavigationReply = SomeWidgetGettingEvent.Widget
            ->OnNavigation(SomeWidgetGettingEvent.Geometry, NavigationEvent)
            .SetHandler(SomeWidgetGettingEvent.Widget);
        if (NavigationReply.GetBoundaryRule() != EUINavigationRule::Escape
            || SomeWidgetGettingEvent.Widget == NavigationWindow || WidgetIndex == 0)
        {
            AttemptNavigation(NavigationSource, NavigationEvent, NavigationReply, SomeWidgetGettingEvent);
            break;
        }
    }
}
```

The widget producing the non-Escape reply becomes both the reply's `EventHandler` **and** the
`BoundaryWidget` whose rect bounds the geometric search.

#### `SWidget::OnNavigation` default — `Engine/Source/Runtime/SlateCore/Private/Widgets/SWidget.cpp:648-662`

```cpp
FNavigationReply SWidget::OnNavigation(const FGeometry&, const FNavigationEvent& E)
{ return OnNavigation(MyGeometry, E.GetNavigationType()); }

FNavigationReply SWidget::OnNavigation(const FGeometry&, const EUINavigation Type)
{
    TSharedPtr<FNavigationMetaData> NavigationMetaData = GetMetaData<FNavigationMetaData>();
    if (NavigationMetaData.IsValid())
        return FNavigationReply(NavigationMetaData->GetBoundaryRule(Type),
                                NavigationMetaData->GetFocusRecipient(Type).Pin(),
                                NavigationMetaData->GetFocusDelegate(Type));
    return FNavigationReply::Escape();
}
```

**Key architectural point for parity**: the default per-widget navigation rules are *not* fields on
`SWidget` — they live in attached `FNavigationMetaData`, which is what UMG writes to. Declared
`SWidget.h:641` / `:646`.

#### `FNavigationMetaData` — `Engine/Source/Runtime/SlateCore/Public/Types/NavigationMetaData.h:18`

Storage `SNavData Rules[(uint8)EUINavigation::Num]` where
`SNavData = { EUINavigationRule BoundaryRule; TWeakPtr<SWidget> FocusRecipient; FNavigationDelegate FocusDelegate; }` `:153-159`.
Default ctor sets **every** direction to `Escape` with null recipient/delegate `:23-31`.
API `GetBoundaryRule` `:39`, `GetFocusRecipient` `:50`, `GetFocusDelegate` `:61`,
`SetNavigationExplicit` `:72`, `SetNavigationCustom(nav, rule, delegate)` (ensures rule ∈
{Custom, CustomBoundary}) `:86`, `SetNavigationWrap` `:99`, `SetNavigationStop` `:109`,
`SetNavigationEscape` `:119`. 5.8 adds `SetNavigationMethodStruct`/`GetNavigationMethodStruct`
`:126`/`:136` and `SetNavigationRoutingPolicy`/`GetNavigationRoutingPolicy` `:141`/`:146`
(default `EWidgetNavigationRoutingPolicy::Default`). Non-shipping `FSimulatedNavigationMetaData` `:174`
backs the navigation-event simulator/debugger.

#### UMG: `FWidgetNavigationData` / `UWidgetNavigation` — `Engine/Source/Runtime/UMG/Public/Blueprint/WidgetNavigation.h`

```cpp
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(UWidget*, FCustomWidgetNavigationDelegate, EUINavigation, Navigation);  // :17

USTRUCT(BlueprintType)
struct FWidgetNavigationData                                    // :23
{
    EUINavigationRule Rule = EUINavigationRule::Escape;          // :29  (default)
    FName WidgetToFocus;                                         // :33  widget name OR function name
    TWeakObjectPtr<UWidget> Widget;                              // :36  resolved pointer
    FCustomWidgetNavigationDelegate CustomDelegate;              // :39
    void Resolve(UUserWidget* Outer, UWidgetTree* WidgetTree);   // :41
};
```

`WidgetToFocus` is doubly-purposed: for `Explicit` it names a widget in the tree; for
`Custom`/`CustomBoundary` it names the function to bind `:31`.

```cpp
UCLASS() class UWidgetNavigation : public UObject               // :52
{
    FWidgetNavigationData Up, Down, Left, Right, Next, Previous;  // :59,:63,:67,:71,:75,:79
    EWidgetNavigationRoutingPolicy RoutingPolicy = EWidgetNavigationRoutingPolicy::AcceptFocus;  // :82
    TInstancedStruct<FNavigationMethod> NavigationMethod;         // :85
    void ResolveRules(UUserWidget*, UWidgetTree*);                // :103
    void UpdateMetaData(TSharedRef<FNavigationMetaData>);         // :106  ← writes into the Slate metadata
    bool IsDefaultNavigation() const;                             // :109
};
```

`Next` maps to Tab, `Previous` to Shift+Tab `:73`,`:77`.
`UWidget::Navigation` (`Components/Widget.h:466`) is `UPROPERTY(Instanced, EditAnywhere, BlueprintReadOnly, Category="Navigation")`
and is **null by default** — created lazily only when non-default rules are configured;
`UWidget::BuildNavigation()` (`Widget.h:945`) pushes it into `FNavigationMetaData` on the `SWidget`.

`UWidget` navigation BP functions (see also §0.5): `SetAllNavigationRules` `:717`,
`SetNavigationRule` (**`UE_DEPRECATED(4.23)`**) `:727`, `SetNavigationRuleBase` `:735`,
`SetNavigationRuleExplicit` `:743`, `SetNavigationRuleCustom` `:751`,
`SetNavigationRuleCustomBoundary` `:759`, C++-only `SetNavigationMethod(const TInstancedStruct<FNavigationMethod>&)` `:765`.
All funnel through the private `SetNavigationRuleInternal(...)` `:1186` and require the widget to be in
a widget tree.

#### 5.8 additions: routing policy and navigation methods

`EWidgetNavigationRoutingPolicy` — `Engine/Source/Runtime/SlateCore/Public/Input/NavigationRouting.h`:
`AcceptFocus` (default), `RouteToTopMostChild`, `RouteToBottomMostChild`, `RouteToLeftMostChild`,
`RouteToRightMostChild`, `RouteToTopLeftChild`, `RouteToTopRightChild`, `RouteToBottomLeftChild`,
`RouteToBottomRightChild`, `MAX` (hidden), `Default = AcceptFocus` (hidden). A container can say
"navigation that lands on me should be forwarded to my top-left child".

`FNavigationMethod` — `Engine/Source/Runtime/SlateCore/Public/Input/NavigationMethod.h:11`, a
`USTRUCT` with `Initialize(const FHittestGrid*, TArray<FDebugWidgetResult>*)` `:20`,
`FindNextFocusableWidget(...)` `:21`, `DrawDebug(...)` `:41`. Pluggable per widget hierarchy, used only
on the **experimental** navigation path (§B.2.7). Not needed for baseline parity.

### B.2 Focus

#### `EFocusCause` — `Engine/Source/Runtime/SlateCore/Public/Input/Events.h:24` (NOT SlateEnums.h)

```cpp
UENUM()
enum class EFocusCause : uint8
{
    Mouse,                 // 0 — focus changed by a mouse action
    Navigation,            // 1 — arrow keys, TAB, D-pad, ...
    SetDirectly,           // 2 — code asked for it (default everywhere)
    Cleared,               // 3 — explicitly cleared (Escape or similar)
    OtherWidgetLostFocus,  // 4 — another widget lost focus and it moved here
    WindowActivate,        // 5 — owning window was activated
};
```

`FFocusEvent` (`Events.h:50`) carries `{ EFocusCause Cause; uint32 UserIndex; }`; default ctor uses
`SetDirectly` and user 0 `:59-62`. Accessors `GetCause()` `:79`, `GetUser()` `:89`.

#### `FSlateApplication` focus API — `Engine/Source/Runtime/Slate/Public/Framework/Application/SlateApplication.h`

Every focus setter defaults its cause to `EFocusCause::SetDirectly`.

| API | Line |
|---|---|
| `bool SetUserFocus(uint32 UserIndex, const TSharedPtr<SWidget>&, EFocusCause = SetDirectly)` | `:678` |
| `void SetAllUserFocus(const TSharedPtr<SWidget>&, EFocusCause = SetDirectly)` | `:686` |
| `void ClearUserFocus(uint32 UserIndex, EFocusCause = SetDirectly)` | `:689` |
| `void ClearAllUserFocus(EFocusCause = SetDirectly)` | `:692` |
| `bool SetKeyboardFocus(const TSharedPtr<SWidget>&, EFocusCause = SetDirectly)` | `:724` |
| `void ClearKeyboardFocus(EFocusCause = SetDirectly)` | `:731` |
| `void SetUserFocusToGameViewport(uint32 UserIndex, EFocusCause = SetDirectly)` | `:644` |
| `void SetAllUserFocusToGameViewport(EFocusCause = SetDirectly)` | `:649` |
| `bool SetKeyboardFocus(const FWidgetPath&, EFocusCause)` | `:1681` |
| `bool SetUserFocus(uint32, const FWidgetPath&, EFocusCause)` | `:1682` |
| `bool SetUserFocusAllowingDescendantFocus(uint32, const FWidgetPath&, EFocusCause)` | `:1683` |
| `void SetAllUserFocus(const FWidgetPath&, EFocusCause)` | `:1684` |
| `void SetAllUserFocusAllowingDescendantFocus(const FWidgetPath&, EFocusCause)` | `:1685` |
| `TSharedPtr<SWidget> GetUserFocusedWidget(uint32 UserIndex) const` | `:1686` |
| `TSharedPtr<SWidget> GetKeyboardFocusedWidget() const` | `:1653` |
| `TOptional<EFocusCause> HasUserFocus(TSharedPtr<const SWidget>, int32 UserIndex) const` | `:1224` |
| `TOptional<EFocusCause> HasAnyUserFocus(TSharedPtr<const SWidget>) const` | `:1225` |
| `bool ShowUserFocus(TSharedPtr<const SWidget>) const` | `:1227` |
| `bool HasUserFocusedDescendants(const TSharedRef<const SWidget>&, int32) const` | `:1672` |
| 5.8: `SetPendingNavigationContext` / `GetPendingNavigationContext<T>` / `ClearPendingNavigationContext` | `:699`, `:706`, `:719` |

`SetPendingNavigationContext` is a typed per-user payload written during `OnNavigation` and readable
during `OnFocusReceived` on the destination; auto-cleared at the end of each navigation event `:695-698`.

#### Focus state storage — `FSlateUser` (there is **no** `FUserFocusEntry` in 5.8)

A repo-wide code search for `FUserFocusEntry` returns **0 results**; that struct was folded into
`FSlateUser` — `Engine/Source/Runtime/Slate/Public/Framework/Application/SlateUser.h:40`:

* `FWeakWidgetPath WeakFocusPath;` `:246` — the persisted focus path.
* `mutable TSharedPtr<FWidgetPath> StrongFocusPath;` `:249` — lazily realised cache; `GetFocusPath()` inlines `WeakFocusPath.ToWidgetPathRef()` `:136-143`.
* `SetFocusPath(const FWidgetPath& NewFocusPath, EFocusCause InFocusCause, bool bInShowFocus)` `:160` — the single write point; cause and show-focus flag are stored alongside the path.
* `GetWeakFocusPath()` `:130`, `HasValidFocusPath()` `:129`, `IsWidgetInFocusPath()` `:61`, `ShouldShowFocus(TSharedPtr<const SWidget>)` `:50`.
* `int32 GetFocusVersion()` / `IncrementFocusVersion()` `:181-182` — reentrancy guard.
* Per-user nav config override `SetUserNavigationConfig` / `GetUserNavigationConfig` `:126-127`.

**Parity note.** Focus is stored as a *path* (window → … → focused widget), not a single pointer.
"Has focused descendants" is answered by path containment, not tree walking.

#### The exact focus-change sequence — `SlateApplication.cpp:2984-3181`

`FSlateApplication::SetUserFocus(FSlateUser&, const FWidgetPath& InFocusPath, EFocusCause InCause)`:

1. **Reject** if the target window houses an interactive tooltip `:2991`, or if a modal window is up and the target is not a descendant of it `:2996`.
2. **Resolve the actual focus target**: walk `InFocusPath` **leaf → root** and take the *first* widget where `SupportsKeyboardFocus()` is true `:3021-3038`. If that widget already has focus, **return false immediately** — no events fire `:3029-3033`.
3. `User.IncrementFocusVersion()`; capture `CurrentFocusVersion` `:3041-3042`. Every subsequent step compares the version and **bails out returning false** if a handler re-entered and changed focus `:3064`,`:3085`.
4. Broadcast `FocusChangingDelegate` `:3049`.
5. `OnFocusChanging(OldPath, NewPath, FocusEvent)` on **every widget in the old path, root → leaf** `:3056-3069`.
6. `OnFocusChanging(...)` on **every widget in the new path, root → leaf** `:3077-3090`.
7. Compute `ShowFocus`: starts as `InCause == EFocusCause::Navigation`, then walks the new path **leaf → root** asking `OnQueryShowFocus(InCause)`; the first widget returning a set `TOptional<bool>` wins `:3096-3109`.
8. `User.SetFocusPath(NewFocusedWidgetPath, InCause, ShowFocus)` `:3112`.
9. Old leaf widget: `OnFocusLost(FocusEvent)` `:3131` (plus a `Paint` invalidate where `PLATFORM_UI_NEEDS_FOCUS_OUTLINES`).
10. New leaf widget: `ActiveTopLevelWindow` is retargeted `:3153`, then `OnFocusReceived(Geometry, FocusEvent)` `:3167`; **its returned `FReply` is fed straight back into `ProcessReply`** `:3168-3171` — so a widget can redirect focus from within `OnFocusReceived`.
11. `NavConfig->OnNavigationChangedFocus(Old, New, FocusEvent)` `:3173`.

`SetAllUserFocus` simply calls the above per user `:3184-3188`.

**Focus-on-click** — `SlateApplication.cpp:5485-5505`: after `OnMouseButtonDown` bubbling, if no
handler changed focus and the reply did not request a focus recipient, Slate walks widgets under the
pointer **leaf → root** and focuses the first `SupportsKeyboardFocus()` widget with `EFocusCause::Mouse`.

#### `SWidget` focus surface — `Engine/Source/Runtime/SlateCore/Public/Widgets/SWidget.h`

| Member | Line | Semantics |
|---|---|---|
| `virtual FReply OnFocusReceived(const FGeometry&, const FFocusEvent&)` | `:316` | reply is processed by `ProcessReply` |
| `virtual void OnFocusLost(const FFocusEvent&)` | `:323` | |
| `virtual void OnFocusChanging(const FWeakWidgetPath& Prev, const FWidgetPath& New, const FFocusEvent&)` | `:326` | fired on **both** paths before the swap |
| `virtual TOptional<bool> OnQueryShowFocus(EFocusCause) const` | `:607` | drives the focus brush |
| `bool CanSupportFocus() const { return bCanSupportFocus; }` | `:922` | cheap static capability flag |
| `virtual bool SupportsKeyboardFocus() const` | `:929` | **`SWidget.cpp:977` returns `false`** — you must override |
| `virtual bool HasKeyboardFocus() const` | `:936` | `SWidget.cpp:982` |
| `TOptional<EFocusCause> HasUserFocus(int32 UserIndex) const` | `:943` | unset ⇒ not focused |
| `TOptional<EFocusCause> HasAnyUserFocus() const` | `:950` | first found |
| `bool HasUserFocusedDescendants(int32) const` | `:957` | |
| `bool HasFocusedDescendants() const` | `:962` | |
| `bool HasAnyUserFocusOrFocusedDescendants() const` | `:967` | |
| `virtual const FSlateBrush* GetFocusBrush() const` | `:1731` | |
| `uint8 bCanSupportFocus : 1;` | `:1866` | bitfield in the SWidget flag block |

`bCanSupportFocus` and `SupportsKeyboardFocus()` are **different gates**: the former is a
construction-time capability used for invalidation decisions (`SlateApplication.cpp:3121`,`:3158`);
the latter is the runtime predicate the focus resolver and the hit-grid navigation search consult.

#### UMG focus surface — `Engine/Source/Runtime/UMG/Public/Components/Widget.h`

| BP function | Line | Impl |
|---|---|---|
| `bool HasKeyboardFocus() const` | `:632` | `Widget.cpp:575` |
| `bool SupportsKeyboardFocus() const` | `:640` | `Widget.cpp:581` |
| `bool HasMouseCapture() const` | `:647` | |
| `bool HasMouseCaptureByUser(int32 UserIndex, int32 PointerIndex = -1) const` | `:656` | |
| `void SetKeyboardFocus()` | `:660` | `Widget.cpp:599` — tries `FSlateApplication::SetKeyboardFocus`; on failure queues `LocalPlayer->GetSlateOperations().SetUserFocus(..., SetDirectly)`. Warns in PIE if the widget does not support focus. |
| `bool HasUserFocus(APlayerController*) const` | `:664` | `Widget.cpp:623` — PC → LocalPlayer → Slate user index |
| `bool HasAnyUserFocus() const` | `:668` | `Widget.cpp:649` |
| `bool HasFocusedDescendants() const` (BP name **`HasAnyUserFocusedDescendants`**) | `:672` | `Widget.cpp:661` |
| `bool HasUserFocusedDescendants(APlayerController*) const` | `:676` | `Widget.cpp:671` |
| `void SetFocus()` | `:680` | `Widget.cpp:695` — `SetUserFocus(GetOwningPlayer())` |
| `void SetUserFocus(APlayerController*)` | `:684` | `Widget.cpp:700` — errors in PIE if the PC is not a local player |

**`SetKeyboardFocus()` vs `SetUserFocus()`**: `SetKeyboardFocus` targets the *keyboard* user
(effectively user 0 / the keyboard user index) and falls back to the local player's deferred Slate
operations if the immediate set fails (e.g. the widget is not in a live path yet). `SetUserFocus(PC)`
resolves an explicit *Slate user index* from the PlayerController's LocalPlayer controller id — this
is the split-screen / multi-gamepad correct call. `SetFocus()` is `SetUserFocus(OwningPlayer)`.

**`bIsFocusable` lives on `UUserWidget`, not `UWidget`** —
`Engine/Source/Runtime/UMG/Public/Blueprint/UserWidget.h`:

```cpp
UE_DEPRECATED(5.2, "Direct access to bIsFocusable is deprecated. Please use the getter. Note that this property is only set at construction and is not modifiable at runtime.")  // :1030
UPROPERTY(EditAnywhere, BlueprintReadWrite, Getter = "IsFocusable", Setter = "SetIsFocusable", Category = "Interaction")
uint8 bIsFocusable : 1;                     // :1033
bool IsFocusable() const;                   // :1102  (UserWidget.cpp:2416)
void SetIsFocusable(bool InIsFocusable);    // :1104  (UserWidget.cpp:2421 — sets + Invalidate(Paint))
```

`SObjectWidget::SupportsKeyboardFocus()` (`Engine/Source/Runtime/UMG/Public/Slate/SObjectWidget.h:64`,
impl `SObjectWidget.cpp:175`) forwards to `WidgetObject->NativeSupportsKeyboardFocus()`, which returns
`bIsFocusable`. Hence the PIE warning text at `Widget.cpp:607`: *"If this is a UserWidget, you should
set bIsFocusable to true."*

`UUserWidget` focus/navigation overridables: BP events `OnFocusReceived` `:569` (returns
`FEventReply`), `OnFocusLost` `:577`; native `NativeOnFocusReceived` `:1601`, `NativeOnFocusLost` `:1602`,
`NativeOnFocusChanging` `:1603`, `NativeOnNavigation(Geometry, NavEvent, InDefaultReply)` `:1606` and
the older `NativeOnNavigation(Geometry, NavEvent)` `:1634`. `NativeOnFocusReceived` also forwards focus
to `GetDesiredFocusWidget()` if one is set (`UserWidget.cpp:2429-2434`).

`UWidgetBlueprintLibrary::SetFocusToGameViewport()` —
`Engine/Source/Runtime/UMG/Public/Blueprint/WidgetBlueprintLibrary.h:68`, impl
`WidgetBlueprintLibrary.cpp:157` = `FSlateApplication::Get().SetAllUserFocusToGameViewport()`.

#### `FNavigationConfig` — `Engine/Source/Runtime/Slate/Public/Framework/Application/NavigationConfig.h`

**Path note**: this is in the **Slate** module under `Framework/Application/`, *not*
`SlateCore/Public/Application/`. There is no `FSlateNavigationConfig` type; the base class is
`FNavigationConfig` and the shipped variants are `FNullNavigationConfig` and `FTwinStickNavigationConfig`.

Public fields with **verbatim defaults** (ctor `NavigationConfig.cpp:8-39`):

```cpp
bool  bTabNavigation                         = true;    // .h:97   .cpp:9
bool  bKeyNavigation                         = true;    // .h:99   .cpp:10
bool  bAnalogNavigation                      = true;    // .h:101  .cpp:11
bool  bIgnoreModifiersForNavigationActions   = true;    // .h:103  .cpp:12
float AnalogNavigationHorizontalThreshold    = 0.50f;   // .h:106  .cpp:13
float AnalogNavigationVerticalThreshold      = 0.50f;   // .h:108  .cpp:14
FKey  AnalogHorizontalKey = EKeys::Gamepad_LeftX;       // .h:111  .cpp:16
FKey  AnalogVerticalKey   = EKeys::Gamepad_LeftY;       // .h:113  .cpp:17
TMap<FKey, EUINavigation>       KeyEventRules;          // .h:116
TMap<FKey, EUINavigationAction> KeyActionRules;         // .h:119
```

Default `KeyEventRules` `:19-29`: `Left`/`Gamepad_DPad_Left` → `Left`;
`Right`/`Gamepad_DPad_Right` → `Right`; `Up`/`Gamepad_DPad_Up` → `Up`;
`Down`/`Gamepad_DPad_Down` → `Down`.
Default `KeyActionRules` `:32-38`: `Enter`, `SpaceBar`, `Virtual_Gamepad_Accept` → `Accept`;
`Escape`, `Virtual_Gamepad_Back` → `Back`.

**`GetNavigationDirectionFromKey`** (`.cpp:59-81`): if the key is in `KeyEventRules` **and**
`bKeyNavigation` → return the rule. Else if `bTabNavigation` and key == `Tab`: Tab is only eaten when
**Ctrl, Alt and Cmd are all up** (`bAllowEatingKeyEvents` `:72`); then `Shift` ⇒ `Previous`, otherwise
`Next`. Else `Invalid`.

**`GetNavigationDirectionFromAnalog`** (`.cpp:83-105`) is stateful and rate-limited:

```cpp
const float RepeatRate = GetRepeatRateForPressure(FMath::Abs(AnalogValue), FMath::Max(AnalogState.Repeats - 1, 0));
if (FApp::GetCurrentTime() - AnalogState.LastNavigationTime > RepeatRate)
{ LastNavigationTime = now; ++Repeats; return Dir; }
```

Repeat state is keyed by `FAnalogNavigationKey{FKey, EUINavigation}` (`.h:14-36`) inside
`FUserNavigationState` per user index (`.h:51-55`, `.h:138`).

**`GetRepeatRateForPressure`** (`.cpp:153-162`) — the exact repeat curve:

```cpp
const float RepeatRate = (InRepeats == 0) ? 0.5f : 0.25f;
if (InPressure > 0.90f) return RepeatRate * 0.5f;   // 0.25 s first repeat, 0.125 s thereafter
return RepeatRate;                                   // 0.5 s first repeat, 0.25 s thereafter
```

**`GetNavigationDirectionFromAnalogInternal`** (`.cpp:107-151`) — note the **sign convention**:
horizontal `< -Threshold` ⇒ `Left`, `> +Threshold` ⇒ `Right`; vertical `> +Threshold` ⇒ **`Up`**,
`< -Threshold` ⇒ **`Down`** (Y is up on the stick). When the stick returns inside the deadzone, the
repeat state for **both** opposing directions is reset to a fresh `FAnalogNavigationState`
`:128-129`,`:144-145` — that is what makes a re-flick navigate immediately.

**`GetNavigationActionFromKey`** (`.cpp:164-176`): if `bIgnoreModifiersForNavigationActions` is false
and any of Ctrl/Alt/Cmd/Shift is held, returns `Invalid`; otherwise defers to the deprecated
`GetNavigationActionForKey(FKey)` (`.h:87`, `UE_DEPRECATED(4.24)`, `.cpp:178`).

Other virtuals: `OnRegister` (resets all user state `.cpp:45`), `OnUnregister`, `OnUserRemoved(int32)`
(`.cpp:54`), `OnNavigationChangedFocus(Old, New, FocusEvent)` (`.h:81`, no-op default), `ToString()`
(`.cpp:187`), `IsAnalogEventBeyondNavigationThreshold` (`.cpp:208`), overridable
`IsAnalogHorizontalKey` / `IsAnalogVerticalKey` (`.h:134-135`).

Variants: `FNullNavigationConfig` (`.h:143`) — all three nav bools `false`.
`FTwinStickNavigationConfig` (`.h:155`, `.cpp:222`) — `bTabNavigation = false`, D-pad-only
`KeyEventRules`, and both `Gamepad_LeftX`/`RightX` count as horizontal and `Gamepad_LeftY`/`RightY`
as vertical.

Installed via `FSlateApplication::SetNavigationConfig(TSharedRef<FNavigationConfig>)`
(`SlateApplication.h:1426`) / `GetNavigationConfig()` `:1420`; the app also holds a separate
`EditorNavigationConfig` `:2203` and picks per-user with `GetRelevantNavConfig(int32 UserIndex)` `:1229`.
The 4.20-era `SetNavigationConfigFactory` is deprecated to a no-op `:1433`.

#### `FSlateApplication::AttemptNavigation` and destination resolution

`AttemptNavigation(NavigationSource, NavigationEvent, NavigationReply, BoundaryWidget)` —
decl `SlateApplication.h:1930`, impl `SlateApplication.cpp:6601`:

```cpp
const FNavigationResult DestinationResult = CalculateDestinationWidget(NavigationSource, NavigationReply, NavigationType, UserIndex, BoundaryWidget);
FScopedNavigationTransition ScopedNavigationTransition(NavigationType, GetUser(UserIndex));
return ExecuteNavigation(NavigationSource, DestinationResult.DestinationWidget, UserIndex, DestinationResult.bAlwaysHandleNavigationAttempt);
```

`CalculateDestinationWidget` (`.cpp:6498`, decl `.h:1953`, result struct `.h:1944`) branches on the
boundary rule:

* **`Explicit`** `:6507`: use `GetFocusRecipient()` **only if** non-null **and** `IsEnabled()` **and** `SupportsKeyboardFocus()` `:6510`; sets `bAlwaysHandleNavigationAttempt = true`. Otherwise nothing (and logs why in debug builds).
* **`Custom`** `:6541`: if the delegate is bound, execute it with the direction; `bAlwaysHandleNavigationAttempt = true`. Note `CustomBoundary` is **not** handled here — it is handled inside the grid search when the boundary is actually reached.
* **everything else** `:6563`:
  * `Next`/`Previous` → `FWeakWidgetPath::ToNextFocusedPath(NavigationType, NavigationReply, BoundaryWidget)` `:6570`.
  * `Left/Right/Up/Down` → `NavigationSource.GetDeepestWindow()->GetHittestGrid().FindNextFocusableWidget(FocusedArrangedWidget, NavigationType, NavigationReply, BoundaryWidget, UserIndex)` `:6590`.

`ExecuteNavigation` (`.cpp:6621`): first offers the navigation to the game viewport
(`ISlateViewport::HandleNavigation(UserIndex, DestinationWidget)`) if the source path contains the
viewport widget `:6630-6641`; otherwise, if a destination exists,
`SetUserFocus(UserIndex, DestinationWidget, EFocusCause::Navigation)` and report handled `:6646-6650`;
if there is no destination but `bAlwaysHandleNavigationAttempt`, report handled anyway `:6651-6654`.

#### Tab / Shift-Tab: `FWeakWidgetPath::ToNextFocusedPath` — `Engine/Source/Runtime/SlateCore/Private/Layout/WidgetPath.cpp:441`

```cpp
FWidgetPath NewFocusPath = this->ToWidgetPath();
bool bMovedFocus = false;
for (int32 FocusNodeIndex = NewFocusPath.Widgets.Num()-1; !bMovedFocus && FocusNodeIndex >= 0; --FocusNodeIndex)
{
    bMovedFocus = NewFocusPath.MoveFocus(FocusNodeIndex, NavigationType);
    if (!bMovedFocus && RuleWidget.Widget == NewFocusPath.Widgets[FocusNodeIndex].Widget)
    {
        if (NavigationReply.GetBoundaryRule() == EUINavigationRule::Stop) break;
        if (NavigationReply.GetBoundaryRule() == EUINavigationRule::Wrap)
        { NewFocusPath.MoveFocus(FocusNodeIndex, NavigationType, /*bSearchFromPathWidget=*/false); break; }
    }
}
```

Start at the leaf and bubble up levels until some level can advance. `Wrap` re-runs the move with
`bSearchFromPathWidget = false`, which restarts from index 0 (`Next`) or `Num-1` (`Previous`).

`FWidgetPath::MoveFocus(PathLevel, NavigationType, bSearchFromPathWidget = true)` (`WidgetPath.cpp:160`):

* At the leaf level, `Next` descends looking for a focusable descendant (`ExtendPathTo(FFocusableWidgetMatcher(), EVisibility::Visible, EWidgetPathSearchPurpose::FocusHandling)` `:175`); `Previous` returns false (move up a level) `:180`.
* At a non-leaf level: `ArrangeChildren` with `EVisibility::Visible` `:189-191`, find the currently-focused child's index, step by ±1, iterate. **Disabled widgets and all their children are skipped** `:208`. For each candidate `GeneratePathToWidget(FFocusableWidgetMatcher(), ...)`; accepted if a focusable descendant was found *or* the child itself `SupportsKeyboardFocus()` `:213`, **and** no widget in the generated sub-path is disabled `:217-222`. On success the path is truncated at `PathLevel+1` and rebuilt `:226-232`.

#### `FHittestGrid::FindNextFocusableWidget` — the directional algorithm

Entry `HittestGrid.cpp:319`: dispatches to `FindNextFocusableWidgetExperimental` when the CVar-backed
`GEnableNavigationExperimental` is set (default **`false`** `:35`), else `FindNextFocusableWidgetDefault`.
**Implement the Default path for parity.**

**Step 1 — set up the swept rect** (`FindNextFocusableWidgetDefault`, `HittestGrid.cpp:575-658`).
Both the starting widget's and the rule (boundary) widget's paint-space geometries are offset by
`-GridWindowOrigin` and reduced to `GetRenderBoundingRect()` `:577-583`. Then per direction:

| Dir | Swept rect | Axis | Incr | `CompareFunc(A,B)` | `SourceSideFunc` | `DestSideFunc` |
|---|---|---|---|---|---|---|
| Left (`:600`) | `Left/Right` ← boundary's; `Top += 0.5f`, `Bottom -= 0.5f` | 0 | −1 | `A - 0.1f < B` | `SourceRect.Left` | `DestRect.Right` |
| Right (`:611`) | same X widening; same Y inset | 0 | +1 | `A + 0.1f > B` | `SourceRect.Right` | `DestRect.Left` |
| Up (`:622`) | `Top/Bottom` ← boundary's; `Left += 0.5f`, `Right -= 0.5f` | 1 | −1 | `A - 0.1f < B` | `SourceRect.Top` | `DestRect.Bottom` |
| Down (`:633`) | same | 1 | +1 | `A + 0.1f > B` | `SourceRect.Bottom` | `DestRect.Top` |

So the "cone" is really a **beam**: the source widget's rect, stretched along the travel axis to span
the entire boundary widget, shrunk by 0.5 px on each cross-axis edge so exactly-abutting neighbours
do not falsely intersect. The 0.1 f epsilon tolerates sub-pixel coincidence.

**Step 2 — the cell sweep** (`FindFocusableWidget`, `HittestGrid.cpp:366-571`):

```
CurrentCellPoint  = GetCellCoordinate(WidgetRect.GetCenter());   // start at the source's centre cell
StartingIndex     = CurrentCellPoint[AxisIndex];
CurrentSourceSide = SourceSideFunc(WidgetRect);
StrideAxis        = 1 - AxisIndex
StrideAxisMin/Max = clamp(floor(SweptRect.Top  or .Left  / CellSize), 0, NumCells-1)
                    clamp(floor(SweptRect.Bottom or .Right / CellSize), 0, NumCells-1)
```

Marching one cell at a time along `AxisIndex` by `Increment` while in range `:396`, for every cell in
the perpendicular stride band `:407`, collect widgets via `GetCollapsedWidgets` and iterate
**front-to-back** (`i = Num-1 … 0`) `:412`. A candidate is rejected, in this order, for:

1. dead weak pointer `:416`;
2. `!IsCompatibleUserIndex(UserIndex, TestCandidate.UserIndex)` `:421` — `INDEX_NONE` on either side is a wildcard (`HittestGrid.h:322-326`);
3. `!(CompareFunc(DestSideFunc(TestRect), CurrentSourceSide) && FSlateRect::DoRectanglesIntersect(SweptRect, TestRect))` `:430` — must be strictly *ahead* on the travel axis **and** overlap the beam;
4. worse than the current best `:436-459`: if `!CompareFunc(BestDestSide, TestDestSide)` it is further along the axis → keep the old best; if **equidistant** on the axis (`CompareFunc` true both ways), tie-break by **squared distance between rect centres**, smaller wins `:452-454`;
5. boundary-rule containment: if the rule is not `Escape` and the reply has a handler, the candidate must be a paint-descendant of the handler (`IsDescendantOf` `:463-469`);
6. `!TestWidget->IsEnabled()` `:471`;
7. `!TestWidget->SupportsKeyboardFocus()` `:477`;
8. already in the `DisabledDestinations` blacklist `:483`.

**Step 3 — boundary application and wrap** `:495-538`. When a `BestWidget` is found in this cell row,
if it lies **past the boundary** (`CompareFunc(DestSideFunc(BestWidgetRect), SourceSideFunc(SweptRect))` `:498`)
the reply's rule kicks in: `Explicit` → return `GetFocusRecipient()`; `Custom` / `CustomBoundary` →
execute `GetFocusDelegate()` with the direction (or return null if unbound); `Stop` → return null;
`Wrap` → set `CurrentSourceSide = DestSideFunc(SweptRect)`, teleport `CurrentCellPoint` to the cell at
the *opposite* edge of the boundary at the source's cross-axis centre, set `bWrapped = true`, continue.
Then a **parent-enabled** check: `UE::Slate::Private::IsParentsEnabled` walks
`Advanced_GetPaintParentWidget()` upward `:334-345`. If any ancestor is disabled, the widget is added
to `DisabledDestinations` and `FindFocusableWidget` **recurses from scratch** `:528-533` — deliberately
last, because it is the expensive check.

**Step 4 — loop termination** `:541-567`. `if (bWrapped && StartingIndex == CurrentCellProcessed) break;`
— full circle. If the next cell index would go out of range: `Wrap` performs the same teleport (and
breaks if already wrapped, meaning the source was not inside the boundary); `CustomBoundary` executes
its delegate here — the *only* place `CustomBoundary` fires on the geometric path.

**Reimplementation summary.** Uniform 128×128 px cell grid over the window; each widget registered
into every cell its render bounding rect touches; per-direction sweep of one cell-column/row at a
time, restricted to the perpendicular band covered by the swept rect; within a band pick the
candidate nearest along the travel axis, tie-broken by centre-to-centre distance; filter by user
index, beam intersection, boundary descendancy, enabled, focusable, ancestor-enabled; apply
`Stop`/`Wrap`/`Explicit`/`Custom`/`CustomBoundary` at the boundary crossing and at grid exit.

#### `UUserInterfaceSettings` — `Engine/Source/Runtime/Engine/Classes/Engine/UserInterfaceSettings.h`

The only *focus/navigation* setting here is the focus-brush rule; there are no navigation-config
settings in this class in 5.8.

```cpp
UENUM() enum class ERenderFocusRule : uint8    // :18
{
    Always,          // 0
    NonPointer,      // 1 — render unless focus came from a pointer cause
    NavigationOnly,  // 2
    Never,           // 3
};
UPROPERTY(config, EditAnywhere, Category = "Focus")
ERenderFocusRule RenderFocusRule;              // :127
```

Class declared `UCLASS(config=Engine, defaultconfig, meta=(DisplayName="User Interface"))` `:116`.
It also owns `HardwareCursors` `:130` and `SoftwareCursors` `:133`.
`USlateSettings` (`Engine/Source/Runtime/Slate/Public/SlateSettings.h`) contains **only**
`bExplicitCanvasChildZOrder` `:25` in 5.8 — no tooltip or navigation settings.

### B.3 Hit testing

#### `EVisibility` — `Engine/Source/Runtime/SlateCore/Public/Layout/Visibility.h:11`

`EVisibility` is a **struct wrapping a bitfield**, not a plain enum. Private bits `:80-91`:

```
VISPRIVATE_Visible                = 1<<0
VISPRIVATE_Collapsed              = 1<<1
VISPRIVATE_Hidden                 = 1<<2
VISPRIVATE_SelfHitTestVisible     = 1<<3
VISPRIVATE_ChildrenHitTestVisible = 1<<4
```

Named constants `:14-29`, composition `:94-107`:

| Constant | Bits | Rendered? | Occupies layout? | Self hit-testable? | Children hit-testable? |
|---|---|---|---|---|---|
| `Visible` | `Visible \| SelfHitTest \| ChildrenHitTest` (`0b11001`) | yes | yes | yes | yes |
| `Collapsed` | `Collapsed` (`0b00010`) | no | **no** | no | no |
| `Hidden` | `Hidden` (`0b00100`) | no | **yes** | no | no |
| `HitTestInvisible` | `Visible` (`0b00001`) | yes | yes | no | **no** |
| `SelfHitTestInvisible` | `Visible \| ChildrenHitTest` (`0b10001`) | yes | yes | no | **yes** |
| `All` | all five bits | — | — | — | filter value only |

Queries `AreChildrenHitTestVisible()` `:56`, `IsHitTestVisible()` `:61`, `IsVisible()` `:66`,
`DoesVisibilityPassFilter(V, Filter)` = bitwise AND ≠ 0 `:71`. Default-constructed value is
`VIS_Visible` `:38-40`. The UMG mirror `ESlateVisibility` is documented in §0.6.

#### `FHittestGrid` — `Engine/Source/Runtime/SlateCore/Public/Input/HittestGrid.h:43`

**Cell size is a hard-coded constant**: `const FVector2f FHittestGrid::CellSize(128.0f, 128.0f);` —
`HittestGrid.cpp:109` (declared `HittestGrid.h:374`).
`SetHittestArea(PosInDesktop, Dimensions, OffsetInWindow)` (`.h:68`, `.cpp:267`) recomputes
`NumCells = CeilToInt(GridSize / CellSize)` and clears everything **only when the size changed**
`:272-291`; origin updates are free.
`GetCellCoordinate(Position)` (`.cpp:749`) = `clamp(FloorToInt(Position / CellSize), 0, NumCells-1)`
per axis — note it **clamps**, so out-of-bounds points land in edge cells.

Per-widget record `FWidgetData` (`.h:187`):

```cpp
TWeakPtr<SWidget> WeakWidget;
TWeakPtr<ICustomHitTestPath> CustomPath;
FIntPoint UpperLeftCell, LowerRightCell;
int64 PrimarySort;                          // (BatchPriorityGroup << 32) | LayerId
FSlateInvalidationWidgetSortOrder SecondarySort;
int32 UserIndex;
```

`FCell` (`.h:251`) is a `TArray<int32>` of widget indices. Widgets live in a
`TSparseArray<FWidgetData> WidgetArray` with a `TMap<const SWidget*, int32> WidgetMap` `.h:336-339`.

**`AddWidget(InWidget, InBatchPriorityGroup, InLayerId, InSecondarySort)`** — `.h:112`, `.cpp:859`:

1. **Gate**: `if (!InWidget->GetVisibility().IsHitTestVisible()) return;` `:862`. So `Collapsed`, `Hidden` and `HitTestInvisible` widgets never enter the grid at all; `SelfHitTestInvisible` also fails `IsHitTestVisible()` (its `SelfHitTest` bit is clear) so it too is absent — but its *children* still register. That is exactly the semantic difference.
2. Paint-space geometry offset by `-GridWindowOrigin`, reduced to `GetRenderBoundingRect()` `:872-876`. **Render transforms are baked in here** — a rotated/scaled widget occupies the axis-aligned bounds of its transformed rect.
3. If a `PaintToHitTestTransform` is set (used by `SRetainerWidget` for non-grid-aligned retainers), the rect is re-derived through that transform `:878-888`.
4. `PrimarySort = ((int64)BatchPriorityGroup << 32) | LayerId` `:894`.
5. If already present with the same cell span, only the sort keys and user index are refreshed; otherwise remove and re-add `:897-913`.
6. Insert the widget index into every cell in `[UpperLeftCell .. LowerRightCell]` `:919-928`.

`RemoveWidget` `.cpp:932`, `UpdateWidget` (sort key only, no re-cell) `.cpp:962`, `ContainsWidget`
`.cpp:971`, `InsertCustomHitTestPath` `.cpp:976`, `AddGrid`/`RemoveGrid` for appended sub-grids
`.h:127-133`, `SetPaintToHitTestTransform` `.h:135`.

**Z-order resolution — `GetCollapsedWidgets`** `.cpp:1111-1149`. The cell's contents from **this grid
and every appended grid** are gathered, then:

```cpp
OutResult.StableSort([](const FWidgetIndex& A, const FWidgetIndex& B) {
    return WidgetDataA.PrimarySort <  WidgetDataB.PrimarySort
       || (WidgetDataA.PrimarySort == WidgetDataB.PrimarySort && WidgetDataA.SecondarySort < WidgetDataB.SecondarySort);
});
```

Back-to-front. `FSlateInvalidationWidgetSortOrder`
(`Engine/Source/Runtime/SlateCore/Public/FastUpdate/SlateInvalidationWidgetSortOrder.h`) is a single
`uint32 Order` built from the widget's position in the invalidation list; it is a **paint-order
tiebreaker within the same layer**, valid only until the next `ProcessInvalidation()`. Comparison
operators only `:32-37`.

**`GetHitIndexFromCellIndex(Params)`** `.cpp:986-1086` iterates the sorted cell **front-to-back**
(`i = Num-1 … 0` `:1016`) and returns the first widget passing all of:

1. valid weak ptr, and (for radius hit-tests only) `TestWidget->IsInteractable()` `:1024`;
2. inside the grid's **culling rect** if one is set `:1033-1036`;
3. inside the widget's **clipping state**: `TestWidget->GetCurrentClippingState()->IsPointInside(WindowSpaceCoordinate)` `:1040-1046` — this is where `SetClipping` participates in hit-testing;
4. `IsOverlappingSlateRotatedRect(WindowSpaceCoordinate, Params.Radius, WindowOrientedClipRect)` `:1073`, where `WindowOrientedClipRect` is the widget's layout rect pushed through `Inverse(AccumulatedLayoutTransform) ∘ AccumulatedRenderTransform` `:1065-1070` — **an actual rotated-rect test, not the AABB used for cell insertion**. With `PaintToHitTestTransform` set, that extra transform is concatenated `:1052-1061`.

For non-zero radius the squared distance to the rotated rect is recorded so the caller can pick the
nearest `:1076-1078`. `FGridTestingParams` (`.cpp:123`) defaults: `CellCoord(-1,-1)`, `Radius = -1.0f`,
`bTestWidgetIsInteractive = false`.

**`GetBubblePath`** — `.h:58`, `.cpp:189-265`:

```cpp
TArray<FWidgetAndPointer> GetBubblePath(FVector2D DesktopSpaceCoordinate, float CursorRadius,
        bool bIgnoreEnabledStatus, int32 UserIndex = INDEX_NONE,
        TInterval<int32> LayerRange = TInterval<int32>(INT32_MIN, INT32_MAX));
```

1. `CursorPositionInGrid = DesktopSpaceCoordinate - GridOrigin` `:193`.
2. Exact-point test with `Radius = 0.0f`, `bTestWidgetIsInteractive = false` `:197-204`.
3. If the hit widget's `UserIndex` is compatible `:210`, build the path by walking **`Advanced_GetPaintParentWidget()`** upward from the hit widget `:214-226` — the *paint* parent chain, which is what makes retainers/invalidation panels resolve correctly. Each widget's paint-space geometry is offset into desktop space by `GridOrigin - GridWindowOrigin` `:217-218`. Widgets outside `LayerRange` are skipped `:220`, where the layer is the low 32 bits of `PrimarySort` (`IsWidgetDataInLayerRange` `.cpp:181-187`).
4. **If the topmost element of the path is not a window, the whole path is discarded** `:228-231`.
5. `Algo::Reverse` → root-first order `:233`.
6. Unless `bIgnoreEnabledStatus`, the path is **truncated at the first disabled widget** (root-ward), removing it and everything below `:241-246`.
7. If nothing was truncated and the hit widget has an `ICustomHitTestPath` (`.h:31`), that path's `GetBubblePathAndVirtualCursors(...)` is appended `:251-256` — the mechanism world-space widgets use.

Event routing then bubbles leaf → root over this array (`FEventRouter::FBubblePolicy`) or tunnels
root → leaf (`FTunnelPolicy`); see `SlateApplication.cpp:5432` (PreviewMouseButtonDown, tunnel) and
`:5443` (MouseButtonDown, bubble).

#### `bIsEnabled`

* `SWidget::SetEnabled(TAttribute<bool>)` / `IsEnabled()` — `SWidget.h:992` / `:998`, backed by `EnabledStateAttribute`.
* Disabled widgets are **not removed from the grid** — they are pruned from the bubble path (`HittestGrid.cpp:241`) and rejected as navigation destinations (`:471`, plus the ancestor check `:528`). Key input skips them too (`SlateApplication.cpp:5049`, `:5117`).
* UMG: `UWidget::bIsEnabled` (`Components/Widget.h:327`, `FieldNotify`, direct access `UE_DEPRECATED(5.1)`), `GetIsEnabled()` `:541` / `SetIsEnabled(bool)` `:545` (virtual), bindable via `bIsEnabledDelegate` `:268` with `PROPERTY_BINDING_IMPLEMENTATION(bool, bIsEnabled)` `:1248`.

#### Render transform and hit-testing

* `SWidget::GetRenderTransform()` / `SetRenderTransform(TAttribute<TOptional<FSlateRenderTransform>>)` — `SWidget.h:1235` / `:1275`; pivot `:1281` / `:1288`; flow-direction-aware variants `:1240`, `:1259`.
* Cell insertion uses the **AABB** of the transformed rect (`GetRenderBoundingRect()` `HittestGrid.cpp:876`), but the final point test uses the **true rotated rect** (`HittestGrid.cpp:1065-1073`). A rotated widget is conservatively binned and exactly tested.
* UMG: `UWidget::RenderTransform` (`FWidgetTransform`, `Components/Widget.h:298`), `RenderTransformPivot` `:306` (normalised 0..1); setters `SetRenderTransform` `:504`, `SetRenderTransformAngle` `:516`, `SetRenderTransformPivot` `:531`; pushed down by `UpdateRenderTransform()` `:1162`.

#### Clipping — `EWidgetClipping` — `Engine/Source/Runtime/SlateCore/Public/Layout/Clipping.h:19`

```cpp
enum class EWidgetClipping : uint8
{
    Inherit,                          // 0 — no clip of its own; inherits the last clipper's area
    ClipToBounds,                     // 1 — clip to own bounds, intersected with the existing area
    ClipToBoundsWithoutIntersecting,  // 2 — push a NEW clip state (can render outside a clipping ancestor)
    ClipToBoundsAlways,               // 3 — intersecting clip that can never be ignored
    OnDemand,                         // 4 — behaves as ClipToBounds only when DesiredSize > allotted geometry
};
```

`SWidget::SetClipping(EWidgetClipping)` `SWidget.h:1296`, `GetClipping()` `:1299`, storage
`EWidgetClipping Clipping;` `:1956`, change hook `virtual void OnClippingChanged()` `:1753`, current
state `GetCurrentClippingState()` → `PersistentState.InitialClipState` `:1608`, layout-side helper
`CalculateCullingAndClippingRules(...)` `:712`. UMG: `UWidget::Clipping` `Components/Widget.h:435`,
`GetClipping` `:606` / `SetClipping` `:610`.
Hit-testing consults the clip state at `HittestGrid.cpp:1040-1046` — **a point outside the widget's
clipping state is not a hit even if it is inside the widget's rect**.

### B.4 `FReply` — `Engine/Source/Runtime/SlateCore/Public/Input/Reply.h`

`class FReply : public TReplyBase<FReply>` `:23`. Base `FReplyBase` (`Input/ReplyBase.h:14`) provides
`IsEventHandled()` and `GetHandler()`; `TReplyBase::SetHandler` is protected and friended to `FEventRouter`.

**Construction**

| Method | Line | Meaning |
|---|---|---|
| `static FReply Handled()` | `:233` | `[[nodiscard]]`; marks the event consumed — bubbling/tunnelling stops |
| `static FReply Unhandled()` | `:241` | `[[nodiscard]]`; keep routing |

The `bool` constructor is **private** `:258`; copy ctor/assignment are explicitly out-of-line `:248`,`:251`.

**Mutators** (all return `FReply&` for chaining)

| Method | Line | Effect |
|---|---|---|
| `CaptureMouse(TSharedRef<SWidget> InMouseCaptor)` | `:28` | route all subsequent pointer events for this pointer index to that widget |
| `UseHighPrecisionMouseMovement(TSharedRef<SWidget>)` | `:38` | raw input, no ballistics; **implies capture + hidden movement**; releasing capture deactivates it |
| `SetMousePos(const FIntPoint& NewMousePos)` | `:48` | warp the cursor (stored as `TOptional<FIntPoint> RequestedMousePos`) |
| `SetUserFocus(TSharedRef<SWidget> GiveMeFocus, EFocusCause = SetDirectly, bool bInAllUsers = false)` | `:51` | request focus |
| `ClearUserFocus(bool bInAllUsers = false)` | `:54` | inline overload → `ClearUserFocus(SetDirectly, bInAllUsers)` |
| `ClearUserFocus(EFocusCause, bool bInAllUsers = false)` | `:60` | |
| `CancelFocusRequest()` | `:67` | strips any set/clear-focus request from a cached reply |
| `SetNavigation(EUINavigation, ENavigationGenesis, ENavigationSource = FocusedWidget)` | `:70` | directional / next-previous navigation; clears `NavigationDestination` |
| `SetNavigation(TSharedRef<SWidget> Destination, ENavigationGenesis, ENavigationSource = FocusedWidget)` | `:80` | direct destination; sets `NavigationType = EUINavigation::Invalid` |
| `LockMouseToWidget(TSharedRef<SWidget>)` | `:93` | confine the cursor to the widget's bounds; clears `bShouldReleaseMouseLock` |
| `ReleaseMouseLock()` | `:103` | |
| `ReleaseMouseCapture()` | `:114` | also clears high-precision mode |
| `DetectDrag(const TSharedRef<SWidget>& DetectDragInMe, FKey MouseButton)` | `:129` | arm drag detection; `OnDragDetected` fires once the pointer moves past the trigger distance while that button is down |
| `BeginDragDrop(TSharedRef<FDragDropOperation>)` | `:143` | start a drag-drop with this payload |
| `EndDragDrop()` | `:150` | terminate the current drag-drop |
| `PreventThrottling()` | `:157` | suppress Slate's responsiveness throttling on mouse-down |

**Queries**: `ShouldReleaseMouse()` `:166`, `ShouldSetUserFocus()` `:169`, `ShouldReleaseUserFocus()` `:172`,
`AffectsAllUsers()` `:175`, `ShouldUseHighPrecisionMouse()` `:178`, `ShouldReleaseMouseLock()` `:181`,
`ShouldThrottle()` (= `!bPreventThrottling`) `:184`, `GetMouseLockWidget()` `:187`,
`GetUserFocusRecepient()` [sic] `:190`, `GetFocusCause()` `:193`, `GetMouseCaptor()` `:196`,
`GetNavigationType()` `:199`, `GetNavigationGenesis()` `:202`, `GetNavigationSource()` `:205`,
`GetNavigationDestination()` `:208`, `GetDragDropContent()` `:211`, `ShouldEndDragDrop()` `:214`,
`GetDetectDragRequest()` `:217`, `GetDetectDragRequestButton()` `:220`, `GetRequestedMousePos()` `:223`,
`ToString()` `:226`.
State `:265-285`: eight 1-bit flags — `bReleaseMouseCapture`, `bSetUserFocus`, `bReleaseUserFocus`,
`bAllUsers`, `bShouldReleaseMouseLock`, `bUseHighPrecisionMouse`, `bPreventThrottling`, `bEndDragDrop`
— plus the weak widget pointers and the shared `DragDropContent`.

**Reply execution order — `FSlateApplication::ProcessReply`** `SlateApplication.cpp:3387`:

1. Release capture if requested **or if a drag-drop is starting** `:3398-3401`.
2. Clear focus if requested (all users or this user) `:3404-3414`.
3. Cancel any in-flight drag-drop if `bDisableLastDragOnDragEnter` and a new drag is starting, or if `ShouldEndDragDrop()` `:3416-3419`.
4. If starting a drag: set the drag content on the user, send `OnMouseLeave` to everything under the previous cursor path, then `OnDragEnter` bubbled over the *current event path* `:3421-3488`. The documented resulting event order `:3482-3487` is: `B OnDragDetected` → `A OnMouseLeave` → `B OnDragEnter` → `B OnDragLeave` → `C OnDragEnter` → `C OnDragOver`.
5. Apply capture / mouse position / mouse lock — **only when the application is active** (or `bHandleDeviceInputWhenApplicationNotActive`, or a virtual window); releases of capture/lock are always allowed `:3491-3501`.
6. Navigation `:3650-3698` (see §B.1).
7. Drag-detect arming: `SlateUser->StartDragDetection(WidgetsUnderMouse->GetPathDownTo(DetectDragWidget), PointerIndex, Button, ScreenPos)` `:3700-3711`.
8. Set focus if requested `:3713-3719`.

**`FEventReply` (UMG BP wrapper)** — `Components/SlateWrapperTypes.h:128`:

```cpp
USTRUCT(BlueprintType)
struct FEventReply
{
    FEventReply(bool IsHandled = false) : NativeReply(IsHandled ? FReply::Handled() : FReply::Unhandled()) {}
    bool operator==(const FEventReply& Other) const { return NativeReply.IsEventHandled() == Other.NativeReply.IsEventHandled(); }
    FReply NativeReply;   // :141
};
```

Trait `WithIdenticalViaEquality = true` `:144-151`. **Equality compares only the handled flag** — two
replies with different capture/focus requests compare equal.

**`UWidgetBlueprintLibrary` reply helpers** — `Engine/Source/Runtime/UMG/Public/Blueprint/WidgetBlueprintLibrary.h`.
All take `UPARAM(ref) FEventReply& Reply` and return it, so BP can chain.

| Function | Line |
|---|---|
| `FEventReply Handled()` | `:135` |
| `FEventReply Unhandled()` | `:139` |
| `FEventReply CaptureMouse(FEventReply&, UWidget* CapturingWidget)` | `:143` |
| `FEventReply ReleaseMouseCapture(FEventReply&)` | `:147` |
| `FEventReply LockMouse(FEventReply&, UWidget* CapturingWidget)` | `:150` |
| `FEventReply UnlockMouse(FEventReply&)` | `:153` |
| `FEventReply SetUserFocus(FEventReply&, UWidget* FocusWidget, bool bInAllUsers = false)` | `:157` |
| `FEventReply CaptureJoystick(...)` | `:160` — **deprecated**, "Use SetUserFocus() instead" |
| `FEventReply ClearUserFocus(FEventReply&, bool bInAllUsers = false)` | `:164` |
| `FEventReply ReleaseJoystickCapture(FEventReply&, bool bInAllJoysticks = false)` | `:167` |
| `FEventReply SetMousePosition(FEventReply&, FVector2D NewMousePosition)` | `:171` |
| `FEventReply DetectDrag(FEventReply&, UWidget* WidgetDetectingDrag, FKey DragKey)` | `:181` |
| `FEventReply DetectDragIfPressed(const FPointerEvent&, UWidget* WidgetDetectingDrag, FKey DragKey)` | `:191` |
| `FEventReply EndDragDrop(FEventReply&)` | `:197` |
| `UDragDropOperation* CreateDragDropOperation(TSubclassOf<UDragDropOperation>)` | `:41` |
| `bool IsDragDropping()` | `:203` |
| `UDragDropOperation* GetDragDroppingContent()` | `:209` |
| `void CancelDragDrop()` | `:215` |
| `void SetInputMode_UIOnlyEx(APlayerController*, UWidget* InWidgetToFocus = nullptr, EMouseLockMode = DoNotLock, bool bFlushInput = false)` | `:49` |
| `void SetInputMode_GameAndUIEx(APlayerController*, UWidget* = nullptr, EMouseLockMode = DoNotLock, bool bHideCursorDuringCapture = true, bool bFlushInput = false)` | `:56` |
| `void SetInputMode_GameOnly(APlayerController*, bool bFlushInput = false)` | `:64` |
| `void SetFocusToGameViewport()` | `:68` |

`DetectDragIfPressed` (`WidgetBlueprintLibrary.cpp:405-414`) is exactly:

```cpp
if (PointerEvent.GetEffectingButton() == DragKey || PointerEvent.IsTouchEvent())
{ FEventReply Reply = Handled(); return DetectDrag(Reply, WidgetDetectingDrag, DragKey); }
return Unhandled();
```

Note the `IsTouchEvent()` short-circuit: on touch, *any* touch qualifies regardless of `DragKey`.

### B.5 Drag and drop

#### `FDragDropOperation` — `Engine/Source/Runtime/SlateCore/Public/Input/DragAndDrop.h:19`

`class FDragDropOperation : public TSharedFromThis<FDragDropOperation>`

| Member | Line | Default / semantics |
|---|---|---|
| `FDragDropOperation()` | `:27` | `DragAndDrop.cpp:12` — `bCreateNewWindow(true)` |
| `~FDragDropOperation()` | `:32` | destroys the decorator window (`DragAndDrop.cpp:17`) |
| `template<class TType> bool IsOfType() const` | `:35` | `IsOfTypeImpl(TType::GetTypeId())` |
| `virtual bool AffectedByPointerEvent(const FPointerEvent&)` | `:46` | **default `true`** |
| `virtual void OnDrop(bool bDropWasHandled, const FPointerEvent&)` | `:54` | base impl destroys the decorator window (`DragAndDrop.cpp:26`) |
| `virtual void OnDragged(const FDragDropEvent&)` | `:61` | base impl repositions the decorator window at `ScreenSpacePosition + CursorSize`, run through `CalculateTooltipWindowPosition` for screen-edge fitting (`DragAndDrop.cpp:31-46`) |
| `virtual FCursorReply OnCursorQuery()` | `:64` | override → `MouseCursorOverride`, else `MouseCursor`, else Unhandled (`DragAndDrop.cpp:48-61`) |
| `virtual TSharedPtr<SWidget> GetDefaultDecorator() const` | `:70` | **returns null by default — no decorator unless you override** |
| `virtual FVector2D GetDecoratorPosition() const` | `:73` | default `(0,0)` |
| `virtual void SetDecoratorVisibility(bool)` | `:76` | windowed: Show/HideWindow; windowless: sets the decorator widget to `HitTestInvisible` / `Hidden` (`DragAndDrop.cpp:63-90`) |
| `virtual bool IsExternalOperation() const` | `:79` | `false` |
| `virtual bool IsWindowlessOperation() const` | `:82` | `bCreateNewWindow == false` |
| `void SetCursorOverride(TOptional<EMouseCursor::Type>)` | `:89` | temporary feedback cursor (e.g. "no drop here") |
| `virtual bool IsOfTypeImpl(const FString& Type) const` | `:94` | `false` |
| `virtual FStringView GetTypeIdString() const` | `:99` | |
| `virtual void Construct()` | `:108` | `DragAndDrop.cpp:100` — `if (bCreateNewWindow) CreateCursorDecoratorWindow();` |
| `CreateCursorDecoratorWindow()` / `DestroyCursorDecoratorWindow()` | `:113` / `:118` | `SWindow::MakeCursorDecorator()` with the decorator as content |
| `bool bCreateNewWindow;` | `:131` | |
| `TSharedPtr<SWindow> CursorDecoratorWindow;` | `:134` | |
| `TOptional<EMouseCursor::Type> MouseCursor, MouseCursorOverride;` | `:137`,`:140` | |

**RTTI macro** — `DRAG_DROP_OPERATOR_TYPE(TYPE, BASE)` `:212`:

```cpp
static const FString& GetTypeId() { static FString Type = TEXT(#TYPE); return Type; }
virtual FStringView GetTypeIdString() const override { return GetTypeId(); }
virtual bool IsOfTypeImpl(const FString& Type) const override { return GetTypeId() == Type || BASE::IsOfTypeImpl(Type); }
```

String-based, chained through the base — cheap enough because the strings are static singletons
compared by content.

`FDragDropEvent : public FPointerEvent` `:145` carries `TSharedPtr<FDragDropOperation> Content`;
`GetOperation()` `:161`, `GetOperationAs<T>()` `:168` (defined in `DragAndDrop.inl`).
Delegates `FOnDragDropEnded(bool bWasDropHandled, const FDragDropEvent&)` `:184`,
`FOnDragDropUpdate(const FDragDropEvent&)` `:195`.

Built-ins: `FExternalDragOperation` `:222` for OS/OLE drags — `NewText` / `NewFiles` / `NewOperation`
`:234-238`, `HasText()` `:241` / `HasFiles()` `:243`, `DragText = 1<<0`, `DragFiles = 1<<1` `:256-257`.
`FGameDragDropOperation` `:263` overrides `GetDecoratorPosition()` from a stored
`FVector2D DecoratorPosition` `:277`; its ctor sets `bCreateNewWindow = false` — the windowless /
in-viewport variant.

#### `UDragDropOperation` — `Engine/Source/Runtime/UMG/Public/Blueprint/DragDropOperation.h:54`

```cpp
UENUM(BlueprintType) enum class EDragPivot : uint8   // :35
{
    MouseDown,     // 0 — keep the grab offset from where the drag started
    TopLeft,       // 1
    TopCenter,     // 2
    TopRight,      // 3
    CenterLeft,    // 4
    CenterCenter,  // 5
    CenterRight,   // 6
    BottomLeft,    // 7
    BottomCenter,  // 8
    BottomRight    // 9
};

UENUM(BlueprintType) enum class EUMGItemDropZone : uint8 { AboveItem, OntoItem, BelowItem, None };  // :20
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDragDropMulticast, UDragDropOperation*, Operation);   // :28
```

| Property | Line | Notes |
|---|---|---|
| `FString Tag` | `:64` | `ExposeOnSpawn`; also the fallback drag-visual text |
| `TObjectPtr<UObject> Payload` | `:71` | `ExposeOnSpawn`; arbitrary dragged data |
| `TObjectPtr<UWidget> DefaultDragVisual` | `:78` | `ExposeOnSpawn`, `DisplayName="Drag Visual"`, `BlueprintReadOnly` |
| `EDragPivot Pivot` | `:85` | `ExposeOnSpawn` |
| `FVector2D Offset` | `:89` | `AdvancedDisplay`; a **percentage** (−1..+1) of the drag visual's desired size, applied from the pivot |
| `FOnDragDropMulticast OnDrop` | `:93` | `BlueprintAssignable` |
| `FOnDragDropMulticast OnDragCancelled` | `:97` | |
| `FOnDragDropMulticast OnDragged` | `:101` | |
| `void Drop(const FPointerEvent&)` | `:105` | `BlueprintNativeEvent` |
| `void DragCancelled(const FPointerEvent&)` | `:109` | `BlueprintNativeEvent` |
| `void Dragged(const FPointerEvent&)` | `:113` | `BlueprintNativeEvent` |
| `static EUMGItemDropZone ConvertSlateDropZoneToUMG(TOptional<EItemDropZone>)` | `:60` | |

Class meta `UCLASS(BlueprintType, Blueprintable, meta=(DontUseGenericSpawnObject="True"))` `:53`.

#### `UUserWidget` drag handlers — `Blueprint/UserWidget.h`

| BP event | Line | Native | Line |
|---|---|---|---|
| `void OnDragDetected(FGeometry, const FPointerEvent&, UDragDropOperation*& Operation)` | `:746` | `NativeOnDragDetected` | `:1620` |
| `void OnDragCancelled(const FPointerEvent&, UDragDropOperation*)` | `:756` | `NativeOnDragCancelled` | `:1625` |
| `void OnDragEnter(FGeometry, FPointerEvent, UDragDropOperation*)` | `:766` | `NativeOnDragEnter` | `:1621` |
| `void OnDragLeave(FPointerEvent, UDragDropOperation*)` | `:775` | `NativeOnDragLeave` | `:1622` |
| `bool OnDragOver(FGeometry, FPointerEvent, UDragDropOperation*)` | `:787` | `NativeOnDragOver` | `:1623` |
| `bool OnDrop(FGeometry, FPointerEvent, UDragDropOperation*)` | `:799` | `NativeOnDrop` | `:1624` |
| `FEventReply OnMouseButtonDown(FGeometry, const FPointerEvent&)` | `:659` | `NativeOnMouseButtonDown` | `:1612` |

The natives are pure forwarders (`UserWidget.cpp:2560-2588`). Note `OnDragDetected` returns the
operation via an **out-parameter**, not a return value.

#### The `OnMouseButtonDown` + `DetectDragIfPressed` idiom

```
UUserWidget::OnMouseButtonDown
  └─ return UWidgetBlueprintLibrary::DetectDragIfPressed(MouseEvent, this, EKeys::LeftMouseButton);
        → FReply::Handled().DetectDrag(this, LeftMouseButton)
FSlateApplication::ProcessReply (SlateApplication.cpp:3700)
  └─ SlateUser->StartDragDetection(PathDownTo(widget), PointerIndex, Button, ScreenPos)
… on pointer move (SlateApplication.cpp:5707)
  └─ SlateUser->DetectDrag(PointerEvent, GetDragTriggerDistance())
        (SlateUser.cpp:1220) — fires when |DragStart - Current|² > DragTriggerDistance²
  └─ route OnDragDetected via FDirectPolicy (SlateApplication.cpp:5720)
UUserWidget::OnDragDetected → creates a UDragDropOperation
SObjectWidget::OnDragDetected (SObjectWidget.cpp:371-392)
  └─ FUMGDragDropOp::New(Operation, PointerIndex, ScreenCursorPos, MyGeometry.GetAbsolutePosition(), DPIScale, this)
  └─ return FReply::Handled().BeginDragDrop(DragDropOp)
```

**Drag trigger distance** — `FSlateApplication::SetupPhysicalSensitivities`,
`SlateApplication.cpp:992-1008`: computed as 1 **millimetre** converted to pixels, then floored at
**5 px on desktop** and **10 px on non-desktop**. Accessors `GetDragTriggerDistance()` /
`GetDragTriggerDistanceSquared()` / `SetDragTriggerDistance(float)` — `SlateApplication.h:1520`,
`:1523`, `:1533`. The same value seeds `FGestureDetector::LongPressAllowedMovement` `:1007`.

`Escape` during a drag cancels it:
`if (SlateUser->IsDragDropping() && Key == EKeys::Escape) { SlateUser->CancelDragDrop(); Reply = Handled(); }`
— `SlateApplication.cpp:4992-4997`.

Drag-drop release: `FSlateUser::NotifyPointerReleased(...)` (`SlateUser.cpp:1244`) removes the
drag-detection state when the trigger button is released `:1249-1254` and executes the drop via the
*passed-in* `DroppedContent` — the app clears its cached content **before** routing pointer-up so a
re-entrant `OnDrop` cannot double-fire (comment `:1271-1273`).

#### `FUMGDragDropOp` — how UMG renders the drag visual

`Engine/Source/Runtime/UMG/Public/Slate/UMGDragDropOp.h` —
`class FUMGDragDropOp : public FGameDragDropOperation, public FGCObject`. Because it derives from
`FGameDragDropOperation`, `bCreateNewWindow == false` ⇒ **windowless**: no OS decorator window; the
visual is painted into the game window.

`FUMGDragDropOp::New(...)` — `UMGDragDropOp.cpp`:
`MouseDownOffset = ScreenPositionOfDragee - PointerPosition`; `StartingScreenPos = ScreenPositionOfDragee`.
If `DefaultDragVisual == nullptr`, the decorator is `SNew(STextBlock).Text(FText::FromString(Operation->Tag))`
— **the Tag is the fallback visual**. Otherwise `DefaultDragVisual->TakeWidget()`. Wrapped in
`SNew(SDPIScaler).DPIScale(DPIScale)`, then `SlatePrepass()`, then `Construct()`.

`OnDragged` computes the decorator position each frame:

```
Position  = DragDropEvent.GetScreenSpacePosition();
Position += CachedDesiredSize * DragOperation->Offset;       // percentage offset
switch (Pivot):
  MouseDown    : Position += MouseDownOffset;
  TopLeft      : (no change)
  TopCenter    : Position -= Size * (0.5, 0)
  TopRight     : Position -= Size * (1, 0)
  CenterLeft   : Position -= Size * (0, 0.5)
  CenterCenter : Position -= Size * (0.5, 0.5)
  CenterRight  : Position -= Size * (1, 0.5)
  BottomLeft   : Position -= Size * (0, 1)
  BottomCenter : Position -= Size * (0.5, 1)
  BottomRight  : Position -= Size * (1, 1)
// 0.150 s ease-in from the grab point:
DeltaTime < 0.150 ? DecoratorPosition = StartingScreenPos + (Position - StartingScreenPos) * (DeltaTime/0.150)
                  : DecoratorPosition = Position;
```

`OnDrop`: if handled **and** the pointer index matches → `DragOperation->Drop(MouseEvent)`; otherwise
`SourceUserWidget->OnDragCancelled(...)` then `DragOperation->DragCancelled(MouseEvent)`.
`AffectedByPointerEvent` returns `DragOperation && PointerEvent.GetPointerIndex() == PointerIndex` —
multi-touch safe. `OnCursorQuery` falls back to `EMouseCursor::Default`, then lets the
`UGameViewportClient` map a software cursor widget, then lets the source `UUserWidget`'s
`bOverride_Cursor`/`GetCursor()` win.

#### Rendering the windowless decorator — `FSlateUser::DrawWindowlessDragDropContent` — `SlateUser.cpp:660-687`

There is **no `SDragDropDecorator` class** in 5.8. The windowless decorator is painted directly, per
window, as the very last thing:

```cpp
if (DragDropContent && DragDropContent->IsWindowlessOperation())
  if (DragDropWindowPtr.Pin() == WindowToDraw)
    if (DecoratorWidget && DecoratorWidget->GetVisibility().IsVisible())
    {
      const float WindowRootScale = SlateApp.GetApplicationScale() * NativeWindow->GetDPIScaleFactor();
      DecoratorWidget->SetVisibility(EVisibility::HitTestInvisible);   // never intercepts the drop
      DecoratorWidget->SlatePrepass(WindowRootScale);
      FVector2f Local = WindowGeometryInScreen.AbsoluteToLocal(DragDropContent->GetDecoratorPosition()) * WindowRootScale;
      FGeometry G = FGeometry::MakeRoot(DecoratorWidget->GetDesiredSize(), FSlateLayoutTransform(Local));
      DecoratorWidget->Paint(FPaintArgs(...), G, WindowClipRect, WindowElementList, ++MaxLayerId, FWidgetStyle(), Window->IsEnabled());
    }
```

`++MaxLayerId` puts it above everything else in the window, and `HitTestInvisible` guarantees the
decorator never blocks the widget being dropped on. Declared `SlateUser.h:145`.
For **windowed** operations (`bCreateNewWindow == true`, the editor default) the decorator lives in an
OS cursor-decorator window created by `CreateCursorDecoratorWindow()` and repositioned in
`FDragDropOperation::OnDragged`.

**Drag-drop over a game viewport**: because UMG's op is windowless and drawn into the game window at
max layer, drag-drop works inside the viewport with no OS window involvement. External OS drags enter
through `FSlateApplication::OnDragEnter(SWindow, TSharedRef<FExternalDragOperation>)`
(`SlateApplication.h:1746`), `ProcessDragEnterEvent(SWindow, const FDragDropEvent&)` `:1368`, and
`OnDragDrop(TSharedPtr<FGenericWindow>)` `:1750`.

**App-level drag state**: `FSlateApplication::IsDragDropping()` `SlateApplication.h:913`,
`IsDragDroppingAffected(const FPointerEvent&)` `:916`, `GetDragDroppingContent()` `:919`,
`CancelDragDrop()` `:922`, plus the editor hook `FDragDropCheckingOverride OnDragDropCheckOverride`
`:61`,`:1909` (consulted at `SlateApplication.cpp:5701`).
`FSlateUser`: `StartDragDetection` `SlateUser.h:173`, `DetectDrag` `:174`, `IsDetectingDrag` `:175`,
`ResetDragDetection` `:176`, `SetDragDropContent` `:178`, `ResetDragDropContent` `:179`.

### B.6 Tooltips

#### UMG — `Engine/Source/Runtime/UMG/Public/Components/Widget.h`

```cpp
UPROPERTY() FGetText ToolTipTextDelegate;                                   // :272
UE_DEPRECATED(5.1, "Direct access to ToolTipText is deprecated...")
UPROPERTY(EditAnywhere, BlueprintReadWrite, Getter, Setter, BlueprintSetter="SetToolTipText",
          Category="Behavior", meta=(MultiLine=true))
FText ToolTipText;                                                          // :277

UE_DEPRECATED(5.1, "Direct access to ToolTipWidget is deprecated...")
UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Getter="GetToolTip", Setter="SetToolTip",
          BlueprintSetter="SetToolTip", Category="Behavior", AdvancedDisplay)
TObjectPtr<UWidget> ToolTipWidget;                                          // :282

UPROPERTY() FGetWidget ToolTipWidgetDelegate;                               // :286
```

Accessors `GetToolTipText()` `:552`, `SetToolTipText(const FText&)` `:556` (`BlueprintCallable`),
`GetToolTip()` `:559`, `SetToolTip(UWidget*)` `:563` (`BlueprintCallable`). Text binding
`PROPERTY_BINDING_IMPLEMENTATION(FText, ToolTipText)` `:1247`.

`UWidget::SetToolTip(UWidget*)` — `Components/Widget.cpp:543-566` — wraps the widget in a bare `SToolTip`:

```cpp
TSharedRef<SToolTip> ToolTip = SNew(SToolTip).TextMargin(FMargin(0)).BorderImage(nullptr)
    [ ToolTipWidget->TakeWidget() ];
SafeWidget->SetToolTip(ToolTip);
```

i.e. **no padding, no background** — the supplied widget is fully responsible for its own chrome.

**There is no `GetToolTipWidget` BlueprintNativeEvent on `UWidget` in 5.8.** The deferred / on-demand
tooltip is `ToolTipWidgetDelegate` (an `FGetWidget` dynamic delegate) consumed by a private `IToolTip`
adapter class at the top of `Components/Widget.cpp` `:82-139`: `GetContentWidget()` executes the
delegate and caches the result `:82-90`, `IsEmpty()` returns `!ToolTipWidgetDelegate.IsBound()` `:109`,
`IsInteractive()` returns `false` `:117`, `OnClosed()` drops the cache `:122-127`. Binding the delegate
in the designer ("Tool Tip Widget" binding) is the UMG equivalent of a lazily-built tooltip.

#### Slate — `SWidget` tooltip surface

| Member | Line (`SlateCore/Public/Widgets/SWidget.h`) | Impl |
|---|---|---|
| `virtual TSharedPtr<IToolTip> GetToolTip()` | `:1010` | `SWidget.cpp:1228` — reads `FSlateToolTipMetaData` |
| `virtual void OnToolTipClosing()` | `:1013` | `SWidget.cpp:1237` |
| `void SetToolTipText(const TAttribute<FText>&)` | `:1373` | `SWidget.cpp:1192` — builds via `FSlateApplicationBase::MakeToolTip`; **unset attribute removes the metadata** |
| `void SetToolTipText(const FText&)` | `:1376` | `SWidget.cpp:1204` — **empty/whitespace text removes the metadata** |
| `void SetToolTip(const TAttribute<TSharedPtr<IToolTip>>&)` | `:1379` | `SWidget.cpp:1216` |
| `void EnableToolTipForceField(bool)` | `:1021` | |
| `bool HasToolTipForceField() const` | `:1024` | reads `bToolTipForceFieldEnabled : 1` `:1887` |
| `void SetToolTipForceFieldExpansion(const TAttribute<TOptional<FSlateRect>>&)` | `:1030` | custom repel zone; requires the force field enabled |
| `TOptional<FSlateRect> GetToolTipForceFieldExpansion() const` | `:1033` | union of all expansion metadata |

**Parity note.** Tooltips are stored as *optional metadata* (`FSlateToolTipMetaData`), not as a field
on every `SWidget` — that keeps `sizeof(SWidget)` down.

#### `IToolTip` and `SToolTip`

`IToolTip` — `Engine/Source/Runtime/SlateCore/Public/Widgets/IToolTip.h:11`, pure virtual:
`AsWidget()` `:20`, `GetContentWidget()` `:27`, `SetContentWidget(const TSharedRef<SWidget>&)` `:34`,
`ResetContentWidget()` `:39` (default no-op), `IsEmpty()` `:46`,
`IsInteractive()` `:53` — "can be made interactive by holding **Ctrl**", `OnOpening()` `:58`,
`OnClosed()` `:63`, `OnSetInteractiveWindowLocation(FVector2D& InOutDesiredLocation)` `:73` (default
no-op; input is the previous frame's cursor position + `SlateDefs::TooltipOffsetFromMouse`).

`SToolTip : public SCompoundWidget, public IToolTip` —
`Engine/Source/Runtime/Slate/Public/Widgets/SToolTip.h:19`. `SLATE_BEGIN_ARGS` defaults `:26-34`:

```cpp
_Text()                                                             // SLATE_ATTRIBUTE(FText, Text)                 :37
_Content()                                                          // SLATE_DEFAULT_SLOT — overrides Text          :40
_Font(FCoreStyle::Get().GetFontStyle("ToolTip.Font"))               // SLATE_ATTRIBUTE(FSlateFontInfo, Font)        :43
_TextMargin(FMargin(8.0f))                                          // SLATE_ATTRIBUTE(FMargin, TextMargin)         :46
_BorderImage(FCoreStyle::Get().GetBrush("ToolTip.Background"))      // SLATE_ATTRIBUTE(const FSlateBrush*)          :49
_IsInteractive(false)                                               // SLATE_ATTRIBUTE(bool, IsInteractive)         :52
_OnSetInteractiveWindowLocation()                                   // SLATE_EVENT(FOnSetInteractiveWindowLocation) :55
```

`DECLARE_DELEGATE_OneParam(FOnSetInteractiveWindowLocation, FVector2D&)` `:24`. Also
`static float GetToolTipWrapWidth()` `:96` and `virtual const FText& GetTextTooltip() const` `:89`.

`FSlateApplication::MakeToolTip(const TAttribute<FText>&)` / `MakeToolTip(const FText&)`
(`SlateApplication.h:1678-1679`) return an `SDeferredToolTipText` (`SlateApplication.cpp:4872-4881`) —
the tooltip widget is **not** constructed until it is actually shown.

#### Timings, gating and the force field

**All tooltip timing lives in CVars in
`Engine/Source/Runtime/Slate/Private/Framework/Application/SlateUser.cpp`, not in a settings class:**

| CVar | Variable & default | Line |
|---|---|---|
| `Slate.TooltipSummonDelay` | `float TooltipSummonDelay = 0.15f` | `:52` |
| `Slate.TooltipIntroDuration` | `float TooltipIntroDuration = 0.1f` | `:58` |
| `Slate.CursorSignificantMoveDetectionThreshold` | `float = 0.0` | `:64` |
| `Slate.AllowTooltipsWithHiddenCursor` | `bool = false` | `:70` |
| `Slate.AllowTooltipsWhileMouseDown` | `bool = false` | `:76` |
| `Slate.EnableTooltipForceFieldDraw` (WITH_SLATE_DEBUGGING) | `bool = false` | `:83` |

Positioning constants (`SlateUser.cpp:26-30`; duplicated in `SlateApplication.cpp:619-622`):

```cpp
static const FVector2f TooltipOffsetFromMouse(12.0f, 8.0f);
static const FVector2f TooltipOffsetFromForceField(4.0f, 3.0f);
```

Fade-in (`SlateUser.cpp:1642-1643`):

```cpp
const float TimeSinceSummon  = PlatformSeconds - TooltipSummonDelay - ActiveTooltipInfo.SummonTime;
const float TooltipOpacity   = FMath::Clamp(TimeSinceSummon / TooltipIntroDuration, 0.0f, 1.0f);
```

`FSlateUser::UpdateTooltip(const FMenuStack&, bool bCanSpawnNewTooltip)` — `SlateUser.cpp:1315`:

* Bail and close if `!FSlateApplication::GetAllowTooltips()` `:1322-1326`.
* `bCanSpawnNewTooltip &&= (bCanDrawCursor || bAllowTooltipsWithHiddenCursor) && (now - LastCursorSignificantMoveTime > 0.05)` — a hard-coded **50 ms motionless requirement** `:1330-1331`.
* Tooltip changes are only *checked for* when: on the game thread, not using high-precision mouse movement, **not drag-dropping**, LMB not down (unless `bAllowTooltipsWhileMouseDown`), and the app is active or the cursor is directly over a Slate window `:1336-1346`.
* Widgets under the cursor are gathered **including disabled widgets** (`bIgnoreEnabledStatus = true` `:1351`) — so disabled widgets can still show tooltips. Tooltip windows themselves are excluded `:1354-1368`.
* The path is walked **leaf → root** and the first widget whose `GetToolTip()` is valid and `!IsEmpty()` wins `:1374-1388`.
* **Force field**: the same walk unions the rects of every widget with `HasToolTipForceField()`, plus each one's `GetToolTipForceFieldExpansion()` `:1391-1412`, then expands it by the intersecting `MenuStack.GetToolTipForceFieldRect(...)` `:1469-1478`. The tooltip is then shifted out by `TooltipOffsetFromForceField` — right/below first, then above if it does not fit, with fallback candidate positions tested for intersection `:1524-1570`. Force-field repulsion is skipped once the tooltip has become interactive (`!ActiveTooltipInfo.WasInteractive` `:1524`).

`FActiveTooltipInfo` (`SlateUser.h:318-352`) holds `TWeakPtr<IToolTip> Tooltip`,
`TWeakPtr<SWidget> TooltipVisualizer`, the source widget, `DesiredLocation`, `DesiredSize`,
`SummonTime`, a cached `ETooltipOffsetDirection OffsetDirection` `:312` (cached so the tooltip does not
teleport as the cursor moves), `WasInteractive`, and a positioned flag. 5.8 keeps **per-owner tooltip
windows** in `TMap<TWeakPtr<SWindow>, TWeakPtr<SWindow>> TooltipWindowsByOwner` `:308` — required on
Wayland where tooltips are `xdg_popup`s that must be parented to the right toplevel.

App-level API: `SpawnToolTip(const TSharedRef<IToolTip>&, FVector2D SpawnLocation)`
(`SlateApplication.h:1484`, `.cpp:3747` → `GetCursorUser()->ShowTooltip(...)`), `CloseToolTip()`
(`.h:1487`, `.cpp:3752`), `UpdateToolTip(bool bAllowSpawningOfNewToolTips)` (`.h:1489`, `.cpp:3757`),
`SetAllowTooltips(bool)` / `GetAllowTooltips()` (`.h:1598-1599`, `.cpp:4115` / `:4120`, backed by
`bEnableTooltips`). Positioning helper `CalculateTooltipWindowPosition(...)`
(`SlateApplication.cpp:4138-4182`) flips the tooltip across the cursor if it would leave the work area.

**There are no tooltip settings on `UUserInterfaceSettings` or `USlateSettings` in 5.8** —
`ToolTipIntroDuration` / summon delay / `bAllowToolTips` / force-field rect are all runtime CVars or
per-user state as above.

### B.7 Input modes and capture — does the UI receive input at all?

#### `EMouseCaptureMode` — `Engine/Source/Runtime/Engine/Classes/Engine/EngineBaseTypes.h:40`

```cpp
enum class EMouseCaptureMode : uint8
{
    NoCapture,                                    // 0
    CapturePermanently,                           // 1 — capture on click, CONSUME the initiating mouse-down
    CapturePermanently_IncludingInitialMouseDown, // 2 — capture on click, let player input see the mouse-down
    CaptureDuringMouseDown,                       // 3 — capture only while a button is held
    CaptureDuringRightMouseDown,                  // 4 — RMB only
};
```

#### `EMouseLockMode` — `EngineBaseTypes.h:56`

```cpp
enum class EMouseLockMode : uint8
{
    DoNotLock,        // 0
    LockOnCapture,    // 1
    LockAlways,       // 2
    LockInFullscreen, // 3
};
```

#### `FInputMode*` — `Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerController.h`

(These live in `PlayerController.h`, **not** `PlayerInput.h` or `InputCoreTypes.h`.)

`FInputModeDataBase` `:102`: pure-virtual `ApplyInputMode(FReply& SlateOperations, UGameViewportClient&)` `:108`,
`virtual bool ShouldFlushInputOnViewportFocus() const { return true; }`, and the shared helper
`SetFocusAndLocking(...)` `:112` (impl `PlayerController.cpp:6313`) which does
`SlateOperations.SetUserFocus(Widget)` and `LockMouseToWidget(Viewport)` / `ReleaseMouseLock()`.

**`FInputModeUIOnly`** `.h:126` — ctor `MouseLockMode = EMouseLockMode::LockInFullscreen`
(`PlayerController.cpp:6352-6356`). `SetWidgetToFocus` logs an **Error** if the widget does not
`SupportsKeyboardFocus()` `:6343-6346`. `ApplyInputMode` `:6372-6387`:

```cpp
SetFocusAndLocking(...);  SlateOperations.ReleaseMouseCapture();
GameViewportClient.SetMouseLockMode(MouseLockMode);
GameViewportClient.SetIgnoreInput(true);
GameViewportClient.SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
```

**`FInputModeGameAndUI`** `.h:181` — ctor `MouseLockMode = DoNotLock`,
`bHideCursorDuringCapture = true` `:6389-6394`. `ShouldFlushInputOnViewportFocus()` returns **false**
`.h:196`. `ApplyInputMode` `:6398-6414`:

```cpp
SetFocusAndLocking(...);  SlateOperations.ReleaseMouseCapture();
SetMouseLockMode(MouseLockMode);  SetIgnoreInput(false);
SetHideCursorDuringCapture(bHideCursorDuringCapture);
SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringMouseDown);
```

**`FInputModeGameOnly`** `.h:212` — ctor `bConsumeCaptureMouseDown = true` `:6424-6427`.
`ApplyInputMode` `:6439-6452`:

```cpp
SlateOperations.UseHighPrecisionMouseMovement(Viewport);
SlateOperations.SetUserFocus(Viewport);
SlateOperations.LockMouseToWidget(Viewport);
SetMouseLockMode(EMouseLockMode::LockOnCapture);  SetIgnoreInput(false);
SetMouseCaptureMode(bConsumeCaptureMouseDown ? CapturePermanently : CapturePermanently_IncludingInitialMouseDown);
```

Entry point `APlayerController::SetInputMode(const FInputModeDataBase&)` (`PlayerController.cpp:6454`)
→ `InData.ApplyInputMode(LocalPlayer->GetSlateOperations(), *GameViewportClient)` `:6460`. Note that
the input mode is expressed as **a deferred `FReply`** (`GetSlateOperations()`), applied on the next
Slate tick — the same reply mechanism as §B.4.

The decisive knob for "does UI get input at all" is `UGameViewportClient::SetIgnoreInput(bool)`:
`true` under UIOnly (game input suppressed, UI only), `false` under GameAndUI and GameOnly.

### B.8 Gotchas worth carrying over

1. **`FNavigationReply` has no public ctor and defaults to `Escape`.** The bubble stops at the first non-`Escape` reply; that widget becomes the boundary rect for the geometric search.
2. **`EUINavigationRule` order is Escape, Explicit, Wrap, Stop, Custom, CustomBoundary, Invalid** — do not serialise it by integer against a different order.
3. **`Custom` fires eagerly** (in `CalculateDestinationWidget`); **`CustomBoundary` fires only inside the grid sweep**, at the boundary crossing or at grid exit.
4. **`SWidget::SupportsKeyboardFocus()` returns `false` by default.** `bCanSupportFocus` is a *separate*, coarser flag used for invalidation.
5. **Focus is a path, not a pointer**, and it is versioned — any handler that re-enters and re-focuses aborts the in-flight focus change (`FocusVersion`).
6. `SetUserFocus` **resolves leaf→root to the first focusable widget in the path**, and returns `false` (no events) if that resolves to the already-focused widget.
7. **Hit-grid cells are 128×128 px, hard-coded**; widgets are binned by AABB but tested against the true rotated rect plus the clipping state plus the culling rect.
8. **Z-order = `(BatchPriorityGroup << 32) | LayerId`, tiebroken by `FSlateInvalidationWidgetSortOrder`**; the bubble path is built from the **paint** parent chain and is discarded entirely if it does not terminate at a window.
9. `HitTestInvisible` / `SelfHitTestInvisible` / `Collapsed` / `Hidden` widgets are **never added to the grid**; the difference between the two invisible modes is entirely in whether *children* register.
10. `FReply::Handled()`/`Unhandled()` are `[[nodiscard]]`; `FEventReply::operator==` compares only the handled bit.
11. Drag trigger distance = max(1 mm in px, 5 px desktop / 10 px mobile). UMG's drag visual is windowless, `HitTestInvisible`, painted at `++MaxLayerId`, with a **0.150 s ease-in** from the grab point and a `Pivot` + percentage-`Offset` placement model.
12. Tooltips: **0.15 s summon delay, 0.10 s fade-in, 50 ms motionless requirement**, suppressed while drag-dropping or while LMB is down, queried over the *disabled-inclusive* path, repelled from force-field rects by (4, 3) px and offset from the cursor by (12, 8) px.

---

## Capability checklist

Flat, dense enumeration for diffing against another engine's UI system. Format:
`Class (SlateWidget) — prop:Type=default; …  | events: …  | style: FStruct{fields}`.
`†` = deprecated direct access (getter/setter only). `‡` = construction-time only (no runtime setter).
`X` = Experimental. Panels are named only (sibling report).

### C.1 Universal — every widget

```
UWidget — Slot:UPanelSlot*; ToolTipText:FText†; ToolTipTextDelegate:FGetText; ToolTipWidget:UWidget*†;
  ToolTipWidgetDelegate:FGetWidget; VisibilityDelegate:FGetSlateVisibility; RenderTransform:FWidgetTransform†;
  RenderTransformPivot:FVector2D†; FlowDirectionPreference:EFlowDirectionPreference†; bIsVariable:bit;
  bCreatedByConstructionScript:bit; bIsEnabled:bit†(FieldNotify); bOverride_Cursor:bit; bIsVolatile:bit;
  bWrappedByComponent:bit; Cursor:EMouseCursor::Type†; Clipping:EWidgetClipping†; Visibility:ESlateVisibility†(FieldNotify);
  PixelSnapping:EWidgetPixelSnapping; RenderOpacity:float†(0..1); AccessibleWidgetData:USlateAccessibleWidgetData*;
  Navigation:UWidgetNavigation*=null
  editor-only — bOverrideAccessibleDefaults, bCanChildrenBeAccessible, AccessibleBehavior:ESlateAccessibleBehavior,
  AccessibleSummaryBehavior, AccessibleText:FText, AccessibleTextDelegate, AccessibleSummaryText,
  AccessibleSummaryTextDelegate, bHiddenInDesigner, bExpandedInDesigner, bLockedInDesigner
  API — SetRenderTransform/Scale/Shear/TransformAngle/Translation/TransformPivot, GetRenderTransformAngle,
  Get/SetIsEnabled, IsInViewport, Get/SetToolTipText, Get/SetToolTip, Get/SetCursor, ResetCursor, IsRendered,
  IsVisible, Get/SetVisibility, Get/SetRenderOpacity, Get/SetClipping, Get/SetPixelSnapping, ForceVolatile,
  IsHovered, HasKeyboardFocus, SupportsKeyboardFocus, HasMouseCapture, HasMouseCaptureByUser, SetKeyboardFocus,
  HasUserFocus, HasAnyUserFocus, HasFocusedDescendants, HasUserFocusedDescendants, SetFocus, SetUserFocus,
  ForceLayoutPrepass, InvalidateLayoutAndVolatility, GetDesiredSize, SetAllNavigationRules,
  SetNavigationRule†(4.23), SetNavigationRuleBase/Explicit/Custom/CustomBoundary, SetNavigationMethod,
  GetParent, RemoveFromParent, GetCachedGeometry, GetTickSpaceGeometry, GetPaintSpaceGeometry,
  GetGameInstance, GetOwningPlayer, GetOwningLocalPlayer, GetAccessibleText, GetAccessibleSummaryText
```

Enums every widget depends on:
`ESlateVisibility{Visible,Collapsed,Hidden,HitTestInvisible,SelfHitTestInvisible}` ·
`EWidgetClipping{Inherit,ClipToBounds,ClipToBoundsWithoutIntersecting,ClipToBoundsAlways,OnDemand}` ·
`EWidgetPixelSnapping` · `EFlowDirectionPreference` · `ESlateAccessibleBehavior{NotAccessible,Auto,Summary,Custom,ToolTip}` ·
`ESlateSizeRule{Automatic,Fill}` · `FSlateChildSize{Value=1.0f,SizeRule=Fill}` ·
`EVirtualKeyboardType{Default,Number,Web,Email,Password,AlphaNumeric}`.

Binding delegate types: `FGetBool FGetFloat FGetInt32 FGetText FGetSlateColor FGetLinearColor FGetSlateBrush
FGetSlateVisibility FGetMouseCursor FGetCheckBoxState FGetWidget FGenerateWidgetForString
FGenerateWidgetForObject FOnReply FOnPointerEvent`.

Designer/compile metadata: `EntryClass EntryInterface DesignerRebuild BindWidget BindWidgetOptional
OptionalWidget BindWidgetAnim BindWidgetAnimOptional IsBindableEvent MustImplement ExposeOnSpawn
InlineEditConditionToggle EditCondition EditConditionHides AdvancedDisplay ShowOnlyInnerProperties
TitleProperty RowType RequiredAssetDataTags GetOptions ClampMin ClampMax UIMin UIMax sRGB MultiLine
AllowPrivateAccess ValidEnumValues DisplayAfter PrioritizeCategories DisableNativeTick`.
`EWidgetDesignFlags{None,Designing,ShowOutline,ExecutePreConstruct,Previewing}`.

### C.2 Authored widget

```
UUserWidget (SObjectWidget) — ColorAndOpacity:FLinearColor(+delegate); ForegroundColor:FSlateColor(+delegate);
  Padding:FMargin; Priority:int32; bIsFocusable:bit†(5.2, ctor-only); bStopAction:bit;
  bAutomaticallyRegisterInputOnConstruction:bit; TickFrequency:EWidgetTickFrequency; DesiredFocusWidget:FWidgetChild;
  editor-only DesignTimeSize, DesignSizeMode, PaletteCategory, PreviewBackground
  | events(BP): OnVisibilityChanged | lifecycle: OnInitialized, PreConstruct(bIsDesignTime), Construct, Destruct,
  Tick(Geometry,Delta), OnPaint(FPaintContext&), IsInteractable
  | input: OnFocusReceived→FEventReply, OnFocusLost, OnAddedToFocusPath, OnRemovedFromFocusPath, OnKeyChar,
  OnPreviewKeyDown, OnKeyDown, OnKeyUp, OnAnalogValueChanged, OnMouseButtonDown, OnPreviewMouseButtonDown,
  OnMouseButtonUp, OnMouseMove, OnMouseEnter, OnMouseLeave, OnMouseWheel, OnMouseButtonDoubleClick,
  OnTouchGesture, OnTouchStarted, OnTouchMoved, OnTouchEnded, OnMotionDetected, OnMouseCaptureLost,
  OnTouchForceChanged, OnTouchFirstMove
  | drag: OnDragDetected(out Operation), OnDragCancelled, OnDragEnter, OnDragLeave, OnDragOver→bool, OnDrop→bool
  | viewport: AddToViewport(ZOrder=0), AddToPlayerScreen(ZOrder=0), RemoveFromViewport,
  SetPositionInViewport(Pos,bRemoveDPIScale=true), SetDesiredSizeInViewport, SetAnchorsInViewport,
  SetAlignmentInViewport, GetAnchorsInViewport, GetAlignmentInViewport, GetIsVisible, SetOwningPlayer,
  GetOwningPlayerPawn, GetOwningPlayerCameraManager, SetDesiredFocusWidget
  | animation: PlayAnimation(Anim,StartAt=0,Loops=1,PlayMode=Forward,Speed=1,bRestoreState=false),
  PlayAnimationTimeRange, PlayAnimationForward, PlayAnimationReverse, StopAnimation, StopAllAnimations,
  PauseAnimation, Get/SetAnimationCurrentTime, IsAnimationPlaying, IsAnyAnimationPlaying, SetNumLoopsToPlay,
  SetPlaybackSpeed, ReverseAnimation, IsAnimationPlayingForward, FlushAnimations, IsPlayingAnimation,
  Queue{PlayAnimation,PlayAnimationTimeRange,PlayAnimationForward,PlayAnimationReverse,StopAnimation,
  StopAllAnimations,PauseAnimation}, BindToAnimationStarted/Finished/Event, OnAnimationStarted/Finished
  | extensions: Get/GetAll/Add/GetOrAdd/Remove/RemoveExtensions (UUserWidgetExtension)
  | legacy input: ListenForInputAction, StopListeningForInputAction, StopListeningForAllInputActions,
  Register/UnregisterInputComponent, IsListeningForInputAction, SetInputActionPriority, SetInputActionBlocking
  | named slots: NamedSlotBindings (FNamedSlotBinding{Name,Guid,Content}); CancelLatentActions,
  StopAnimationsAndLatentActions, PlaySound
```

### C.3 Common widgets

```
UTextLayoutWidget(abstract) — ShapedTextOptions:FShapedTextOptions{bOverride_TextShapingMethod=false,
  bOverride_TextFlowDirection=false, TextShapingMethod=Auto, TextFlowDirection=Auto};
  Justification:ETextJustify=Left; WrappingPolicy:ETextWrappingPolicy=DefaultWrapping; AutoWrapText:bit=false;
  ApplyLineHeightToBottomLine:bool=true; FontFacesLoadingPaintPolicy=DoNotPaint; WrapTextAt:float=0.0f;
  Margin:FMargin=0; LineHeightPercentage:float=1.0f | events: OnAllFontFacesFinishLoading

UTextBlock (STextBlock, DisplayName "Text") : UTextLayoutWidget
  — Text:FText†(MultiLine)+TextDelegate; ColorAndOpacity:FSlateColor=White†+delegate; MinDesiredWidth:float=0†;
  Font:FSlateFontInfo=Roboto/24/Bold†; StrikeBrush:FSlateBrush†; ShadowOffset:FVector2D=(1,1)†;
  ShadowColorAndOpacity:FLinearColor=Transparent†+delegate; bWrapWithInvalidationPanel:bool=false;
  TextTransformPolicy=None†; TextOverflowPolicy=Clip†; bSimpleTextMode:bool=false(DesignerRebuild)
  | events: (none) | style: NONE — flat font/colour/shadow
  | API: Get/SetText, Get/SetColorAndOpacity, SetOpacity, Get/SetShadowColorAndOpacity, Get/SetShadowOffset,
  Get/SetFont, Get/SetStrikeBrush, Get/SetMinDesiredWidth, SetAutoWrapText, Get/SetTextTransformPolicy,
  Get/SetTextOverflowPolicy, SetFontMaterial, SetFontOutlineMaterial, Set/GetFontSize(DPI-aware),
  GetDynamicFontMaterial, GetDynamicOutlineMaterial

URichTextBlock (SRichTextBlock) : UTextLayoutWidget — visibility ctor-set SelfHitTestInvisible
  — Text:FText†(MultiLine); TextStyleSet:UDataTable*†(RowStructure=RichTextStyleRow);
  DecoratorClasses:TArray<TSubclassOf<URichTextBlockDecorator>>; DefaultTextStyleOverride:FTextBlockStyle†;
  MinDesiredWidth:float†; bOverrideDefaultStyle:bool=false; TextTransformPolicy=None†; TextOverflowPolicy=Clip†
  | style: per-run FTextBlockStyle from a DataTable row (FRichTextStyleRow{TextStyle:FTextBlockStyle})
  | API: SetDefaultColorAndOpacity, SetDefaultShadowColorAndOpacity, SetDefaultShadowOffset, SetDefaultFont,
  SetDefaultStrikeBrush, SetMinDesiredWidth, SetAutoWrapText, SetTextTransformPolicy, SetTextOverflowPolicy,
  SetDefaultTextStyle, SetDefaultMaterial, ClearAllDefaultStyleOverrides, GetDefaultDynamicMaterial,
  SetDecorators, Get/SetText, Get/SetTextStyleSet, GetDecoratorByClass, RefreshTextLayout
  | extension points: CreateDecorators, CreateMarkupParser, CreateMarkupWriter, UpdateStyleData,
  ApplyUpdatedDefaultTextStyle; ValidateCompiledDefaults
URichTextBlockDecorator(Abstract,Blueprintable) — CreateDecorator(URichTextBlock*)
  FRichTextDecorator : ITextDecorator — Supports(RunParseResult,Text), CreateDecoratorWidget(RunInfo,DefaultStyle),
  CreateDecoratorText(RunInfo, InOutStyle, InOutString)
URichTextBlockImageDecorator(Abstract,Blueprintable) — ImageSet:UDataTable*(RowStructure=RichImageRow);
  FRichImageRow{Brush:FSlateBrush}; markup <img id="NameOfBrushInTable"></>

UImage (SImage) — Brush:FSlateBrush†(FieldNotify)+BrushDelegate; ColorAndOpacity:FLinearColor=White†(sRGB)+delegate;
  bFlipForRightToLeftFlowDirection:bool=false†; OnMouseButtonDownEvent:FOnPointerEvent(IsBindableEvent)
  | style: NONE (bare brush) | async: RequestAsyncLoad/CancelImageStreaming/OnImageStreamingStarted/Complete
  | API: Set/GetColorAndOpacity, SetOpacity, SetBrushSize†(5.0), SetDesiredSizeOverride, SetBrushTintColor,
  SetBrushResourceObject, Set/GetBrush, SetBrushFromAsset, SetBrushFromTexture(tex,bMatchSize=false),
  SetBrushFromAtlasInterface(atlas,bMatchSize=false), SetBrushFromTextureDynamic(tex,bMatchSize=false),
  SetBrushFromMaterial, SetBrushFromSoftTexture(soft,bMatchSize=false), SetBrushFromSoftMaterial,
  GetDynamicMaterial, Set/ShouldFlipForRightToLeftFlowDirection

UButton (SButton) : UContentWidget — WidgetStyle:FButtonStyle†(DisplayName "Style")=DefaultStyleCache;
  ColorAndOpacity:FLinearColor=White†; BackgroundColor:FLinearColor=White†; ClickMethod=DownAndUp†;
  TouchMethod=DownAndUp†; PressMethod=DownAndUp†; IsFocusable:bool=true†‡; bAllowDragDrop:bool=false
  | events(BP): OnClicked, OnPressed, OnReleased, OnHovered, OnUnhovered
  | events(native): OnButtonDragDetected, OnButtonDragEnter, OnButtonDragLeave, OnButtonDragOver, OnButtonDrop,
  OnReceivedFocus, OnLostFocus
  | style: FButtonStyle{Normal,Hovered,Pressed,Disabled:FSlateBrush; NormalForeground,HoveredForeground,
  PressedForeground,DisabledForeground:FSlateColor; NormalPadding,PressedPadding:FMargin;
  PressedSlateSound,ClickedSlateSound,HoveredSlateSound:FSlateSound; +2 deprecated FName sounds}
  | API: Get/SetStyle, Get/SetColorAndOpacity, Get/SetBackgroundColor, IsPressed, Get/SetClickMethod,
  Get/SetTouchMethod, Get/SetPressMethod, GetIsFocusable, SetAllowDragDrop

UCheckBox (SCheckBox) : UContentWidget — CheckedState:ECheckBoxState=Unchecked†(FieldNotify);
  CheckedStateDelegate†(5.2→InitCheckedStateDelegate); WidgetStyle:FCheckBoxStyle†; HorizontalAlignment=HAlign_Fill;
  ClickMethod=DownAndUp†; TouchMethod=DownAndUp†; PressMethod=DownAndUp†; IsFocusable:bool=true†‡
  | events: OnCheckStateChanged(bool bIsChecked)
  | style: FCheckBoxStyle{CheckBoxType:ESlateCheckBoxType; Unchecked/UncheckedHovered/UncheckedPressed;
  Checked/CheckedHovered/CheckedPressed; Undetermined/UndeterminedHovered/UndeterminedPressed :FSlateBrush;
  Padding:FMargin; BackgroundImage/BackgroundHoveredImage/BackgroundPressedImage:FSlateBrush;
  ForegroundColor,HoveredForeground,PressedForeground:FSlateColor}
  | API: IsPressed, IsChecked, GetCheckedState, SetIsChecked(bool), SetCheckedState(ECheckBoxState),
  Get/SetWidgetStyle, Get/SetClickMethod, Get/SetTouchMethod, Get/SetPressMethod, GetIsFocusable
  | widget-state: UWidgetCheckedStateRegistration{Unchecked,Checked,Undetermined}, StateName="CheckedState"

USlider (SSlider) — Value:float=0†(FieldNotify,UIMin0/UIMax1)+ValueDelegate; MinValue:float=0.0f†;
  MaxValue:float=1.0f†; WidgetStyle:FSliderStyle†; Orientation=Orient_Horizontal†; SliderBarColor=White†;
  SliderHandleColor=White†; IndentHandle:bool=false†; Locked:bool=false†; MouseUsesStep:bool=false;
  RequiresControllerLock:bool=TRUE; StepSize:float=0.01f†; IsFocusable:bool=true; bPreventThrottling:bool=false
  | events: OnMouseCaptureBegin, OnMouseCaptureEnd, OnControllerCaptureBegin, OnControllerCaptureEnd,
  OnValueChanged(float)
  | style: FSliderStyle{NormalBarImage,HoveredBarImage,DisabledBarImage,NormalThumbImage,HoveredThumbImage,
  DisabledThumbImage:FSlateBrush; BarThickness:float}
  | API: GetValue, GetNormalizedValue, SetValue, Get/SetMinValue, Get/SetMaxValue, Get/SetWidgetStyle,
  Get/SetOrientation, HasIndentHandle/SetIndentHandle, IsLocked/SetLocked, Get/SetStepSize,
  Get/SetSliderBarColor, Get/SetSliderHandleColor

UProgressBar (SProgressBar) — WidgetStyle:FProgressBarStyle†(FillImage.TintColor forced White);
  Percent:float=0†(FieldNotify,0..1)+PercentDelegate; BarFillType=LeftToRight†; BarFillStyle=Mask†;
  bIsMarquee:bool=false†(FieldNotify); BorderPadding:FVector2D=(0,0)†;
  FillColorAndOpacity:FLinearColor=White†(FieldNotify)+delegate
  | events: (none)
  | style: FProgressBarStyle{BackgroundImage,FillImage,MarqueeImage:FSlateBrush; EnableFillAnimation:bool}
  | API: Get/SetWidgetStyle, Get/SetPercent, Get/SetBarFillType, Get/SetBarFillStyle, UseMarquee/SetIsMarquee,
  Get/SetBorderPadding, Get/SetFillColorAndOpacity

USpacer (SSpacer) — Size:FVector2D=(1.0f,1.0f)†; ctor bIsVariable=false, visibility=SelfHitTestInvisible
  | API: Get/SetSize

UBorder (SBorder) : UContentWidget — HorizontalAlignment=HAlign_Fill†; VerticalAlignment=VAlign_Fill†;
  bShowEffectWhenDisabled:bit=true†; ContentColorAndOpacity=White†(sRGB)+delegate; Padding:FMargin=(4,2)†;
  Background:FSlateBrush(DisplayName "Brush", BlueprintReadOnly)+BackgroundDelegate; BrushColor=White†(sRGB)+delegate;
  DesiredSizeScale:FVector2D=(1,1)†; bFlipForRightToLeftFlowDirection:bool=false
  | bindable events: OnMouseButtonDownEvent, OnMouseButtonUpEvent, OnMouseMoveEvent, OnMouseDoubleClickEvent
  | style: NONE
  | API: Get/SetContentColorAndOpacity, Get/SetPadding, Get/SetHorizontalAlignment, Get/SetVerticalAlignment,
  Get/SetBrushColor, SetBrush, SetBrushFromAsset, SetBrushFromTexture, SetBrushFromMaterial,
  Get/SetShowEffectWhenDisabled, GetDynamicMaterial, Get/SetDesiredSizeScale

UThrobber (SThrobber) — NumberOfPieces:int32=3†(1..25); bAnimateHorizontally:bool=true†;
  bAnimateVertically:bool=true†; bAnimateOpacity:bool=true†; Image:FSlateBrush†
  | API: Set/GetNumberOfPieces, Set/IsAnimateHorizontally, Set/IsAnimateVertically, Set/IsAnimateOpacity, Set/GetImage

UCircularThrobber (SCircularThrobber) — NumberOfPieces:int32=6†(1..25); Period:float=0.75f†;
  Radius:float=16.0f†(EditCondition bEnableRadius); Image:FSlateBrush†; bEnableRadius:bool=true(Transient)
  | API: Set/GetNumberOfPieces, Set/GetPeriod, Set/GetRadius, Set/GetImage
  | NOTE: no USpinningImage in UE 5.8
```

### C.4 Input widgets

```
UEditableText (SEditableText) — Text:FText†(FieldNotify)+TextDelegate; HintText:FText†(MultiLine)+HintTextDelegate;
  WidgetStyle:FEditableTextStyle(ShowOnlyInnerProperties, font Roboto/24/Regular); IsReadOnly:bool=false†;
  IsPassword:bool=false†; MinimumDesiredWidth:float=0.0f†; IsCaretMovedWhenGainFocus:bool=TRUE†;
  SelectAllTextWhenFocused:bool=false†; RevertTextOnEscape:bool=false†; ClearKeyboardFocusOnCommit:bool=TRUE†;
  SelectAllTextOnCommit:bool=false†; AllowContextMenu:bool=true; KeyboardType=Default;
  VirtualKeyboardOptions:FVirtualKeyboardOptions; VirtualKeyboardTrigger=OnFocusByPointer;
  VirtualKeyboardDismissAction=TextChangeOnDismiss; Justification=Left†; OverflowPolicy=Clip†;
  ShapedTextOptions:FShapedTextOptions; EnableIntegratedKeyboard:bool=false; FontFacesLoadingPaintPolicy=DoNotPaint
  ctor also sets Clipping=ClipToBounds
  | events: OnTextChanged(FText), OnTextCommitted(FText,ETextCommit::Type), OnAllFontFacesFinishLoading
  | style: FEditableTextStyle{Font:FSlateFontInfo; ColorAndOpacity:FSlateColor; BackgroundImageSelected,
  BackgroundImageComposing, CaretImage:FSlateBrush}
  | API: Get/SetText, Get/SetIsPassword, Get/SetHintText, Get/SetMinimumDesiredWidth,
  Get/SetIsCaretMovedWhenGainFocus, Get/SetSelectAllTextWhenFocused, Get/SetRevertTextOnEscape,
  Get/SetClearKeyboardFocusOnCommit, Get/SetSelectAllTextOnCommit, SetWidgetStyle, Get/SetIsReadOnly,
  Get/SetJustification, Get/SetTextOverflowPolicy, SetKeyboardType, Get/SetFont, SetFontMaterial,
  SetFontOutlineMaterial, Get/SetEnableIntegratedKeyboard, ToggleVirtualKeyboard(bShow),
  SetFontFacesLoadingPaintPolicy

UEditableTextBox (SEditableTextBox, DisplayName "Text Box") — same property set as UEditableText except
  WidgetStyle:FEditableTextBoxStyle; MinimumDesiredWidth setter is SetMinDesiredWidth; no EnableIntegratedKeyboard
  | events: OnTextChanged, OnTextCommitted, OnCursorMovedWithSelectionEvent(FTextLocation,FTextSelection)[native]
  | style: FEditableTextBoxStyle{BackgroundImageNormal/Hovered/Focused/ReadOnly:FSlateBrush; Padding:FMargin;
  Font_DEPRECATED; TextStyle:FTextBlockStyle; ForegroundColor, BackgroundColor, ReadOnlyForegroundColor,
  FocusedForegroundColor:FSlateColor; HScrollBarPadding, VScrollBarPadding:FMargin; ScrollBarStyle:FScrollBarStyle}
  | extra API: SetError(FText), ClearError, HasError, IsAnyTextSelected, SetForegroundColor, Get/SetWidgetStyle

UMultiLineEditableText (SMultiLineEditableText, DisplayName "Editable Text (Multi-Line)") : UTextLayoutWidget
  — Text:FText†(FieldNotify,MultiLine); HintText:FText†(MultiLine)+HintTextDelegate;
  WidgetStyle:FTextBlockStyle(ShowOnlyInnerProperties, font Roboto/12/Bold); bIsReadOnly:bool=false†;
  SelectAllTextWhenFocused:bool=false†; ClearTextSelectionOnFocusLoss:bool=TRUE†; RevertTextOnEscape:bool=false†;
  ClearKeyboardFocusOnCommit:bool=TRUE†; AllowContextMenu:bool=true; VirtualKeyboardOptions;
  VirtualKeyboardDismissAction=TextChangeOnDismiss; ctor sets Clipping=ClipToBounds and AutoWrapText=TRUE
  | events: OnTextChanged, OnTextCommitted
  | style: FTextBlockStyle{Font; ColorAndOpacity(DisplayName "Color"); ShadowOffset; ShadowColorAndOpacity;
  SelectedBackgroundColor; HighlightColor; HighlightShape; StrikeBrush; UnderlineBrush}
  | API: Get/SetText, Get/SetHintText, Get/SetSelectAllTextWhenFocused, Get/SetClearTextSelectionOnFocusLoss,
  Get/SetRevertTextOnEscape, Get/SetClearKeyboardFocusOnCommit, Get/SetIsReadOnly, SetWidgetStyle,
  Get/SetFont, SetFontMaterial, SetFontOutlineMaterial

UMultiLineEditableTextBox (SMultiLineEditableTextBox, DisplayName "Text Box (Multi-Line)") : UTextLayoutWidget
  — Text†(FieldNotify); HintText†+delegate; WidgetStyle:FEditableTextBoxStyle;
  TextStyle_DEPRECATED:FTextBlockStyle(5.1); bIsReadOnly:bool=false†; AllowContextMenu:bool=true;
  VirtualKeyboardOptions; VirtualKeyboardDismissAction=TextChangeOnDismiss; ctor AutoWrapText=TRUE, font Roboto/24/Regular
  | events: OnTextChanged, OnTextCommitted
  | API: Get/SetText, Get/SetHintText, SetError, Get/SetIsReadOnly, SetTextStyle, SetForegroundColor
  | TODO in source: no setter for ReadOnlyForegroundColor / BackgroundColor / Font

USpinBox (SSpinBox<float>) — Value:float=0†(FieldNotify)+ValueDelegate; WidgetStyle:FSpinBoxStyle†;
  MinFractionalDigits:int32=1†; MaxFractionalDigits:int32=6†; bAlwaysUsesDeltaSnap:bool=false†;
  bEnableSlider:bool=true†; Delta:float=0†; SliderExponent:float=1†; Font:FSlateFontInfo=Roboto/12/Bold†;
  Justification=Left†; MinDesiredWidth:float=0†; KeyboardType=Number; VirtualKeyboardDismissAction=TextChangeOnDismiss;
  ClearKeyboardFocusOnCommit:bool=FALSE†; SelectAllTextOnCommit:bool=TRUE†; ForegroundColor:FSlateColor†;
  bOverride_MinValue/MaxValue/MinSliderValue/MaxSliderValue:bit; MinValue=0† MaxValue=0† MinSliderValue=0† MaxSliderValue=0†
  | events: OnValueChanged(float), OnValueCommitted(float,ETextCommit::Type), OnBeginSliderMovement,
  OnEndSliderMovement(float)
  | style: FSpinBoxStyle{BackgroundBrush, ActiveBackgroundBrush, HoveredBackgroundBrush, ActiveFillBrush,
  HoveredFillBrush, InactiveFillBrush, ArrowsImage:FSlateBrush; ForegroundColor:FSlateColor;
  TextPadding, InsetPadding:FMargin}
  | API: Get/SetValue, Get/SetWidgetStyle, Get/SetMin/MaxFractionalDigits, Get/SetAlwaysUsesDeltaSnap,
  Get/SetEnableSlider, Get/SetDelta, Get/SetSliderExponent, Get/SetFont, Get/SetJustification,
  Get/SetMinDesiredWidth, Get/SetClearKeyboardFocusOnCommit, Get/SetSelectAllTextOnCommit,
  Get/Set/ClearMinValue, Get/Set/ClearMaxValue, Get/Set/ClearMinSliderValue, Get/Set/ClearMaxSliderValue,
  Get/SetForegroundColor

UComboBoxString (SComboBox<TSharedPtr<FString>>, DisplayName "ComboBox (String)")
  — DefaultOptions:TArray<FString>; SelectedOption:FString(FieldNotify); WidgetStyle:FComboBoxStyle†;
  ItemStyle:FTableRowStyle†; ScrollBarStyle:FScrollBarStyle†‡; ContentPadding:FMargin=(4,2)†;
  MaxListHeight:float=450.0f†; HasDownArrow:bool=true†; EnableGamepadNavigationMode:bool=TRUE†;
  Font:FSlateFontInfo=Roboto/16/Bold†‡; ForegroundColor:FSlateColor=ItemStyle.TextColor†‡(DesignerRebuild);
  bIsFocusable:bool=true†‡
  | events: OnGenerateWidgetEvent(FGenerateWidgetForString,IsBindableEvent),
  OnSelectionChanged(FString,ESelectInfo::Type), OnOpening()
  | style: FComboBoxStyle{ComboButtonStyle:FComboButtonStyle; PressedSlateSound; SelectionChangeSlateSound;
  ContentPadding:FMargin; MenuRowPadding:FMargin; +2 deprecated FName sounds}
  FComboButtonStyle{ButtonStyle:FButtonStyle; DownArrowImage:FSlateBrush; ShadowOffset; ShadowColorAndOpacity;
  MenuBorderBrush:FSlateBrush; MenuBorderPadding, ContentPadding, DownArrowPadding:FMargin;
  DownArrowAlign:EVerticalAlignment}
  FTableRowStyle{SelectorFocusedBrush; ActiveHoveredBrush; ActiveBrush; InactiveHoveredBrush; InactiveBrush;
  bUseParentRowBrush:bool; ParentRowBackgroundBrush; ParentRowBackgroundHoveredBrush;
  EvenRowBackgroundHoveredBrush; EvenRowBackgroundBrush; OddRowBackgroundHoveredBrush; OddRowBackgroundBrush;
  TextColor, SelectedTextColor:FSlateColor; DropIndicator_Above/Onto/Below:FSlateBrush;
  ActiveHighlightedBrush; InactiveHighlightedBrush}
  | API: AddOption, RemoveOption, FindOptionIndex, GetOptionAtIndex, ClearOptions, ClearSelection,
  RefreshOptions, SetSelectedOption, SetSelectedIndex, GetSelectedOption, GetSelectedIndex, GetOptionCount,
  IsOpen, Get/SetContentPadding, Is/SetEnableGamepadNavigationMode, Is/SetHasDownArrow, Get/SetMaxListHeight,
  GetFont, Get/SetWidgetStyle, Get/SetItemStyle, GetScrollBarStyle, IsFocusable, GetForegroundColor

UComboBoxKey (SComboBox<FName>, DisplayName "ComboBox (Key)") — Options:TArray<FName>; SelectedOption:FName(FieldNotify);
  WidgetStyle:FComboBoxStyle†; ItemStyle:FTableRowStyle†; ScrollBarStyle†‡; ForegroundColor†‡(DesignerRebuild);
  ContentPadding=(4,2)†; MaxListHeight=450.0f†; bHasDownArrow:bool=true†; bEnableGamepadNavigationMode:bool=true†;
  bIsFocusable:bool=true†‡
  | events: OnGenerateContentWidget(FGenerateWidgetEvent), OnGenerateItemWidget(FGenerateWidgetEvent),
  OnSelectionChanged(FName,ESelectInfo::Type), OnOpening()
  | API: AddOption, RemoveOption, ClearOptions, ClearSelection, Set/GetSelectedOption, IsOpen,
  Get/SetContentPadding, Is/SetEnableGamepadNavigationMode, Is/SetHasDownArrow, Get/SetMaxListHeight,
  Get/SetWidgetStyle, Get/SetItemStyle, GetScrollBarStyle, IsFocusable, GetForegroundColor

UComboBox X (SComboBox<UObject*>, DisplayName "ComboBox (Object)") — ScrollBarStyle:FScrollBarStyle;
  Items:TArray<UObject*>; OnGenerateWidgetEvent(FGenerateWidgetForObject); bIsFocusable:bool
  | NO selection API, NO events — unfinished; use UComboBoxKey instead

UInputKeySelector (SInputKeySelector) — WidgetStyle:FButtonStyle†; TextStyle:FTextBlockStyle†(font Roboto/24/Bold);
  SelectedKey:FInputChord=EKeys::Invalid†(FieldNotify, no EditAnywhere); Margin:FMargin†;
  KeySelectionText:FText="..."†; NoKeySpecifiedText:FText="Empty"†; bAllowModifierKeys:bool=TRUE†;
  bAllowGamepadKeys:bool=FALSE†; EscapeKeys:TArray<FKey>={Gamepad_Special_Right}
  | events: OnKeySelected(FInputChord), OnIsSelectingKeyChanged()
  | API: Set/GetSelectedKey, Set/GetKeySelectionText, Set/GetNoKeySpecifiedText, Set/AllowModifierKeys,
  Set/AllowGamepadKeys, GetIsSelectingKey, SetTextBlockVisibility, Set/GetButtonStyle(+deprecated ptr overload),
  Set/GetTextStyle, Set/GetMargin, SetEscapeKeys

UScrollBar X (SScrollBar) — WidgetStyle:FScrollBarStyle†; bAlwaysShowScrollbar:bool=TRUE†;
  bAlwaysShowScrollbarTrack:bool=TRUE†; Orientation=Orient_Vertical†‡; Thickness:FVector2D=(16,16)†;
  Padding:FMargin=2.0f†
  | events: (none) | API: SetState(OffsetFraction, ThumbSizeFraction), Get/SetWidgetStyle,
  Is/SetAlwaysShowScrollbar, Is/SetAlwaysShowScrollbarTrack, GetOrientation, Get/SetThickness, Get/SetPadding
  | style: FScrollBarStyle{HorizontalBackgroundImage; VerticalBackgroundImage; VerticalTopSlotImage;
  HorizontalTopSlotImage; VerticalBottomSlotImage; HorizontalBottomSlotImage; NormalThumbImage;
  HoveredThumbImage; DraggedThumbImage :FSlateBrush; Thickness:float}
```

### C.5 Panels (named only — sibling report)

```
UCanvasPanel(SConstraintCanvas)/UCanvasPanelSlot · UGridPanel(SGridPanel)/UGridSlot ·
UUniformGridPanel(SUniformGridPanel)/UUniformGridSlot — SlotPadding:FMargin, MinDesiredSlotWidth:float,
  MinDesiredSlotHeight:float, AddChildToUniformGrid(Content,Row=0,Column=0) ·
UHorizontalBox(SHorizontalBox)/UHorizontalBoxSlot · UVerticalBox(SVerticalBox)/UVerticalBoxSlot ·
UStackBox(SStackBox)/UStackBoxSlot — Orientation:EOrientation · UOverlay(SOverlay)/UOverlaySlot ·
UWrapBox(SWrapBox)/UWrapBoxSlot — InnerSlotPadding:FVector2D, WrapSize:float, bExplicitWrapSize:bool,
  HorizontalAlignment, Orientation=Orient_Horizontal ·
UScrollBox(SScrollBox)/UScrollBoxSlot (see below) · USizeBox(SBox)/USizeBoxSlot ·
UScaleBox(SScaleBox)/UScaleBoxSlot · USafeZone(SSafeZone)/USafeZoneSlot — PadLeft/PadRight/PadTop/PadBottom:bool ·
UWidgetSwitcher(SWidgetSwitcher)/UWidgetSwitcherSlot · UBorder(SBorder)/UBorderSlot ·
UButton(SButton)/UButtonSlot · UBackgroundBlur(SBackgroundBlur)/UBackgroundBlurSlot ·
UWindowTitleBarArea(SWindowTitleBarArea)/UWindowTitleBarAreaSlot · UMenuAnchor(SMenuAnchor) ·
URetainerBox(SRetainerWidget) · UInvalidationBox(SInvalidationPanel) · UNamedSlot(SBox)

UPanelWidget(abstract) — Slots:TArray<UPanelSlot*>; GetChildrenCount, GetChildAt, GetAllChildren,
  GetChildIndex, HasChild, RemoveChildAt, AddChild(+SlotTemplate overload), InsertChildAt(+overload),
  ShiftChild, RemoveChild, HasAnyChildren, ClearChildren, GetSlots, CanHaveMultipleChildren,
  CanAddMoreChildren, GetSlotClass, bCanHaveMultipleChildren; editor ReplaceChildAt, ReplaceChild, LockToPanelOnDrag
UContentWidget(abstract) : UPanelWidget — GetContentSlot, SetContent, GetContent

UScrollBox (SScrollBox) : UPanelWidget — ScrollAnimationInterpolationSpeed:float=15.0f;
  bEnableTouchScrolling:bool=true; bConsumePointerInput:bool=true; AnalogMouseWheelKey:FKey=none;
  bIsFocusable:bool=false; WidgetStyle:FScrollBoxStyle†; WidgetBarStyle:FScrollBarStyle†;
  Orientation=Orient_Vertical†; ScrollBarVisibility=Visible†; ConsumeMouseWheel=WhenScrollingPossible†;
  ScrollbarThickness:FVector2D=(9,9)†; ScrollbarPadding:FMargin=2.0f†; AlwaysShowScrollbar:bool=false†;
  AlwaysShowScrollbarTrack:bool=false†; AllowOverscroll:bool=true†; BackPadScrolling:bool=false†‡;
  FrontPadScrolling:bool=false†‡; bAnimateWheelScrolling:bool=false†; NavigationDestination=IntoView†;
  NavigationScrollPadding:float=0.0f†‡; ScrollWhenFocusChanges=NoScroll†; bAllowRightClickDragScrolling:bool=true†;
  WheelScrollMultiplier:float=1.0f†; ctor Clipping=ClipToBounds, visibility=Visible
  | events: OnUserScrolled(float), OnScrollBarVisibilityChanged(ESlateVisibility), OnFocusReceived,
  OnFocusLost, OnFocusUpdated(UWidget*,bool)
  | style: FScrollBoxStyle{BarThickness:float; TopShadowBrush; BottomShadowBrush; LeftShadowBrush;
  RightShadowBrush:FSlateBrush; HorizontalScrolledContentPadding=FMargin(0,0,1,0);
  VerticalScrolledContentPadding=FMargin(0,0,0,1)}
  | API: Set/GetScrollOffset, GetOverscrollOffset, GetOverscrollPercentage, GetScrollOffsetOfEnd,
  GetViewFraction, GetViewOffsetFraction, ScrollToStart, ScrollToEnd,
  ScrollWidgetIntoView(Widget,bAnimate=true,Destination=IntoView,Padding=0), GetIsScrolling,
  EndInertialScrolling + full getter/setter set
```

### C.6 Lists and data

```
UListViewBase(abstract, EntryInterface=UserListEntry) — EntryWidgetClass:TSubclassOf<UUserWidget>
  (DesignerRebuild, MustImplement=UserListEntry); WheelScrollMultiplier:float=1.0f;
  bEnableScrollAnimation:bool=false; ScrollingAnimationInterpolationSpeed:float=12.0f;
  bInEnableTouchAnimatedScrolling:bool=false; AllowOverscroll:bool=true; bEnableRightClickScrolling:bool=true;
  bEnableTouchScrolling:bool=true; bIsPointerScrollingEnabled:bool=true; bIsGamepadScrollingEnabled:bool=true;
  bEnableFixedLineOffset:bool=false; FixedLineScrollOffset:float=0.0f(0..0.5); bAllowDragging:bool=true;
  bAllowDragDrop:bool=false; DragDropVisualPivot:EDragPivot=CenterCenter; DragDropVisualOffset:FVector2D=(0,0);
  DragDropVisualEntryClass:TSubclassOf<UUserWidget>=null; DragDropOperationClass:TSubclassOf<UDragDropOperation>=null;
  DragVisualWidget:UWidget*=null(Transient); bIsDragging:bool=false(Transient); bSelectItemOnNavigation:bool=true;
  bAllowKeepPreselectedItems:bool=true; NumDesignerPreviewEntries:int32=5(editor)
  ctor bIsVariable=true, Clipping=ClipToBounds
  | events(BP): BP_OnEntryGenerated(UUserWidget*), BP_OnEntriesGenerated(int32), BP_OnEntryReleased(UUserWidget*)
  | events(native): OnEntryWidgetGenerated, OnEntryWidgetReleased
  | API: GetEntryWidgetClass, GetDisplayedEntryWidgets, GetScrollOffset, GetListObjectFromEntry,
  GetIsDraggingListItem, RegenerateAllEntries, ScrollToTop, ScrollToBottom, SetScrollOffset,
  EndInertialScrolling, SetWheelScrollMultiplier, Set/GetScrollbarVisibility, SetAllowOverScroll,
  GetOverscroll, SetIsPointerScrollingEnabled, SetIsTouchScrollingEnabled, SetIsGamepadScrollingEnabled,
  CancelListViewDragDrop, CreateDragDropOperation(Item), RequestRefresh
  | subclass hooks: HandleListEntryHovered/Unhovered/DragDetected/CanAcceptDrop/AcceptDrop/DragEnter/
  DragLeave/DragCancelled; NativeOnEntryGenerated/EntriesGenerated/EntryReleased; GenerateTypedEntry<>

ITypedUMGListView<ItemType> (template, not a UCLASS) — events: OnItemClicked, OnItemDoubleClicked,
  OnItemDragDetected, OnItemAcceptDrop, OnItemDragEnter, OnItemDragLeave, OnItemDragCancelled(FDragDropEvent),
  OnItemCanAcceptDrop(Item,bool), OnItemIsHoveredChanged(Item,bool), OnItemSelectionChanged(NullableItem),
  OnListViewScrolled(float,float), OnFinishedScrolling, OnTouchStart, OnTouchMove, OnTouchEnd,
  OnItemScrolledIntoView(Item,UUserWidget&), OnItemExpansionChanged(Item,bool),
  OnGetEntryClassForItem(Item)->TSubclassOf<UUserWidget>, OnIsItemSelectableOrNavigable(Item)->bool
  | API: GetSelectedItem, ItemFromEntryWidget, GetEntryWidgetFromItem<>, GetIndexInList, GetSelectedItems,
  GetNumItemsSelected, SetSelectedItem(item,SelectInfo=Direct), SetItemSelection(item,bool,SelectInfo=Direct),
  ClearSelection, IsItemVisible, IsItemSelected, RequestNavigateToItem, RequestScrollItemIntoView,
  CancelScrollIntoView
  | FListViewConstructArgs{bAllowFocus=true, SelectionMode=Single, bClearSelectionOnClick=false,
  ConsumeMouseWheel=WhenScrollingPossible, bReturnFocusToSelection=false, bEnableProximateEntryNavigation=false,
  bClearScrollVelocityOnSelection=true, Orientation=Orient_Vertical, ScrollIntoViewAlignment=CenterAligned,
  ListViewStyle, ScrollBarStyle, ShadowBoxStyle, bEnableShadowBoxStyle=false, ScrollBarPadding=0,
  bPreventThrottling=false}
  | FTileViewConstructArgs : above + {TileAlignment=EvenlyDistributed, EntryHeight, EntryWidth,
  bWrapDirectionalNavigation=false, ScrollBarStyle, ScrollbarDisabledVisibility=Collapsed}
  | FTreeViewConstructArgs{SelectionMode=Single, bClearSelectionOnClick=false,
  ConsumeMouseWheel=WhenScrollingPossible, bReturnFocusToSelection=false, TreeViewStyle, ScrollBarStyle}
  | all three SNew with HandleGamepadEvents(true)

UListView (SListView<UObject*>, EntryInterface=UserObjectListEntry) : UListViewBase, ITypedUMGListView<UObject*>
  — WidgetStyle:FTableViewStyle(DisplayName "Style"); ScrollBarStyle:FScrollBarStyle; bEnableShadowBrush:bool=false;
  ShadowBrushStyle:FScrollBoxStyle; Orientation=Orient_Vertical; SelectionMode=Single;
  ConsumeMouseWheel=WhenScrollingPossible; bClearSelectionOnClick=false; bIsFocusable=true;
  bClearScrollVelocityOnSelection=true; bReturnFocusToSelection=false; bEnableProximateEntryNavigation=false X;
  ScrollIntoViewAlignment=CenterAligned; EntrySpacing:float=0(DEPRECATED, editor-only);
  HorizontalEntrySpacing:float=0; VerticalEntrySpacing:float=0; ScrollBarPadding:FMargin
  | style: FTableViewStyle{BackgroundBrush:FSlateBrush}   ← one field only
  | API: SetListItems<>, GetListItems, AddItem, AddItemAt, AddItems, AddItemsAt, RemoveItem, RemoveItems,
  GetItemAt, GetNumItems, GetIndexForItem, ClearListItems, SetSelectionMode, SetScrollIntoViewAlignment,
  SetEnableProximateEntryNavigation, Set/GetScrollBarPadding, IsRefreshPending, ScrollIndexIntoView,
  SetSelectedIndex, NavigateToIndex, Get/SetHorizontalEntrySpacing, Get/SetVerticalEntrySpacing,
  SetReturnFocusToSelection, Get/SetShadowBrushStyle
  | BP wrappers: BP_SetSelectedItem, BP_SetItemSelection, BP_ClearSelection, BP_GetNumItemsSelected,
  BP_GetSelectedItems, BP_IsItemVisible, BP_NavigateToItem, BP_ScrollItemIntoView, BP_CancelScrollIntoView,
  BP_SetListItems, BP_GetSelectedItem
  | BP events: OnEntryInitialized(Item,Widget), OnItemClicked(Item), OnItemDoubleClicked(Item),
  OnItemDragDetected(Item,Geometry), OnItemDragEnter(Item,Op), OnItemDragLeave(Item,Op),
  OnItemAcceptDrop(Item,DropZone,Op), OnItemDragCancelled(Op), OnListViewDraggingStateChanged(bool),
  OnItemIsHoveredChanged(Item,bool), OnItemSelectionChanged(Item,bool), OnItemScrolledIntoView(Item,Widget),
  OnListViewScrolled(ItemOffset,DistanceRemaining), OnListViewFinishedScrolling, OnListViewTouchStart,
  OnListViewTouchMove, OnListViewTouchEnd, OnIsItemSelectableOrNavigable(Item)->bool

UTileView (STileView<UObject*>) : UListView — EntryHeight:float=128.0f; EntryWidth:float=128.0f;
  TileAlignment:EListItemAlignment; bWrapHorizontalNavigation:bool=false;
  ScrollbarDisabledVisibility=Collapsed(Collapsed|Hidden|Visible); bEntrySizeIncludesEntrySpacing:bool=true
  | API: Set/GetEntryHeight, Set/GetEntryWidth; protected GetTotalEntryHeight/Width, IsAligned

UTreeView (STreeView<UObject*>) : UListView — BP_OnGetItemChildren(FOnGetItemChildrenDynamic, IsBindableEvent);
  BP_OnItemExpansionChanged(Item,bool); native OnGetItemChildren
  | API: SetItemExpansion(Item,bool), ExpandAll, CollapseAll, SetOnGetItemChildren(UObject/SharedRef overloads)
  | BP delegate only consulted when the native one is unbound

IUserListEntry (UUserListEntry, BlueprintType) — IsListItemSelected, IsListItemExpanded, GetOwningListView,
  IsListItemSelectable(native, default true)
  | native virtuals: NativeOnItemSelectionChanged(bool), NativeOnItemExpansionChanged(bool), NativeOnEntryReleased
  | BP events: BP_OnItemSelectionChanged(bool), BP_OnItemExpansionChanged(bool), BP_OnEntryReleased,
  BP_OnUpdateEntryDropIndicator(EUMGItemDropZone), BP_OnEntryDragOverChanged(bool),
  BP_OnEndEntryDropOperation(bSuccess), BP_OnEntryDropped(Op), BP_OnEntryDragged(Op)
  | statics: ReleaseEntry, UpdateItemSelection, UpdateItemExpansion, UpdateEntryDropIndicator,
  UpdateEntryDragOverState, EndEntryDropOperation, HandleEntryDropped, HandleEntryDragged
  | library UUserListEntryLibrary: IsListItemSelected, IsListItemExpanded, GetOwningListView (DefaultToSelf)

IUserObjectListEntry : IUserListEntry — GetListItem<T>(); NativeOnListItemObjectSet(UObject*);
  OnListItemObjectSet(UObject*)[BP event]
  | library UUserObjectListEntryLibrary: GetListItemObject, GetListItemIndex, IsFirstWidget, IsLastWidget

UDynamicEntryBoxBase(abstract) — EntrySpacing:FVector2D=(0,0)†; SpacingPattern:TArray<FVector2D>(Overlay only);
  EntryBoxType:EDynamicBoxType=Horizontal†‡(DesignerRebuild); EntrySizeRule:FSlateChildSize={1.0f,Fill}†‡;
  EntryHorizontalAlignment†‡; EntryVerticalAlignment†‡; MaxElementSize:int32=0†‡;
  RadialBoxSettings:FRadialBoxSettings†
  EDynamicBoxType{Horizontal,Vertical,Wrap,VerticalWrap,Radial,Overlay}
  | API: GetBoxType, GetEntrySpacing, GetAllEntries, GetEntrySizeRule, GetRadialBoxSettings, GetTypedEntries<>,
  GetNumEntries, SetEntrySpacing, SetRadialSettings, GetEntryVerticalAlignment, GetEntryHorizontalAlignment,
  GetMaxElementSize; protected CreateEntryInternal, RemoveEntryInternal, BuildEntryPadding, ResetInternal

UDynamicEntryBox : UDynamicEntryBoxBase — EntryWidgetClass:TSubclassOf<UUserWidget>;
  NumDesignerPreviewEntries:int32=3(editor, 0..20); OnPreviewEntryCreatedFunc(editor)
  | API: GetEntryWidgetClass, CreateEntry<>(ExplicitEntryClass), Reset<>(fn,bDeleteWidgets),
  Reset(bDeleteWidgets=false), RemoveEntry, BP_CreateEntry, BP_CreateEntryOfClass; ValidateCompiledDefaults
```

### C.7 Media, 3D and render-target

```
UWidgetComponent (UMeshComponent, Blueprintable, BlueprintSpawnableComponent)
  — Space:EWidgetSpace=World; TimingPolicy:EWidgetTimingPolicy=RealTime; WidgetClass:TSubclassOf<UUserWidget>;
  DrawSize:FIntPoint=(500,500); bManuallyRedraw:bool=false; RedrawTime:float=0.0f;
  bUseInvalidationInWorldSpace:bool=false; bDrawAtDesiredSize:bool=false; Pivot:FVector2D=(0.5,0.5);
  bReceiveHardwareInput:bool=false; bWindowFocusable:bool=TRUE; WindowVisibility=SelfHitTestInvisible;
  bApplyGammaCorrection:bool=false; BackgroundColor:FLinearColor=Transparent; TintColorAndOpacity=White;
  OpacityFromTexture:float=1.0f(0..1); BlendMode:EWidgetBlendMode=Masked; bOverrideRenderTargetFormat:bool=false;
  RenderTargetFormatOverride=RTF_RGBA8; bIsTwoSided:bool=false; TickWhenOffscreen:bool=false;
  SharedLayerName:FName="WidgetComponentScreenLayer"; LayerZOrder:int32=-100; GeometryMode=Plane;
  CylinderArcAngle:double=180.0(1..180); TickMode=Automatic|Enabled
  enums: EWidgetSpace{World,Screen} · EWidgetTimingPolicy{RealTime,GameTime} ·
  EWidgetBlendMode{Opaque,Masked,Transparent} · EWidgetGeometryMode{Plane,Cylinder} ·
  EWindowVisibility{Visible,SelfHitTestInvisible} · ETickMode{Disabled,Enabled,Automatic}
  material params driven: SlateUI(Texture), BackColor(Vector), TintColorAndOpacity(Vector), OpacityFromTexture(Scalar)
  collision profile forced to "UI"; pass-through materials /Engine/EngineMaterials/Widget3DPassThrough_*
  | events(native): OnMaterialInstanceUpdated
  | API: GetLocalHitLocation, GetCylinderHitLocation, GetUserWidgetObject, GetSlateWidget,
  GetHitWidgetPath(World|2D, bIgnoreEnabledStatus, CursorRadius=0), GetRenderTarget, GetMaterialInstance,
  GetSlateWindow, Get/SetWidget, SetSlateWidget, SetOwnerPlayer, SetManuallyRedraw, GetOwnerPlayer,
  GetDrawSize, GetCurrentDrawSize, SetDrawSize, RequestRedraw, RequestRenderUpdate, SetBlendMode,
  SetTwoSided, SetBackgroundColor, SetTintColorAndOpacity, SetOpacityFromTexture, GetVirtualWindow,
  UpdateMaterialInstanceParameters, SetWidgetClass, SetWindowFocusable, SetWindowVisibility, SetTickMode,
  IsWidgetVisible, CanReceiveHardwareInput, RegisterHitTesterWithViewport, RegisterWindow
  static FWidget3DHitTester WidgetHitTester

URetainerBox (SRetainerWidget) : UContentWidget — bRetainRender:bool=true†;
  RenderOnInvalidation:bool=false†‡; RenderOnPhase:bool=TRUE†‡; Phase:int32=0†‡; PhaseCount:int32=1†‡;
  EffectMaterial:UMaterialInterface*=null†; TextureParameter:FName=DefaultTextureParameterName†;
  bShowEffectsInDesigner:bool=true(editor); ctor visibility=Visible
  | API: SetRenderingPhase(Phase,TotalPhases), RequestRender, Get/SetEffectMaterial(→MID),
  SetTextureParameter, Set/IsRetainRendering, GetPhase, GetPhaseCount, IsRenderOnInvalidation,
  IsRenderOnPhase, GetCachedAllottedGeometry, GetTextureParameter, GetEffectMaterialInterface
  | Phase 0/PhaseCount 1 = every frame; Phase 0/PhaseCount 2 = every other frame

UBackgroundBlur (SBackgroundBlur) : UContentWidget — Padding:FMargin=(0,0)†; HorizontalAlignment=HAlign_Fill†;
  VerticalAlignment=VAlign_Fill†; bApplyAlphaToBlur:bool=TRUE†; BlurStrength:float=0.0f†(0..100);
  bOverrideAutoRadiusCalculation:bool=false†(no EditAnywhere); BlurRadius:int32=0†(0..255, AdvancedDisplay);
  CornerRadius:FVector4=(0,0,0,0)†(X=TL,Y=TR,Z=BR,W=BL); LowQualityFallbackBrush:FSlateBrush=NoResource†
  ctor bIsVariable=false, visibility=SelfHitTestInvisible; cvar Slate.ForceBackgroundBlurLowQualityOverride
  | API: Set/GetPadding, Set/GetHorizontalAlignment, Set/GetVerticalAlignment, Set/GetApplyAlphaToBlur,
  Set/GetOverrideAutoRadiusCalculation, Set/GetBlurRadius, Set/GetBlurStrength, Set/GetCornerRadius,
  Set/GetLowQualityFallbackBrush

UPostBufferUpdate (SPostBufferUpdate) — UpdateBufferInfos:TArray<FSlatePostBufferUpdateInfo>
  FSlatePostBufferUpdateInfo{BufferToUpdate:ESlatePostRT=None; PostParamUpdater:USlatePostBufferProcessorUpdater*}
  deprecated: bUpdateOnlyPaintArea(5.7), bPerformDefaultPostBufferUpdate(5.7), BuffersToUpdate(5.5)
  USlatePostBufferProcessorUpdater(Abstract,Blueprintable) — bSkipBufferUpdate†(5.7), GetRenderThreadProxy

UViewport X (SAutoRefreshViewport) : UContentWidget — BackgroundColor:FLinearColor=Black†;
  bIsEditorPreview:bool=false; ctor ShowFlags=ESFIM_Game + DisableAdvancedFeatures()
  | API: GetViewportWorld, Get/SetViewLocation, Get/SetViewRotation, Spawn(ActorClass), Get/SetBackgroundColor,
  SetEnableAdvancedFeatures, SetLightIntensity, SetSkyIntensity, SetShowFlag(name,bool), GetViewProjectionMatrix
  | FUMGViewportCameraTransform{SetLocation,SetRotation,SetLookAt,SetOrthoZoom,TransitionToLocation,
  UpdateTransition,ComputeOrbitMatrix}; FUMGViewportClient

UWidgetInteractionComponent (USceneComponent) — VirtualUserIndex:int32(ExposeOnSpawn);
  PointerIndex:int32(0..9, ExposeOnSpawn); TraceChannel:ECollisionChannel; InteractionDistance:float;
  InteractionSource:EWidgetInteractionSource{World,Mouse,CenterScreen,Custom}; bEnableHitTesting:bool;
  bShowDebug:bool; DebugSphereLineThickness:float(≥0.001); DebugLineThickness:float(0.001..50); DebugColor:FLinearColor
  | events: OnHoveredWidgetChanged(New,Previous)
  | API: PressPointerKey, ReleasePointerKey, PressKey(Key,bRepeat=false), ReleaseKey, PressAndReleaseKey,
  SendKeyChar(Chars,bRepeat=false), ScrollWheel(Delta), GetHoveredWidgetComponent, IsOverInteractableWidget,
  IsOverFocusableWidget, IsOverHitTestVisibleWidget, GetHoveredWidgetPath, GetLastHitResult,
  Get2DHitLocation, SetCustomHitResult, SetFocus(UWidget*)
  | HoveredWidgetComponent deprecated 5.6 → WeakHoveredWidgetComponent
```

### C.8 Navigation and misc

```
UWidgetSwitcher (SWidgetSwitcher) : UPanelWidget — ActiveWidgetIndex:int32=0†(FieldNotify, ≥0)
  ctor bIsVariable=true, visibility=SelfHitTestInvisible
  | API: GetNumWidgets, GetActiveWidgetIndex, SetActiveWidgetIndex, SetActiveWidget, GetWidgetAtIndex, GetActiveWidget

UMenuAnchor (SMenuAnchor) : UContentWidget — MenuClass:TSubclassOf<UUserWidget>;
  OnGetMenuContentEvent†(4.26); OnGetUserMenuContentEvent:FGetUserWidget; Placement=MenuPlacement_ComboBox†;
  bFitInWindow:bool=TRUE†; ShouldDeferPaintingAfterWindowContent:bool=TRUE†‡;
  UseApplicationMenuStack:bool=TRUE†‡; ShowMenuBackground:bool=TRUE†‡(EditCondition UseApplicationMenuStack)
  | events: OnMenuOpenChanged(bool bIsOpen)
  | API: Set/GetPlacement, FitInWindow/IsFitInWindow, IsDeferPaintingAfterWindowContent,
  IsUseApplicationMenuStack, IsShowMenuBackground, ToggleOpen(bFocusOnOpen), Open(bFocusMenu), Close,
  IsOpen, ShouldOpenDueToClick, GetMenuPosition, HasOpenSubMenus

UNamedSlot (SBox) : UContentWidget — bExposeOnInstanceOnly:bool=false(editor); SlotGuid:FGuid
INamedSlotInterface (CannotImplementInterfaceInBlueprint) — GetSlotNames, GetContentForSlot,
  SetContentForSlot, ContainsContent, FindSlotForContent, ReleaseNamedSlotSlateResources,
  SetNamedSlotDesignerFlags(editor)

UInvalidationBox (SInvalidationPanel) : UContentWidget — bCanCache:bool=true†;
  ctor visibility=SelfHitTestInvisible; InvalidateCache()†(4.27)
  | API: GetCanCache, SetCanCache

UWindowTitleBarArea (SWindowTitleBarArea) : UContentWidget — bWindowButtonsEnabled:bool†;
  bDoubleClickTogglesFullscreen:bool=false†; ctor bIsVariable=false, visibility=Visible
  | API: SetPadding, SetHorizontalAlignment, SetVerticalAlignment, Set/IsWindowButtonsEnabled,
  Set/IsDoubleClickTogglesFullscreen

UNativeWidgetHost (raw SWidget) — no UPROPERTYs; SetContent(TSharedRef<SWidget>), GetContent, GetDefaultContent

UExpandableArea (SExpandableArea) : UWidget + INamedSlotInterface — Style:FExpandableAreaStyle†;
  BorderBrush:FSlateBrush†; BorderColor:FSlateColor=White†; bIsExpanded:bool=false†(FieldNotify);
  MaxHeight:float=0†; HeaderPadding:FMargin=(4,2)†; AreaPadding:FMargin=1†;
  named slots: HeaderContent, BodyContent
  | events: OnExpansionChanged(UExpandableArea*, bool)
  | style: FExpandableAreaStyle{CollapsedImage, ExpandedImage:FSlateBrush; RolloutAnimationSeconds:float}
  | API: Get/SetIsExpanded, SetIsExpanded_Animated, Get/SetStyle, Get/SetBorderBrush, Get/SetBorderColor,
  Get/SetMaxHeight, Get/SetHeaderPadding, Get/SetAreaPadding

UIComponents (composition, wrap the owner's content in an extra SWidget):
UMouseHoverComponent : UUIComponent — bIsHovered:bool=false(VisibleAnywhere, FieldNotify); GetIsHovered,
  OnMouseHoverChanged
USizeBoxComponent X : UUIComponent (SBox) — Padding:FMargin=(0,0); HorizontalAlignment=HAlign_Fill;
  VerticalAlignment=VAlign_Fill; WidthOverride=0; HeightOverride=0; MinDesiredWidth=0; MinDesiredHeight=0;
  MaxDesiredWidth=0; MaxDesiredHeight=0; MinAspectRatio=1.0; MaxAspectRatio=1.0 — each with a
  bOverride_* InlineEditConditionToggle and Get/Is…Override/Set/Clear… API
UScaleBoxComponent X : UUIComponent (SScaleBox) — HorizontalAlignment=HAlign_Center; VerticalAlignment=VAlign_Center;
  Stretch=EStretch::ScaleToFit; StretchDirection=EStretchDirection::Both; UserSpecifiedScale=1.0f;
  IgnoreInheritedScale=false
```

### C.9 CommonUI plugin

```
UCommonUserWidget : UUserWidget — bDisplayInActionBar:bool=false; bConsumePointerInput:bool=false
  | API: RegisterUIActionBinding(FBindUIActionArgs)→handle, RegisterUIAction(UInputAction*,bool),
  RegisterUIActionsFromMappingContext, RemoveUIAction, RemoveAllUIActionBinding,
  Register/UnregisterScrollRecipientExternal, RegisterScrollRecipient(Widget, OwningNodeSource)
UCommonActivatableWidget : UCommonUserWidget — bIsBackHandler=false; bIsBackActionDisplayedInActionBar=false;
  OverrideBackActionDisplayName:FText; bAutoActivate=false; bSupportsActivationFocus=TRUE; bIsModal=false;
  bAutoRestoreFocus=false; bOverrideActionDomain=false; InputMapping:UInputMappingContext*=null;
  InputMappingPriority:int32=0; ActionDomainOverride:TSoftObjectPtr<UCommonInputActionDomain>;
  bSetVisibilityOnActivated=false; ActivatedVisibility=SelfHitTestInvisible; bSetVisibilityOnDeactivated=false;
  DeactivatedVisibility=Collapsed; bIsActive=false(readonly)
  | events: BP_OnWidgetActivated, BP_OnWidgetDeactivated; native OnActivated, OnDeactivated, OnSlateReleased,
  OnRequestRefreshFocus, static OnRebuilding
  | hooks: BP_GetDesiredFocusTarget, BP_GetDesiredInputConfig→FUIInputConfig, BP_OnActivated, BP_OnDeactivated,
  BP_OnHandleBackAction→bool; C++ GetDesiredInputConfig()→TOptional<FUIInputConfig>,
  GetActivationMetadata()→TOptional<FActivationMetadata>
  | API: ActivateWidget, DeactivateWidget, BindVisibilityToActivation, SetBindVisibilities,
  RequestRefreshFocus, GetCalculatedActionDomain, ResetCalculatedActionDomainCache, ClearFocusRestorationTarget
UCommonUIVisibilitySubsystem : ULocalPlayerSubsystem — GetVisibilityTags, HasVisibilityTag,
  Add/RemoveUserVisibilityCondition, OnVisibilityTagsChanged; adds TAG_INPUT_{MOUSEANDKEYBOARD,GAMEPAD,TOUCH}
  + UCommonUISettings::PlatformTraits
UCommonUISubsystemBase : UGameInstanceSubsystem — GetInputActionButtonIcon, GetEnhancedInputActionButtonIcon,
  SetAnalyticProvider, FireEvent_ButtonClicked, FireEvent_PanelPushed, Set/IsInputAllowed

UCommonButtonStyle (UObject, Abstract, Blueprintable) — bSingleMaterial; SingleMaterialBrush;
  NormalBase/NormalHovered/NormalPressed; SelectedBase/SelectedHovered/SelectedPressed; Disabled;
  ButtonPadding:FMargin; CustomPadding:FMargin; MinWidth/MinHeight/MaxWidth/MaxHeight:int32;
  NormalTextStyle/NormalHoveredTextStyle/SelectedTextStyle/SelectedHoveredTextStyle/DisabledTextStyle
  :TSubclassOf<UCommonTextStyle>; PressedSlateSound, ClickedSlateSound, HoveredSlateSound:FSlateSound;
  SelectedPressed/SelectedClicked/LockedPressed/LockedClicked/SelectedHovered/LockedHovered
  :FCommonButtonStyleOptionalSlateSound{bHasSound=false, Sound}
UCommonButtonBase : UCommonUserWidget (SCommonButton:SButton via UCommonButtonInternalBase X)
  — MinWidth/MinHeight/MaxWidth/MaxHeight=0; Style:TSubclassOf<UCommonButtonStyle>; bHideInputAction=false;
  9 × …SlateSoundOverride:FSlateSound; bApplyAlphaOnDisable=TRUE; bLocked=false; bSelectable=false;
  bShouldSelectUponReceivingFocus=false; bInteractableWhenSelected=false; bToggleable=false;
  bTriggerClickedAfterSelection=false; bDisplayInputActionWhenNotInteractable=TRUE;
  bHideInputActionWithKeyboard=false; bShouldUseFallbackDefaultInputAction=TRUE; bRequiresHold=false;
  HoldData:TSubclassOf<UCommonUIHoldData>; bSimulateHoverOnTouchInput=TRUE; ClickMethod; TouchMethod;
  PressMethod; InputPriority:int32=0(legacy); TriggeringInputAction:FDataTableRowHandle;
  TriggeringEnhancedInputAction:UInputAction*; bNavigateToNextWidgetOnDisable=false;
  bIsPersistentBinding=false(DANGER); InputModeOverride=Menu; InputActionWidget:UCommonActionWidget*(BindWidget optional)
  | events: OnSelectedChangedBase + OnButtonBase{Clicked,DoubleClicked,Hovered,Unhovered,Focused,Unfocused,
  LockClicked,LockDoubleClicked,Selected,Unselected,DragDetected,DragEnter,DragOver,Drop,DragLeave}
  | 23 BP hooks incl. OnActionProgress(float HeldPercent), OnActionComplete()
  | widget-state: UWidgetLockedStateRegistration "Locked"
UCommonTextStyle (UObject) — Font; Color=Black; bUsesDropShadow; ShadowOffset; ShadowColor; Margin;
  StrikeBrush; LineHeightPercentage=1.0f; ApplyLineHeightToBottomLine=true
UCommonTextScrollStyle (UObject) — Speed; StartDelay; EndDelay; FadeInDelay; FadeOutDelay; Clipping=OnDemand
UCommonTextBlock : UTextBlock (STextBlock in STextScroller) — MobileFontSizeMultiplier=1.0f(0.01..5);
  bIsScrollingEnabled=true; bDisplayAllCaps_DEPRECATED; bAutoCollapseWithEmptyText=false;
  Style:TSubclassOf<UCommonTextStyle>; ScrollStyle:TSubclassOf<UCommonTextScrollStyle>;
  ScrollOrientation=Orient_Horizontal
UCommonBorderStyle (UObject) — Background:FSlateBrush
UCommonBorder : UBorder — Style:TSubclassOf<UCommonBorderStyle>; bReducePaddingBySafezone=false; MinimumPadding=0
UCommonRichTextBlock : URichTextBlock — InlineIconDisplayMode:ERichTextInlineIconDisplayMode;
  bTintInlineIcon=false; MobileTextBlockScale=1.0f; DefaultTextStyleOverrideClass; ScrollStyle;
  ScrollOrientation=Orient_Horizontal; bIsScrollingEnabled=true; bAutoCollapseWithEmptyText=false
UCommonUIRichTextData (UObject) — InlineIconSet:UDataTable*(RichTextIconData{DisplayName, ResourceObject, ImageSize=(64,64)})
UCommonNumericTextBlock : UCommonTextBlock — CurrentNumericValue:float; NumericType:ECommonNumericType
  {Number,Percentage,Seconds,Distance}; FormattingSpecification:FCommonNumberFormattingOptions
  {RoundingMode=HalfFromZero, AlwaysSign=false, UseGrouping=true, Min/MaxIntegralDigits, MinFractionalDigits=0,
  MaxFractionalDigits=0}; EaseOutInterpolationExponent(≥1); InterpolationUpdateInterval(≥0);
  PostInterpolationShrinkDuration; PerformSizeInterpolation
  | events: OnInterpolationStarted/Updated/Outro/Ended; InterpolateToValue(Target,MaxDuration=3,MinChangeRate=1,OutroOffset=0)
UCommonDateTimeTextBlock : UCommonTextBlock — CustomTimespanFormat:FText; bCustomTimespanLeadingZeros=false;
  SetDateTimeValue(DateTime,bShowAsCountdown,RefreshDelay=1.0f), SetTimespanValue, SetCountDownCompletionText

SCommonAnimatedSwitcher : SWidgetSwitcher — InitialIndex=0; TransitionType=FadeOnly;
  TransitionCurveType=CubicInOut; TransitionDuration=0.4f; TransitionFallbackStrategy=Previous;
  Visibility=SelfHitTestInvisible
  ECommonSwitcherTransition{FadeOnly,Horizontal,Vertical,Zoom} ·
  ETransitionCurve{Linear,QuadIn,QuadOut,QuadInOut,CubicIn,CubicOut,CubicInOut} ·
  ECommonSwitcherTransitionFallbackStrategy{None,Previous,Next,First,Last}
UCommonAnimatedSwitcher : UWidgetSwitcher — TransitionType=FadeOnly; TransitionCurveType=CubicInOut;
  TransitionDuration=0.4f; TransitionFallbackStrategy=None
  | API: ActivateNext/PreviousWidget(bCanWrap), HasWidgets, SetDisableTransitionAnimation,
  IsCurrentlySwitching, IsTransitionPlaying
UCommonActivatableWidgetSwitcher : UCommonAnimatedSwitcher — bClearFocusRestorationTargetOfDeactivatedWidgets=false
UCommonActivatableWidgetContainerBase (Abstract) : UWidget — TransitionType=FadeOnly; TransitionCurveType=Linear;
  TransitionDuration=0.4f; bResetPoolWhenReleasingSlateResources=false; TransitionFallbackStrategy=None;
  pooled via FUserWidgetPool; SOverlay{SCommonAnimatedSwitcher + SSpacer input guard}
  | API: AddWidget<T>(class[,InitFunc]) ["Push Widget"], AddWidgetInstance(legacy), RemoveWidget,
  GetActiveWidget, GetWidgetList, ClearWidgets, Set/GetTransitionDuration
  | events: OnDisplayedWidgetChanged, OnTransitioningChanged
UCommonActivatableWidgetStack — RootContentWidgetClass:TSubclassOf<UCommonActivatableWidget>; GetRootContent
UCommonActivatableWidgetQueue — (no own properties)
UCommonVisibilitySwitcher : UOverlay — ShownVisibility=SelfHitTestInvisible; ActiveWidgetIndex=0(≥-1);
  bAutoActivateSlot=true; bActivateFirstSlotOnAdding=false; slot UCommonVisibilitySwitcherSlot:UOverlaySlot
UCommonWidgetCarousel : UPanelWidget (SWidgetCarousel) — ActiveWidgetIndex:int32(≥0); MoveSpeed:float;
  bCacheChildren:bool; OnCurrentPageIndexChanged; NextPage/PreviousPage, BeginAutoScrolling(Interval=10)/EndAutoScrolling
UCommonWidgetCarouselNavBar : UWidget — ButtonWidgetType:TSubclassOf<UCommonButtonBase>; ButtonPadding:FMargin;
  SetLinkedCarousel
UCommonTabListWidgetBase : UCommonUserWidget — NextTabInputActionData; PreviousTabInputActionData;
  NextTabEnhancedInputAction; PreviousTabEnhancedInputAction; bAutoListenForInput=false;
  bShouldWrapNavigation=true; bDeferRebuildingTabList=false
  | events: OnTabSelected(FName), OnTabButtonCreation, OnTabButtonRemoval, OnTabListRebuilt;
  BP hooks HandlePre/PostLinkedSwitcherChanged_BP; native events HandleTabCreation/HandleTabRemoval
  | API: SetLinkedSwitcher, RegisterTab(NameID,ButtonType,Content,Index=-1), RemoveTab, RemoveAllTabs,
  SelectTabByID(FName,bSuppressClickFeedback=false), SetTabVisibility/Enabled/InteractionEnabled,
  DisableTabWithReason, SetListeningForInput, RegisterTabContentWidget, SetSelectionRequired

UCommonListView/UCommonTileView/UCommonTreeView — SCommon{List,Tile,Tree}View<T>: focus-selects-item-0,
  proximate + intra-entry navigation via the hittest grid, touch fixes; UCommonListView::SetEntrySpacing
SCommonButtonTableRow<T> : SObjectTableRow<T> — entry IS a UCommonButtonBase; overrides its
  IsToggleable/IsSelectable/IsInteractableWhenSelected/AllowDragDrop/TouchMethod=PreciseTap from ESelectionMode
FCommonNativeListItem — hand-rolled RTTI (IsDerivedFrom<T>, AsTypedItem<T>, DERIVED_LIST_ITEM macro)
UCommonHierarchicalScrollBox : UScrollBox — recursive AppendFocusableChildren in OnNavigation
UCommonCustomNavigation : UBorder — OnNavigationEvent(EUINavigation)→bool (IsBindableEvent)

SLoadGuard : SCompoundWidget — ThrobberHAlign=HAlign_Center, Throbber, GuardText, GuardTextStyle,
  GuardBackgroundBrush, OnLoadingStateChanged; GuardAndLoadAsset
ULoadGuardSlot : UPanelSlot — Padding; HorizontalAlignment=HAlign_Fill; VerticalAlignment=VAlign_Fill
UCommonLoadGuard : UContentWidget — LoadingBackgroundBrush; LoadingThrobberBrush; ThrobberAlignment;
  ThrobberPadding; LoadingText; TextStyle; SpinnerMaterialPath(config); bShowLoading=false(editor)
UCommonLazyImage : UImage — bShowLoading=false(editor); LoadingBackgroundBrush; LoadingThrobberBrush;
  MaterialTextureParamName; SetBrushFromLazy{Texture,Material,DisplayAsset}, IsLoading
UCommonLazyWidget : UWidget — WidgetClass:TSoftClassPtr<UUserWidget>; LoadingThrobberBrush;
  LoadingBackgroundBrush; SetLazyContent[WithCallback], LoadLazyContent, GetContent, IsLoading
UCommonVideoPlayer : UWidget — Video:UMediaSource*; bMatchSize=false; SetVideo/Seek/Close/SetPlaybackRate/
  SetLooping/SetIsMuted/SetShouldMatchSize/Play/Reverse/Pause/PlayFromStart/GetVideoDuration/GetPlaybackTime/
  GetPlaybackRate/IsLooping/IsPaused/IsPlaying/IsMuted; OnPlaybackResumed/Paused/Complete
  ← the ONLY media widget in the UE UI stack
UCommonVisualAttachment : USizeBox (SVisualAttachmentBox:SBox) — ContentAnchor:FVector2D (direct access †5.4)
UAnalogSlider : USlider (SAnalogSlider:SSlider) — OnAnalogCapture; SLATE defaults IndentHandle=true,
  Locked=false, Orient_Horizontal, White/White, StepSize=0.01f, Value=1.f, IsFocusable=true
UCommonRotator : UCommonButtonBase — MyText:UCommonTextBlock(BindWidget); OnRotatedWithDirection(int32,
  ERotatorDirection{Right,Left}); OnRotated†(5.4); PopulateTextLabels, GetSelectedText, SetSelectedItem,
  ShiftTextLeft/Right
ICommonPoolableWidgetInterface — OnAcquireFromPool, OnReleaseToPool (BlueprintNativeEvent)
TWidgetFactory<W> : FGCObject — PreConstruct(n), Acquire, Release, Reset, TakeAndCacheWidget/Row
UCommonUILibrary — RefreshFocusIfLeafmostDescendant, FindParentWidgetOfType,
  FindParentWidgetImplementingInterface, Conv_UITagToGameplayTag, Conv_UIActionTagToGameplayTag

UCommonWidgetGroupBase (Abstract) — GetWidgetType, AddWidget(s), RemoveWidget, RemoveAll
UCommonButtonGroupBase — bSelectionRequired; 8 event pairs (SelectedButtonLostSelection,
  SelectedButtonBaseChanged, HoveredButtonBaseChanged, ButtonBaseClicked, ButtonBaseDoubleClicked,
  SelectionCleared, ButtonBaseLockClicked, ButtonBaseLockDoubleClicked); SetSelectionRequired, DeselectAll,
  SelectNext/PreviousButton(bAllowWrap=true,bAllowSound=true), SelectButtonAtIndex, GetSelected/HoveredButtonIndex,
  FindButtonIndex, ForEach, GetButtonBaseAtIndex, GetSelectedButtonBase, HasAnyButtons, GetButtonCount

UCommonHardwareVisibilityBorder : UCommonBorder — VisibilityQuery:FGameplayTagQuery(Categories=Input,Platform.Trait);
  VisibleType=SelfHitTestInvisible; HiddenType=Collapsed
UDEPRECATED_UCommonVisibilityWidgetBase : UCommonBorder — VisibilityControls:TMap<FName,bool>;
  bShowForGamepad; bShowForMouseAndKeyboard; bShowForTouch; VisibleType; HiddenType
UCommonUISettings (config=Game) — bAutoLoadData=true; DefaultImageResourceObject; DefaultThrobberMaterial;
  DefaultRichTextDataClass; PlatformTraits:TArray<FGameplayTag>(ConfigHierarchyEditable);
  CommonButtonAcceptKeyHandling=Ignore{Ignore,TriggerClick}
UCommonUIEditorSettings (config=Editor) — TemplateTextStyle; TemplateButtonStyle; TemplateBorderStyle

FUITag : FGameplayTag ("UI") · FUIActionTag : FUITag ("UI.Action")
FGlobalUITags — UIAction_Cancel, UIAction_Confirm, UIAction_PreviousTab, UIAction_NextTab
```

### C.10 CommonUI input routing

```
ECommonInputType{MouseAndKeyboard,Gamepad,Touch,Count}
ECommonInputMode{Menu,Game,All,MAX}
UCommonInputSubsystem : ULocalPlayerSubsystem — GetCurrentInputType, GetDefaultInputType,
  IsInputMethodActive, GetCurrentGamepadName, IsUsingPointerInput, ShouldShowInputKeys,
  PlatformSupportsHardwareCursor, GetIsGamepadSimulatedClick, HadAnyChangeOfInputMethodInTheLastThrashingWindow,
  SetCurrentInputType, SetGamepadInputType, SetInputTypeFilter, AddOrRemoveInputTypeLock,
  Set/UpdateCursorPosition; events OnInputMethodChangedNative + OnInputMethodChanged(ECommonInputType),
  GetOnGamepadChangeDetected; static GetOnPlatformInputSupportOverride
FCommonInputPreprocessor : IInputProcessor — runs BEFORE any UI sees input; per-type filter reasons
FCommonInputTypeInfo — Key:FKey; AdditionalKeys:TArray<FKey>; OverrrideState:EInputActionState
  {Enabled,Disabled,Hidden,HiddenAndDisabled}=Enabled; bActionRequiresHold=false; HoldTime=0.5f;
  HoldRollbackTime=0.0f(0..10); OverrideBrush:FSlateBrush(DrawAs=NoDrawType)
FCommonInputActionDataBase : FTableRowBase — DisplayName:FText; HoldDisplayName:FText; NavBarPriority=0;
  KeyboardInputTypeInfo; DefaultGamepadInputTypeInfo; GamepadInputOverrides:TMap<FName,FCommonInputTypeInfo>;
  TouchInputTypeInfo; GetCurrentInputTypeInfo, GetInputTypeInfo, GetCurrentInputActionIcon,
  IsKeyBoundToInputActionData, HasHoldBindings, AddGamepadInputOverride
UCommonInputBaseControllerData (Abstract,Blueprintable) — InputType; GamepadName; GamepadDisplayName;
  GamepadCategory; GamepadPlatformName; GamepadHardwareIdMapping:TArray<FInputDeviceIdentifierPair>;
  ControllerTexture; ControllerButtonMaskTexture;
  InputBrushDataMap:TArray<FCommonInputKeyBrushConfiguration{Key,KeyBrush}>;
  InputBrushKeySets:TArray<FCommonInputKeySetBrushConfiguration{Keys[],KeyBrush}>;
  TryGetInputBrush(key|keys), static GetRegisteredGamepads
UCommonInputPlatformSettings : UPlatformSettings (config=Game) — DefaultInputType; bSupportsMouseAndKeyboard;
  bSupportsTouch; bSupportsGamepad; DefaultGamepadName; bCanChangeGamepadType;
  ControllerData:TArray<TSoftClassPtr<UCommonInputBaseControllerData>>;
  TryGetInputBrush, GetControllerDataForInputType, GetBestGamepadNameForHardware, SupportsInputType
UCommonUIInputData (Abstract,Blueprintable) — DefaultClickAction; DefaultBackAction; DefaultHoldData;
  EnhancedInputClickAction; EnhancedInputBackAction
UCommonUIHoldData (Abstract,Blueprintable) — KeyboardAndMouse/Gamepad/Touch : FInputHoldData
  {HoldTime, HoldRollbackTime}; ctor sets all three HoldTime=0.75f, HoldRollbackTime=0.0f
UCommonActionWidget : UWidget — ProgressMaterialBrush; ProgressMaterialParam:FName; IconRimBrush;
  InputActions:TArray<FDataTableRowHandle>; EnhancedInputAction:UInputAction*; DesignTimeKey(editor)
  | events: OnInputMethodChanged(bUsingGamepad), OnInputIconUpdated
  | API: GetIcon, GetDisplayText, GetIconDynamicMaterial, SetInputAction, SetInputActionBinding(handle),
  SetInputActions, SetEnhancedInputAction, SetIconRimBrush, IsHeldAction, OnActionProgress(float),
  OnActionComplete, SetHidden — resolves the subsystem from the BINDING's local player (split-screen correct)
UCommonBoundActionBar : UDynamicEntryBoxBase — ActionButtonClass(MustImplement CommonBoundActionButtonInterface);
  bDisplayOwningPlayerActionsOnly=true; bIgnoreDuplicateActions=true; OnActionBarUpdated
ICommonBoundActionButtonInterface — SetRepresentedAction(FUIActionBindingHandle)
UCommonBoundActionButton : UCommonButtonBase — Text_ActionName:UCommonTextBlock(BindWidget);
  bLinkRequiresHoldToBindingHold=false; OnUpdateInputAction

UCommonUIInputSettings (config=Input) — bLinkCursorToGamepadFocus=TRUE; UIActionProcessingPriority=10000;
  InputActions:TArray<FUIInputAction>; ActionOverrides:TArray<FUIInputAction>(config-only);
  AnalogCursorSettings:FCommonAnalogCursorSettings; DefaultVirtualPointerClass
FUIActionKeyMapping{Key:FKey, HoldTime=0.f, HoldRollbackTime=0.f}
FUIInputAction{ActionTag:FUIActionTag, DefaultDisplayName:FText, KeyMappings:TArray<FUIActionKeyMapping>}
FCommonAnalogCursorSettings{PreprocessorPriority=2†(5.5); PreprocessorRegistrationInfo={Game,2};
  bEnableCursorAcceleration=true; CursorAcceleration=1500.f; CursorMaxSpeed=2200.f; CursorDeadZone=0.25f(0..0.9);
  HoverSlowdownFactor=0.4f; ScrollDeadZone=0.2f; ScrollUpdatePeriod=0.1f; ScrollMultiplier=2.5f;
  MaxHoldDuration=1.0f}
FBindUIActionArgs{ActionTag | LegacyActionTableRow | InputAction; InputMode=Menu; KeyEvent=IE_Pressed;
  InputTypesExemptFromValidKeyCheck={MouseAndKeyboard,Touch}; bIsPersistent=false; bConsumeInput=TRUE;
  bDisplayInActionBar=TRUE; bForceHold=false; OverrideDisplayName:FText; PriorityWithinCollection=0;
  OnExecuteAction; OnHoldActionProgressed(float); OnHoldActionPressed; OnHoldActionReleased}
FUIActionBinding — non-copyable; TryCreate(Widget,Args,UserIndex); global AllRegistrationsByHandle +
  CurrentHoldActionKeys; NormalMappings/HoldMappings split by HoldTime>0;
  ProcessHoldInput→EProcessHoldActionResult{Handled,GeneratePress,Unhandled}; BeginHold, UpdateHold,
  CancelHold, BeginRollback, GetSecondsHeld, IsHoldActive, ResetHold
FUIActionBindingHandle{RegistrationId=INDEX_NONE} — IsValid, Unregister, ResetHold, GetActionName,
  Get/SetDisplayName, Get/SetDisplayInActionBar, GetBoundWidget, GetBoundLocalPlayer
FUIInputConfig{bIgnoreMoveInput=false; bIgnoreLookInput=false; InputMode=Menu; MouseCaptureMode=NoCapture;
  MouseLockMode=DoNotLock; bHideCursorDuringViewportCapture=TRUE}
  — 3-arg ctor derives MouseLockMode = LockOnCapture for CapturePermanently[_IncludingInitialMouseDown], else DoNotLock
FActivationMetadata{TOptional<uint8> MetadataEnum}
UCommonUIActionRouterBase : ULocalPlayerSubsystem — ERouteUIInputResult{Handled,BlockGameInput,Unhandled};
  RootNodes/ActiveRootNode/PersistentActions/ActionDomainRootNodes;
  RegisterUIActionBinding, Add/RemoveBinding, NotifyUserWidgetConstructed/Destructed,
  Register/UnregisterScrollRecipient, RegisterLinkedPreprocessor, ProcessInput(FKey,EInputEvent),
  GetActiveInputMode(Default=All), GetActiveMouseCaptureMode(Default=NoCapture), CanProcessNormalGameInput,
  SetActiveUIInputConfig, ApplyUIInputConfig, GatherActiveBindings, GatherActiveAnalogScrollRecipients,
  IsWidgetInActiveRoot, IsWidgetInLeafmostNodeHierarchy, GetLeafmostActivatableWidget, IsPendingTreeChange,
  ShouldAlwaysShowCursor, SetIsActivatableTreeEnabled, FlushInput, RefreshActiveRootFocus,
  RefreshUIInputConfig, MakeAnalogCursor, static FindOwningActivatable/FindActivatable
  | dispatch order: PIE-escape chord → held-key tracking → HOLD pass → NORMAL pass; each pass:
  PersistentActions → ActiveRootNode (leaf-first) → action domains
UCommonInputActionDomain : UDataAsset — Behavior=BlockIfActive; InnerBehavior=BlockIfHandled;
  bUseActionDomainDesiredInputConfig; InputMode=Game; MouseCaptureMode=CapturePermanently;
  ECommonInputEventFlowBehavior{BlockIfActive,BlockIfHandled,NeverBlock}
UCommonInputActionDomainTable : UDataAsset — ActionDomains:TArray<UCommonInputActionDomain*>(ascending order);
  InputMode=Game; MouseCaptureMode=CapturePermanently; bHideCursorDuringViewportCapture=true
UCommonInputSettings : UDeveloperSettings (config=Game) — InputData; PlatformInput:FPerPlatformSettings;
  bEnableInputMethodThrashingProtection=TRUE; InputMethodThrashingLimit=30;
  InputMethodThrashingWindowInSeconds=3.0; InputMethodThrashingCooldownInSeconds=1.0;
  bAllowOutOfFocusDeviceInput=false; bEnableDefaultInputConfig=TRUE; bEnableEnhancedInputSupport=FALSE;
  bEnableAutomaticGamepadTypeDetection=TRUE; ActionDomainTable; PlatformNameUpgrades
FCommonAnalogCursor : FAnalogCursor, FGCObject — hidden cursor centred on the focused widget;
  SetCursorMovementStick, ShouldHandleRightAnalog, IsAnalogMovementEnabled, DetermineScrollOrientation,
  IsVirtualPointerEnabled/IsUsingVirtualPointer/SetVirtualPointerVisibility,
  ShouldVirtualAcceptSimulateMouseButton, OnVirtualAcceptHoldCanceled, IsGameViewportInFocusPathWithoutCapture,
  IsUsingFakeTouch, CanReleaseMouseCapture
  ECursorVisualState{Default=0(Hidden),Hover=1,Pressed=2,Drag=4,Hold=8}
  IVirtualPointerVisualStateInterface — OnVirtualPointerVisualStateChanged, OnVirtualPointerHoldProgress(float)
UCommonGameViewportClient : UGameViewportClient — REQUIRED for CommonUI routing; overrides InputKey,
  InputAxis, InputTouch, MouseMove, CapturedMouseMove, MapCursor; OnRerouteInput, OnRerouteAxis,
  GetRerouteTouchRegistration, OnRerouteBlockedInput; IsKeyPriorityAboveUI; SetUseVirtualPointerCursor
  (touch APIs deprecated 5.8 → FTouchId variants)
UCommonInputMetadata : UObject — NavBarPriority=0; bIsGenericInputAction=true
```

### C.11 Focus, navigation, hit-testing, replies, drag-drop, tooltips

```
EUINavigation{Left=0,Right=1,Up=2,Down=3,Next=4,Previous=5,Num=6(hidden),Invalid=7}
EUINavigationAction{Accept=0,Back=1,Num(hidden),Invalid}
ENavigationSource{FocusedWidget=0,WidgetUnderCursor=1}
ENavigationGenesis{Keyboard=0,Controller=1,User=2}
EUINavigationRule{Escape=0,Explicit=1,Wrap=2,Stop=3,Custom=4,CustomBoundary=5,Invalid=6}
EFocusCause{Mouse=0,Navigation=1,SetDirectly=2,Cleared=3,OtherWidgetLostFocus=4,WindowActivate=5}
EWidgetNavigationRoutingPolicy{AcceptFocus(Default),RouteToTopMostChild,RouteToBottomMostChild,
  RouteToLeftMostChild,RouteToRightMostChild,RouteToTopLeftChild,RouteToTopRightChild,
  RouteToBottomLeftChild,RouteToBottomRightChild,MAX}
ERenderFocusRule{Always=0,NonPointer=1,NavigationOnly=2,Never=3} (UUserInterfaceSettings::RenderFocusRule)
EVisibility (bitfield struct): Visible=0b11001, Collapsed=0b00010, Hidden=0b00100,
  HitTestInvisible=0b00001, SelfHitTestInvisible=0b10001, All=all bits
EWidgetClipping{Inherit=0,ClipToBounds=1,ClipToBoundsWithoutIntersecting=2,ClipToBoundsAlways=3,OnDemand=4}
EMouseCaptureMode{NoCapture=0,CapturePermanently=1,CapturePermanently_IncludingInitialMouseDown=2,
  CaptureDuringMouseDown=3,CaptureDuringRightMouseDown=4}
EMouseLockMode{DoNotLock=0,LockOnCapture=1,LockAlways=2,LockInFullscreen=3}
EDragPivot{MouseDown=0,TopLeft,TopCenter,TopRight,CenterLeft,CenterCenter,CenterRight,BottomLeft,
  BottomCenter,BottomRight}
EUMGItemDropZone{AboveItem,OntoItem,BelowItem,None}

FNavigationReply — factories Escape/Explicit(widget)/Custom(delegate)/CustomBoundary(delegate)/Wrap/Stop;
  GetHandler, GetBoundaryRule, GetFocusRecipient, GetFocusDelegate; NO public ctor; default rule=Escape
FNavigationDelegate = DECLARE_DELEGATE_RetVal_OneParam(TSharedPtr<SWidget>, …, EUINavigation)
FNavigationMetaData — Rules[6] of {BoundaryRule, FocusRecipient, FocusDelegate}, all Escape by default;
  SetNavigationExplicit/Custom/Wrap/Stop/Escape; 5.8 SetNavigationMethodStruct, SetNavigationRoutingPolicy
UWidgetNavigation — Up/Down/Left/Right/Next/Previous : FWidgetNavigationData{Rule=Escape, WidgetToFocus:FName,
  Widget, CustomDelegate}; RoutingPolicy=AcceptFocus; NavigationMethod:TInstancedStruct<FNavigationMethod>;
  ResolveRules, UpdateMetaData, IsDefaultNavigation. Next=Tab, Previous=Shift+Tab.

FNavigationConfig — bTabNavigation=true; bKeyNavigation=true; bAnalogNavigation=true;
  bIgnoreModifiersForNavigationActions=true; AnalogNavigationHorizontalThreshold=0.50f;
  AnalogNavigationVerticalThreshold=0.50f; AnalogHorizontalKey=Gamepad_LeftX; AnalogVerticalKey=Gamepad_LeftY;
  KeyEventRules{Left|DPad_Left→Left, Right|DPad_Right→Right, Up|DPad_Up→Up, Down|DPad_Down→Down};
  KeyActionRules{Enter|SpaceBar|Virtual_Gamepad_Accept→Accept, Escape|Virtual_Gamepad_Back→Back}
  repeat curve: first repeat 0.5s, subsequent 0.25s; halved (0.25s / 0.125s) when |pressure|>0.90
  vertical sign: +Y ⇒ Up, −Y ⇒ Down. Deadzone re-entry resets both opposing repeat states.
  variants FNullNavigationConfig (all nav off), FTwinStickNavigationConfig (no Tab, both sticks)
FSlateApplication — Set/ClearUserFocus, SetAllUserFocus, Set/ClearKeyboardFocus, SetUserFocusToGameViewport,
  SetAllUserFocusToGameViewport, GetUserFocusedWidget, GetKeyboardFocusedWidget, HasUserFocus,
  HasAnyUserFocus, ShowUserFocus, HasUserFocusedDescendants, Set/Get/ClearPendingNavigationContext(5.8),
  Set/GetNavigationConfig, GetRelevantNavConfig, AttemptNavigation, CalculateDestinationWidget,
  ExecuteNavigation, ProcessReply, IsDragDropping, GetDragDroppingContent, CancelDragDrop,
  SpawnToolTip, CloseToolTip, UpdateToolTip, Set/GetAllowTooltips, MakeToolTip,
  Get/SetDragTriggerDistance(+Squared)
FSlateUser — WeakFocusPath, StrongFocusPath, SetFocusPath(path,cause,bShowFocus), GetWeakFocusPath,
  HasValidFocusPath, IsWidgetInFocusPath, ShouldShowFocus, Get/IncrementFocusVersion,
  Set/GetUserNavigationConfig, StartDragDetection, DetectDrag, IsDetectingDrag, ResetDragDetection,
  Set/ResetDragDropContent, DrawWindowlessDragDropContent, UpdateTooltip
  (NO FUserFocusEntry type in 5.8)
SWidget focus — OnFocusReceived→FReply, OnFocusLost, OnFocusChanging(old,new,event),
  OnQueryShowFocus→TOptional<bool>, CanSupportFocus()/bCanSupportFocus:bit,
  SupportsKeyboardFocus()=FALSE by default, HasKeyboardFocus, HasUserFocus(idx), HasAnyUserFocus,
  HasUserFocusedDescendants, HasFocusedDescendants, HasAnyUserFocusOrFocusedDescendants, GetFocusBrush
UUserWidget::bIsFocusable:bit†(5.2, ctor-only) — IsFocusable/SetIsFocusable;
  SObjectWidget::SupportsKeyboardFocus forwards to it

FHittestGrid — CellSize = 128×128 px (hard-coded); FWidgetData{WeakWidget, CustomPath, UpperLeftCell,
  LowerRightCell, PrimarySort=(BatchPriorityGroup<<32)|LayerId, SecondarySort:FSlateInvalidationWidgetSortOrder,
  UserIndex}; AddWidget (gated on GetVisibility().IsHitTestVisible()), RemoveWidget, UpdateWidget,
  ContainsWidget, InsertCustomHitTestPath, Add/RemoveGrid, SetPaintToHitTestTransform,
  GetBubblePath(DesktopPos, CursorRadius, bIgnoreEnabledStatus, UserIndex=INDEX_NONE, LayerRange=all),
  FindNextFocusableWidget(start, dir, navReply, boundary, userIndex), GetCollapsedWidgets (stable sort by
  PrimarySort then SecondarySort), GetHitIndexFromCellIndex, GetCellCoordinate (clamped)
  hit test = culling rect ∧ clipping state ∧ rotated-rect overlap (binned by AABB, tested rotated)
  bubble path built from Advanced_GetPaintParentWidget(); discarded if it does not end at a window;
  truncated at the first disabled widget unless bIgnoreEnabledStatus
FReply — Handled()[[nodiscard]], Unhandled()[[nodiscard]], CaptureMouse, UseHighPrecisionMouseMovement,
  SetMousePos, SetUserFocus(widget,cause=SetDirectly,bAllUsers=false), ClearUserFocus(×2),
  CancelFocusRequest, SetNavigation(dir|widget, genesis, source=FocusedWidget), LockMouseToWidget,
  ReleaseMouseLock, ReleaseMouseCapture, DetectDrag(widget,button), BeginDragDrop(op), EndDragDrop,
  PreventThrottling
  queries ShouldReleaseMouse, ShouldSetUserFocus, ShouldReleaseUserFocus, AffectsAllUsers,
  ShouldUseHighPrecisionMouse, ShouldReleaseMouseLock, ShouldThrottle, GetMouseLockWidget,
  GetUserFocusRecepient, GetFocusCause, GetMouseCaptor, GetNavigationType, GetNavigationGenesis,
  GetNavigationSource, GetNavigationDestination, GetDragDropContent, ShouldEndDragDrop,
  GetDetectDragRequest, GetDetectDragRequestButton, GetRequestedMousePos, ToString
FEventReply{NativeReply:FReply} — operator== compares ONLY the handled bit
UWidgetBlueprintLibrary — Handled, Unhandled, CaptureMouse, ReleaseMouseCapture, LockMouse, UnlockMouse,
  SetUserFocus, CaptureJoystick†, ClearUserFocus, ReleaseJoystickCapture, SetMousePosition, DetectDrag,
  DetectDragIfPressed, EndDragDrop, CreateDragDropOperation, IsDragDropping, GetDragDroppingContent,
  CancelDragDrop, SetInputMode_UIOnlyEx, SetInputMode_GameAndUIEx, SetInputMode_GameOnly, SetFocusToGameViewport

FDragDropOperation — bCreateNewWindow=true; AffectedByPointerEvent=true; OnDrop, OnDragged, OnCursorQuery,
  GetDefaultDecorator()=NULL by default, GetDecoratorPosition=(0,0), SetDecoratorVisibility,
  IsExternalOperation=false, IsWindowlessOperation, SetCursorOverride, IsOfType<T>/IsOfTypeImpl,
  Construct, Create/DestroyCursorDecoratorWindow; DRAG_DROP_OPERATOR_TYPE(TYPE,BASE) string RTTI
  FExternalDragOperation{NewText,NewFiles,NewOperation,HasText,HasFiles; DragText=1<<0, DragFiles=1<<1}
  FGameDragDropOperation — bCreateNewWindow=false (windowless)
UDragDropOperation — Tag:FString(ExposeOnSpawn, fallback visual text); Payload:UObject*(ExposeOnSpawn);
  DefaultDragVisual:UWidget*(ExposeOnSpawn, "Drag Visual"); Pivot:EDragPivot(ExposeOnSpawn);
  Offset:FVector2D(AdvancedDisplay, percentage −1..+1); OnDrop/OnDragCancelled/OnDragged(BlueprintAssignable);
  Drop/DragCancelled/Dragged(BlueprintNativeEvent); ConvertSlateDropZoneToUMG
FUMGDragDropOp : FGameDragDropOperation — windowless; visual = DefaultDragVisual or STextBlock(Tag),
  wrapped in SDPIScaler; 0.150 s ease-in from grab point; pivot + percentage offset placement;
  painted at ++MaxLayerId with EVisibility::HitTestInvisible (NO SDragDropDecorator class in 5.8)
Drag trigger distance = max(1 mm in px, 5 px desktop / 10 px non-desktop); Escape cancels a drag

Tooltips — UWidget::ToolTipText†+ToolTipTextDelegate, ToolTipWidget†+ToolTipWidgetDelegate;
  SetToolTip wraps in SToolTip{TextMargin=0, BorderImage=nullptr}
  IToolTip — AsWidget, GetContentWidget, SetContentWidget, ResetContentWidget, IsEmpty, IsInteractive
  (Ctrl to interact), OnOpening, OnClosed, OnSetInteractiveWindowLocation
  SToolTip args — Text, Content(overrides Text), Font="ToolTip.Font", TextMargin=FMargin(8.0f),
  BorderImage="ToolTip.Background", IsInteractive=false, OnSetInteractiveWindowLocation
  SWidget — GetToolTip, OnToolTipClosing, SetToolTipText(×2), SetToolTip, EnableToolTipForceField,
  HasToolTipForceField, Set/GetToolTipForceFieldExpansion (stored as FSlateToolTipMetaData, not a field)
  CVars — Slate.TooltipSummonDelay=0.15f; Slate.TooltipIntroDuration=0.1f;
  Slate.CursorSignificantMoveDetectionThreshold=0.0; Slate.AllowTooltipsWithHiddenCursor=false;
  Slate.AllowTooltipsWhileMouseDown=false; Slate.EnableTooltipForceFieldDraw=false
  constants — TooltipOffsetFromMouse=(12,8); TooltipOffsetFromForceField=(4,3); 50 ms motionless requirement
  suppressed while drag-dropping / LMB down; queried over the disabled-INCLUSIVE path
  NO tooltip settings on UUserInterfaceSettings or USlateSettings in 5.8

FInputModeUIOnly — MouseLockMode=LockInFullscreen; ReleaseMouseCapture; SetIgnoreInput(TRUE);
  MouseCaptureMode=NoCapture
FInputModeGameAndUI — MouseLockMode=DoNotLock; bHideCursorDuringCapture=true;
  ShouldFlushInputOnViewportFocus=FALSE; SetIgnoreInput(false); MouseCaptureMode=CaptureDuringMouseDown
FInputModeGameOnly — bConsumeCaptureMouseDown=true; UseHighPrecisionMouseMovement; SetUserFocus(Viewport);
  LockMouseToWidget(Viewport); MouseLockMode=LockOnCapture; SetIgnoreInput(false);
  MouseCaptureMode = CapturePermanently | CapturePermanently_IncludingInitialMouseDown
APlayerController::SetInputMode → ApplyInputMode(LocalPlayer->GetSlateOperations(), GameViewportClient)
  — expressed as a DEFERRED FReply, applied next Slate tick
```

### C.12 Style structs, complete field lists

```
FButtonStyle          — Normal, Hovered, Pressed, Disabled :FSlateBrush; NormalForeground, HoveredForeground,
                        PressedForeground, DisabledForeground :FSlateColor; NormalPadding, PressedPadding :FMargin;
                        PressedSlateSound, ClickedSlateSound, HoveredSlateSound :FSlateSound;
                        PressedSound_DEPRECATED, HoveredSound_DEPRECATED :FName
FCheckBoxStyle        — CheckBoxType:ESlateCheckBoxType; UncheckedImage, UncheckedHoveredImage,
                        UncheckedPressedImage, CheckedImage, CheckedHoveredImage, CheckedPressedImage,
                        UndeterminedImage, UndeterminedHoveredImage, UndeterminedPressedImage :FSlateBrush;
                        Padding:FMargin; BackgroundImage, BackgroundHoveredImage, BackgroundPressedImage :FSlateBrush;
                        ForegroundColor, HoveredForeground, PressedForeground :FSlateColor
FTextBlockStyle       — Font:FSlateFontInfo; ColorAndOpacity:FSlateColor(DisplayName "Color");
                        ShadowOffset:FVector2D; ShadowColorAndOpacity:FLinearColor;
                        SelectedBackgroundColor, HighlightColor :FSlateColor;
                        HighlightShape, StrikeBrush, UnderlineBrush :FSlateBrush
FSliderStyle          — NormalBarImage, HoveredBarImage, DisabledBarImage, NormalThumbImage,
                        HoveredThumbImage, DisabledThumbImage :FSlateBrush; BarThickness:float
FProgressBarStyle     — BackgroundImage, FillImage, MarqueeImage :FSlateBrush; EnableFillAnimation:bool
FEditableTextStyle    — Font; ColorAndOpacity; BackgroundImageSelected; BackgroundImageComposing; CaretImage
FEditableTextBoxStyle — BackgroundImageNormal, BackgroundImageHovered, BackgroundImageFocused,
                        BackgroundImageReadOnly :FSlateBrush; Padding:FMargin; Font_DEPRECATED;
                        TextStyle:FTextBlockStyle; ForegroundColor, BackgroundColor, ReadOnlyForegroundColor,
                        FocusedForegroundColor :FSlateColor; HScrollBarPadding, VScrollBarPadding :FMargin;
                        ScrollBarStyle:FScrollBarStyle
FSpinBoxStyle         — BackgroundBrush, ActiveBackgroundBrush, HoveredBackgroundBrush, ActiveFillBrush,
                        HoveredFillBrush, InactiveFillBrush, ArrowsImage :FSlateBrush;
                        ForegroundColor:FSlateColor; TextPadding, InsetPadding :FMargin
FComboBoxStyle        — ComboButtonStyle:FComboButtonStyle; PressedSlateSound, SelectionChangeSlateSound;
                        ContentPadding, MenuRowPadding :FMargin; +2 deprecated FName sounds
FComboButtonStyle     — ButtonStyle:FButtonStyle; DownArrowImage:FSlateBrush; ShadowOffset;
                        ShadowColorAndOpacity; MenuBorderBrush:FSlateBrush; MenuBorderPadding,
                        ContentPadding, DownArrowPadding :FMargin; DownArrowAlign:EVerticalAlignment
FScrollBarStyle       — HorizontalBackgroundImage, VerticalBackgroundImage, VerticalTopSlotImage,
                        HorizontalTopSlotImage, VerticalBottomSlotImage, HorizontalBottomSlotImage,
                        NormalThumbImage, HoveredThumbImage, DraggedThumbImage :FSlateBrush; Thickness:float
FScrollBoxStyle       — BarThickness:float; TopShadowBrush, BottomShadowBrush, LeftShadowBrush,
                        RightShadowBrush :FSlateBrush; HorizontalScrolledContentPadding=FMargin(0,0,1,0);
                        VerticalScrolledContentPadding=FMargin(0,0,0,1)
FExpandableAreaStyle  — CollapsedImage, ExpandedImage :FSlateBrush; RolloutAnimationSeconds:float
FTableViewStyle       — BackgroundBrush:FSlateBrush   ← one field
FTableRowStyle        — SelectorFocusedBrush, ActiveHoveredBrush, ActiveBrush, InactiveHoveredBrush,
                        InactiveBrush :FSlateBrush; bUseParentRowBrush:bool; ParentRowBackgroundBrush,
                        ParentRowBackgroundHoveredBrush, EvenRowBackgroundHoveredBrush, EvenRowBackgroundBrush,
                        OddRowBackgroundHoveredBrush, OddRowBackgroundBrush :FSlateBrush;
                        TextColor, SelectedTextColor :FSlateColor; DropIndicator_Above, DropIndicator_Onto,
                        DropIndicator_Below, ActiveHighlightedBrush, InactiveHighlightedBrush :FSlateBrush
```

### C.13 Widget census — one line per author-facing class

```
COMMON      UTextBlock URichTextBlock URichTextBlockDecorator URichTextBlockImageDecorator UImage UButton
            UCheckBox USlider UProgressBar USpacer UBorder UThrobber UCircularThrobber
INPUT       UEditableText UEditableTextBox UMultiLineEditableText UMultiLineEditableTextBox USpinBox
            UComboBoxString UComboBoxKey UComboBox(X) UInputKeySelector UScrollBar(X)
PANELS      UCanvasPanel UGridPanel UUniformGridPanel UHorizontalBox UVerticalBox UStackBox UOverlay
            UWrapBox UScrollBox USizeBox UScaleBox USafeZone UWidgetSwitcher
SLOTS       UPanelSlot UCanvasPanelSlot UGridSlot UUniformGridSlot UHorizontalBoxSlot UVerticalBoxSlot
            UStackBoxSlot UOverlaySlot UWrapBoxSlot UScrollBoxSlot USizeBoxSlot UScaleBoxSlot USafeZoneSlot
            UWidgetSwitcherSlot UBorderSlot UButtonSlot UBackgroundBlurSlot UWindowTitleBarAreaSlot
LISTS       UListViewBase UListView UTileView UTreeView UDynamicEntryBoxBase UDynamicEntryBox
            IUserListEntry IUserObjectListEntry UUserListEntryLibrary UUserObjectListEntryLibrary
MEDIA/3D    UWidgetComponent UWidgetInteractionComponent URetainerBox UBackgroundBlur UPostBufferUpdate
            USlatePostBufferProcessorUpdater UViewport(X)
NAV/MISC    UMenuAnchor UExpandableArea UNamedSlot INamedSlotInterface UInvalidationBox UNativeWidgetHost
            UWindowTitleBarArea
UICOMPONENT UMouseHoverComponent USizeBoxComponent(X) UScaleBoxComponent(X)
BASE        UVisual UWidget UPanelWidget UContentWidget UTextLayoutWidget UUserWidget
COMMONUI    UCommonUserWidget UCommonActivatableWidget UCommonActivatableWidgetSwitcher
            UCommonActivatableWidgetContainerBase UCommonActivatableWidgetStack UCommonActivatableWidgetQueue
            UCommonButtonBase UCommonButtonInternalBase UCommonButtonStyle UCommonTextBlock UCommonTextStyle
            UCommonTextScrollStyle UCommonBorder UCommonBorderStyle UCommonRichTextBlock UCommonUIRichTextData
            UCommonNumericTextBlock UCommonDateTimeTextBlock UCommonAnimatedSwitcher UCommonVisibilitySwitcher
            UCommonVisibilitySwitcherSlot UCommonWidgetCarousel UCommonWidgetCarouselNavBar
            UCommonTabListWidgetBase UCommonListView UCommonTileView UCommonTreeView
            UCommonHierarchicalScrollBox UCommonCustomNavigation UCommonLoadGuard ULoadGuardSlot
            UCommonLazyImage UCommonLazyWidget UCommonVideoPlayer UCommonVisualAttachment UAnalogSlider
            UCommonRotator UCommonActionWidget UCommonBoundActionBar UCommonBoundActionButton
            UCommonHardwareVisibilityBorder UCommonWidgetGroupBase UCommonButtonGroupBase
            UCommonUIVisibilitySubsystem UCommonUISubsystemBase UCommonUIActionRouterBase
            UCommonInputSubsystem UCommonGameViewportClient UCommonUISettings UCommonUIEditorSettings
            UCommonUIInputSettings UCommonInputSettings UCommonInputPlatformSettings
            UCommonInputBaseControllerData UCommonUIInputData UCommonUIHoldData UCommonInputActionDomain
            UCommonInputActionDomainTable UCommonUILibrary
```

**Count**: 55 author-facing widget classes in `UMG/Public/Components/` (12 common, 10 input, 13 panels
+ 18 slots, 6 list/data, 7 media/3D, 7 nav/misc, 3 UI components, 6 base), plus ~50 CommonUI classes.
15 style structs. 5 of the 55 are `Experimental` (`UComboBox`, `UScrollBar`, `UViewport`,
`USizeBoxComponent`, `UScaleBoxComponent`); `UListView::bEnableProximateEntryNavigation` is a
per-property `Experimental`. Roughly 40 % of all author-facing properties carry a `UE_DEPRECATED`
direct-access marker (5.1 / 5.2) and must be reached through getters/setters.
