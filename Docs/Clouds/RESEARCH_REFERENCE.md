# Nubis3 Vulkan Cloud Renderer — Research Reference

**Subject of study (read-only):** `/Users/daniilsavcenko/Desktop/Programming/C++/myptoject`
**Purpose:** source material for a from-scratch volumetric cloud + procedural sky system in DesertEngine.
**Date of analysis:** 2026-08-10.

### Conventions used in this document

* **[CODE]** — verified by reading the source; file path and line numbers given.
* **[README]** — claimed by `README.md`; *not* necessarily what the code does.
* **[DISAGREES]** — the code and the README contradict each other.
* **[BUG]** — I believe this is a defect. Stated with the reasoning so you can re-check.
* **"not specified in source"** — I looked and the number is genuinely absent. I have invented nothing.

Line numbers refer to the files as they exist in the target repo today.

---

## A. Executive summary

1. This is a **university course project** (UPenn CIS 565, authors Janet Wang, Xinyu Niu, Yue Zhang) that reimplements Guerrilla Games' *Nubis3* (SIGGRAPH 2023 Advances) cloud renderer in raw Vulkan (`README.md:1-11`).
2. It renders **one hero cloud** (chosen from two pre-authored VDB assets), **not** a cloudscape. The cloud lives in a fixed axis-aligned **box** of 2048 × 2048 × 256 world units (`computeNubisCubed.comp:25-26`), not a shell or dome.
3. Cloud shape is **not procedural**: it is baked, offline, from Houdini/OpenVDB into a 512×512×64 RGBA8 3D texture whose channels are *Dimensional Profile / Detail Type / Density Scale / SDF* (`computeNubisCubed.comp:48-56`, `Renderer.cpp:221-222`).
4. Only the *detail erosion* is procedural-ish, and even that ships as a baked 128³ RGBA 3D noise texture (`Renderer.cpp:224`).
5. Runtime is **3 compute dispatches + 1 fullscreen graphics pass**: light-grid bake → near-cloud raymarch (half res) → far-cloud raymarch (full res, composites the near result) → tonemap/godray/vignette blit (`Renderer.cpp:366-404`, `Renderer.cpp:449-452`).
6. Lighting is a **256×256×32 "light voxel grid"** pre-integrated toward the sun each frame, plus a 2-step local march, plus Nubis2-style Henyey-Greenstein / Beer-Powder terms (`lightGrid.comp`, `computeNubisCubed.comp:287-376`).
7. Sky is a **Preetham analytic model** with a day/night cycle (30-second day!) and a procedural star field (`computeNubisCubed.comp:512-654`, `Scene.cpp:4-30`).
8. **Temporal reprojection does not exist at runtime.** `reproject.comp` is compiled but its reprojection maths is commented out and the shader is never instantiated (`reproject.comp:109-129`, `Renderer.cpp:193`). What the README calls "temporal upscaling" is in fact a **spatial** near/far resolution split with no history buffer and no jitter.
9. Claimed cost: 306.6 MiB VRAM, ~110 FPS at 500 m and ~60 FPS inside the cloud on an RTX 4070 Laptop (`README.md:233-239`).
10. Code quality is *prototype grade*: several dead constants, at least six substantive bugs listed in section J, no GPU synchronisation between passes, and a GPL-3 third-party VDB viewer vendored into an MIT-licensed repo.

---

## B. Pass-by-pass pipeline

### B.0 Frame flow (host side)

`Renderer::Frame()` (`Renderer.cpp:588-632`) each frame:

1. `vkQueueSubmit` the **pre-recorded compute command buffer** to the compute queue, with **no fence and no semaphores** (`Renderer.cpp:591-600`).
2. `swapChain->Acquire()`.
3. `RecordCommandBuffer(index)` — re-records the graphics command buffer *every frame* (it also builds the ImGui draw data inside the render pass) (`Renderer.cpp:412-524`).
4. Submit graphics, waiting only on the swapchain image-available semaphore (`Renderer.cpp:610-627`).
5. Present.
6. `UpdateUniformBuffers()` afterwards — time, prev-camera, pixel offset, UI buffer (`Renderer.cpp:581-586`, called at `main.cpp:157`).

The compute command buffer is recorded **once**, in the constructor (`Renderer.cpp:37`), with `VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT` (`Renderer.cpp:349`). Consequences:

* **[BUG]** There is **no barrier between the three compute dispatches** (`Renderer.cpp:366-404`). Light-grid writes, near-cloud writes and far-cloud reads are not ordered.
* **[BUG]** There is **no synchronisation at all between the compute queue and the graphics queue**. The tonemap pass samples `imageCurTexture` while the compute queue may still be writing it.

Both of these will show up as validation errors / flicker the moment you port the structure literally. Do not copy the frame flow; copy the passes.

### B.1 Pass 1 — Light Voxel Grid bake (compute)

| Property | Value | Source |
|---|---|---|
| Shader | `src/shaders/lightGrid.comp` | `ComputeLightGridShader.cpp:9` |
| Local size | `32 × 32 × 1` | `lightGrid.comp:9` |
| Dispatch | `(256+31)/32, (256+31)/32, 32` = **8 × 8 × 32 groups** | `Renderer.cpp:368-373` |
| Output | `lightGridTexture`, 3D storage image, **256 × 256 × 32**, `VK_FORMAT_R32G32B32A32_SFLOAT`, layout `GENERAL` | `Renderer.cpp:227`, `Image.cpp:522-541` |
| Inputs | `modelingParkourTexture`, `modelingStormBirdTexture` (set 1, bindings 0/1); `Time` UBO (set 2); `UIParam` UBO (set 3) | `lightGrid.comp:19-45`, `ComputeLightGridShader.cpp:18-23` |
| Writes | `.r` = density accumulated toward the sun; `.g` = local profile density (used as "low LOD density") | `lightGrid.comp:85,101` |

Algorithm (`lightGrid.comp:74-103`):

```
coord = gl_GlobalInvocationID.xyz                       // integer voxel index
density = profile(coord) * densityScale(coord)          // sampled from modeling NVDF
finalColor.g = density                                  // low-LOD density
if (density <= 0) { store; return }                     // empty-space early out
nextCoord = coord + sunDir                              // NOTE: sunDir is a WORLD-space unit vector
while (nextCoord inside 256x256x32) {
    density += profile(nextCoord) * densityScale(nextCoord)
    nextCoord += sunDir
}
finalColor.r = density
```

Notes and defects:

* The density is a **raw sum with no step length, no extinction coefficient and no normalisation** (`lightGrid.comp:97`). One light voxel is 8 world units on a side (2048/256 = 8, 256/32 = 8), but the sum does not multiply by 8. The accumulated value can reach the hundreds, which drives `exp(-density_to_sun)` to 0 in any thick cloud.
* **[BUG] Sign inversion.** The grid is indexed in the *unflipped* modeling-texture space (`lightGrid.comp:49`), but the raymarcher reads it through `GetSampleCoord()`, which does `coord = 1 - coord` (`computeNubisCubed.comp:167-175`). Therefore `d(texcoord)/d(world) = -1/extent`. Marching `+sunDir` in grid index space corresponds to marching **-sunDir in world space**, i.e. *away* from the sun. If you reimplement this, march toward the light in world space, then convert.
* `Time` and `UIParam` are bound but `deltaTime`/`animate_speed`/`tiling_freq` are unused here; only `sunPositionXYZ` and `cloud_type` matter.
* The grid is rebuilt **every frame** (it is in the static compute command buffer), even though the sun moves slowly. This is a trivially cachable pass.
* `X_SIZE`/`Z_SIZE` (`lightGrid.comp:6-7`) are `#define`s that must be kept in sync by hand with `Renderer.cpp:227` and `Renderer.cpp:368`. Three places, no shared header. **Should be tunable** and derived from one source.

### B.2 Pass 2 — Near-cloud raymarch (compute, half resolution)

| Property | Value | Source |
|---|---|---|
| Shader | `src/shaders/nearCloud.comp` | `ComputeNearShader.cpp:9` |
| Local size | `32 × 32` | `nearCloud.comp:9` (via `WORKGROUP_SIZE 32`) |
| Dispatch | `(W/2+31)/32, (H/2+31)/32, 1` → at 1920×1080 = **30 × 17 groups** | `Renderer.cpp:376-380` |
| Render resolution | **960 × 540** (half in each axis = quarter the pixels) | `Image.cpp:496-500` |
| Output 0 | `nearCloudColorTexture` — RGBA32F storage image, 960×540. Stores `vec4(cloudColor.rgb, 1)` | `nearCloud.comp:689`, `Renderer.cpp:230` |
| Output 1 | `nearCloudDensityTexture` — RGBA32F storage image, 960×540. Stores `vec4(density, transmittance, alpha, 0)` | `nearCloud.comp:690`, `Renderer.cpp:231` |
| Distance range | `[tmin_of_box, 500)` — hard `break` at `NEAR_THRESHOLD` | `nearCloud.comp:482-485`, `nearCloud.comp:9` |

Descriptor sets bound (`ComputeNearShader.cpp:57-71`):

| Set | Contents |
|---|---|
| 0 | storage image: near cloud **colour** |
| 1 | Camera UBO (view/proj/position), prev-camera UBO, camera-param UBO (halfTanFOV, aspectRatio, pixelOffset) |
| 2 | 3 × `sampler3D`: modeling Parkour NVDF, modeling Stormbird NVDF, detail noise |
| 3 | Time UBO (deltaTime, totalTime, sunPosition XYZ) |
| 4 | `sampler3D` light grid |
| 5 | UIParam UBO |
| 6 | storage image: near cloud **density** |

The near pass computes the full sky (`GetSkyColor`) too, but throws it away — only the cloud colour and the three accumulators are written. That is wasted work at half res, but harmless.

### B.3 Pass 3 — Far-cloud raymarch (compute, full resolution)

| Property | Value | Source |
|---|---|---|
| Shader | `src/shaders/farCloud.comp` | `ComputeFarShader.cpp:9` |
| Local size | `32 × 32` | `farCloud.comp:9` |
| Dispatch | `(W+31)/32, (H+31)/32, 1` → at 1920×1080 = **60 × 34 groups** | `Renderer.cpp:383-386` |
| Output | `imageCurTexture` — RGBA32F storage image at swapchain resolution, layout `GENERAL` | `Renderer.cpp:210`, `Image.cpp:472-494` |
| Distance range | `[max(tmin, 500), min(farclip, tmax)]` | `farCloud.comp:453-454`, `:456` |
| Extra inputs | set 6 = `sampler2D nearCloudColorTex`, set 7 = `sampler2D nearCloudDensityTex` | `farCloud.comp:91-92`, `ComputeFarShader.cpp:72-73` |

Composition (`farCloud.comp:679-688`):

```glsl
vec4 nearCloudColor   = texture(nearCloudColorTex, uv);
vec4 nearCloudDensity = texture(nearCloudDensityTex, uv);
ioPixelData.mDensity       = nearCloudDensity.a;   // <-- reads .a
ioPixelData.mTransmittance = nearCloudDensity.g;
ioPixelData.mAlpha         = nearCloudDensity.b;
ioPixelData.mCloudColor    = nearCloudColor.rgb;
```

**[BUG]** The near pass writes density into `.r` and a literal `0` into `.a` (`nearCloud.comp:690`). The far pass reads density from `.a`. The comment two lines above (`farCloud.comp:682`) even says *"r,g,b = density, transmittance, alpha"*. So **accumulated near-cloud density is always 0 in the far pass**. Transmittance and alpha do carry over, so the visual result is only subtly wrong (density is used for the star/sun-disk masking and the `bgColor` selection), but this is a real defect — do not copy it.

The far pass then continues the march from 500 units and does the final composite:

```glsl
vec3 cloudColor = mNight * mCloudColor * max(0, transmittance);
finalColor      = vec4(mix(bgColor, cloudColor, 1 - mAlpha), 1);
```

### B.4 Pass 4 — Tonemap / godray / vignette (graphics)

| Property | Value | Source |
|---|---|---|
| Vertex shader | `post.vert` — pure passthrough of a fullscreen quad at `z = 0.99` | `post.vert:16-20`, `Model.cpp:10-14` |
| Fragment shader | `tone.frag` | `PostShader.cpp:12-13`, `Renderer.cpp:192` |
| Geometry | 4-vertex, 6-index quad, `TRIANGLE_LIST`, back-face cull, CCW | `Model.cpp:10-19`, `PostShader.cpp:42,70-71` |
| Render pass | 1 colour attachment (swapchain format, `CLEAR`→`PRESENT_SRC_KHR`) + 1 depth attachment (`D32_SFLOAT` preferred) | `Renderer.cpp:64-128` |
| Depth | test ON, write ON, `LESS`. Pointless for a fullscreen quad but it is enabled. | `PostShader.cpp:88-96` |
| Blending | disabled | `PostShader.cpp:102` |
| Descriptor sets | 0 = `sampler2D texColor` (= `imageCurTexture`), 1 = Camera UBO, 2 = Time UBO, 3 = UIParam UBO | `PostShader.cpp:121-126,168-173` |
| Also in this pass | ImGui draw data (`ImGui_ImplVulkan_RenderDrawData`) | `Renderer.cpp:513` |

`tone.frag:92-108`:
1. sample the compute output;
2. optionally add screen-space god rays (section G);
3. Uncharted-2 filmic tonemap with `exposure = 0.7`, `invGamma = 1/2.2`, `whitepoint = 1.0`;
4. lerp toward `vec3(0.1, 0.05, 0.13)` by `dot(uv-0.5, uv-0.5)` — a purple vignette.

