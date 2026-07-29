# Desert Engine — Lua Scripting Reference

Game behavior lives in hot-reloadable `.lua` files instead of compiled C++. Attach a script to any
entity via **Details → Add Component → Script** (an entity may run several script *slots*, each an
independent sandbox). Scripts run in **Play** mode only and hot-reload on save (~0.7 s poll) — edit
while playing.

Example scripts: `Editor/Resources/Scripts/Examples/`.

## Script lifecycle

```lua
Properties = {          -- editor-exposed values (number / bool / string)
    Speed = 2.0,        -- shown in the Details panel; edits apply LIVE while playing
}

function OnStart()      -- once, when Play starts (and again after a hot-reload)
end

function OnUpdate(dt)   -- every frame; dt in seconds
end
```

`self` is the entity the script is attached to (an `Entity` handle, see below). Runtime errors go
to the **Logs** panel; an `OnUpdate` error is reported once (not 60×/sec) until it changes or the
script reloads. A load error never kills the session.

## Entity

The same handle type everywhere: `self`, `World.find(...)`, raycast hits, spawn results.

### Identity / lifetime

| Call | Meaning |
| --- | --- |
| `e:valid()` | `false` for dead/missing entities — check results of `World.find` etc. |
| `e:name()` | The entity's tag name. |
| `e:destroy()` | Removes the entity (and its subtree) from the scene. |
| `e:call("Fn", ...)` | Cross-entity message: invokes `Fn(...)` in **every** script slot of `e` that defines it. Convention: pass `self` as the first arg. |

### Transform

