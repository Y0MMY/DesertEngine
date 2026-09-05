I have everything needed for the reframed deliverable. Writing up now.

**Source:** `EpicGames/UnrealEngine` @ `release` = **UE 5.8.2** (`Engine/Build/Build.version`). Every claim below is read from those headers, not from docs. Paths are repo-relative.

---

# 1. Architecture — the short version (as requested, one section not an essay)

`UWidget : UVisual : UObject` (`Runtime/UMG/Public/Components/Widget.h:215`, `Visual.h:11`). Each `UWidget` *builds and owns* a Slate widget; it is not one. The bridge is exactly four members:

- `virtual TSharedRef<SWidget> RebuildWidget()` (`Widget.h:1148`) — constructs the `SWidget`.
- `TSharedRef<SWidget> TakeWidget()` / `TakeDerivedWidget<T>()` (`Widget.h:823,848`) — builds-or-returns; the derived variant must be called *before* `TakeWidget`.
- `TWeakPtr<SWidget> MyWidget` (`Widget.h:1195`) — the cache; `GetCachedWidget()` (`:857`) never rebuilds. Also `ComponentWrapperWidget` (`:1198`) and `DesignWrapperWidget` (`:1228`).
- `virtual void SynchronizeProperties()` (`Widget.h:938`) — pushes every UPROPERTY onto the live `SWidget`; there is a `bRoutedSynchronizeProperties` + `VerifySynchronizeProperties()` assert (`:1237-1243`) that catches subclasses forgetting `Super::`.

What the UObject layer buys: reflection-driven Details editing, Blueprint scripting, serialization as an asset, GC, and — new in UE5 — **field notification**. `UWidget` now literally implements `INotifyFieldValueChanged` (`Widget.h:215`), with `K2_AddFieldValueChangedDelegate` / `K2_BroadcastFieldValueChanged` on the widget itself. Properties opt in via a `FieldNotify` UPROPERTY specifier — present on `UImage::Brush`, `UProgressBar::Percent/bIsMarquee/FillColorAndOpacity`, `USlider::Value`, `USpinBox::Value`, `UCheckBox::CheckedState`, `UWidgetSwitcher::ActiveWidgetIndex`, `UWidget::bIsEnabled`, `UWidget::Visibility`, and the text of every editable-text widget. **This is the replacement for the old per-frame property-binding dropdown**, and it is what the MVVM plugin consumes.

Structure: `UWidgetTree : UObject` holds `RootWidget`, `AllWidgets`, `NamedSlotBindings` (`Blueprint/WidgetTree.h:456-459`). `UWidgetBlueprintGeneratedClass` (`Blueprint/WidgetBlueprintGeneratedClass.h:362`) carries the template `WidgetTree`, `Bindings` (`FDelegateRuntimeBinding` — the legacy binding system, `EBindingKind::Property|Function`), `Animations`, `NamedSlots` / `NamedSlotsWithID` / `AvailableNamedSlots` / `InstanceNamedSlots`, and the perf flags `bClassRequiresNativeTick`, `bCanCallPreConstruct`. `UUserWidget : UWidget, INamedSlotInterface` (`Blueprint/UserWidget.h:242`) carries `bHasScriptImplementedTick` / `bHasScriptImplementedPaint` (`:320-321`) and `EWidgetTickFrequency TickFrequency` (`:330`) — the tick-cost controls.

**MVVM** is a separate plugin: `Engine/Plugins/Runtime/ModelViewViewModel`. `EMVVMBindingMode` = `OneTimeToDestination, OneWayToDestination, TwoWay, OneTimeToSource (hidden), OneWayToSource` (`Types/MVVMBindingMode.h`). Public surface includes `MVVMViewModelBase.h`, `MVVMSubsystem.h`, `MVVMGameSubsystem.h`, `View/MVVMView.h`, `View/MVVMViewClass.h`, `Types/MVVMExecutionMode.h`, `Types/MVVMConditionOperation.h`.

**UI Components (UE5, notable and easy to miss):** `Extensions/UIComponent.h` — `UUIComponent : UObject, INotifyFieldValueChanged`, "base class for UI Components that can be added to any UMG Widget in UMG Designer", with `Initialize/PreConstruct/Construct/Destruct` lifecycle. Shipping components: `UScaleBoxComponent`, `USizeBoxComponent`, `UMouseHoverComponent`. This is composition-over-nesting: you get ScaleBox/SizeBox behaviour without inserting a wrapper widget.

---

# 2. THE INVENTORY — every `U*` widget in `Runtime/UMG/Public/Components`

84 headers; authoritative list. Slot classes are listed with their panel.

## 2.1 Base / infrastructure
| Class | Base | Notes |
|---|---|---|
| `UVisual` | `UObject` | root of everything |
| `UWidget` | `UVisual`, `INotifyFieldValueChanged` | see §3 for its universal properties |
| `UPanelWidget` | `UWidget` | `TArray<UPanelSlot*> Slots` (Instanced); `AddChild/RemoveChild/GetChildAt/ClearChildren` |
| `UContentWidget` | `UPanelWidget` | exactly one child; `SetContent/GetContent/GetContentSlot` |
| `UPanelSlot` | `UVisual` | `Parent`, `Content` |
| `UTextLayoutWidget` | `UWidget` | shared text-layout base — see §2.4 |
| `UNativeWidgetHost` | `UWidget` | hosts a raw `SWidget` inside UMG |
| `UNamedSlot` | `UContentWidget` | `bExposeOnInstanceOnly`, `SlotGuid` |
| `INamedSlotInterface` | — | `GetSlotNames / GetContentForSlot / SetContentForSlot` |

## 2.2 Layout panels
| Class | Slot | Designer-facing properties |
|---|---|---|
| `UCanvasPanel` | `UCanvasPanelSlot` | slot: `FAnchorData LayoutData {Offsets, Anchors, Alignment}`, `bAutoSize`, `ZOrder`. Setters `SetPosition/SetSize/SetOffsets/SetAnchors/SetAlignment/SetAutoSize/SetZOrder` |
| `UHorizontalBox` | `UHorizontalBoxSlot` | `FSlateChildSize Size`, `Padding`, `HAlign`, `VAlign` |
| `UVerticalBox` | `UVerticalBoxSlot` | same four |
| `UStackBox` | `UStackBoxSlot` | `EOrientation Orientation` on the box — unified H/V box; slot has `Padding`, `Size`, `HAlign`, `VAlign` |
| `UOverlay` | `UOverlaySlot` | `Padding`, `HAlign`, `VAlign`; `ReplaceOverlayChildAt` |
| `UGridPanel` | `UGridSlot` | panel: `ColumnFill[]`, `RowFill[]`. Slot: `Row`, `RowSpan`, `Column`, `ColumnSpan`, **`Layer`**, **`Nudge`**, `Padding`, `HAlign`, `VAlign` |
| `UUniformGridPanel` | `UUniformGridSlot` | panel: `SlotPadding`, `MinDesiredSlotWidth/Height`. Slot: `Row`, `Column`, `HAlign`, `VAlign` |
| `UWrapBox` | `UWrapBoxSlot` | panel: `InnerSlotPadding`, `WrapSize` + `bExplicitWrapSize`, `HorizontalAlignment`, `Orientation`. Slot: `Padding`, `FillSpanWhenLessThan`, `bFillEmptySpace`, `bForceNewLine`, `HAlign`, `VAlign` |
| `UScrollBox` | `UScrollBoxSlot` | see §2.6 |
| `UWidgetSwitcher` | `UWidgetSwitcherSlot` | `ActiveWidgetIndex` (FieldNotify); slot `Padding`/`HAlign`/`VAlign` |