### B.5 Passes that exist in the tree but never run

| Pass | Why dead |
|---|---|
| `reproject.comp` / `ReprojectShader` | Never instantiated (`Renderer.cpp:193` commented out); dispatch commented out (`Renderer.cpp:357-363`); and the shader body itself is a straight copy (`reproject.comp:109-129`). |
| `computeNubisCubed.comp` / `ComputeNubisCubedShader` | Pipeline *is* created (`Renderer.cpp:195`) but the dispatch is commented out (`Renderer.cpp:388-395`). This is the **single-pass, full-resolution reference version** of the near+far shaders — the cleanest one to read. |
| `compute.comp` / `ComputeShader` (Nubis1/2 path) | Only reachable when `useNubisCubed == 0` (`Renderer.cpp:396-404`); that member is hard-coded to `1` (`Renderer.h:133`) and the radio buttons that would change it are commented out (`Renderer.cpp:467-469`). Also, its `RecordComputeCommandBuffer` is only called once, so toggling it at runtime would not even work. |

`compute.comp` still matters as **documentation of the Nubis1/2 algorithm** — see section C.7.

### B.6 Pipeline diagram (`img/pipe.png`)

The README's diagram is consistent with the code. It shows a yellow "Inputs" box containing *Mesh → Atlas Tool → Modeling Data*, *Noise Generator → 3D Noise*, and *Light Information*; then a blue chain **Light Voxel → {Near Cloud Raymarching, Far Cloud Raymarching} → Integrate Lighting into Pixel → Post Process → final image**. Near feeds into Far (matching `ComputeFarShader.cpp:72-73`). Note the diagram places "Integrate Lighting into Pixel" as a separate box; in the code that step is inlined in the far pass (`farCloud.comp:696-711`), there is no fourth compute pass.

---

## C. The raymarching algorithm in detail

All references in this section are to `computeNubisCubed.comp` unless noted; `nearCloud.comp` and `farCloud.comp` are byte-identical apart from the differences listed in B.2/B.3 (verified by `diff`).

### C.1 Ray setup

`GenerateRay()` (`:487-506`) builds the ray manually from the **view matrix basis vectors**, not from an inverse-projection:

```glsl
camLook  = normalize(vec3(view[0][2], view[1][2], view[2][2]));
camRight = normalize(vec3(view[0][0], view[1][0], view[2][0]));
camUp    = normalize(vec3(view[0][1], view[1][1], view[2][1]));
screenPoint = uv * 2 - 1;
refPoint = cameraPos - camLook;                                  // focal plane at distance 1
p = refPoint + aspect * sx * halfTanFOV * camRight
             -          sy * halfTanFOV * camUp;
ray.mOrigin    = cameraPos;
ray.mDirection = normalize(p - cameraPos);
```

`halfTanFOV = tan(radians(45/2))` and `aspectRatio = 1920/1080` (`Camera.cpp:41-42`). The `proj` matrix is uploaded but is only used by the god-ray pass. **There is no jitter** — the `// TODO: Jitter point` in `compute.comp:346` was never done, and `pixelOffset` (cycling 0..15, `Camera.cpp:150-153`) is dead in all live shaders.

### C.2 Cloud geometry — a BOX, not a shell

