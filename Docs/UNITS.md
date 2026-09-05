# World units — 1 unit = 1 centimetre

Desert uses the Unreal convention: **one world unit is one centimetre**, everywhere. There is no
conversion layer and no "which unit is this number in?" — components, meshes, scene files, physics,
shaders and the editor UI all speak the same units.

| Quantity | Value |
| --- | --- |
| 1 m | `100` units |
| 1 km | `100000` units |
| Gravity | `-981` units/s² |
| Default primitive (Cube/Sphere/Plane) | `100` units — one metre |
| Default CubeGrid block | `100` units |
| Character capsule | radius `30`, height `180` |
| Camera near / far | `10` / `100000` |

Helpers live in `Desert/Common/Source/Common/Core/Units.hpp`:

```cpp
float far  = Common::Units::Metres( 1000.0f ); // 100000 units
float step = Common::Units::Cm( 25.0f );       // 25 units
Common::Units::FormatLength( buf, sizeof( buf ), 450.0f ); // "4.5 m"
```

Use `Metres()` when a number reads better in metres (demo scenes, character rigs) instead of writing
the multiplied literal — the intent stays visible in the code.

## Marking a length in the editor

Annotate reflected length fields so the Details panel labels and drags them sensibly (a 1 cm step
instead of the 0.01 that suits unitless ratios):

```cpp
PROPERTY( DisplayName( "Radius" ), Category( "Light" ), Range( 0.0f, 10000.0f ), Length )
float Radius = 1000.0f;
```

`Length` is parsed by DesertHeaderTool into `PropertyMetadata::IsLength`; `Range(min,max)` is written
in world units like everything else.

## Old scenes

Scenes authored before the switch (world unit = 1 m) carry no `UnitVersion` field. `SceneSerializer`
detects that and upgrades them on load (`MigrateMetresToUnits`): positions ×100, plus every length a
component owns (light radius/range, camera near/far, collider extents, character capsule, terrain
size/height, 3D text size, scene gravity). `Scale` is multiplied only for **file-backed** meshes — a
procedural primitive's geometry is regenerated at the new size by the factory, so scaling it too would
compound. Re-saving the scene stamps `UnitVersion: 1` and the migration never runs again.