⚠️ **There is no `URadialBox` widget.** `RadialBoxSettings.h` defines `FRadialBoxSettings` (`StartingAngle`, `bDistributeItemsEvenly`, `bClockwiseOrder`, `AngleBetweenItems`, `SectorCentralAngle`, `MarginSize`) which is consumed **only** by `UDynamicEntryBoxBase` via `EDynamicBoxType::Radial`.

## 2.3 Layout modifiers (single-child)
- **`USizeBox`** (`UContentWidget`) — the constraint box. Eight optional overrides, each with an `InlineEditConditionToggle` checkbox: `WidthOverride`, `HeightOverride`, `MinDesiredWidth/Height`, `MaxDesiredWidth/Height`, `MinAspectRatio`, `MaxAspectRatio`, each with `Set*` and `Clear*`. Slot: `Padding`, `HAlign`, `VAlign`.
- **`UScaleBox`** (`UContentWidget`) — `EStretch Stretch`, `EStretchDirection StretchDirection`, `UserSpecifiedScale`, `IgnoreInheritedScale`. Slot has only `HAlign`/`VAlign` (its `Padding` is `_DEPRECATED`).
- **`USafeZone`** (`UContentWidget`) — `PadLeft/PadRight/PadTop/PadBottom` + `SetSidesToPad`. Slot `USafeZoneSlot`: `bIsTitleSafe`, `FMargin SafeAreaScale`, `HAlign`, `VAlign`, `Padding`.
- **`UBorder`** (`UContentWidget`) — `FSlateBrush Background`, `BrushColor`, `ContentColorAndOpacity`, `Padding`, `HAlign`, `VAlign`, `DesiredSizeScale`, `bShowEffectWhenDisabled`, `bFlipForRightToLeftFlowDirection`; four bindable pointer events (`OnMouseButtonDown/Up/Move/DoubleClick`); `SetBrushFromTexture/Material/Asset`, `GetDynamicMaterial()`.
- **`UBackgroundBlur`** (`UContentWidget`) — `BlurStrength` (0..100), `BlurRadius` (advanced), `bApplyAlphaToBlur`, **`FVector4 CornerRadius`**, `LowQualityFallbackBrush`, `Padding`, `HAlign`, `VAlign`.
- **`UWindowTitleBarArea`** (`UContentWidget`) — `bWindowButtonsEnabled`, `bDoubleClickTogglesFullscreen`. Drag region for borderless windows.
- **`USpacer`** (`UWidget`) — `FVector2D Size`.
- **`UCommonVisualAttachment`** (CommonUI, `USizeBox`) — adds `ContentAnchor`; attaches a visual that doesn't affect layout.

## 2.4 Text
`UTextLayoutWidget` supplies to all of its subclasses: `Justification` (`ETextJustify`), `AutoWrapText`, `WrapTextAt`, `WrappingPolicy` (`ETextWrappingPolicy`), `Margin`, `LineHeightPercentage`, `ApplyLineHeightToBottomLine`, `FShapedTextOptions ShapedTextOptions` (= `TextShapingMethod` + **`ETextFlowDirection TextFlowDirection`**, each behind an override toggle), `EFontFacesLoadingPaintPolicy FontFacesLoadingPaintPolicy` + `OnAllFontFacesFinishLoading`.

| Class | Key properties |
|---|---|
| `UTextBlock` | `Text` (+`TextDelegate`), `Font` (`FSlateFontInfo`), `ColorAndOpacity` (`FSlateColor`), `ShadowOffset`, `ShadowColorAndOpacity`, `StrikeBrush`, `MinDesiredWidth`, `ETextTransformPolicy TextTransformPolicy`, `ETextOverflowPolicy TextOverflowPolicy`, perf flags `bSimpleTextMode` + `bWrapWithInvalidationPanel`. Fns: `SetFontMaterial`, `SetFontOutlineMaterial`, `GetDynamicFontMaterial()`, `GetDynamicOutlineMaterial()`, `SetFontSize/GetFontSize` |
| `URichTextBlock` | `Text`, **`UDataTable* TextStyleSet`** (rows = `FRichTextStyleRow{FTextBlockStyle}`), `TArray<TSubclassOf<URichTextBlockDecorator>> DecoratorClasses`, `DefaultTextStyleOverride` + `bOverrideDefaultStyle`, transform/overflow policies, `SetDefaultMaterial`, `GetDefaultDynamicMaterial()`, `RefreshTextLayout()` |
| `URichTextBlockDecorator` / `URichTextBlockImageDecorator` | inline markup decorators; image decorator takes `UDataTable* ImageSet` with rows `FRichImageRow{FSlateBrush}` |
| `UEditableText` | `Text`, `HintText`, `FEditableTextStyle WidgetStyle`, `IsReadOnly`, `IsPassword`, `MinimumDesiredWidth`, `Justification`, `OverflowPolicy`, behaviour set (`IsCaretMovedWhenGainFocus`, `SelectAllTextWhenFocused`, `RevertTextOnEscape`, `ClearKeyboardFocusOnCommit`, `SelectAllTextOnCommit`, `AllowContextMenu`), full virtual-keyboard block (`KeyboardType`, `VirtualKeyboardOptions`, `VirtualKeyboardTrigger`, `VirtualKeyboardDismissAction`, `EnableIntegratedKeyboard`), `ShapedTextOptions`. Events `OnTextChanged`, `OnTextCommitted` |
| `UEditableTextBox` | as above but `FEditableTextBoxStyle`, plus **error state**: `SetError/ClearError/HasError` |
| `UMultiLineEditableText` | `UTextLayoutWidget`; `Text`, `HintText`, `FTextBlockStyle WidgetStyle`, `bIsReadOnly`, `ClearTextSelectionOnFocusLoss`, virtual-keyboard options |
| `UMultiLineEditableTextBox` | `FEditableTextBoxStyle`, `SetError`, `SetTextStyle`, `SetForegroundColor` |