```glsl
#define VOXEL_BOUND_MIN vec3(-1024.0, -1024.0, -128.0)
#define VOXEL_BOUND_MAX vec3( 1024.0,  1024.0,  128.0)
```
(`:25-26`). Z is up (`GetHeightFractionForPoint` uses `inPosition.z`, `:281-285`; the sky's `UP` is `vec3(0,0,-1)`, `:529`).

`SetRaymarchLimit()` (`:382-444`) is a hand-written slab test: for each of the 6 planes it computes `t`, checks that the hit point lies inside the other two axes' ranges, and accumulates `tmin`/`tmax`. If the camera is already inside the box, `tmin = 0`.

**[BUG]** It uses `abs((bound - origin)/direction)` (`:393, :400, :410, ...`). Taking the absolute value discards the sign, so a plane *behind* the camera yields a positive `t`. When you are outside the box looking away from it, the marcher can still find a "hit". Use a signed slab test.

This is the **most important structural difference from a shippable cloud system.** Nubis1/2 (and every ship-title cloudscape) uses a *spherical shell*: `compute.comp:16-19` defines `ATMOSPHERE_RADIUS 1200000`, inner/outer shells and a thickness, then intersects both. Nubis3 as implemented here renders **one bounded cloud in a box**, which is why every screenshot shows a single hero cloud on an empty sky.

### C.3 The march loop

`RaymarchVoxelClouds()` (`:446-485`):

```glsl
SetRaymarchLimit(ray, raymarch_info);        // sets mDistance = tmin
cos_angle = dot(ray.mDirection, lightDir);

while (mTransmittance > uiParam.transmittance_limit &&
       mDistance < min(uiParam.farclip, mLimit.y))
{
    sample_position = origin + direction * mDistance;
    sample_coord    = GetSampleCoord(sample_position);      // 1 - normalized(world)

    if (sample_coord inside [0,1]^3) {
        modeling_data = GetVoxelCloudModelingData(sample_coord, 0.0);

        adaptive_step_size = max(1.0, max(sqrt(mDistance), EPSILON) * 0.08);
        mCloudDistance     = modeling_data.mSdf;             // SDF from the .a channel
        mStepSize          = max(mCloudDistance, adaptive_step_size);

        if (mCloudDistance < 0.0) {                          // inside the cloud
            samples = GetVoxelCloudDensitySamples(..., inHFDetails = true);
            if (samples.mProfile > 0) {
                mDensity += samples.mFull;
                IntegrateLightEnergy(...);
            }
        }
    }
    mDistance += mStepSize;
}
```

Three acceleration mechanisms, all present:

1. **Empty-space skipping via SDF.** `mStepSize = max(sdf, adaptive)` — if the SDF says the nearest surface is 300 units away, take a 300-unit step. This is what `img/step_size.png` illustrates (a camera on the right, large open circles for the long strides outside the cloud, tight red dots for the dense samples once inside, dashed lines toward the sun).
2. **Distance-adaptive step size.** `max(1.0, sqrt(d) * 0.08)`. Below `d ≈ 156` the step is pinned at 1.0 unit; at `d = 700` (the default farclip) it is ≈ 2.1 units. It grows as `sqrt`, i.e. *sub*-linearly — much gentler than the usual `d * k` cone-tracing rule.
3. **Transmittance early-out**, at `uiParam.transmittance_limit` (default 0.01).

**[BUG]** The loop condition tests `ioPixelData.mTransmittance > limit`, but `mTransmittance` is *accumulated upward* from 0 (`:373`: `mTransmittance += mFull * light_energy * mAlpha`). It is a scattered-light accumulator, **not** a transmittance. With `mTransmittance` starting at 1.0 (`:678`) and only ever increasing, the "early out" fires only if the very first test fails. The variable that actually behaves like transmittance is `mAlpha` (`:375`: `mAlpha *= exp(-mFull)`). If you port this, gate the loop on `mAlpha`.

**No jitter, no blue-noise offset, no cone tracing, no LOD/mip selection at runtime** (see C.5).

### C.4 Density sampling — the exact composition

**Step 1 — modeling data lookup.** `GetVoxelCloudModelingData()` (`:180-194`) samples one of two 512×512×64 RGBA textures based on `uiParam.cloud_type`:

| Channel | Meaning | Post-processing |
|---|---|---|
| R | Dimensional Profile | used raw, `[0,1]` |
| G | Detail Type (billow ↔ wispy blend) | used raw, `[0,1]` |
| B | Density Scale | used raw, `[0,1]` |
| A | Signed Distance Field | `ValueRemap(a, 0, 1, -256, 4096)` (`:191`) → world units, negative = inside |

`GetSampleCoord()` (`:167-175`): `s = 1 - (p - MIN)/(MAX - MIN)`. **The model is mirrored on all three axes** relative to world space. That is almost certainly an accident that got baked into the look; it also causes the light-grid sign issue in B.1.

**Step 2 — profile density.** `GetVoxelCloudDensitySamples()` (`:248-265`):

```glsl
if (dimensional_profile > 0) {
    mProfile = dimensional_profile * density_scale;
    mFull    = GetUprezzedVoxelCloudDensity(...)
             * ValueRemap(mDistance, 10.0, 120.0, 0.25, 1.0);   // distance fade-in
}
```
That last factor fades density from 25 % at 10 units to 100 % at 120 units — a cheap way of stopping the cloud from filling the screen when you fly through it.

**Step 3 — detail erosion.** `GetUprezzedVoxelCloudDensity()` (`:197-244`), the heart of the look:

```glsl
// 3a. wind offset (world-space translation of the noise lookup, not of the cloud)
inSamplePos -= vec3(0.1, 0.1, 0.0) * uiParam.animate_speed * time.totalTime;

// 3b. sample the 4-channel detail noise, tiled
vec4 noise = texture(cloudDetailNoiseTexture, inSamplePos * uiParam.tiling_freq);
//  R = Low  Freq "Curl-Alligator"   G = High Freq "Curl-Alligator"
//  B = Low  Freq "Alligator"        A = High Freq "Alligator"

// 3c. two erosion families, blended by Detail Type
wispy_noise           = mix(noise.r, noise.g, dimensional_profile);
billowy_type_gradient = pow(dimensional_profile, 0.25);
billowy_noise         = mix(noise.b * 0.3, noise.a * 0.3, billowy_type_gradient);
noise_composite       = mix(wispy_noise, billowy_noise, detail_type);

// 3d. optional "high-high frequency" layer, faded in by distance
if (inHFDetails) {
    hhf_wisps   = 1.0 - pow(abs(abs(noise.g * 2 - 1) * 2 - 1), 4.0);
    hhf_billows =       pow(abs(abs(noise.a * 2 - 1) * 2 - 1), 2.0);
    hhf_noise   = clamp(mix(hhf_wisps, hhf_billows, detail_type), 0, 1);
    blend       = ValueRemap(mDistance, 50, 150, 0.9, 1.0);
    noise_composite = mix(hhf_noise, noise_composite, blend);
}

// 3e. erode the profile by the noise
uprezzed = ValueErosion(dimensional_profile, noise_composite);
//   ValueErosion(v, m) = clamp((v - m) / (1 - m), 0, 1)

// 3f. density-scale shaping
powered_scale = pow(clamp(density_scale, 0, 1), 4.0);
uprezzed     *= powered_scale;
uprezzed      = pow(uprezzed, mix(0.3, 0.6, max(EPSILON, powered_scale)));

// 3g. distance-based softening
if (inHFDetails) {
    t = GetFractionFromValue(mDistance, 50, 150);
    uprezzed = pow(uprezzed, mix(0.5, 1.0, t)) * mix(0.666, 1.0, t);
}
```

`ValueErosion` is exactly the `saturate(noise - (1 - dimensional_profile))` idea shown on the reference slide `img/profile.png` (which contrasts a **Vertical Profile Method** cloudscape against an **Envelope Method** single cloud, and captions `cloud_density = saturate(noise - (1.0 - dimensional_profile));`).

**There is no weather map and no altitude gradient in the Nubis3 path.** Coverage, cloud type and vertical falloff are all pre-baked into the R/G/B channels of the modeling texture. This is the single biggest architectural difference from Nubis1/2 and from every "weather-map + height-gradient" cloud system. See section E for what that means for us.

### C.5 Mip / LOD — computed and thrown away

`GetVoxelCloudMipLevel()` (`:161-165`) computes `log2(1 + |distance| * 10) + inMipLevel` when `USE_FINE_DETAIL_MIPMAP` is false (which it is, `:19`). But `GetUprezzedVoxelCloudDensity` stores the result in `mipmap_level` and then calls plain `texture()` without a LOD (`:203-206`). Furthermore every texture is created with `mipLevels = 1` (`Image.cpp:58`) and every sampler with `maxLod = 0` (`Image.cpp:224`). **Dead code — there is no mip chain.** Aliasing in the distance is handled purely by the `mix(0.666, 1.0, t)` softening in 3g.

### C.6 Light energy

`IntegrateLightEnergy()` (`:311-376`). Inputs: the sample's profile and full density, world position, texture coord, light direction, and `cos_angle = dot(viewDir, lightDir)`.

**Density to sun** (`GetDensityToSun`, `:287-305`):
```glsl
for (i = 0..1) { pos += lightDir * mStepSize; totalDensity += FullDensity(pos); }  // 2 local steps
pos += lightDir * mStepSize;
totalDensity += texture(lightGrid, GetSampleCoord(pos)).r;                          // long range
```
That is the Nubis3 idea: two high-quality local shadow samples, then hand off to the pre-baked grid. Note the local steps use `mStepSize`, i.e. the *view-ray* step, which is SDF-driven and can be enormous — so the two "local" samples may land hundreds of units away.

**Low-LOD density** (`GetDensityLowLOD`, `:307-309`): `texture(lightGrid, coord).g`, the per-voxel profile density written in pass 1.

**Ambient scattering** (`:328-332`):
```glsl
height_fraction = clamp((p.z - (-128)) / 256, 0, 1);         // COMPUTED BUT NEVER USED
profile *= exp(-density_to_sun *
               Remap(cos_angle, 0.0, 0.9, 0.25,
                     Remap(cloud_distance, -128.0, 0.0, 0.05, 0.25)));
ambient_scattering = pow(1.0 - profile, 0.5) * exp(-density_to_sun);
```

**Direct scattering** (`:335-357`):
```glsl
silver_spread = 1.32;  silver_intensity = 1.27;  brightness = 0.5;  eccentricity = 1.0;

primary_attenuation   = exp(-density_to_sun);
secondary_attenuation = exp(-density_to_sun * 0.25) * 0.7;
attenuation_probability = max(Remap(cos_angle, 0.7, 1.0,
                                    secondary_attenuation, secondary_attenuation * 0.25),
                              primary_attenuation);

depth_probability    = mix(0.05 + pow(density_Lod, Remap(0.5, 0.3, 0.85, 0.5, 2.0)),
                           1.0, clamp(density_to_sun / 0.5, 0, 1));
vertical_probability = pow(Remap(0.5, 0.07, 0.14, 0.1, 1.0), 0.8);
in_scatter_probability = clamp(depth_probability * vertical_probability, 0, 1);

phase_probability = max(HG(cos_angle, eccentricity),
                        silver_intensity * HG(cos_angle, 0.99 - silver_spread));

light_energy = attenuation_probability * in_scatter_probability * phase_probability * brightness;
light_energy = exp(-light_energy * 5);         // <-- inverts the meaning
```

Three serious findings here:

* **[BUG] `Remap(0.5, ...)` — the height fraction was substituted by a literal `0.5`.** In the Nubis2 formulation these are `Remap(height_fraction, 0.3, 0.85, 0.5, 2.0)` and `Remap(height_fraction, 0.07, 0.14, 0.1, 1.0)`. Here both first arguments are the constant `0.5`, so:
  * `depth_probability` exponent ≡ `1.0454…` (constant),
  * `vertical_probability` ≡ `pow(5.628…, 0.8) ≈ 3.94` (constant, and >1, so it just saturates `in_scatter_probability` to 1 whenever `depth_probability > 0.254`).
  `height_fraction` is computed on line 328 and never read. **The vertical in-scatter gradient is entirely missing from the shipped look.**
* **[BUG] `eccentricity = 1.0` kills the forward lobe.** `HG(c, 1.0) = ((1 - 1) / …) = 0` exactly (`:270-274`). So `phase_probability` reduces to `1.27 * HG(cos_angle, -0.33)` — a *backward*-scattering lobe only. The famous silver-lining forward peak is not present.
* **`light_energy = exp(-light_energy * 5)` inverts the quantity.** More computed energy ⇒ *smaller* final `light_energy`. It then feeds the colour ramp below. This is not physical; the authors say as much in `README.md:204` ("We are not sure if it is physical enough").

**Colour composition** (`:359-375`):
```glsl
_colB = mix(skyColorNoSun, vec3(0.23, 0.36, 0.47), clamp(-lightDir.z + 0.5, 0, 1));  // shadow tint
_colA = mix(skyColorNoSun, vec3(1.00, 0.87, 0.65), clamp(-lightDir.z + 0.2, 0, 1));  // lit tint
cloudColor = mix(_colA, white,  clamp(light_energy * 0.16, 0, 1));
cloudColor = mix(_colB, cloudColor, clamp(pow(light_energy * 12.6, 3), 0, 1));
if (mDensity <= 1.0) cloudColor = (1 - ambient)*cloudColor + ambient*_colB;

mTransmittance += mFull * light_energy * mAlpha;    // scattered-light accumulator
mCloudColor    += mFull * cloudColor   * mAlpha;    // premultiplied colour accumulator
mAlpha         *= exp(-mFull * 1.0);                // the REAL Beer transmittance, sigma_t = 1
```

The extinction coefficient is the literal `1.0` on line 375. `DENSITY_SCALE 0.01` is `#define`d at `:29` and **never referenced anywhere** — a dead knob that was clearly meant to be the extinction scale. **Mark as: should be tunable.**

There is **no Beer-Powder term** (`1 - exp(-2d)`) anywhere in the Nubis3 path. The Nubis2 path has the "beersModulated" variant (`compute.comp:267-269`).

The ambient/no-ambient comparison in the README (`img/am.png` vs `img/am_off.png`) shows this working: with ambient the cloud has a soft dark-blue-tinted base and visible top-to-bottom contrast; without it the image is markedly flatter and washed out. (`am_off.png` also carries a hand-drawn orange sun doodle added by the authors.)

### C.7 The Nubis1/2 path, for contrast (`compute.comp`)

Worth reading because it is the *classic* algorithm and it is what we would want for a full cloudscape:

| Aspect | Nubis1/2 (`compute.comp`) | Nubis3 (`computeNubisCubed.comp`) |
|---|---|---|
| Geometry | spherical shell, `R = 1.2e6`, outer = `R*1.05`, thickness = half the gap (`:16-19`) | AABB 2048×2048×256 (`:25-26`) |
| Ray/volume test | two `RaySphereIntersection` calls (`:570-571`) | 6-plane slab test (`:382-444`) |
| Coverage | weather map `.r`, `pow(coverage, Remap(h, 0.7, 0.8, 1.0, 0.8))` anvil bias (`:186`) | baked into modeling `.r` |
| Cloud type | weather map `.b`, blends stratus / stratocumulus / cumulus gradients (`:161-171`) | baked into modeling `.g` |
| Height gradient | `GetCloudLayerDensity(relativeHeight, cloudType)` — the classic 3-preset remap stack (`:161-171`) | none |
| Base shape | `profileCloudShape` 3D noise `.r`, FBM-combined `.gba` erosion (`:181-190`) | modeling texture `.r` |
| Detail | `detailCloudShape` 3D noise + 2D curl-noise displacement (`:195-206`) | 4-channel detail noise (`:197-244`) |
| Wind | `SkewSamplePointWithWind` — height-sheared advection (`:97-103`) | flat `vec2(0.1,0.1) * speed * t` translation (`:200`) |
| Light march | 6 cone-jittered samples in a sun-aligned frame (`:211-274`) | 2 local steps + light voxel grid |
| Step strategy | fixed `0.05 * thickness`, ×0.3 on first hit, ÷0.3 after 10 misses (`:283, :311, :326`) | SDF + `sqrt(d)` adaptive |
| Phase | single `HG(cos, 0.2)` (`:288`) | dual-lobe HG (broken, see above) |

**`GetCloudLayerDensity` (`compute.comp:161-171`) is the single most reusable function in the whole repo** if we want a procedural cloudscape rather than a baked hero cloud.

`img/envelope.png` (unreferenced by the README) is a Nubis reference slide that gives the *other* half — the vertical envelope construction:
```
height_fraction    = Remap(height, min_height, max_height, 0.0, 1.0);
top_gradient       = pow(1.0 - height_fraction, 1.5);
bottom_gradient    = pow(height_fraction, 2.0);
edge_gradient      = Remap(sample_height, 0.0, 35.0, 1.0, 0.0);
dimensional_profile = bottom_gradient * top_gradient * edge_gradient;
```
That is exactly how a *procedural* dimensional profile is built, and it is the piece we will need since we will not have Guerrilla's VDBs.

---

## D. Temporal reprojection & upscaling

### D.1 What the README claims

`README.md:177-185`:
> We used `temporal upscaling` and split the render into two passes: High resolution in the distance … and low resolution up close … We set a threshold in 200-500 meter … the blurriness is tolerable with only 1/4 its origin work load with a 30% - 70% FPS increase.

`img/upscaling.png` draws a camera on the right, a "200 m" bracket, a near tile labelled **270 px × 480 px** and a far tile labelled **540 px × 960 px**.

### D.2 What the code actually does

| Claim | Reality |
|---|---|
| "Temporal" | **[DISAGREES]** There is nothing temporal. No history buffer, no motion vectors, no jitter, no accumulation, no rejection/clamping. |
| Threshold "200–500 m" | `#define NEAR_THRESHOLD 500.f` (`nearCloud.comp:9`, `farCloud.comp:10`). Fixed at 500, and it is a **compile-time constant, not a uniform**. |
| Near at 1/4 | Correct in pixel count: 960×540 vs 1920×1080 (`Image.cpp:499-500`, `Renderer.cpp:378-379`). |
| Upscale filter | `texture(nearCloudColorTex, uv)` with a `VK_FILTER_LINEAR` / `REPEAT` sampler (`farCloud.comp:679`, `Image.cpp:209-213`) — **plain bilinear**, no depth-aware or bilateral filter. |

### D.3 The reprojection that was written and then disabled

`reproject.comp:109-126` contains a complete, commented-out implementation:

```glsl
// Ray ray = GenerateRay(uv);
// earthCenter = ray.mOrigin; earthCenter.y = -ATMOSPHERE_RADIUS * 0.5;
// isect = RaySphereIntersection(ray, earthCenter, ATMOSPHERE_RADIUS_INNER);
// intersectionPos = (cameraPrev.view * vec4(isect.mPoint, 1.0)).xyz;
// oldCamRayDir = normalize(intersectionPos); oldCamRayDir /= -oldCamRayDir.z;
// oldU = 0.5 + 0.5 * oldCamRayDir.x / (aspectRatio * halfTanFOV);
// oldV = 0.5 - 0.5 * oldCamRayDir.y / halfTanFOV;
// sourceColor = imageLoad(sourceImage, clamp(ivec2(oldUV*dim), 0, dim-1));
```

This is the standard **"reproject through the cloud shell"** trick: project the pixel onto the inner atmosphere sphere, transform into the previous frame's view space, and re-derive the UV. Note `ATMOSPHERE_RADIUS 1500000` here (`:6`) versus `1200000` in `compute.comp:16` — the two shaders already disagreed.

The live body is `sourceColor = imageLoad(sourceImage, gl_GlobalInvocationID.xy); imageStore(targetImage, ...)` — an identity copy (`:128-129`).

The infrastructure for the ping-pong exists but is disabled: `imagePrevTexture` is commented out (`Renderer.cpp:211`, `Renderer.h:93`), `ReprojectShader::swapBuffers` flips a set index (`ReprojectShader.cpp:56-60`), `Camera::UpdatePrevBuffer` still runs every frame (`Camera.cpp:145-148`), and `Camera::UpdatePixelOffset` still cycles `pixelOffset` 0..15 (`Camera.cpp:150-153`) for a 4×4 checkerboard that the shaders no longer implement (`computeNubisCubed.comp:664-666`, commented out).

### D.4 Artefacts traded away — and traded in

* **Traded away:** temporal ghosting, temporal disocclusion, the need for motion vectors and a rejection heuristic. There is simply no temporal state.
* **Traded in:** a visible **seam at exactly 500 units** where a bilinearly-magnified half-res layer meets a full-res layer, plus half-res aliasing on any near cloud silhouette. The README's own `img/near.gif` was meant to show the near-only pass; **that GIF could not be read** (24 MB, above the tool's image limit) — I make no claim about its contents. Same for `img/cloud_short.gif` (59 MB) and `img/cloud_day_night.gif` (95 MB).
* The blockiness visible in `img/2.png` and the faceting in `img/b2.png` (top-right panel) are consistent with a coarse step size, not with temporal artefacts.

**Conclusion for us:** treat this project's "temporal upscaling" as *not implemented*. If we want TAA-style upscaling for clouds we are designing it from scratch; the only thing to salvage is the near/far split idea and the commented reprojection sketch.

---

## E. Modeling data — offline vs runtime

### E.1 What ships

| Asset | Path | Size | Used at runtime? |
|---|---|---|---|
| `ParkouringCloud.vdb` | `src/images/vdb/example1/` | 47.0 MB | **No** |
| `StormbirdCloud.vdb` | `src/images/vdb/example2/` | 48.9 MB | **No** |
| `NubisVoxelCloudNoise.vdb` | `src/images/noise/` | 30.9 MB | **No** |
| `modeling_data(0..63).tga` ×2 sets | `src/images/vdb/example{1,2}/tga/` | 512×512, 32 bpp, RLE | **Yes** |
| `field_data(0..63).tga` ×2 sets | same dirs | 512×512, 32 bpp, RLE | **No** (loader commented out, `Renderer.cpp:223`) |
| `NubisVoxelCloudNoise(0..127).tga` | `src/images/noise/tga/` | 128×128, 32 bpp, RLE | **Yes** |
| `lowResCloud(0..127).tga` | `src/images/lowResCloudShape/` | 128×128, 32 bpp | Loaded, but only the dead Nubis2 path samples it |
| `hiResClouds (0..31).tga` | `src/images/hiResCloudShape/` | 32×32, 32 bpp | same |
| `weather.png` | `src/images/` | 512×512, 16-bit RGBA | same |
| `curlNoise.png` | `src/images/` | 128×128, 8-bit RGB | same |

Total asset payload on disk: ~190 MB, of which ~127 MB is `.vdb` files that are **never opened at runtime**.

### E.2 Is OpenVDB a runtime dependency?

**Build-time: yes, hard. Runtime: no.**

* `CMakeLists.txt:12` — `find_package(OpenVDB REQUIRED)`.
* `src/CMakeLists.txt:74` — `target_link_libraries(... OpenVDB::openvdb)`.
* `Image.cpp:3` — `#include <openvdb/openvdb.h>` unconditionally, even though the VDB code path is disabled.
* `src/vdb/Types.h:36`, `vdb.h:23`, `Grid.h:24` — OpenVDB in the headers.
* But `Renderer.cpp:219` — `// modelingDataTexture = Image::CreateTextureFromVDBFile(...)` is **commented out**. The live loads are `Image::CreateTexture3DFromFiles(...)` on the TGA slice sequences (`Renderer.cpp:221-224`).