| Call | Meaning |
| --- | --- |
| `e:getPosition()` → `x, y, z` | World translation. |
| `e:setPosition(x, y, z)` | |
| `e:translate(dx, dy, dz)` | Relative move. |
| `e:getRotation()` → `x, y, z` | Euler radians. |
| `e:setRotation(x, y, z)` | |
| `e:getScale()` → `x, y, z` / `e:setScale(x, y, z)` | |
| `e:forward()` → `x, y, z` | World-space facing direction (−Z rotated by the entity's rotation). |
| `e:right()` → `x, y, z` | World-space right (+X rotated). |
| `e:distanceTo(other)` | Distance between the two entities' positions (−1 if either has no transform). |

```lua
-- Move 3 units in the facing direction:
local fx, fy, fz = self:forward()
self:translate(fx * 3, fy * 3, fz * 3)
```

### Components (reflection-driven)

Any `PROPERTY()`-reflected component is scriptable automatically — same metadata that powers the
Details panel. Registered names: `Camera`, `DirectionLight`, `PointLight`, `SpotLight`, `Terrain`,
`Collider`, `RigidBody`, `CharacterController`.

| Call | Meaning |
| --- | --- |
| `e:component("PointLight")` | Proxy over the component's data, or `nil` if absent. Fields read/write by their C++ names; vec fields are `{x=, y=, z=}` tables (also accepts `{r=, g=, b=}` or array style). |
| `e:hasComponent("PointLight")` | |
| `e:addComponent("PointLight")` | Adds it (no-op if present) and returns the proxy. |
| `e:removeComponent("PointLight")` | No-op if absent. |

```lua
local light = self:addComponent("PointLight")
light.Intensity = 5.0
light.Color     = { x = 1.0, y = 0.6, z = 0.2 }
```

### Materials (unified protocol)

Params are addressed by **shader-schema name** — the names declared in the `.shader` file
(`AlbedoColor`, `RoughnessFactor`, … for PBR; `Color` for Unlit; anything for your own shader).

| Call | Meaning |
| --- | --- |
| `e:setMaterialParam(name, x [, y, z, w])` | `w` defaults to 1 (colours read naturally). |
| `e:getMaterialParam(name)` → `x, y, z, w` | |
| `e:setShader(name)` | Assign a surface shader by name; `""` returns to the PBR slot materials. |
| `e:getShader()` | |

### Character controller (needs `CharacterController` component; otherwise no-ops)

The engine only executes physics (the Jolt capsule); *all* control policy — WASD→move, mouse→look,
water→swim, sprint — lives in the script. See the complete reference controller in
`Examples/PlayerController.lua`. Capsule + `Gravity` (fall accel, m/s²) are authored on the component;
control *feel* (speeds, jump height, look sensitivity) is the script's `Properties`.

| Call | Meaning |
| --- | --- |
| `e:move(forward, right, speed)` | Axes −1..1, speed m/s. |
| `e:jump(strength)` | |
| `e:isOnGround()` | |
| `e:addYaw(radians)` | Turns the whole entity. |
| `e:addCameraPitch(radians)` | Tilts the child camera only (clamped ±85°). |
| `e:setSwimming(on)` / `e:swim(vertical)` / `e:isSwimming()` | Buoyancy; `vertical` is +1 up / −1 down. Detect the water crossing yourself against a `World.get("waterLevel")` blackboard value — no engine hardcode. |

### Attachment

| Call | Meaning |
| --- | --- |
| `e:attachTo(target, "bone_name")` | Socket-attach to a bone of `target` (e.g. `weapon:attachTo(player, "hand_r")`). |
| `e:detach()` | |

## Input

| Call | Meaning |
| --- | --- |
| `Input.isKeyDown("W")` | Held state. |
| `Input.wasPressed("Space")` | `true` only on the frame the key goes down. |
| `Input.mouseDelta()` → `dx, dy` | Per-frame mouse movement (cursor-capture aware). |
| `Input.isMouseDown("left")` | `"left"` / `"right"` / `"middle"`. |
| `Input.lockCursor()` / `Input.showCursor()` | Capture / free the cursor (cooperates with the Escape toggle). |

Key names: any single letter `"A"`–`"Z"` (case-insensitive), any digit `"0"`–`"9"`, plus `"Space"`,
`"Shift"`, `"Ctrl"`, `"Alt"`, `"Tab"`, `"Enter"`, `"Escape"`, `"Left"`, `"Right"`, `"Up"`, `"Down"`.
Note: **Escape** also toggles the editor cursor capture while playing.

## Timer

| Call | Meaning |
| --- | --- |
| `Timer.after(seconds, fn)` | Runs `fn` once after `seconds` of game time. |

Timers belong to the script slot that scheduled them: destroying the entity or hot-reloading the
script cancels its pending timers. A repeating timer is a callback that re-arms itself:

```lua
local function beep()
    Log.info("beep")
    Timer.after(1.0, beep)   -- re-arm: fires every second
end

function OnStart()
    Timer.after(1.0, beep)
end
```

## World

| Call | Meaning |
| --- | --- |
| `World.find("Name")` | First entity with that tag name (check `:valid()`). |
| `World.raycast(ox, oy, oz, dx, dy, dz [, maxDist])` | → table `{ hit, entity, x, y, z, nx, ny, nz, dist }`. |
| `World.cameraRay()` → `ox, oy, oz, dx, dy, dz` | Active camera eye + forward — the natural "what am I looking at" ray. |
| `World.spawn(prefabPath, x, y, z)` | Instantiate a prefab; returns the root entity. |
| `World.spawnMarker(x, y, z, scale, r, g, b)` | Small solid-colour Unlit sphere (impact marker); `:destroy()` it later. |
| `World.set(key, value)` / `World.get(key)` / `World.has(key)` | Per-scene variable store ("blackboard") — define your OWN dynamic variables (water level, quest flags, scores, ...) with no engine hardcode. Values are any Lua value. |

```lua
-- Interact with what the camera looks at:
local ox, oy, oz, dx, dy, dz = World.cameraRay()
local hit = World.raycast(ox, oy, oz, dx, dy, dz, 3.0)
if hit.hit then
    hit.entity:call("OnInteract", self)
end
```

## Logging

`log(msg)` or leveled `Log.info(msg)` / `Log.warn(msg)` / `Log.error(msg)` — all land in the
editor's Logs panel.

## Properties

The top-level `Properties` table is the script's editor-facing schema: the Details panel shows each
entry with a typed editor (number / bool / string) and per-entity overrides. Overrides are
re-applied **every frame**, so editing a value in Details changes the running script live.

## Adding engine API (C++ side)

The scripting core (`Engine/Scripting/ScriptEngine.cpp`) owns the VM and knows nothing about
gameplay domains. Each domain registers its own API from its own translation unit by extending the
Lua state — see the architecture note in `Engine/Scripting/Internal/ScriptRuntime.hpp`. Adding a
domain = one `Register*Bindings.cpp` file + one call in the `ScriptEngine` constructor. Reflected
components need **one line** in `kReflectedComponents` (`ReflectionBindings.cpp`); their fields are
scriptable automatically.