## 2.5 Controls
| Class | Key properties |
|---|---|
| `UButton` (`UContentWidget`) | `FButtonStyle WidgetStyle`, `ColorAndOpacity`, `BackgroundColor`, `ClickMethod`/`TouchMethod`/`PressMethod`, `IsFocusable`, `bAllowDragDrop`. Events `OnClicked/OnPressed/OnReleased/OnHovered/OnUnhovered` |
| `UCheckBox` (`UContentWidget`) | `ECheckBoxState CheckedState` (FieldNotify, tri-state), `FCheckBoxStyle WidgetStyle`, `HorizontalAlignment`, click/touch/press methods, `IsFocusable`. Event `OnCheckStateChanged`. Also ships `UWidgetCheckedStateRegistration` (widget state binding) |
| `USlider` | `Value` (FieldNotify) + `ValueDelegate`, `MinValue`, `MaxValue`, `FSliderStyle`, `Orientation`, `SliderBarColor`, `SliderHandleColor`, `IndentHandle`, `Locked`, `MouseUsesStep`, `RequiresControllerLock`, `StepSize`, `IsFocusable`, `bPreventThrottling`. Events: `OnValueChanged`, `OnMouseCaptureBegin/End`, `OnControllerCaptureBegin/End` |
| `USpinBox` | `Value` (FieldNotify), `FSpinBoxStyle`, `Min/MaxValue` and separate `Min/MaxSliderValue` (all four individually overridable), `Delta`, `SliderExponent`, `MinFractionalDigits`/`MaxFractionalDigits`, `bAlwaysUsesDeltaSnap`, `bEnableSlider`, `Font`, `Justification`, `MinDesiredWidth`, `ForegroundColor`, keyboard opts. Events: `OnValueChanged/OnValueCommitted/OnBeginSliderMovement/OnEndSliderMovement` |
| `UProgressBar` | `Percent` (FieldNotify) + delegate, `FProgressBarStyle`, `EProgressBarFillType BarFillType`, `EProgressBarFillStyle BarFillStyle`, `bIsMarquee`, `BorderPadding`, `FillColorAndOpacity` |
| `UComboBoxString` | `DefaultOptions[]`, `SelectedOption` (FieldNotify), `FComboBoxStyle`, `FTableRowStyle ItemStyle`, `FScrollBarStyle`, `ContentPadding`, `MaxListHeight`, `HasDownArrow`, `EnableGamepadNavigationMode`, `Font`, `ForegroundColor`, `bIsFocusable`, `OnGenerateWidgetEvent`. Full Add/Remove/Find/Clear/Refresh/SetSelectedIndex API |
| `UComboBoxKey` | same shape but `FName Options[]`; two generator events (`OnGenerateContentWidget`, `OnGenerateItemWidget`) |
| `UComboBox` | legacy/thin `UObject`-item variant |
| `UInputKeySelector` | key-rebinding control: `FInputChord SelectedKey` (FieldNotify), `FButtonStyle`, `FTextBlockStyle`, `KeySelectionText`, `NoKeySpecifiedText`, `bAllowModifierKeys`, `bAllowGamepadKeys`, `TArray<FKey> EscapeKeys`. Events `OnKeySelected`, `OnIsSelectingKeyChanged` |
| `UMenuAnchor` (`UContentWidget`) | popup anchor: `TSubclassOf<UUserWidget> MenuClass`, `OnGetUserMenuContentEvent`, `EMenuPlacement Placement`, `bFitInWindow`, `ShouldDeferPaintingAfterWindowContent`, `UseApplicationMenuStack`, `ShowMenuBackground`. `Open/Close/ToggleOpen/IsOpen/GetMenuPosition/HasOpenSubMenus` |
| `UExpandableArea` (`UWidget, INamedSlotInterface`) | `FExpandableAreaStyle Style`, `BorderBrush`, `BorderColor`, `bIsExpanded`, `MaxHeight`, `HeaderPadding`, `AreaPadding`; **two named slots** `HeaderContent`/`BodyContent`; `SetIsExpanded_Animated` |
| `UThrobber` | `NumberOfPieces` (1..25), `bAnimateHorizontally/Vertically/Opacity`, `FSlateBrush Image` |
| `UCircularThrobber` | `NumberOfPieces`, `Period`, `Radius` (+`bEnableRadius` toggle), `Image` |
| `UImage` | `FSlateBrush Brush` (FieldNotify), `ColorAndOpacity`, `bFlipForRightToLeftFlowDirection`, `OnMouseButtonDownEvent`. Nine `SetBrushFrom*` overloads incl. **soft-object** variants (`SetBrushFromSoftTexture/SoftMaterial`) and `UTexture2DDynamic`; `SetDesiredSizeOverride`, `GetDynamicMaterial()` |
| `UViewport` (`UContentWidget`) | embedded 3D scene: `BackgroundColor`, `bIsEditorPreview`; `GetViewportWorld`, `Spawn(ActorClass)`, `SetViewLocation/Rotation`, `SetShowFlag`, `GetViewProjectionMatrix`, `SetLightIntensity`, `SetSkyIntensity` |
| `UScrollBar` | standalone bar: `FScrollBarStyle`, `bAlwaysShowScrollbar`, `bAlwaysShowScrollbarTrack`, `Orientation`, `Thickness`, `Padding`, `SetState(offsetFrac, thumbFrac)` |

## 2.6 Scrolling & virtualized lists — the non-trivial ones

**`UScrollBox`** (`UPanelWidget`) is the widest control in UMG. Style: `FScrollBoxStyle WidgetStyle` + `FScrollBarStyle WidgetBarStyle`. Axis/visibility: `Orientation`, `ScrollBarVisibility`, `ScrollbarThickness`, `ScrollbarPadding`, `AlwaysShowScrollbar`, `AlwaysShowScrollbarTrack`. Physics: `AllowOverscroll`, `BackPadScrolling`, `FrontPadScrolling`, `bAnimateWheelScrolling`, `ScrollAnimationInterpolationSpeed`, `WheelScrollMultiplier`, `EConsumeMouseWheel ConsumeMouseWheel`, `bAllowRightClickDragScrolling`, `bEnableTouchScrolling`, `bConsumePointerInput`, `FKey AnalogMouseWheelKey`. Focus integration: `EScrollWhenFocusChanges ScrollWhenFocusChanges`, `EDescendantScrollDestination NavigationDestination`, `NavigationScrollPadding`, `bIsFocusable`. Query/command API: `SetScrollOffset/GetScrollOffset/GetScrollOffsetOfEnd/GetViewFraction/GetViewOffsetFraction/GetOverscrollOffset/GetOverscrollPercentage/ScrollToStart/ScrollToEnd/ScrollWidgetIntoView/EndInertialScrolling`. Events: `OnUserScrolled`, `OnScrollBarVisibilityChanged`, `OnFocusReceived/Lost/Updated`. Slot: `Size`, `Padding`, `HAlign`, `VAlign`.