So the VDB→3D-texture conversion (`Image::FromVDBFile`, `Image.cpp:362-419` → `Image::GenerateVDBSlice`, `:608-627`) is an **import-time tool** that was used once to generate the TGA sequences and then switched off. `README.md:116, 133` describes it as the loading path, which is out of date: **[DISAGREES]** with `Renderer.cpp:219-224`.

**For DesertEngine: we do not need OpenVDB at runtime.** We would only need it (or a Houdini export) in an offline importer, if we chose to ship baked cloud volumes at all.

**[BUG]** `Image::GenerateVDBSlice` (`Image.cpp:608-627`) writes `float` fields (`vdat.dimensional_profile` etc., declared `float` in `vdb/Types.h:91-94`) directly into an `unsigned char*`. Values in `[0,1]` truncate to `0`. If we ever revive this path, the values need `* 255`.

### E.3 What the VDB grids contain

`VDB::getMeshValuesScalar` (`vdb.cpp:614-670`) walks the active voxels of each named grid and dispatches on the grid name:

| Grid name in the `.vdb` | Destination field | Default if absent |
|---|---|---|
| `dimensional_profile` | `VDatAlt::dimensional_profile` | `-1` (`Types.h:93`) |
| `detail_type` | `VDatAlt::detail_type` | `1` |
| `density_scale` | `VDatAlt::density_scale` | `1` |
| `sdf` | `VDatAlt::sdf` | `0` |

Voxel array is hard-coded to 512 × 512 × 64 with the indexing `mDataPoints[x*512*64 + z*64 + y]` (`vdb.cpp:639`, dims at `:581-583`) — note the **y/z swap**, another mirror/axis convention baked in.

### E.4 The Nubis3 authoring pipeline (reference material)

`img/modelprocess.png` is a Nubis slide reading:
> **Voxel Cloud Modeling**: Grow Clouds using Fluid Simulations. / Edit and composite them into "Frankencloudscapes." / Store them in a voxel grid to be sampled at render time.

`img/readmeref1.png` is the SIGGRAPH 2023 slide *"Nubis³ / Voxel Clouds / Sampling Density"* giving the exact spec:
> **Modeling Data / NVDF's / 512 x 512 x 64 / BC6, 1 Byte / Texel / 16.777 Mb**, with the three stacked planes labelled `Dimensional Profile`, `Detail Type`, `Density Scale`.

Note the slide says **BC6 at 1 byte/texel = 16.8 MB**; this project uses **uncompressed R8G8B8A8 = 64 MiB per cloud** (`Image.cpp:566`). Guerrilla also lists only *three* channels; this project adds a fourth (SDF) for empty-space skipping.

`img/houdinivdb.png` shows the source asset in what appears to be Houdini: an orange wireframe bound box around a grey, visibly stair-stepped voxel mesh of a cumulonimbus with a tall stack of lobes over a spreading base.

`img/light_voxel_grid.png` is a *photograph of a monitor* showing a chunky, low-resolution voxelised cloud fragment in cream/white against pale blue, with a faint shadow streak to the lower-left — i.e. the light grid visualised directly. Low information content; do not rely on it.

`img/grid.png` is the schematic: a sun in the upper left, a wireframe box labelled **256 Voxels × 256 Voxels × 32 Voxels**, two small grey clouds inside, and red sample dots strung along lines running from the clouds toward the sun. This matches `lightGrid.comp` — except that the arrows point *toward* the sun, which is the opposite of what the code does (see B.1).

### E.5 "Generate profile data"

`README.md:36` ticks *"Cloud modeling and noise texture generation — [x] Generate profile data"*. I could find **no generator code** in the repo: the only thing that produces profile data is `Image::GenerateVDBSlice` (a VDB→TGA slicer), and `README.md:120` itself says *"reimplementing the data generation steps will be a whole another project."* Treat the checkbox as referring to the slicing tool, not to procedural generation.

---

## F. Noise / texture assets

### F.1 Live (Nubis3 path)

| Texture | Dimensions | Vulkan format | Channels | Loaded from | Sampled where |
|---|---|---|---|---|---|
| `modelingDataParkourTexture` | 512 × 512 × 64 | `R8G8B8A8_UNORM` | R=Dimensional Profile, G=Detail Type, B=Density Scale, A=SDF | `images/vdb/example1/tga/modeling_data(i).tga`, i=0..63 | `computeNubisCubed.comp:184`, `lightGrid.comp:53` |
| `modelingDataStormBirdTexture` | 512 × 512 × 64 | `R8G8B8A8_UNORM` | same | `images/vdb/example2/tga/modeling_data(i).tga` | `:186`, `lightGrid.comp:55` |
| `cloudDetailNoiseTexture` | 128 × 128 × 128 | `R8G8B8A8_UNORM` | R=LF Curl-Alligator, G=HF Curl-Alligator, B=LF Alligator, A=HF Alligator | `images/noise/tga/NubisVoxelCloudNoise(i).tga`, i=0..127 | `computeNubisCubed.comp:206` |
| `lightGridTexture` | 256 × 256 × 32 | `R32G32B32A32_SFLOAT` | R=density-to-sun, G=local profile density, B/A unused | written by `lightGrid.comp` | `:302, :308` |
| `imageCurTexture` | 1920 × 1080 | `R32G32B32A32_SFLOAT` | RGB = HDR scene, A = 1 | written by `farCloud.comp` | `tone.frag:4` |
| `nearCloudColorTexture` | 960 × 540 | `R32G32B32A32_SFLOAT` | RGB = accumulated cloud colour | written by `nearCloud.comp` | `farCloud.comp:679` |
| `nearCloudDensityTexture` | 960 × 540 | `R32G32B32A32_SFLOAT` | R=density, G=transmittance accumulator, B=alpha, A=0 | written by `nearCloud.comp` | `farCloud.comp:682` |

Sampler for **every** texture (`Image.cpp:206-231`): `LINEAR`/`LINEAR`, address mode `REPEAT` on all three axes, anisotropy 16, `mipmapMode LINEAR` but `minLod = maxLod = 0`.

> The `REPEAT` address mode on the 512×512×64 modeling texture is why the detail noise tiles (the grid pattern visible in `img/cloudd.png` and `img/am.png`) — and it means a sample coordinate outside `[0,1]³` would wrap rather than clamp. The raymarcher guards against that explicitly (`computeNubisCubed.comp:460`), but the light grid does not.

### F.2 Loaded but only used by the dead Nubis1/2 path

| Texture | Dimensions | Format | Channel meaning (from `compute.comp`) |
|---|---|---|---|
| `lowResCloudShape` | 128 × 128 × 128 | `R8G8B8A8_UNORM` | `.r` = base shape (remapped 0.3→1.0), `.gba` = FBM erosion octaves (`compute.comp:181-190`) |
| `hiResCloudShape` | 32 × 32 × 32 | `R8G8B8A8_UNORM` | `.rgb` = detail erosion octaves at weights 0.625/0.25/0.125 (`compute.comp:201-205`) |
| `weather.png` | 512 × 512, 16-bit RGBA | loaded as `R8G8B8A8_UNORM` | `.r` = coverage, `.b` = cloud type (0=stratus, 0.5=stratocumulus, 1=cumulus) (`compute.comp:176-186`) |
| `curlNoise.png` | 128 × 128, 8-bit RGB | `R8G8B8A8_UNORM` | `.rgb` mapped to `[-1,1]`, displaces the detail sample position (`compute.comp:197-199`) |

Note the weather map is authored 16-bit but is force-loaded as 8-bit via `stbi_load(..., STBI_rgb_alpha)` (`Image.cpp:279`) — precision is discarded.

### F.3 Procedural vs must-ship

| Asset | Can we generate it at runtime? |
|---|---|
| Detail noise (128³ 4-channel Curl-Alligator / Alligator) | **Yes.** These are curl-warped Worley/Perlin variants. A compute-shader generator at load time is standard practice and costs a few ms. `img/noise.png` is the Guerrilla slide specifying them: *"Voxel Cloud Noise / 4 Channel / 128 x 128 x 128 Voxels / Uncompressed, 2 Bytes / Texel / 4.194 Megabytes"*, with four swatches captioned `Low Freq "Curly-Alligator"`, `High Freq "Curly-Alligator"`, `Low Freq Alligator`, `High Freq Alligator`. Note the slide says **2 bytes/texel**; this project uses 4. |
| Low-res / hi-res cloud shape noise | **Yes**, same reasoning (classic Perlin-Worley). |
| Curl noise 2D | **Yes**, trivially. |
| Weather map | **Yes** — procedural FBM, or authored per-level, or driven by a weather system. |
| **Modeling NVDFs (the actual cloud shapes)** | **No, not as-is.** These are Houdini fluid-sim bakes from Guerrilla's internal tools. See section J risk 1. |
| Light voxel grid | Generated at runtime by definition. |

### F.4 Attribution / licensing of the assets

* The `.vdb` files are stated to be **"provided by Nubis3's team … generated from their internal tools"** (`README.md:123`). **Do not redistribute.** No licence accompanies them.
* `img/noise.png`, `img/profile.png`, `img/readmeref1.png`, `img/modelprocess.png` and `img/envelope.png` are **verbatim slides from the SIGGRAPH 2023 Nubis³ talk / Guerrilla Games material** (the SIGGRAPH 2023 Los Angeles footer and the Guerrilla chevron logo are visible in `readmeref1.png`). Reference only; do not copy into our docs.
* The TGA slices are derived from those VDBs and carry the same restriction.

---

## G. Lighting / atmosphere integration

### G.1 Sun and day/night cycle

`Scene::UpdateTime` (`Scene.cpp:13-30`):
```cpp
const float ONE_DAY      = 30.0f;        // seconds!
const float SUN_DISTANCE = 400000.0f;
theta = controlAngle ? radians(customTheta) : (mod(totalTime, 30) * 2*PI / 30);
float phi = 45;                          // <-- radians, not degrees
sunPositionY = SUN_DISTANCE * cos(theta) * cos(phi);
sunPositionZ = -SUN_DISTANCE * sin(theta);
sunPositionX = -SUN_DISTANCE * cos(theta) * sin(phi);
```

* **A full day lasts 30 seconds.** That is a demo setting; for us it must be a rate parameter.
* **[BUG]** `phi = 45` is fed straight into `cos`/`sin` as radians. `cos(45 rad) = 0.5253`, `sin(45 rad) = 0.8509`. The intent was clearly 45°. The result is a fixed but arbitrary azimuth. Worth fixing rather than reproducing.
* `deltaTime` is measured with `high_resolution_clock` and `totalTime` accumulates it (`Scene.cpp:14-19`).
* UI override: `Custom Control Sun Angle` checkbox + a `Sun Angle` slider in degrees, 0–360 (`Renderer.cpp:501-506`).

### G.2 Sky — Preetham analytic model

`GetSkyColor()` (`computeNubisCubed.comp:599-654`). This is a direct port of the well-known "Sky-Shader" Preetham implementation (`README.md:222` cites `tw1ddle.github.io/Sky-Shader`). It produces **three outputs** per pixel:

* `mSkyColorNoSun` — Rayleigh + Mie in-scatter, tonemapped. Used as the base for the cloud's lit/shadow tints (`:360-361`).
* `mSkyColor` — the same plus the solar disc term `L0 = 0.1*Fex + sunE * 1900 * Fex * sundisk`.
* `mSunDisk` — `smoothstep(cos(0.032°), cos(0.032°)+0.00002, cosTheta)`.
* `mNight = mix(0.06, 1, clamp(-sunDir.z, 0, 1))` — a global night dimming factor applied to the cloud colour at `:700`.

Up vector for the sky is `vec3(0, 0, -1)` (`:529`) — i.e. **the sky model's "up" is -Z while the cloud box's up is +Z**. That inconsistency is why `sunPositionZ = -SUN_DISTANCE * sin(theta)` is negated in `Scene.cpp:26`. Be very careful if you port the sky and the clouds separately.

Stars (`GetStarColor`, `:581-597`): active only when `sunDir.z > 0` (sun below the horizon in this convention). A hash `fract(415.92653 * (0.7*cos(37.3x) + 1.2*cos(56.1y) + 0.2*cos(45.8z)))` on `floor(rayDir*700 + totalTime*0.1)`, thresholded at `0.97` and sharpened with `pow(..., 10)`, scaled by `0.8` and a `sunDir.z < 0.75` fade-in. `img/sky_night.png` shows the result — and also shows an artefact: a hard horizontal seam at ~70 % screen height below which the sky becomes a flat dark teal band with stars still drawn over it.

`img/sky.png` shows the dusk case: a hazy grey-blue-to-tan gradient with the sun disc peeking past the cloud's left shoulder, very low contrast.

### G.3 God rays — screen-space radial blur

`tone.frag:54-90`, following the GPU Gems 3 Ch. 13 "volumetric light scattering as a post-process" recipe (`README.md:228`):

```glsl
if (time.sunPositionZ > 0) return vec4(0);          // sun below horizon -> no rays
decay   = 0.96;
exposure = mix(uiParam.godray_exposure, 0.02, clamp(-sunDir.z, 0, 1));
density = 0.2;
weight  = 0.58767;
NUM_SAMPLES = 100;

sunScreenPos = (proj * view * vec4(sunPos, 1)).xyz / w;
deltaTexCoord = (uv*2 - 1 - sunScreenPos.xy) * (1/NUM_SAMPLES) * density;
color = texture(texColor, uv) * 0.4;
for (i = 0..99) { uv -= deltaTexCoord; color += texture(texColor, uv) * 0.4 * illum * weight; illum *= decay; }
color.a = exposure;
```
and then `sceneCol += GodRayCol * GodRayCol.a` (`tone.frag:97`).

