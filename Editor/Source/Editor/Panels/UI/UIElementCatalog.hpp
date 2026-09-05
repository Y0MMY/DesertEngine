#pragma once

#include <Editor/Core/IconsMaterialDesignIcons.hpp>

#include <cstddef>

// THE list of UI element types the editor can place on a canvas.
//
// It exists because there were two of them. The UI Editor panel's toolbar and the viewport's "UI" create
// menu were written independently and drifted: the viewport offered UI Image and the panel did not, so
// which elements a scene could contain depended on which window the author happened to be in. Both are
// generated from this macro now, so the two menus cannot disagree again — adding an element is one line
// here and it appears in both.
//
// WHAT BELONGS IN THE LIST. An entry is an ELEMENT: something that exists as a child entity of the canvas
// and means something on its own — a visual (Panel, Text, Button, Image, and the controls) or a container
// that positions its children (Layout Group, Scroll View). Deliberately NOT entries:
//
//   * UICanvasComponent      - the root itself, created by its own "Create UI Canvas" action.
//   * UILayoutComponent      - the rect; every element gets one automatically (see UIElementFactory).
//   * UIIconComponent        - draws NOTHING until an .svg asset is bound to it, so a menu that offered it
//                              would hand the author an invisible entity. Add it in Details, with the asset.
//   * UIScreen/UIScreenStack - the screen machine. A screen with no name is skipped by the renderer and a
//                              stack belongs on the canvas, not on a child; both are Details-side work.
//   * UIAnim, UIBinding, UITween, UIDraggable, UIDropTarget, UIPointerEvents
//                            - MODIFIERS. They are added in Details to an element that already exists and
//                              draw nothing by themselves.
//
// That partition is not a comment that can rot: the `UIElementCensus` suite enumerates the renderer's own
// dispatch and requires every UI component it handles to be either an entry below or one of the exclusions
// above, with the reason. A twenty-third component fails that test until somebody decides which it is.
//
// X( ComponentType, EntityName, Icon, MenuLabel )
//   ComponentType - the ECS component, in namespace Desert::ECS, added alongside a UILayoutComponent.
//   EntityName    - the scene-graph name the new entity gets.
//   Icon          - the toolbar/menu glyph.
//   MenuLabel     - the human label. The panel's toolbar prefixes it with "+ ".
#define DESERT_UI_ELEMENT_LIST( X )                                                                          \
    X( UIPanelComponent, "UI Panel", ICON_MDI_CARD_OUTLINE, "Panel" )                                        \
    X( UITextComponent2D, "UI Text", ICON_MDI_FORMAT_TEXT, "Text" )                                          \
    X( UIButtonComponent, "UI Button", ICON_MDI_BUTTON_POINTER, "Button" )                                   \
    X( UIImageComponent, "UI Image", ICON_MDI_IMAGE, "Image" )                                               \
    X( UILayoutGroupComponent, "UI Layout Group", ICON_MDI_VIEW_GRID, "Layout Group" )                        \
    X( UIProgressBarComponent, "UI Progress Bar", ICON_MDI_PROGRESS_HELPER, "Progress Bar" )                  \
    X( UIToggleComponent, "UI Toggle", ICON_MDI_CHECKBOX_MARKED_OUTLINE, "Toggle" )                           \
    X( UISliderComponent, "UI Slider", ICON_MDI_TUNE_VARIANT, "Slider" )                                     \
    X( UIScrollViewComponent, "UI Scroll View", ICON_MDI_VIEW_LIST, "Scroll View" )                           \
    X( UIInputFieldComponent, "UI Input Field", ICON_MDI_FORM_TEXTBOX, "Input Field" )                        \
    X( UIDropdownComponent, "UI Dropdown", ICON_MDI_MENU_DOWN, "Dropdown" )

namespace Desert::Editor
{
    // One row of the list above, as data. Deliberately free of ECS and of the renderer so the census test
    // can include this header on its own (EditorLayer.cpp and the panels are compiled by no suite).
    struct UIElementEntry
    {
        const char* ComponentType; // "UIPanelComponent" — matched against the renderer's own dispatch
        const char* EntityName;    // "UI Panel"
        const char* Icon;          // ICON_MDI_*
        const char* Label;         // "Panel"
    };

#define DESERT_UI_ELEMENT_ENTRY( Type, EntityName, Icon, Label ) UIElementEntry{ #Type, EntityName, Icon, Label },

    inline constexpr UIElementEntry kUIElements[] = { DESERT_UI_ELEMENT_LIST( DESERT_UI_ELEMENT_ENTRY ) };

#undef DESERT_UI_ELEMENT_ENTRY

    inline constexpr std::size_t kUIElementCount = sizeof( kUIElements ) / sizeof( kUIElements[0] );
} // namespace Desert::Editor