**`UListViewBase`** (`UWidget`) is the virtualization engine. The entry contract is `TSubclassOf<UUserWidget> EntryWidgetClass` with `MustImplement = UserListEntry` — i.e. your row is a Widget Blueprint implementing `IUserListEntry` / `IUserObjectListEntry`. Recycling is via `FUserWidgetPool EntryWidgetPool`. Designer affordance: **`NumDesignerPreviewEntries = 5`** — the designer fakes rows so you can lay out against real content. Scrolling knobs mirror ScrollBox (`WheelScrollMultiplier`, `bEnableScrollAnimation`, `ScrollingAnimationInterpolationSpeed`, `AllowOverscroll`, `bEnableRightClickScrolling`, `bEnableTouchScrolling`, `bInEnableTouchAnimatedScrolling`, `bIsPointerScrollingEnabled`, `bIsGamepadScrollingEnabled`, `bEnableFixedLineOffset` + `FixedLineScrollOffset`). **Built-in drag & drop**: `bAllowDragging`, `bAllowDragDrop`, `DragDropVisualPivot` (`EDragPivot`), `DragDropVisualOffset`, `DragDropVisualEntryClass`, `DragDropOperationClass`, `DragVisualWidget`, `CancelListViewDragDrop()`. Navigation: `bSelectItemOnNavigation`, `bAllowKeepPreselectedItems`. Runtime: `RegenerateAllEntries`, `RequestRefresh`, `ScrollToTop/Bottom`, `GetDisplayedEntryWidgets`, `SetScrollbarVisibility`, `EndInertialScrolling`.

**`UListView`** adds the item model: `TArray<UObject*> ListItems` + `AddItem/AddItemAt/AddItems/AddItemsAt/RemoveItem(s)/GetItemAt/GetNumItems/GetIndexForItem/ClearListItems`. Selection: `ESelectionMode SelectionMode`, `bClearSelectionOnClick`, `bClearScrollVelocityOnSelection`, `bReturnFocusToSelection`, `BP_SetSelectedItem/BP_SetItemSelection/BP_GetSelectedItems/BP_ClearSelection`. Layout: `Orientation`, `HorizontalEntrySpacing`, `VerticalEntrySpacing` (`EntrySpacing` deprecated), `ScrollBarPadding`, `FTableViewStyle WidgetStyle`, optional `ShadowBrushStyle` behind `bEnableShadowBrush`. View control: `ScrollIndexIntoView`, `NavigateToIndex`, `EScrollIntoViewAlignment`, experimental `bEnableProximateEntryNavigation`. **17 Blueprint-assignable events** including `BP_OnEntryInitialized/Generated/Released`, `BP_OnItemClicked/DoubleClicked`, the six drag events, `BP_OnItemSelectionChanged`, `BP_OnItemScrolledIntoView`, `BP_OnListViewScrolled/FinishedScrolling`, `BP_OnListViewTouchStart/Move/End`, `BP_OnItemIsHoveredChanged`, and the filter hook `BP_OnIsItemSelectableOrNavigable`.

**`UTileView : UListView`** — `EntryWidth`, `EntryHeight`, `EListItemAlignment TileAlignment`, `bWrapHorizontalNavigation`, `ScrollbarDisabledVisibility`, `bEntrySizeIncludesEntrySpacing`.
**`UTreeView : UListView`** — `SetItemExpansion`, `ExpandAll`, `CollapseAll`, event `BP_OnGetItemChildren` (bindable, supplies children) and `BP_OnItemExpansionChanged`.

**`UDynamicEntryBoxBase`** (`UWidget`) — non-virtualized pooled entry container, the thing you use for HUD lists / action bars. `EDynamicBoxType EntryBoxType` = `Horizontal | Vertical | Wrap | VerticalWrap | Radial | Overlay`; `EntrySpacing`, `SpacingPattern[]`, `EntrySizeRule` (`FSlateChildSize`), `EntryHorizontalAlignment`, `EntryVerticalAlignment`, `MaxElementSize`, `FRadialBoxSettings RadialBoxSettings`, `FUserWidgetPool EntryWidgetPool`. **`UDynamicEntryBox`** adds `EntryWidgetClass`, `NumDesignerPreviewEntries = 3`, `BP_CreateEntry/BP_CreateEntryOfClass/RemoveEntry/Reset`.

## 2.7 Rendering / performance widgets
- **`URetainerBox`** (`UContentWidget`) — `bRetainRender`, `RenderOnInvalidation`, `RenderOnPhase`, `Phase`, `PhaseCount` (render every Nth frame), `RequestRender()`, and the effect path: **`UMaterialInterface* EffectMaterial` + `FName TextureParameter` + `GetEffectMaterial()` returning a `UMaterialInstanceDynamic`**, plus `bShowEffectsInDesigner`. Underneath, `Slate/SRetainerWidget.h:39` is `SCompoundWidget, public FSlateInvalidationRoot` — it is an *invalidation root*, `Advanced_IsInvalidationRoot()` returns `bEnableRetainedRendering`, it implements `CustomPrepass`, and it has `OnGlobalInvalidationToggled` + `OnRetainerModeCVarChanged` (retainers self-disable under global invalidation).
- **`UInvalidationBox`** (`UContentWidget`) — just `bCanCache` + `InvalidateCache()`.
- **`UPostBufferUpdate`** (`UWidget`) — UE5 Slate post-process buffer: `TArray<ESlatePostRT> BuffersToUpdate`, `UpdateBufferInfos` (`FSlatePostBufferUpdateInfo{BufferToUpdate, USlatePostBufferProcessorUpdater* PostParamUpdater}`), `bUpdateOnlyPaintArea`, `bPerformDefaultPostBufferUpdate`. `UPostBufferBlurUpdater` supplies `GaussianBlurStrength`. **This is how you get scene-sampling blur/effects behind UI.**

---

# 3. `UWidget` universal properties — what every widget exposes in Details

From `Widget.h:1496-1529`. This is the per-widget baseline any clone must match:

- **Layout**: `UPanelSlot* Slot` (shown inline, `ShowOnlyInnerProperties` — this is why slot properties appear under the child).
- **Behavior**: `Visibility` (`ESlateVisibility` = `Visible, Collapsed, Hidden, HitTestInvisible ("Not Hit-Testable (Self & All Children)"), SelfHitTestInvisible ("Not Hit-Testable (Self Only)")`, FieldNotify) + `VisibilityDelegate`; `bIsEnabled` (FieldNotify) + `bIsEnabledDelegate`; `ToolTipText` + `ToolTipWidget` + both delegates; `EMouseCursor Cursor` behind `bOverride_Cursor`.
- **Render Transform**: `FWidgetTransform RenderTransform` (translation/scale/shear/angle) + `RenderTransformPivot`. Setters `SetRenderScale/SetRenderShear/SetRenderTransformAngle/SetRenderTranslation`.
- **Rendering**: `RenderOpacity`, `EWidgetClipping Clipping`, `EWidgetPixelSnapping PixelSnapping`.
- **Localization**: `EFlowDirectionPreference FlowDirectionPreference`.
- **Performance**: `bIsVolatile` + `ForceVolatile()`.
- **Accessibility**: `bOverrideAccessibleDefaults` gating `bCanChildrenBeAccessible`, `AccessibleBehavior`, `AccessibleSummaryBehavior` (`ESlateAccessibleBehavior = NotAccessible, Auto, Summary, Custom, ToolTip`), `AccessibleText`, `AccessibleSummaryText` + delegates; mirrored into `USlateAccessibleWidgetData`.
- **Navigation**: `UWidgetNavigation* Navigation` (Instanced) — see §6.
- **Designer-only flags** (not user-visible but define designer behaviour): `bIsVariable`, `bHiddenInDesigner`, `bExpandedInDesigner`, `bLockedInDesigner`, `DisplayLabel`, `CategoryName`, `uint8 DesignerFlags` with `EWidgetDesignFlags = None, Designing, ShowOutline, ExecutePreConstruct, Previewing`.
- **Focus/geometry API**: `SetUserFocus/SetKeyboardFocus/SetFocus/HasUserFocus/HasFocusedDescendants/HasMouseCapture/SupportsKeyboardFocus`, `GetCachedGeometry/GetTickSpaceGeometry/GetPaintSpaceGeometry`, `GetDesiredSize`, `ForceLayoutPrepass`, `InvalidateLayoutAndVolatility`.
- Legacy binding storage: `TArray<UPropertyBinding*> NativeBindings` (Transient).