Notes:
* **100 taps at full resolution, every pixel, unconditionally** when enabled. This is the most obviously expensive thing in the frame and it is not downsampled.
* It uses `camera.proj`, which was built with `zFar = 100` (`Camera.cpp:26`) while the sun is at distance 400 000. The projection is only used for the x/y screen position after the perspective divide, so it "works", but it is fragile.
* `deltaTexCoord` mixes NDC (`uv*2-1`) with texture-space stepping (`uv -= deltaTexCoord`) — a factor-of-2 inconsistency that just scales the ray length.
* `img/god_ray.png` shows the effect at full strength: a near-monochrome white/grey cloudscape where the entire lower half is washed out to uniform bright white.

### G.4 Tonemapping

Two separate Uncharted-2 tonemaps are applied in sequence:

1. Inside the sky model (`computeNubisCubed.comp:570-579, 631-634, 648-651`), with `A=0.15, B=0.50, C=0.10, D=0.20, E=0.02, F=0.30`, white point `tonemapWeighting = 19.50`, and an exposure-ish pre-scale `log2(2 / luminance⁴)` plus `texColor *= 0.04`, then `pow(color, 1/(1.2 + 1.2*sunfade))`.
2. In `tone.frag:41-52`, again Uncharted-2 (same coefficients hard-coded inline), with `exposure = 0.7`, white point `1.0`, gamma `1/2.2`.

**Double tonemapping is a defect of the design, not a feature.** In our engine the cloud pass should write linear HDR and the engine's existing tonemapper should own the curve.

---

## H. COMPLETE PARAMETER INVENTORY

Legend for the **Exposed** column: `UI` = ImGui slider/checkbox this frame; `UBO` = in the uniform buffer but not surfaced; `#define` = shader compile-time; `const` = C++ constant; `magic` = inline literal in an expression. **"⚠ should be tunable"** marks values that are hard-coded but obviously want to be parameters.

### H.1 Runtime UI / uniform buffer (`UIControlBufferObject`, `Renderer.h:15-29`; sliders `Renderer.cpp:471-508`)

| # | Name | Meaning | Units | Range | Default | Controls |
|---|---|---|---|---|---|---|
| 1 | `tiling_freq` | detail-noise UVW scale | 1/world-unit | 0.01 – 0.1 (UI) | 0.05 | size of the billow/wisp features |
| 2 | `cloud_type` | which baked NVDF (0=Parkouring, 1=Stormbird) | enum | {0,1} | 1 | the entire cloud silhouette |
| 3 | `animate_speed` | wind-offset multiplier | world-units/s | 0 – 100 (UI) | 10.0 | how fast detail scrolls |
| 4 | `animate_offset` | wind direction vector | world units | −1000 – 1000 | (0.1, 0.1, 0) | **commented out** in `Renderer.h:23`, `Renderer.cpp:482`, shader `:84` |
| 5 | `farclip` | max raymarch distance | world units | 0 – 5000 (UI) | 700.0 | how far clouds are drawn |
| 6 | `transmittance_limit` | early-out threshold | — | 0 – 1 (UI) | 0.01 | (see C.3 — currently ineffective) |
| 7 | `enable_godray` | god-ray toggle (float used as bool) | — | {0,1} | 1.0 | post-process on/off |
| 8 | `godray_exposure` | god-ray intensity | — | 0.01 – 0.15 (UI) | 0.09 | strength of light shafts |
| 9 | `sky_turbidity` | Preetham turbidity | — | 1 – 20 (UI) | 12.0 | haze / sky saturation |
| 10 | `customSunAngle` | override day cycle | bool | — | false | freezes the sun |
| 11 | `angle` | manual sun elevation | degrees | 0 – 360 (UI) | 0.0 | sun position |
| 12 | `camera->stepSize` | WASD move speed | world-units/keypress | 0 – 200 (UI) | 5.0 | navigation only |
| 13 | `useNubisCubed` | algorithm selector | enum | {0,1} | 1 | radio buttons **commented out** (`Renderer.cpp:467-469`) |

That is **13** user-facing knobs, of which 2 are disabled. Everything else below is hard-coded.

### H.2 Volume / domain

| # | Name | File:line | Value | Meaning | Notes |
|---|---|---|---|---|---|
| 14 | `VOXEL_BOUND_MIN` | `computeNubisCubed.comp:25` | `(-1024,-1024,-128)` | cloud AABB min | ⚠ should be tunable (per-entity transform) |
| 15 | `VOXEL_BOUND_MAX` | `:26` | `(1024,1024,128)` | cloud AABB max | ⚠ should be tunable |
| 16 | SDF remap range | `:191` | `-256 … 4096` | decode of NVDF `.a` | ⚠ should be tunable; ties the SDF encoding to world scale |
| 17 | modeling texture dims | `Renderer.cpp:221-222` | `512×512×64` | — | ⚠ should be data-driven |
| 18 | detail noise dims | `Renderer.cpp:224` | `128×128×128` | — | ⚠ |
| 19 | `X_SIZE` (light grid XY) | `lightGrid.comp:6` | 256 | — | duplicated in `Renderer.cpp:227,368` ⚠ |
| 20 | `Z_SIZE` (light grid Z) | `lightGrid.comp:7` | 32 | — | ⚠ |
| 21 | `WORKGROUP_SIZE` | all `.comp:4-5` | 32 | 32×32 = 1024 threads | at the Vulkan minimum-guarantee limit; consider 8×8 |
| 22 | `EPSILON` | `computeNubisCubed.comp:16` | 0.1 | step-size floor / density-scale floor | overloaded for two unrelated purposes |
| 23 | `DENSITY_SCALE` | `:29` | 0.01 | **never referenced** | ⚠ almost certainly meant to be the extinction scale |
| 24 | `CLOUD_WIND_OFFSET` | `:14` | `vec2(0.1, 0.1)` | wind direction | ⚠ should be tunable |

### H.3 Raymarching

| # | Name | File:line | Value | Meaning |
|---|---|---|---|---|
| 25 | adaptive step floor | `:464` | `1.0` | minimum step in world units ⚠ |
| 26 | adaptive step coefficient | `:464` | `0.08` | `step = sqrt(d) * 0.08` ⚠ |
| 27 | `NEAR_THRESHOLD` | `nearCloud.comp:9`, `farCloud.comp:10` | `500.0` | near/far split distance ⚠ **should be a uniform** |
| 28 | near-pass resolution divisor | `Image.cpp:499-500`, `Renderer.cpp:378-379` | `2` | half res in each axis ⚠ |
| 29 | `USE_FINE_DETAIL_MIPMAP` | `:19` | `false` | — (dead) |
| 30 | `USE_FINE_DETAIL_MIPMAP_DISTANCE_SCALE` | `:20` | `10.0` | — (dead) |
| 31 | `MAX_RAYMARCHING_DISTANCE` | `:23` | `500.0` | **commented out** (superseded by `farclip`) |
| 32 | `VIEW_RAY_TRANSIMITTANCE_LIMIT` | `:24` | `0.01` | **commented out** |
| 33 | `SUN_LOCATION` | `:10` | `vec3(0,0,0)` | **commented out** |
| 34 | `CLOUD_ANIMATE_SPEED` | `:13` | `10.0` | **commented out** |

### H.4 Density / detail erosion (`GetUprezzedVoxelCloudDensity`, `:197-244`)

| # | Name | Line | Value | What it changes |
|---|---|---|---|---|
| 35 | billowy type gradient exponent | 213 | `0.25` | how quickly billow takes over from wisp with profile density ⚠ |
| 36 | billow noise scale | 214 | `0.3` (both terms) | strength of the Alligator (billow) channels ⚠ |
| 37 | HHF wisp exponent | 222 | `4.0` | sharpness of the high-high-freq wisp ridges ⚠ |
| 38 | HHF billow exponent | 223 | `2.0` | sharpness of the HHF billows ⚠ |
| 39 | HHF distance blend near | 225 | `50.0` | where the HHF layer starts fading ⚠ |
| 40 | HHF distance blend far | 225 | `150.0` | where it is fully faded ⚠ |
| 41 | HHF blend min | 225 | `0.9` | how much HHF survives up close ⚠ |
| 42 | HHF blend max | 225 | `1.0` | — |
| 43 | density-scale power | 231 | `4.0` | contrast of the density-scale channel ⚠ |
| 44 | sharpening exponent min | 235 | `0.3` | low-density regions get sharper ⚠ |
| 45 | sharpening exponent max | 235 | `0.6` | ⚠ |
| 46 | LOD softening near | 239 | `50.0` | ⚠ |
| 47 | LOD softening far | 239 | `150.0` | ⚠ |
| 48 | LOD gamma min/max | 240 | `0.5` / `1.0` | distance-based density gamma ⚠ |
| 49 | LOD scale min/max | 240 | `0.666` / `1.0` | distance-based density attenuation ⚠ |
| 50 | near-fade start | 261 | `10.0` | distance at which density is 25 % ⚠ |
| 51 | near-fade end | 261 | `120.0` | distance at which density is 100 % ⚠ |
| 52 | near-fade min | 261 | `0.25` | ⚠ |
| 53 | near-fade max | 261 | `1.0` | — |

### H.5 Lighting (`IntegrateLightEnergy`, `:311-376`)

| # | Name | Line | Value | Meaning |
|---|---|---|---|---|
| 54 | local sun-march step count | 294 | `2` | ⚠ quality knob |
| 55 | `silver_spread` | 336 | `1.32` | second HG lobe eccentricity = `0.99 - 1.32 = -0.33` ⚠ |
| 56 | `silver_intensity` | 337 | `1.27` | weight of the second lobe ⚠ |
| 57 | `brightness` | 338 | `0.5` | global light-energy multiplier ⚠ |
| 58 | `eccentricity` | 339 | `1.0` | first HG lobe — **makes it evaluate to exactly 0** ⚠ **[BUG]** |
| 59 | secondary attenuation exponent | 343 | `0.25` | ⚠ |
| 60 | secondary attenuation scale | 343 | `0.7` | ⚠ |
| 61 | attenuation remap range | 344 | `cos ∈ [0.7, 1.0]` | forward-scatter blend window ⚠ |
| 62 | attenuation remap far scale | 344 | `0.25` | ⚠ |
| 63 | depth-probability floor | 347 | `0.05` | ⚠ |
| 64 | depth-probability remap | 347 | `Remap(0.5, 0.3, 0.85, 0.5, 2.0)` | **[BUG]** constant `0.5` should be `height_fraction` |
| 65 | depth-probability saturation divisor | 347 | `0.5` | ⚠ |
| 66 | vertical-probability remap | 348 | `Remap(0.5, 0.07, 0.14, 0.1, 1.0)` | **[BUG]** same |
| 67 | vertical-probability exponent | 348 | `0.8` | ⚠ |
| 68 | light-energy exponential | 357 | `exp(-E * 5)` | the `5` ⚠, and the inversion is questionable |
| 69 | ambient cos remap | 331 | `Remap(cos, 0.0, 0.9, 0.25, …)` | ⚠ |
| 70 | ambient cloud-distance remap | 331 | `Remap(d, -128, 0, 0.05, 0.25)` | ⚠ (the `-128` matches `VOXEL_BOUND_MIN.z`, coincidentally or not) |
| 71 | ambient exponent | 332 | `pow(1-profile, 0.5)` | ⚠ |
| 72 | shadow tint `_colB` target | 360 | `vec3(0.23, 0.36, 0.47)` | cool blue in shadow ⚠ **key art-direction colour** |
| 73 | shadow tint blend bias | 360 | `-lightDir.z + 0.5` | ⚠ |
| 74 | lit tint `_colA` target | 361 | `vec3(1.00, 0.87, 0.65)` | warm sunlight ⚠ **key art-direction colour** |
| 75 | lit tint blend bias | 361 | `-lightDir.z + 0.2` | ⚠ |
| 76 | `_colorOffset1` | 363 | `0.16` | knee of the lit→white ramp ⚠ |
| 77 | `_colorOffset2` | 364 | `12.6` | knee of the shadow→lit ramp ⚠ |
| 78 | shadow ramp exponent | 367 | `3` | ⚠ |
| 79 | extinction coefficient | 375 | `1.0` (`exp(-mFull * 1.0)`) | ⚠ **the single most important density knob** |
| 80 | density clamp for ambient | 368 | `mDensity <= 1.0` | ⚠ |

### H.6 Light grid (`lightGrid.comp`)

| # | Name | Line | Value | Notes |
|---|---|---|---|---|
| 81 | march step | 98 | `1 voxel` (`nextCoord += sunDir`) | = 8 world units; ⚠ should be tunable |
| 82 | density formula | 61 | `profile * densityScale` | no extinction coefficient, no step length ⚠ |
| 83 | empty-voxel early out | 87 | `density <= 0` | — |

### H.7 Sky — Preetham (`computeNubisCubed.comp:512-654`)

