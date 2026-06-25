// Reflected component editors: the full Details UI is auto-built from each data block's REFLECT()
// metadata (PropertyEditorBuilder) — no widget class, no edit to ComponentEditor. To expose a new
// reflected component in the editor, copy one line below.

#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>

#include <Engine/ECS/Components.hpp>

DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::DirectionLightComponent, Data, "DirectionalLightData",
                                     "Directional Light" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::PointLightComponent, Data, "PointLightData", "Point Light" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::SpotLightComponent, Data, "SpotLightData", "Spot Light" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::CameraComponent, Data, "CameraData", "Camera" )