---

# 4. Common UI (`Engine/Plugins/Runtime/CommonUI`) — Epic's recommended gamepad/console UI layer

**Styling is class-based, not struct-based** — styles are `UObject` subclasses referenced as `TSubclassOf<>`, i.e. authored as Blueprint assets:
- `UCommonButtonStyle` — either one `SingleMaterialBrush` (with `bSingleMaterial`) or **seven state brushes**: `NormalBase/NormalHovered/NormalPressed/SelectedBase/SelectedHovered/SelectedPressed/Disabled`; plus `ButtonPadding`, `CustomPadding`, `Min/MaxWidth`, `Min/MaxHeight`; five text-style slots (`NormalTextStyle`, `NormalHoveredTextStyle`, `SelectedTextStyle`, `SelectedHoveredTextStyle`, `DisabledTextStyle`); and **twelve sound slots** (pressed/clicked/hovered × normal/selected/locked).
- `UCommonTextStyle` — `Font`, `Color`, `bUsesDropShadow` + `ShadowOffset`/`ShadowColor`, `Margin`, `StrikeBrush`, `LineHeightPercentage`, `ApplyLineHeightToBottomLine`.
- `UCommonBorderStyle` — `FSlateBrush Background`.
- `UCommonTextScrollStyle` — marquee text: `Speed`, `StartDelay`, `EndDelay`, `FadeInDelay`, `FadeOutDelay`, `Clipping`.
- `UCommonUIEditorSettings` — project-level *template* styles (`TemplateTextStyle`, `TemplateButtonStyle`, `TemplateBorderStyle`) applied to newly created widgets. **This is the closest thing to a first-class runtime theme system in UE**: there is no global runtime theme swap; you get style-class references per widget plus project template defaults.

**Activation / stack model** (`Widgets/CommonActivatableWidgetContainer.h`):
- `UCommonActivatableWidget : UCommonUserWidget` — `ActivateWidget/DeactivateWidget/IsActivated`, `GetDesiredFocusTarget()`, `bAutoActivate`, `bSupportsActivationFocus`, `bIsModal`, `bAutoRestoreFocus`, `bIsBackHandler`, `bIsBackActionDisplayedInActionBar`, `OverrideBackActionDisplayName`, visibility-on-activation binding (`ActivatedVisibility`/`DeactivatedVisibility`, `BindVisibilityToActivation`), Enhanced-Input integration (`UInputMappingContext* InputMapping`, `InputMappingPriority`, `ActionDomainOverride`).
- `UCommonActivatableWidgetContainerBase : UWidget` — `WidgetList`, `DisplayedWidget`, `FUserWidgetPool GeneratedWidgetsPool`, transition (`ECommonSwitcherTransition TransitionType`, `ETransitionCurve`, `TransitionDuration = 0.4`, `ECommonSwitcherTransitionFallbackStrategy`). Subclasses **`UCommonActivatableWidgetStack`** (with `RootContentWidgetClass`) and **`UCommonActivatableWidgetQueue`**.

**Input routing**: `UCommonUIActionRouterBase : ULocalPlayerSubsystem` (`Input/CommonUIActionRouterBase.h`). Config in `UCommonUIInputSettings` — `TArray<FUIInputAction> InputActions` keyed by `FUIActionTag` (a `FGameplayTag` subtype, `UITag.h`) with `FUIActionKeyMapping{Key, HoldTime, HoldRollbackTime}`; `bLinkCursorToGamepadFocus`; `UIActionProcessingPriority = 10000`; and a full **analog cursor** config `FCommonAnalogCursorSettings` (`CursorAcceleration = 1500`, `CursorMaxSpeed = 2200`, `CursorDeadZone = 0.25`, `HoverSlowdownFactor = 0.4`, `ScrollDeadZone = 0.2`, `ScrollUpdatePeriod = 0.1`, `ScrollMultiplier = 2.5`, `MaxHoldDuration = 1.0`) plus `DefaultVirtualPointerClass`. `FUIInputConfig` = `{ECommonInputMode InputMode, EMouseCaptureMode, EMouseLockMode, bIgnoreMoveInput, bIgnoreLookInput, bHideCursorDuringViewportCapture}`.

**Action data**: `FCommonInputActionDataBase : FTableRowBase` — per-device key bindings in one data-table row: `KeyboardInputTypeInfo`, `DefaultGamepadInputTypeInfo`, `TMap<FName, FCommonInputTypeInfo> GamepadInputOverrides` (per gamepad model!), `TouchInputTypeInfo`, `DisplayName`, `HoldDisplayName`, `NavBarPriority`. `FCommonInputTypeInfo` = `{Key, AdditionalKeys[], OverrrideState, bActionRequiresHold, HoldTime, HoldRollbackTime, OverrideBrush}`.

**Widgets**: `UCommonButtonBase` (huge: selection/toggle/lock states, hold-to-activate via `bRequiresHold`+`HoldData`, 15 events, `InputActionWidget` bound via `BindWidget`), `UCommonActionWidget` (renders the platform-correct button glyph for an action), `UCommonBoundActionBar` (auto-populated action bar, `ActionButtonClass`), `UCommonBoundActionButton`, `UCommonTextBlock` (+`MobileFontSizeMultiplier`, `bAutoCollapseWithEmptyText`, scroll style), `UCommonRichTextBlock` (+inline icons: `ERichTextInlineIconDisplayMode = IconOnly|TextOnly|IconAndText`, `bTintInlineIcon`), `UCommonNumericTextBlock` (`ECommonNumericType = Number|Percentage|Seconds|Distance`, animated `InterpolateToValue`, `FCommonNumberFormattingOptions`), `UCommonDateTimeTextBlock` (countdowns), `UCommonLazyImage` / `UCommonLazyWidget` / `UCommonLoadGuard` (async-load with throbber), `UCommonVideoPlayer` (`UMediaSource` playback as a widget), `UCommonAnimatedSwitcher` / `UCommonActivatableWidgetSwitcher` / `UCommonVisibilitySwitcher`, `UCommonWidgetCarousel` + `UCommonWidgetCarouselNavBar`, `UCommonTabListWidgetBase` (tabs linked to a switcher), `UCommonRotator` (left/right value picker), `UCommonHardwareVisibilityBorder` (shows/hides by `FGameplayTagQuery` on platform traits), `UAnalogSlider`, `UCommonHierarchicalScrollBox`, `UCommonListView/TileView/TreeView`, `UCommonCustomNavigation`.