| # | Name | Line | Value | Notes |
|---|---|---|---|---|
| 84 | `depolarizationFactor` | 512 | `0.137` | ⚠ |
| 85 | `luminance` | 513 | `1.0` | ⚠ should be an exposure knob |
| 86 | `mieCoefficient` | 514 | `0.0074` | ⚠ haze amount |
| 87 | `mieDirectionalG` | 515 | `0.468` | ⚠ sun-glow tightness |
| 88 | `mieKCoefficient` | 516 | `(0.686, 0.678, 0.666)` | ⚠ |
| 89 | `mieV` | 517 | `4.007` | ⚠ |
| 90 | `mieZenithLength` | 518 | `7100` | ⚠ |
| 91 | `numMolecules` | 519 | `2.542e25` | physical constant |
| 92 | `primaries` (RGB wavelengths) | 520 | `(6.8e-7, 5.5e-7, 4.5e-7)` m | ⚠ |
| 93 | `rayleigh` | 521 | `5.75` | ⚠ sky blueness |
| 94 | `rayleighZenithLength` | 522 | `3795` | ⚠ |
| 95 | `refractiveIndex` | 523 | `1.000128` | physical constant |
| 96 | `sunAngularDiameterDegrees` | 524 | `0.032` | ⚠ |
| 97 | `sunIntensityFactor` | 525 | `1024` | ⚠ |
| 98 | `sunIntensityFalloffSteepness` | 526 | `6.4` | ⚠ (`compute.comp:416` uses `1.4` — the two shaders disagree) |
| 99 | `tonemapWeighting` | 527 | `19.50` | ⚠ white point |
| 100 | `turbidity` | 528 | = `uiParam.sky_turbidity` | the one exposed sky knob |
| 101 | `UP` | 529 | `(0, 0, -1)` | **⚠ conflicts with the cloud box's +Z up** |
| 102 | sunfade divisor | 602 | `450000` | ⚠ |
| 103 | Earth-shadow cutoff angle | 566 | `PI / 1.95` | ⚠ |
| 104 | sun disc smoothstep width | 638 | `0.00002` | ⚠ |
| 105 | ambient sky floor `L0` | 640 | `vec3(0.1) * Fex` | ⚠ |
| 106 | `sunHDR` | 641 | `1900.0` | ⚠ (`compute.comp:521` uses `mix(19000, 1, density)` — 10× different) |
| 107 | sky pre-scale | 629, 646 | `0.04` | ⚠ exposure |
| 108 | sky night lift | 630, 647 | `vec3(0, 0.001, 0.0025) * 0.3` | ⚠ |
| 109 | sky output gamma | 634, 651 | `1 / (1.2 + 1.2*sunfade)` | ⚠ |
| 110 | night dimming | 653 | `mix(0.06, 1, clamp(-sunDir.z))` | ⚠ how dark night gets |
| 111 | Uncharted-2 `A..F` | 572-577 | `0.15, 0.50, 0.10, 0.20, 0.02, 0.30` | ⚠ (6 values) |

### H.8 Stars (`GetStarColor`, `:581-597`)

| # | Name | Line | Value |
|---|---|---|---|
| 112 | `starThreshold` | 533 | `0.97` ⚠ star density |
| 113 | star sharpening exponent | 537 | `10.0` ⚠ |
| 114 | hash constants | 534 | `415.92653`, `0.7/37.3`, `1.2/56.1`, `0.2/45.8` |
| 115 | star field frequency | 592 | `rayDir * 700` ⚠ star size |
| 116 | star drift speed | 592 | `totalTime * 0.1` ⚠ |
| 117 | star brightness | 593 | `0.8` ⚠ |
| 118 | star fade-in threshold | 588 | `sunDir.z < 0.75` ⚠ |

### H.9 Post-process (`tone.frag`)

| # | Name | Line | Value |
|---|---|---|---|
| 119 | god-ray `decay` | 64 | `0.96` ⚠ |
| 120 | god-ray night exposure | 65 | `0.02` ⚠ |
| 121 | god-ray `density` | 66 | `0.2` ⚠ (ray length) |
| 122 | god-ray `weight` | 67 | `0.58767` ⚠ |
| 123 | `NUM_SAMPLES` | 69 | `100` ⚠ **big perf knob** |
| 124 | god-ray sample scale | 78, 83 | `0.4` ⚠ |
| 125 | god-ray horizon cutoff | 56 | `sunPositionZ > 0` |
| 126 | tonemap exposure | 102 | `0.7` ⚠ |
| 127 | tonemap gamma | 102 | `1 / 2.2` ⚠ |
| 128 | tonemap white point | 101 | `1.0` ⚠ |
| 129 | vignette colour | 106 | `vec3(0.1, 0.05, 0.13)` ⚠ |
| 130 | vignette falloff | 104 | `dot(uv-0.5, uv-0.5)` (no exponent/strength) ⚠ |
| 131 | Uncharted-2 inline coefficients | 43 | `0.15, 0.1*0.5, 0.2*0.02, 0.5, 0.2*0.3, 0.02/0.3` (duplicated from the sky shader) |

### H.10 Scene / camera / window

| # | Name | File:line | Value |
|---|---|---|---|
| 132 | `ONE_DAY` | `Scene.cpp:4` | `30.0` s ⚠ **day length** |
| 133 | `SUN_DISTANCE` | `Scene.cpp:5` | `400000.0` ⚠ |
| 134 | sun azimuth `phi` | `Scene.cpp:23` | `45` (radians — **[BUG]**) ⚠ |
| 135 | window size | `main.cpp:120` | `1920 × 1080` |
| 136 | swapchain image count | `main.cpp:141` | `5` |
| 137 | camera FOV | `Camera.cpp:26,42` | `45°` ⚠ |
| 138 | camera near/far | `Camera.cpp:26` | `0.1` / `100` (far is nonsense for a 400 km sun) ⚠ |
| 139 | camera initial eye | `Camera.cpp:24` | `(0, 450, 30)`, immediately overwritten to `(0, -450, 30)` by `UpdateOrbit(0,0,0)` |
| 140 | camera orbit radius | `Camera.cpp:15,114` | `450` ⚠ |
| 141 | camera target | `Camera.cpp:16` | `(0, 0, 30)` |
| 142 | mouse orbit sensitivity | `main.cpp:100` | `0.5` ⚠ |
| 143 | mouse zoom sensitivity | `main.cpp:109` | `0.05` ⚠ |
| 144 | arrow-key rotate step | `Camera.cpp:120-134` | `5` units of target translation ⚠ |
| 145 | `pixelOffset` cycle length | `Camera.cpp:151` | `16` (dead) |
| 146 | sampler anisotropy | `Image.cpp:215` | `16` |
| 147 | background quad depth | `Model.cpp:10-14` | `0.99` |

### H.11 Nubis1/2 path (`compute.comp`) — for a procedural cloudscape

| # | Name | Line | Value |
|---|---|---|---|
| 148 | `ATMOSPHERE_RADIUS` | 16 | `1200000.0` (and `1500000.0` in `reproject.comp:6`) ⚠ |
| 149 | outer shell factor | 18 | `1.05` ⚠ |
| 150 | `WIND_DIRECTION` | 25 | `vec3(1, 0, 0)` ⚠ |
| 151 | `CLOUD_SPEED` | 26 | `100.0` ⚠ |
| 152 | wind height shear | 100-101 | `height_fraction * vec3(0, 0.1, 0)`, `+ height_fraction * 20` ⚠ |
| 153 | cumulus gradient | 164 | `Remap(h,0,0.2,0,1) * Remap(h,0.7,0.9,1,0)` ⚠ |
| 154 | stratocumulus gradient | 165 | `Remap(h,0,0.2,0,1) * Remap(h,0.2,0.7,1,0)` ⚠ |
| 155 | stratus gradient | 166 | `Remap(h,0,0.1,0,1) * Remap(h,0.2,0.3,1,0)` ⚠ |
| 156 | weather-map UV scale | 176 | `0.00001` ⚠ |
| 157 | low-res shape UVW scale | 181 | `0.00002` ⚠ |
| 158 | base shape remap | 182 | `Remap(r, 0.3, 1.0, 0, 1)` ⚠ |
| 159 | anvil bias remap | 186 | `Remap(h, 0.7, 0.8, 1.0, 0.8)` ⚠ |
| 160 | FBM erosion weights | 188, 202 | `0.625 / 0.25 / 0.125` ⚠ |
| 161 | curl-noise UV scale | 197 | `0.0001` ⚠ |
| 162 | curl displacement scale | 199 | `2.0 * curlStrength` ⚠ |
| 163 | detail shape UVW scale | 201 | `0.0004` ⚠ |
| 164 | detail height flip | 203 | `clamp(height * 10, 0, 1)` ⚠ |
| 165 | 6 light-sample offsets | 211-218 | `(0,.6,0) (0,.5,.05) (.1,.75,0) (.2,2.5,.3) (0,6,0) (-.1,1,-.2)` ⚠ **cone kernel** |
| 166 | Beer modulation | 268 | `max(exp(-d), 0.7*exp(-0.25 d))` ⚠ |
| 167 | in-scatter base | 271 | `0.09 + pow(baseDensity, Remap(h, 0.3, 0.85, 0.5, 2.0))` — **here the height fraction IS used correctly** |
| 168 | HG eccentricity | 288 | `0.2` ⚠ |
| 169 | base step size | 283 | `0.05 * ATMOSPHERE_THICKNESS` ⚠ |
| 170 | hit refinement factor | 311, 326 | `× 0.3` / `÷ 0.3` ⚠ |
| 171 | miss count before coarsening | 324 | `10` ⚠ |
| 172 | density saturation cutoff | 331 | `0.999` ⚠ |
| 173 | horizon fade | 582 | `Remap(-rayDir.z, 0, 0.1, 0, 1)` ⚠ |
| 174 | cloud base colour | 583 | `vec3(0.97, 0.86, 0.8)` ⚠ |
| 175 | ambient contribution | 583 | `0.1 * bgColor * exp(-transmittance)` ⚠ |

**Total catalogued: 175 distinct tunables.** Of these, **13 are exposed in the UI** (2 of them disabled), **~15 are physical constants or genuinely structural**, and **~147 are marked "⚠ should be tunable"** — i.e. hard-coded magic numbers that an ECS `VolumetricCloudComponent` would want to own.

### H.12 Suggested preset groupings for our component

Based on which parameters actually move the look, presets ("clear sky", "storm", "overcast") would need to drive at minimum:

* **Shape:** coverage/profile source, `tiling_freq` (#1), density-scale power (#43), extinction (#79), the cloud-layer gradient selection (#153-155 from the Nubis2 path).
* **Detail:** billow/wisp balance (#35, #36), HHF exponents (#37, #38), erosion FBM weights (#160).
* **Lighting:** `brightness` (#57), the two HG lobes (#55, #56, #58, #168), `_colA`/`_colB` tints (#72, #74) and their ramp knees (#76, #77).
* **Atmosphere:** `sky_turbidity` (#9), `rayleigh` (#93), `mieCoefficient` (#86), `mieDirectionalG` (#87), `sunIntensityFactor` (#97).
* **Animation:** `animate_speed` (#3), wind direction (#24 / #150), height shear (#152).
* **Quality (should be a separate quality tier, not a preset):** near/far threshold (#27), resolution divisor (#28), step coefficient (#26), local sun-march count (#54), light-grid dims (#19, #20), god-ray sample count (#123).

---

## I. Performance & memory budget

### I.1 Measured numbers (all **[README]**, none reproducible here)

`img/performance.png` — bar chart, *"Raymarching optimization"*, Y = Frame Rate (0–120), X = *Camera distance to cloud (m)* with three groups: **500**, **50**, **Inside**. Three series:

| Camera distance | Fixed step = 0.1 | Adaptive step | Adaptive + "temporal" upscaling |
|---|---|---|---|
| 500 m | ~12 | ~60 | ~111 |
| 50 m | ~10 | ~55 | ~109 |
| Inside | ~0.5 | ~24 | ~61 |

(Values read off the chart; the chart has no data labels, so these are ±2 FPS.)

`README.md:180` separately claims *"30 % – 70 % FPS increase"* for the upscaling alone, and `README.md:208` claims the light voxel grid *"reduced the render time by about 30 % - 40 %"*.

`img/demo.png` shows the app running at **FPS = 135.00** in the title bar with **152.3** in the ImGui panel (the two disagree — different smoothing).

Hardware: **NVIDIA GeForce RTX 4070 Laptop GPU** (`img/memory.png` header).

### I.2 Memory

`img/memory.png` is a Nsight/Task-Manager-style strip reading `Memory Utilization (0000:01:00.0 - NVIDIA GeForce RTX 4070 Laptop GPU 0)` with `Local: (Y axis 306.60 MiB)` and `NonLocal: (Y axis 306.60 MiB)`. That is an **axis label, not a measurement** — the graph is a rising step curve with no numeric readout. Treat 306.6 MiB as "peak local memory during the capture".

My own computed breakdown from the code:

| Resource | Dimensions | Format | Bytes | MiB |
|---|---|---|---|---|
| modeling NVDF ×2 | 512×512×64 | R8G8B8A8 | 2 × 67,108,864 | **128.0** |
| detail noise | 128×128×128 | R8G8B8A8 | 8,388,608 | 8.0 |
| lowResCloudShape | 128×128×128 | R8G8B8A8 | 8,388,608 | 8.0 |
| hiResCloudShape | 32×32×32 | R8G8B8A8 | 131,072 | 0.125 |
| weather map | 512×512 | R8G8B8A8 | 1,048,576 | 1.0 |
| curl noise | 128×128 | R8G8B8A8 | 65,536 | 0.0625 |
| light grid | 256×256×32 | R32G32B32A32 | 33,554,432 | **32.0** |
| `imageCurTexture` | 1920×1080 | R32G32B32A32 | 33,177,600 | **31.6** |
| near colour + density | 2 × 960×540 | R32G32B32A32 | 2 × 8,294,400 | 15.8 |
| depth | 1920×1080 | D32_SFLOAT | 8,294,400 | 7.9 |
| swapchain | 5 × 1920×1080 | B8G8R8A8 | 41,472,000 | 39.6 |
| **Total** | | | | **≈ 272 MiB** |

Plus staging buffers, ImGui, and the driver's own allocations — consistent with the reported 306.6 MiB.

**Dominant costs:** the two 64 MiB uncompressed modeling textures (47 % of the budget) and the 32 MiB RGBA32F light grid, which uses 4 channels to store 2 values.

Immediate wins for us: BC4/BC7 or `R8` per-channel for the modeling data (the Nubis3 slide says BC6 at 1 byte/texel → 16.8 MB, a **4× saving**); `R16G16_SFLOAT` for the light grid (32 → 4 MiB, an **8× saving**); `R11G11B10_UFLOAT` or `RGBA16F` for the scene targets.

### I.3 What dominates the frame

Not measured in the repo, but inferable from the code:

1. **Far-cloud pass at full resolution.** 1920×1080 rays, each with an unbounded `while` loop, and each *density* sample triggering a `GetDensityToSun` that does 2 more full density evaluations (each of which is another 3D texture fetch chain) plus a light-grid fetch. So each shaded step costs ≈ 4 modeling fetches + 3 noise fetches + 1 grid fetch.
2. **God rays: 100 full-resolution texture taps per pixel**, unconditionally, when enabled (`tone.frag:69,80-87`). This is very likely the second-largest cost and it is trivially optimisable (half res + bilateral upsample, 32 taps).
3. **Light grid rebuild every frame**: 2 M voxels, each marching up to ~256 steps in the worst case with no early termination other than leaving the box. Worst case ~500 M texture fetches. In practice the `density <= 0` early-out (`lightGrid.comp:87`) kills most of them.
4. **Near pass** at quarter the pixels but with the *densest* part of the volume.

**Resolution dependence:** the far pass and the god rays are linear in output pixels; the near pass is linear in output pixels / 4; the light grid is resolution-independent. So going 1080p → 4K roughly quadruples passes 2/3/4 and leaves pass 1 flat.

---

## J. Risks and open questions for porting into DesertEngine

### J.1 Things that will NOT port cleanly

**Risk 1 — The cloud shapes are not ours and are not procedural. (Severity: blocking)**
The entire Nubis3 look depends on two 512×512×64 NVDF textures baked from Guerrilla's internal Houdini tools and explicitly described as *"provided by Nubis3's team"* (`README.md:123`). We cannot ship them. We have three options:
  (a) build our own Houdini/VDB authoring path (weeks of DCC work, plus an OpenVDB import dependency);
  (b) generate the dimensional profile procedurally from the *envelope method* in `img/envelope.png` (`bottom_gradient * top_gradient * edge_gradient`) driven by a coverage/type weather map — i.e. go back to **Nubis2**;
  (c) hybrid: procedural cloudscape (Nubis2) with optional baked hero clouds (Nubis3).
  **Recommendation: (c), starting with (b).** The Nubis2 code in `compute.comp` is the more valuable half of this repo for us, even though it is the "dead" path here.

**Risk 2 — Coordinate conventions are inconsistent and partly wrong. (Severity: high)**
Z-up for clouds vs `UP = (0,0,-1)` for the sky (`computeNubisCubed.comp:281-285` vs `:529`); the `1 - coord` mirror in `GetSampleCoord` (`:167-175`); the light grid marching *away* from the sun (see B.1); the `x*512*64 + z*64 + y` y/z swap in the VDB loader (`vdb.cpp:639`); `phi = 45` radians (`Scene.cpp:23`); `abs()` in the slab test (`:393`). DesertEngine is **Y-up with 1 unit = 1 cm** (see the engine memory notes) — the world scale alone is a 100× difference from this project's implicit metres. **Every distance constant in section H must be rescaled by 100** if we keep centimetres, or we introduce a per-cloud world-to-cloud-space transform (which is the right answer anyway, since #14/#15 must become an entity transform).

**Risk 3 — The lighting model has at least three functional defects. (Severity: high)**
`eccentricity = 1.0` zeroes the forward HG lobe (#58); `Remap(0.5, …)` replaces the height fraction with a constant in both in-scatter terms (#64, #66); `mTransmittance` is used as both an early-out and a light accumulator (C.3). If we "port faithfully" we inherit a look that is accidental. We must reimplement the Nubis2/3 *formulas from the slides*, not from this code, and use this code only to see how the pieces are wired together. Expect the port to look *different* — and better.

**Risk 4 — No temporal anything, and no synchronisation. (Severity: medium-high)**
Section D: there is no reprojection to port. Section B.0: there are no barriers between passes and no compute↔graphics sync. In our render-graph engine, the pass dependencies will have to be declared from scratch. On the plus side, our render graph gives us that for free — but it also means the reference gives us no guidance on where the barriers belong. Note the existing engine constraint: *one SceneRenderer per frame*; per-frame cloud state must live in the shared parent material, not in a second live renderer.

**Risk 5 — Cost/quality is calibrated for one hero cloud on an empty sky. (Severity: medium)**
`farclip` defaults to 700 units and the far pass therefore only covers 500–700 units. The SDF empty-space skip is what makes the whole thing affordable, and the SDF only exists because the shape is baked. A procedural cloudscape has no SDF, so we would need either a coarse occupancy volume (a "cloud min-max mip") or the Nubis2 miss-counting heuristic (`compute.comp:322-328`). Budget for that separately; the FPS numbers in `img/performance.png` do **not** transfer.

### J.2 Licensing / attribution

| Item | Status | Action |
|---|---|---|
| `LICENSE` at repo root | MIT, `Copyright (c) 2014-2026 Y0MMY` — this does not match the README's stated authors and appears to be a boilerplate carry-over | do not rely on it |
| `src/vdb/*` (`vdb.h/.cpp`, `Grid`, `BoundBox`, `Types.h`, `Plane.h`, `Utilities`) | **GPL-3, Copyright (C) 2014 Callum James** (`vdb.h:3-18`, `Types.h:2-17`, `Grid.h:1-16`) | **Do not copy any of it.** GPL-3 inside an MIT repo is already a licence conflict in the source project. If we ever need VDB import, write it against the OpenVDB API directly (OpenVDB is MPL-2.0). |
| `.vdb` assets and derived `.tga` slices | Guerrilla Games internal tools, no licence given | **Do not redistribute.** |
| `img/noise.png`, `profile.png`, `readmeref1.png`, `modelprocess.png`, `envelope.png` | verbatim SIGGRAPH 2023 / Guerrilla slides | reference only |
| `src/ImGui/*`, `external/GLFW`, `external/glm`, `external/stb` | standard third-party, we already have our own copies | ignore |
| The shader maths | The *formulas* are from published SIGGRAPH talks (Schneider, Nubis 1/2/3) and are freely reimplementable. The *specific magic constants* in section H are this project's tuning. | Reimplement the formulas; treat the constants as starting values, and cite the Nubis talks in our docs. |

**Bottom line on copying:** we should reimplement, not port. Nothing in this repo is safe to copy verbatim except ideas.

### J.3 Confirmed defects (consolidated)

| # | Defect | Location | Impact |
|---|---|---|---|
| 1 | Near→far density handoff reads `.a` (always 0) instead of `.r` | `farCloud.comp:685` vs `nearCloud.comp:690` | near-cloud density lost across the split |
| 2 | Light grid marches away from the sun | `lightGrid.comp:93-99` + `computeNubisCubed.comp:173` | self-shadowing comes from the wrong side |
| 3 | `HG(cos, 1.0)` ≡ 0 — forward lobe missing | `computeNubisCubed.comp:339, 352` | no silver lining |
| 4 | `Remap(0.5, …)` instead of `Remap(height_fraction, …)` (×2) | `:347-348` | no vertical in-scatter gradient; `height_fraction` (`:328`) unused |
| 5 | `mTransmittance` used as both loop guard and accumulator | `:373` vs `:455` | transmittance early-out never fires |
| 6 | `abs()` in the AABB slab test | `:393, 400, 410, 417, 427, 434` | rays can "hit" a box behind the camera |
| 7 | `prevCameraBufferInfo` reused for bindings 1 and 2; `paramBufferInfo` written and discarded | `Descriptor.cpp:388-427` | binding 1 (prev camera) actually points at the camera-param buffer. Harmless today only because binding 1 is unused. |
| 8 | `GenerateVDBSlice` truncates `float` in `[0,1]` to `unsigned char` | `Image.cpp:619-622` | VDB import produces all-zero textures (path is disabled, so latent) |
| 9 | `phi = 45` treated as radians | `Scene.cpp:23` | arbitrary sun azimuth |
| 10 | No barriers between compute dispatches; no compute↔graphics sync | `Renderer.cpp:366-404, 588-627` | races |
| 11 | `mipmap_level` computed then discarded; `maxLod = 0` everywhere | `:161-165, 203-206`; `Image.cpp:224` | no mip chain, so no distance filtering |
| 12 | `DENSITY_SCALE 0.01` defined and never used | `:29` | dead knob that should be the extinction scale |
| 13 | Double tonemapping (sky model + `tone.frag`) | `:631-651`, `tone.frag:102` | crushed highlights, non-linear compositing |
| 14 | `sunIntensityFalloffSteepness` 6.4 vs 1.4, `sunHDR` 1900 vs 19000, `ATMOSPHERE_RADIUS` 1.2e6 vs 1.5e6 | across `computeNubisCubed.comp`, `compute.comp`, `reproject.comp` | the three shaders' sky models are not consistent with each other |

### J.4 Open questions

1. **What does `field_data` contain?** Both example directories ship 64 slices of it and the sampler was declared (`computeNubisCubed.comp:59-60`) and then commented out, along with its descriptor (`Descriptor.cpp:136-141, 575-583`). The Nubis3 talk mentions "Field Data NVDFs" but this project never used them. Unknown whether they hold velocity, temperature, or a second detail set. **Not specified in source.**
2. **Why is the model mirrored (`1 - coord`)?** No comment explains it. It may compensate for the VDB loader's `x*512*64 + z*64 + y` indexing (`vdb.cpp:639`). If we re-author the data we should drop the mirror entirely.
3. **What is the world-unit scale?** The README says "meter" (`README.md:180`, `img/performance.png` X axis) and the box is 2048 × 2048 × 256, which is plausible for one cumulonimbus. But nothing in the code declares a unit. **Not specified in source.**
4. **Were the FPS numbers taken at 1920×1080?** `main.cpp:120` sets that, but the chart does not say, and `img/upscaling.png` draws 540×960 / 270×480 tiles, which matches neither. **Not specified in source.**
5. **How was the light-grid "30–40 % render time reduction" measured?** No before/after code path exists in the repo to reproduce it.
6. **`img/near.gif`, `img/cloud_short.gif`, `img/cloud_day_night.gif` could not be read** (24 MB / 59 MB / 95 MB, all above the image-reading limit). I make no claim about their contents. If they matter, someone should open them manually.

### J.5 What I would actually take from this project

1. The **pass decomposition**: light-grid bake → near (low-res) → far (full-res, composites near) → post. That shape is sound and maps cleanly onto our render graph.
2. The **light voxel grid** concept — pre-integrating density toward the sun into a coarse 3D texture — is the single best idea here, and it is cheap. Fix the direction and the missing step-length weighting.
3. The **SDF channel for empty-space skipping**, if we ever bake volumes.
4. `GetCloudLayerDensity` (`compute.comp:161-171`) and the envelope construction from `img/envelope.png` — the procedural profile we will actually need.
5. The **detail erosion composition** in `GetUprezzedVoxelCloudDensity` (`:197-244`) — the wispy/billowy blend driven by a Detail Type channel is genuinely good and portable.
6. The **`_colA`/`_colB` two-tint cloud colour ramp** (`:359-371`) — art-directable, cheap, and it is what makes the screenshots look like clouds rather than grey fog.
7. The **Preetham sky** as a starting point, with the `UP` convention fixed and the tonemapping removed from the sky itself.

Everything else — the frame flow, the synchronisation, the reprojection, the specific lighting constants, and all of `src/vdb/` — should be written fresh.

---

## Appendix: image inventory

Every file in `img/`, with what it actually shows.

| File | Referenced by README | Content |
|---|---|---|
| `pipe.png` | yes | Pipeline block diagram. Yellow "Inputs" box: Mesh → Atlas Tool → Modeling Data; Noise Generator → 3D Noise (4 greyscale swatches); Light Information. Then Light Voxel → {Near Cloud Raymarching → Far Cloud Raymarching} → Integrate Lighting into Pixel → Post Process → rendered cloud. Matches the code (B.6). |
| `step_size.png` | yes | Dark schematic: camera at right, horizontal view ray to the left through a grey cloud silhouette. Large open circles mark long strides outside the cloud, tight red filled dots mark dense sampling inside. Dashed lines run up-left toward the sun. Illustrates SDF-driven adaptive stepping (C.3). |
| `upscaling.png` | yes | Dark schematic: camera at right, a "200 m" bracket, a near tile labelled 270 px × 480 px and a far tile labelled 540 px × 960 px. **Conflicts with the code's 500-unit threshold** (D.2). |
| `grid.png` | yes | Light-grid schematic: sun upper-left, wireframe box labelled 256 Voxels × 256 Voxels × 32 Voxels, two grey clouds inside, red sample dots along lines toward the sun. |
| `light_voxel_grid.png` | yes | Photograph of a monitor showing a chunky low-res voxelised cloud fragment (cream/white on pale blue) with a faint shadow streak. Low information content. |
| `modelprocess.png` | yes | Nubis slide: "Voxel Cloud Modeling — Grow Clouds using Fluid Simulations / Edit and composite them into 'Frankencloudscapes.' / Store them in a voxel grid to be sampled at render time." |
| `readmeref1.png` | yes | SIGGRAPH 2023 slide "Nubis³ / Voxel Clouds / Sampling Density": Modeling Data / NVDF's / 512×512×64 / BC6, 1 Byte/Texel / 16.777 Mb, three planes labelled Dimensional Profile, Detail Type, Density Scale. |
| `noise.png` | yes | Nubis slide "Voxel Cloud Noise / 4 Channel / 128×128×128 Voxels / Uncompressed, 2 Bytes/Texel / 4.194 Megabytes" with swatches: Low/High Freq "Curly-Alligator", Low/High Freq Alligator. |
| `profile.png` | **no** | Nubis slide comparing "Vertical Profile Method" (cloudscape) vs "Envelope Method" (single flat cloud), captioned `cloud_density = saturate(noise - (1.0 - dimensional_profile));`. |
| `envelope.png` | **no** | Nubis slide: a dome cross-section annotated `min_height`, `max_height`, `top_gradient`, `bottom_gradient`, `edge_gradient`, with the five-line GLSL construction of `dimensional_profile` (quoted in full in section C.7). **The most directly useful reference image in the repo.** |
| `profile_shape.png` | yes | Render: a completely smooth, blown-out white cloud on a slate-blue-to-grey sky gradient — the profile-only pass before detail noise. |
| `profile_sample.png` | **no** | Render: interior view of a cloud layer, very smooth low-frequency grey/tan/pink gradients, a V-shaped gap of sky in the centre, scattered single-pixel dark speckles. |
| `detail.png` | yes | Render: same cloud/sky as `profile_shape.png` but with a crisp, detailed puffy silhouette and a flat over-exposed white interior — the detail-noise pass. |
| `houdinivdb.png` | **no** | DCC viewport (Houdini): orange wireframe bound box around a grey, visibly stair-stepped voxel mesh of a cumulonimbus. The source asset. |
| `demo.png` | yes | App screenshot, title `Vulkan Cloud Rendering  FPS = 135.00`. Full Control Panel visible — all default values transcribed in section H.1. Panel shows `Current Frame Rate: 152.3`, Nubis 3 selected, Stormbird Cloud selected. |
| `1.png` | **no** | Render: single detailed cumulus, near-white top, grey base, fine cauliflower silhouette, a small circular dimple in the upper body, faint diagonal light shafts. |
| `2.png` | yes | Render: taller cumulus centre-right with a trailing tail of detached puffs; visible blocky/banded facets inside the lit body (coarse stepping artefact). |
| `cloud.png` | **no** | App screenshot, no UI panel. Wide cloudscape on saturated blue; many small angular white shard artefacts through the mid-sky — undersampled ray-march / noise-tiling. |
| `cloud1125.png` | **no** | App screenshot. Screen filled by a flat, cartoon-like cloud layer with only a few holes of uniform light-blue sky; much lower detail than the other renders. |
| `cloudd.png` | **no** | Render: single lit cumulus, with a visible regular grid/tile pattern and repeated round bumps across the body — detail-noise tiling artefact. |
| `sky.png` | yes | Render: backlit cloud on a grey-blue-to-tan dusk gradient, sun disc at the cloud's left shoulder, very low contrast, heavy haze. |
| `sky_night.png` | yes | Render: dark warm-brown cloud masses with stars; a hard horizontal seam at ~70 % height below which the sky is flat dark teal with stars drawn over it. Artefact, not intentional. |
| `god_ray.png` | yes | Render with a collapsed `▶ Control Panel` bar. Near-monochrome cloudscape, entire lower half washed out to uniform bright white by the god-ray pass. |
| `am.png` | yes | Render with ambient scattering ON: soft creamy cumulus, dark ragged base tendrils, faint tiling seams visible. |
| `am_off.png` | yes | Same view with ambient OFF: markedly flatter and washed out. Carries a hand-drawn orange sun doodle added by the authors, plus a white strip along the top edge. |
| `b1.png` | yes (blooper) | Three-panel debug/failure comparison: (L) cloud filled with blocky orange/yellow/red/white garbage values; (M) black cloud with a bright white rim and red/green contour lines; (R) black cloud with cyan blobs and vertical yellow/green/magenta stripes. NaN/uninitialised-memory captures. |
| `b2.png` | yes (blooper) | Four-panel: (TL) flat beige with crawling worm-like noise; (TR) cloud with obvious blocky voxel stair-stepping; (BL) white cloud with solid black holes punched into it; (BR) interior view with a smooth grey funnel descending from the top. |
| `memory.png` | yes | Nsight-style strip: `Memory Utilization (0000:01:00.0 - NVIDIA GeForce RTX 4070 Laptop GPU 0)`, `Local: (Y axis 306.60 MiB)`, `NonLocal: (Y axis 306.60 MiB)`. The 306.60 is an **axis label**, not a labelled measurement. |
| `performance.png` | yes | Bar chart "Raymarching optimization", Y = Frame Rate (0–120), X = Camera distance to cloud (m): 500 / 50 / Inside. Series: Fixed Step Size = 0.1 (orange), Adaptive Step Size (yellow), Adaptive Step Size + Temperal Upscaling (green). Values in section I.1. |
| `near.gif` | yes | **Could not be read** — 24.4 MB, above the image-reading limit. No claim made about its contents. |
| `cloud_short.gif` | **no** | **Could not be read** — 59.1 MB. No claim made. |
| `cloud_day_night.gif` | **no** | **Could not be read** — 95.6 MB. No claim made. |

## K. Addendum, 2026-08-11 — permission, and the decision it did not change

**Permission.** One of the three authors (Janet Wang / `YueZhang1027`) granted use of the code in
writing, including substantial verbatim copying, with attribution waived. That grant was re-examined
against the repository rather than taken at face value, and two facts bound it:

1. **`github.com/YueZhang1027/CIS5650-Final-Project-Frostnova` carries NO licence file** (GitHub API,
   `license: null`). The MIT `LICENSE` described in J.2 exists only in the local working copy this
   document was written from and names *our* copyright holder — a boilerplate carry-over, as J.2 already
   said. Upstream, the default applies: all rights reserved.
2. **The repository has three contributors** — `YueZhang1027` (75 commits), `xinyuniu123` (45),
   `xchennnw` (44) — matching the three authors named in A.1. A grant from one covers one author's
   contributions; the other two remain copyright holders of theirs, and in a project of this size the
   contributions are interleaved rather than separable by directory. `src/vdb/*` adds a fourth party
   (GPL-3, Callum James, J.2) whom nobody in the team can license to us at all.

**Decision: unchanged from J.2 — reimplement, not port.** Not primarily for the licensing, but because
the port does not buy what it would cost. Re-verified against the repository today:

* **The two defects that prompted this pass are not fixable from here.** The flat-white clouds were our
  tonemapper (see below), and the edge flicker is temporal — and section D still holds: `reproject.comp`
  is a stub with its body commented out. There is no reprojection in this repository to take.
* **Our raymarch is already at or past its lighting model.** `CloudRaymarch.shader` integrates
  multi-scattered Beer, a dual-lobe phase with silver lining, powder, a sun cone march, sky/ground
  ambient and aerial perspective — without defects 3, 4 and 5 of J.3, which the reference has.
* **Its shape model is baked, ours is procedural.** Taking it means ~190 MB of `src/images/vdb` +
  `src/images/noise` — assets Guerrilla's tools produced, which J.2 already marks *do not redistribute* —
  into a public repository, in exchange for losing an infinite procedural cloudscape.

J.5 stands unchanged as the list of what is still worth taking, and every item on it is an idea to be
built, not a file to be copied.

**What was actually fixed, same day (commit `clouds: the tonemapper that was not one…`):**

1. `SceneComposite.shader` hard-coded the extended-Reinhard white point to 1.0, at which the operator
   reduces algebraically to the identity: nothing was tonemapped and every luminance above 1 clipped at
   the 8-bit store. That, and not the cloud model, is what rendered lit clouds as flat white cut-outs.
   The white point is now `SceneSettings::WhitePoint` (default 4.0).
   Note the symmetry with J.3 defect 13: the reference double-tonemaps, we did not tonemap at all.
2. `CloudTemporalResolve` gave a pixel with no usable history the current frame bit for bit — a raw
   jittered half-resolution sample beside an interior averaging ten frames, along the whole screen edge
   the camera turns toward. It now resolves to the 3×3 mean of the current frame.

## L. Parity audit, 2026-08-12 — what we ended up with, checked against the reference

Asked directly: did we reimplement this one-to-one? **No, and in most places deliberately not.** Read
against the working copy at `/Users/daniilsavcenko/Desktop/Programming/C++/myptoject`, element by element.

### L.1 Present on both sides, ours equal or ahead

| Element | Reference | Ours |
|---|---|---|
| Vertical envelope by cloud type | `GetCloudLayerDensity` (`compute.comp:161-172`), three profiles hard-coded in the shader | `CloudHeightGradient` — the same three, but AUTHORED per preset as four-component gradients plus base/top powers |
| Weather map composition | `GetBaseDensity` (`:174-193`): r = coverage, b = type | `CloudDensityCheap`: r = coverage, g = type, b = precipitation, a = density scale |
| Shape erosion fbm | `0.625*g + 0.25*b + 0.125*w` (`:187`) | identical weights, `CloudDensityProcedural.glslh` |
| Coverage / anvil | `pow(coverage, ValueRemap(height, 0.7, 0.8, 1.0, 0.8))` (`:185`) | `CloudAnvilCoverage`, same shape, the 0.5 endpoint exposed as Anvil Bias |
| Curl warp at the cloud base | `GetDetailDensity` (`:195-210`) | same, `curl * (1 - heightFraction)` |
| Wind skew with height | `SkewSamplePointWithWind` (`:97`) | `CloudShapeSamplePos` / `CloudDetailSamplePos`, plus an uplift term |
| Sun cone march | six hard-coded offsets rotated into a sun basis (`:211-240`) | `CloudConeSampleOffset` — golden-angle spiral, count authored, and a basis that does not degenerate near the poles |
| Phase function | single HG, and `eccentricity = 1.0` makes it identically ZERO (J.3 #3) | dual-lobe HG with a silver-lining term |
| In-scatter probability | `Remap(0.5, …)` twice — the height fraction replaced by a constant (J.3 #4) | the real height fraction |
| Beer / powder | both present | both, with powder strength and scale authored |
| Multiple scattering | none in the Nubis2 path | `CloudMultiScatter`, octaves with extinction/scatter/phase falloff |
| Step integration | `mTransmittance += full * light_energy * alpha` (`computeNubisCubed.comp:370`) | `CloudIntegrateInScatter` — Hillaire's analytic form, bounded by its source and tested |
| Temporal | `reproject.comp` is a stub, body commented out (section D) | reprojection through the shell, neighbourhood clamp, mean fallback with no history |
| Sky | Preetham, tonemapped INSIDE the sky (J.3 #13), `UP` convention inconsistent with the cloud code | one shared atmosphere model, evaluated in the ray's own direction, tonemapped once at the end |
| Empty-space skip | baked SDF channel (Nubis3) / miss counting (Nubis2) | coarse/fine tier with miss counting — no bake required |

### L.2 Present there, absent here

1. **The two-tint cloud colour ramp** (`computeNubisCubed.comp:356-371`, J.5 #6) — **the one gap that shows.**
   The reference never lets a cloud be white: it ramps between `_colB`, a cool blue anchored to the sky
   (`0.23, 0.36, 0.47`), and `_colA`, a warm cream (`1.0, 0.87, 0.65`), both mixed toward the sky colour by
   sun elevation. We have the knobs for this — `ScatteringAlbedo`, `SunTint`, `ShadowTint` — and **every
   preset leaves all three at pure white**, so our only colour comes from the sun and the sky ambient.
   With the numbers the Cirrus scene actually carries, that is `sunColour = SunColor x SunIntensity x
   SunLightIntensityScale = (24.2, 23.2, 21.3)` against `ambient ~ (0.05, 0.14, 0.36)` — a ratio of about
   **160:1**. A lit face therefore saturates while a shaded one falls to nearly black, which is precisely
   the "white texture, correct shape" reading. The reference avoids the problem by never using physical
   magnitudes at all.
2. **The light voxel grid** (J.5 #2) — we cone-march per shaded sample instead. Costlier, and correct: the
   reference's grid marches AWAY from the sun (J.3 #2).
3. **God rays** — a radial blur in `tone.frag:54-90`. We have no equivalent.
4. **Star field / night sky** — `GetStarColor` (`compute.comp:471`). Absent here.
5. **Baked NVDF hero clouds** — out by decision, see section K.

### L.3 What to do about L.2.1

Ours is the more physical model and it is not the thing to throw away; what is missing is that nobody has
ever authored the colour. Three levers, in the order worth trying:

* **White Point 4 -> 8** (Scene Settings). A lit cloud lands around 5-7 with the radiance fix in; a white
  point of 4 still clips its core, 8 fits the whole range and keeps the gradient.
* **Ambient Sky Contribution up.** 160:1 is not what a cloud's shaded side does in life — the sky dome is
  a large source. Raising it lifts shadows out of near-black without touching the lit side.
* **Then, and only then, the tints.** `ShadowTint` toward the reference's cool blue and `SunTint` toward
  its warm cream reproduce its look through knobs we already have — as art direction, on top of a model
  that is right, rather than in place of one.