`UCommonUIVisibilitySubsystem` + `UCommonUISettings::PlatformTraits` drive per-platform widget visibility by gameplay tag.

---

# 5. Widgets in the world — `UWidgetComponent : UMeshComponent`

`EWidgetSpace = World | Screen`. Render path is explicit in the header: a `UTextureRenderTarget2D* RenderTarget` + `UMaterialInstanceDynamic* MaterialInstance` chosen from six preset materials (`Translucent`, `Translucent_OneSided`, `Opaque`, `Opaque_OneSided`, `Masked`, `Masked_OneSided`).

Properties: `WidgetClass`, `FIntPoint DrawSize`, `bDrawAtDesiredSize` (+ `CurrentDrawSize`), `Pivot`, `EWidgetGeometryMode GeometryMode = Plane | Cylinder` with `CylinderArcAngle` (1..180°), `EWidgetBlendMode BlendMode = Opaque | Masked | Transparent`, `bIsTwoSided`, `BackgroundColor`, `TintColorAndOpacity`, `OpacityFromTexture`, `bOverrideRenderTargetFormat` + `RenderTargetFormatOverride`, `bApplyGammaCorrection`. Update control: `bManuallyRedraw` + `RequestRedraw()`, `RedrawTime`, `TickWhenOffscreen`, `ETickMode = Disabled | Enabled | Automatic`, `EWidgetTimingPolicy = RealTime | GameTime`, `bUseInvalidationInWorldSpace` (World space only). Input: `bReceiveHardwareInput`, `bWindowFocusable`, `EWindowVisibility WindowVisibility = Visible | SelfHitTestInvisible`. Screen-space layering: `SharedLayerName`, `LayerZOrder`. Also `UBodySetup* BodySetup` — the component generates collision so traces can hit it.

**`UWidgetInteractionComponent : USceneComponent`** — `EWidgetInteractionSource = World | Mouse | CenterScreen | Custom`, `VirtualUserIndex`, `PointerIndex`, `TraceChannel`, `InteractionDistance`, `bEnableHitTesting`, `CustomHitResult`/`SetCustomHitResult`. Input injection: `PressPointerKey/ReleasePointerKey/PressKey/ReleaseKey/PressAndReleaseKey/SendKeyChar/ScrollWheel/SetFocus`. Queries: `IsOverInteractableWidget/IsOverFocusableWidget/IsOverHitTestVisibleWidget`, `Get2DHitLocation`, `GetHoveredWidgetComponent`, event `OnHoveredWidgetChanged`. Debug: `bShowDebug`, `DebugColor`, `DebugSphereLineThickness`, `DebugLineThickness`, plus a `UArrowComponent`.

**Material domain**: `EMaterialDomain::MD_UI` — display name **"User Interface"**, comment "The material will be used for UMG or Slate UI" (`Runtime/Engine/Public/MaterialDomain.h`). `FSlateBrush::ResourceObject` accepts `UTexture`, `UMaterialInterface`, or `ISlateTextureAtlasInterface` (`AllowedClasses` meta), explicitly **disallowing `UMediaTexture`**.

---

# 6. Navigation & focus

`UWidgetNavigation : UObject` (`Blueprint/WidgetNavigation.h:442`) has six directions — `Up, Down, Left, Right, Next, Previous` — each an `FWidgetNavigationData{EUINavigationRule Rule (default Escape), FName WidgetToFocus, TWeakObjectPtr<UWidget> Widget, FCustomWidgetNavigationDelegate CustomDelegate}`. UE5 adds `EWidgetNavigationRoutingPolicy RoutingPolicy` and `TInstancedStruct<FNavigationMethod> NavigationMethod` (pluggable navigation algorithm). Setter API on `UWidget`: `SetAllNavigationRules`, `SetNavigationRule`, `SetNavigationRuleBase`, `SetNavigationRuleExplicit`, `SetNavigationRuleCustom`, `SetNavigationRuleCustomBoundary`.

`UUserWidget` adds `FWidgetChild DesiredFocusWidget` (`{FName WidgetName}`) + `SetDesiredFocusWidget()` — declarative initial focus.
Input mode helpers in `UWidgetBlueprintLibrary`: `SetInputMode_UIOnlyEx`, `SetInputMode_GameAndUIEx`, `SetInputMode_GameOnly`, `SetFocusToGameViewport`.
Focus rendering is project-wide: `UUserInterfaceSettings::RenderFocusRule` = `Always | NonPointer | NavigationOnly | Never`.

---

# 7. Designer tooling (from `Editor/UMGEditor/Private/Designer`)

Files present: `SDesignerView.cpp`, `SDesignSurface.cpp`, `SZoomPan.cpp`, `SRuler.cpp/h`, `STransformHandle.cpp/h`, `SDesignerToolBar.cpp`, `SDisappearingBar.cpp`, `SPaintSurface.h`, `DesignerCommands.cpp`, `DesignTimeUtils.cpp`.

**Shortcuts** (verbatim from `DesignerCommands.cpp`):
| Command | Key | Description |
|---|---|---|
| `LayoutTransform` | **W** | "Layout Transform Mode — adjust widget layout transform" |
| `RenderTransform` | **E** | "Render Transform Mode — adjust widget render transform" |
| `ToggleOutlines` | **G** | "Show Outlines — enables or disables showing the dashed outlines" |
| `ToggleRespectLocks` | **L** | "Respect Locks … locked widgets prevent being selected in the designer" |
| `LocationGridSnap` | — | grid snapping while dragging |
| `RotationGridSnap` | — | rotation-grid snapping |
| `ToggleLocalizationPreview` | — | "localization preview for the current preview language (see Editor Settings → Region & Language)" |

**Toolbar** (from `SDesignerView.cpp` LOCTEXT keys): *Zoom To Fit*; *Switch between Landscape and Portrait* (`SwapAspectRatio`); ***Flip the current safe zones*** (`HandleFlipSafeZonesClicked`); **Screen Size** dropdown ("Change the size of the designer canvas to the screen resolution", `GetResolutionsMenu`); **Screen Fill** dropdown ("Adjust how the widget content fills the selected Screen Size"); **custom Width/Height** spinners, where the tooltip states `1+ = sets the size; 0 = match the desired size of the widget`; live **resolution text**, **current safe-zone text**, and **current DPI scale** readout with a **DPI Settings** button ("Configure the UI Scale Curve to control how the UI is scaled on different resolutions", icon `UMGEditor.DPISettings`). Two `SRuler`s (top and side). The preview is wrapped in an `SDPIScaler` driven by `GetPreviewDPIScale`. Resolutions come from `Settings->DebugResolutions` / `DesignerSettings->DefaultPreviewResolution`, persisted in `GEditorPerProjectIni`. `FCoreDelegates::OnSafeFrameChangedEvent` drives `SwapSafeZoneTypes`.

**Per-user-widget designer settings** (`UserWidget.h:316-319`): `FVector2D DesignTimeSize`, `EDesignPreviewSizeMode DesignSizeMode`, `FText PaletteCategory` (where it appears in the Palette), `UTexture2D* PreviewBackground`. `UWidgetBlueprint` adds thumbnail control (`EThumbnailPreviewSizeMode ThumbnailSizeMode`, `ThumbnailImage`) and, notably, **asset-registry-searchable perf metadata**: `TickFrequency`, `EWidgetCompileTimeTickPrediction TickPrediction`, `TickPredictionReason`, `int32 PropertyBindings` — i.e. the editor statically predicts and records tick cost and counts legacy bindings per widget asset.

**DPI** (`Engine/Classes/Engine/UserInterfaceSettings.h`): `EUIScalingRule = ShortestSide | LongestSide | Horizontal | Vertical | ScaleToFit | Custom`; `FRuntimeFloatCurve UIScaleCurve` (displayed as "DPI Curve", X="Resolution", Y="Scale"); `float ApplicationScale`; `CustomScalingRuleClass` (a `UDPICustomScalingRule`); `FIntPoint DesignScreenSize = (1920,1080)` used only by `ScaleToFit`; `bAllowHighDPIInGameMode`. Also `EFontDPI = Standard (72 DPI, default) | Unreal (96 DPI)` with `CustomFontDPI`/`bUseCustomFontDPI` and `bEnableDistanceFieldFontRasterization`. Hardware cursors are configured here too (`FHardwareCursorReference` with the documented `.ani → .cur → .png` / `@1.25x @1.5x @2x` fallback chain), plus `SoftwareCursors` mapping a cursor type to a `UUserWidget`. And: **`bAuthorizeAutomaticWidgetVariableCreation`** — the project-level switch behind "Is Variable".

**Anchors** (`Runtime/Slate/Public/Widgets/Layout/Anchors.h`): `FAnchors{FVector2D Minimum; FVector2D Maximum;}` with `IsStretchedHorizontal()`/`IsStretchedVertical()` — that pair of predicates is exactly what makes the Details panel swap Position/Size for Offsets. `FAnchorData{FMargin Offsets; FAnchors Anchors; FVector2D Alignment;}` (`CanvasPanelSlot.h:133`). The same struct is reused for viewport-level placement: `FGameViewportWidgetSlot{Anchors, Offsets, Alignment, ZOrder, bAutoRemoveOnWorldRemoved}` via `UGameViewportSubsystem::AddWidget/SetWidgetSlot`.

---

# 8. Animation

`UWidgetAnimation : UMovieSceneSequence` (`Animation/WidgetAnimation.h:81`) — it *is* a Sequencer sequence, holding a `UMovieScene* MovieScene` plus `TArray<FWidgetAnimationBinding> AnimationBindings` where each binding is `{FName WidgetName, FName SlotWidgetName, FGuid AnimationGuid, bool bIsRootWidget, FMovieSceneDynamicBinding DynamicBinding}` — note it can bind a **widget's slot**, not just the widget, which is how layout properties get animated.

UMG-specific track types: `UMovieScene2DTransformTrack`/`Section` (channels `Translation[2]`, `Rotation`, `Scale[2]`, `Shear[2]` with an `FMovieScene2DTransformMask`), `UMovieSceneMarginTrack`/`Section` (`TopCurve/LeftCurve/RightCurve/BottomCurve` — margins/padding are keyable), and **`UMovieSceneWidgetMaterialTrack`** with `TArray<FName> BrushPropertyNamePath` — **yes, widget material parameters are animatable**, resolved through `WidgetMaterialTrackUtilities.h` + `MovieSceneWidgetMaterialSystem`.

Playback on `UUserWidget`: `PlayAnimation/PlayAnimationTimeRange/PlayAnimationForward/PlayAnimationReverse` (all returning `FWidgetAnimationHandle`), `StopAnimation/StopAllAnimations/PauseAnimation`, `GetAnimationCurrentTime/SetAnimationCurrentTime`, `IsAnimationPlaying/IsAnyAnimationPlaying/IsAnimationPlayingForward`, `SetNumLoopsToPlay`, `SetPlaybackSpeed`, `ReverseAnimation`, `FlushAnimations`. A **queued** variant of each exists (`QueuePlayAnimation…`, `FQueuedWidgetAnimationTransition`) for sequencing transitions. Events: `EWidgetAnimationEvent` + `BindToAnimationStarted/Finished/BindToAnimationEvent` with an `FName UserTag` filter; `FBlueprintWidgetAnimationDelegateBinding` wires these at compile time. Ticking is centralised in `UUMGSequenceTickManager` (one linker for all widgets) with `UUMGSequencePlayer : IMovieScenePlayer`.

---

# 9. Debugging — exact console command names (read from `SlateCore/Private/Debugging/*.cpp`, not from docs)

- **Master**: `SlateDebugger.Start`, `SlateDebugger.Stop`
- **Events** (`ConsoleSlateDebugger.cpp`): `SlateDebugger.Event.Start/.Stop`, `.LogInputEvent`, `.LogFocusEvent`, `.LogAttemptNavigationEvent`, `.LogExecuteNavigationEvent`, `.LogCaptureStateChangeEvent`, `.LogCursorChangeEvent`, `.LogWarning`, `.CaptureStack`, `.SetInputFilter`, `.SetFocusFilter`, `.EnableAllInputFilters`, `.DisableAllInputFilters`, `.EnableAllFocusFilters`, `.DisableAllFocusFilters`, `.InputRoutingModeEnabled`
- **Invalidation**: `SlateDebugger.Invalidate`, `.Enabled`, `.Start/.Stop`, `.SetInvalidateWidgetReasonFilter`, `.SetInvalidateRootReasonFilter`, `.bLogInvalidatedWidget`, `.bShowLegend`, `.bShowWidgetList`, `.bUsePerformanceThreshold`, `.ThresholdPerformanceMS`
- **Paint**: `SlateDebugger.Paint.Start/.Stop/.Enable`, `.DrawBorder`, `.DrawFill`, `.LogOnce`, **`.LogWarningIfWidgetIsPaintedMoreThanOnce`**, `.EnableWidgetNameList`, `.ToggleWidgetNameList`, `.MaxNumberOfWidgetDisplayedInList`, `.OnlyGameWindow`, `.OnlyInvalidationRoot`, `.OnlyProjectContent`
- **Prepass**: `SlateDebugger.Prepass.Enable/.DrawBorder/.DrawFill/.EnableWidgetNameList/.OnlyInvalidationRoot`
- **Update**: `SlateDebugger.Update.Start/.Stop/.Enable`, `.SetWidgetUpdateFlagsFilter`, `.SetInvalidationRootIdFilter`, `.ToggleLegend`, `.ToggleWidgetNameList`, `.ToggleUpdateFromPaint`, `.OnlyGameWindow`, `.OnlyProjectContent`
- **InvalidationRoot**: `SlateDebugger.InvalidationRoot.Start/.Stop/.Enable/.ToggleLegend/.ToggleWidgetNameList`
- **Breakpoints**: `SlateDebugger.Break.OnWidgetBeginPaint`, `.OnWidgetEndPaint`, `.OnWidgetInvalidation`, `.RemoveAll`

**`Slate.*` cvars surfaced in the Widget Reflector's own options panel** (`Developer/SlateReflector/Private/Widgets/SSlateOptions.cpp`): `Slate.EnableTooltips` (note spelling — **not** `EnableToolTips`), `Slate.ShowClipping`, `Slate.DebugCulling`, `Slate.ShowBatching`, `Slate.ShowOverdraw`, `Slate.HitTestGridDebugging`, `Slate.EnableGlobalInvalidation`, `Slate.EnableInvalidationPanels`, `Slate.EnableFastWidgetPath`, `Slate.EnableDesignerRetainedRendering`, `Slate.EnableFocusOnPick`, `Slate.ApplyDisabledEffectOnWidgets`, `Slate.EnsureAllVisibleWidgetsPaint`, `Slate.EnsureOutgoingLayerId`, `Slate.VerifyParentChildrenRelationship`, `Slate.VerifyWidgetLayerId`, and eleven `Slate.InvalidationRoot.Verify*` checks (`VerifyWidgetList`, `VerifyWidgetsIndex`, `VerifyValidWidgets`, `VerifyHittestGrid`, `VerifyWidgetVisibility`, `VerifyWidgetVolatile`, `VerifyWidgetUpdateList`, `VerifyWidgetsAreUpdatedOnce`, `VerifyCachedElementDataList`, `VerifySlateAttribute`).

Tracing: `SlateCore/Private/Trace/SlateTrace.cpp` (Unreal Insights channel). Reflector module is `Engine/Source/Developer/SlateReflector`.

---

# 10. Styling primitives

`FSlateBrush` (`SlateCore/Public/Styling/SlateBrush.h`): `ResourceObject` (Texture/Material/atlas), `ImageSize`, `FMargin Margin` (UV-space), `FSlateColor TintColor`, `ESlateBrushDrawType DrawAs = NoDrawType | Box | Border | Image | **RoundedBox**`, `ESlateBrushTileType Tiling = NoTile|Horizontal|Vertical|Both`, `ESlateBrushMirrorType Mirroring`, `ESlateBrushImageType ImageType = NoImage|FullColor|Linear|**Vector**`, `FSlateBrushOutlineSettings OutlineSettings` (`FVector4 CornerRadii`, `FSlateColor Color`, `float Width`, `ESlateBrushRoundingType RoundingType = FixedRadius|HalfHeightRadius`, `bUseBrushTransparency`), `UVRegion`, `ResourceName`. **Rounded rectangles and outlines are first-class in the brush — no material needed.**

Per-widget style structs used in UMG: `FButtonStyle`, `FCheckBoxStyle`, `FSliderStyle`, `FSpinBoxStyle`, `FProgressBarStyle`, `FScrollBarStyle`, `FScrollBoxStyle`, `FComboBoxStyle`, `FTableRowStyle`, `FTableViewStyle`, `FEditableTextStyle`, `FEditableTextBoxStyle`, `FTextBlockStyle`, `FExpandableAreaStyle`.

---

# 11. Corrections to the widget list you gave me from memory

- **`RadialBox` does not exist** as a widget (see §2.2).
- Widgets you didn't list that do exist: **`UStackBox`** (+slot), **`UDynamicEntryBox`/`UDynamicEntryBoxBase`**, **`UComboBoxKey`**, **`UComboBox`**, **`UWindowTitleBarArea`** (+slot), **`UPostBufferUpdate`** + `UPostBufferBlurUpdater`, **`UMouseHoverComponent` / `UScaleBoxComponent` / `USizeBoxComponent`** (the new `UUIComponent` family), `URichTextBlockDecorator`/`URichTextBlockImageDecorator`, `UListViewBase`, `UTextLayoutWidget`.
- Every one of your remembered names otherwise checks out.

---

# 12. What I could NOT confirm in this session

I hit the GitHub API rate limit near the end, so these are **unverified** and should not be quoted as fact:

- **Accessibility platform coverage.** Confirmed: the `WITH_ACCESSIBILITY` define is set from `Rules.bCompileWithAccessibilitySupport && !bIsBuildingConsoleApplication` (`UnrealBuildTool/Configuration/UEBuildTarget.cs:6429`), and the full interface set exists (`IAccessibleWidget`, `IAccessibleWindow`, `IAccessibleText`, `IAccessibleTable`, `IAccessibleTableRow`, `IAccessibleActivatable`, `IAccessibleProperty`, `FGenericAccessibleUser`/`Registry`/`MessageHandler`, `EAccessibleWidgetType`, `EAccessibleEvent`) in `ApplicationCore/Public/GenericPlatform/Accessibility/GenericAccessibleInterfaces.h`. **Not confirmed: which platforms actually implement the backend** (I could not enumerate the per-platform `bCompileWithAccessibilitySupport` settings).
- **`EStretch` / `EStretchDirection` enumerator lists** — they live in SlateCore, which I did not fetch.
- **`ECommonInputType` / `ECommonInputMode` enumerator lists** — they live in the separate `CommonInput` module (`CommonInputModeTypes.h`), not the CommonUI headers I pulled. `CommonUI/Public/Input/CommonInputMode.h` is now just a deprecation stub redirecting there (deprecated 5.1).
- **Widget Reflector UI specifics** (Pick-Painted-Widget shortcut, snapshot format, hit-test grid view) — I have the module path (`Developer/SlateReflector`) and its cvar panel, but not the interaction details.
- **Localization pipeline** (Localization Dashboard, String Tables, culture switching) — not investigated; only the widget-level hooks (`FText` everywhere, `ETextFlowDirection`, `EFlowDirectionPreference`, `bFlipForRightToLeftFlowDirection` on `UImage`/`UBorder`, and the designer's localization-preview toggle) are confirmed.

Five web-research agents covering documentation URLs for the designer, styling/animation/localization/accessibility, debugging tools, runtime, and architecture are **still running**; their results had not arrived when I finished. If you want the doc-page citations to sit alongside this source-derived inventory, ping me and I'll fold them in when they land.

**Working files** (headers + digests, if you want to grep them yourself): `/private/tmp/claude-501/-Users-daniilsavcenko-Desktop-Programming-C---DesertEngine/9c96c319-6c5b-40e0-8d5c-21d3a87b40e0/scratchpad/` — `umg/Components/` (84 headers), `umg/Blueprint/`, `umg/Animation/`, `commonui/`, plus `digest_components.txt`, `digest_umg_other.txt`, `digest_commonui.txt`.