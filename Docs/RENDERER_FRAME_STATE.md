# Per-frame scene state lives in the shared material — and must not

## The problem, precisely

`MaterialPBRBase::UpdateCamera / UpdateLights / UpdateShadow / UpdateEnvironment` all write through
`instance->GetParentMaterial()`. That parent is **one object per shader** — every mesh drawn with
`StaticMeshPBR` shares it — which is why `MeshRenderer` writes it *once per batch group* rather than per
object, and why the writes look cheap.

The buffers behind it are single mapped allocations: `UniformBufferProperty::SetRawData` writes at offset
0 of one `m_Buffer` and marks it dirty. A Vulkan descriptor set **references** that buffer; it does not
copy it. So the value the GPU reads is whatever was written last before the queue executed the frame —
not what was set when the draw was recorded.

Consequences, all observed:

* **Two renderers in one frame overwrite each other.** The Details panel's live preview ran with shadows
  disabled, so every frame it wrote `enabled = 0` and identity cascade matrices into the shared material
  and the viewport lost its shadows. Fixed by removing the second renderer (`0b510b6`), not by fixing
  this.
* **Multi-scene editing has the same flaw today**: each scene view owns a `SceneRenderer`, and they share
  every material. It is visible as views borrowing each other's camera/light state.
* Per-OBJECT values avoid the problem only because they travel as push constants.

`SceneRenderer::BeginScene` logs a one-time warning when a second renderer draws the same frame. It does
not refuse — multi-view would break — but nothing about this should be silent.

## What the code actually looks like (checked, 2026-08-06)

Facts that decide how the fix has to be built — all verified in the source, not assumed:

* **Reflection already handles multiple sets.** `VulkanShader` reads
  `spv::DecorationDescriptorSet` and fills `m_ReflectionData.ShaderDescriptorSets[set]`, and
  `GetDescriptorSetLayout(set)` exists. Shaders simply declare no `set = N` today, so everything lands in
  set 0.
* **The MATERIAL owns every set.** `VulkanMaterialBackend::AllocateDescriptorSets()` allocates
  `framesInFlight × setCount` sets from the material's own pool, and `BindDescriptorSets()` binds all of
  them starting at `firstSet = 0`. Nothing else in the engine owns a descriptor set for a graphics
  pipeline.
* **Buffers are created from shader reflection, by shader.** `ShaderResourcesManager(debugName, shader)`
  builds every UniformBuffer/StorageBuffer/sampler the shader declares; `UniformBuffer::Create` is private
  to it. There is currently no way to own a shader-declared buffer *outside* a material.
* Sets are already per-frame-in-flight, so the frame dimension exists; only the OWNER dimension is missing.

So the missing concept is "a descriptor set owned by something that is not a material". That is the whole
job — the rest (writing the payload) is already centralised in `MeshRenderer::FrameState`.

Two shapes are possible, and the second is cheaper than this document originally assumed:

**A. Split the sets (this document's plan).** Frame blocks move to `set = 1`; the material backend stops
allocating/binding that set; the renderer allocates it against `VulkanShader::GetDescriptorSetLayout(1)`
and binds with `firstSet = 1`. Clean and standard. Touches every lit shader.

**B. Give the existing set an OWNER dimension.** Keep one set and the shaders exactly as they are; make
the backend allocate `framesInFlight × renderers` sets and the frame-scoped UniformBuffers hold one buffer
per renderer slot; the renderer passes its slot when binding. No shader edits, no layout changes — but it
duplicates the whole material set (textures included) per renderer, so the memory cost scales with
materials × renderers, and every bind site grows a slot argument.

A is the right long-term shape; B is the smaller change if the only goal is correctness for 2-3 views.

## The fix

Move frame-scoped state out of the material and into a **per-renderer descriptor set** bound once per
pass, with the material keeping only what is genuinely per-material.

1. **Define the frame set.** One UBO block (camera + lights + shadow + IBL handles) owned by the
   `SceneRenderer`, one instance per frame-in-flight. Bound at `set = 0`; materials move to `set = 1`.
2. **Shaders**: `Editor/Resources/Shaders/Programs/**` — move `Camera`, `LightsMetadata`, `PointLights`,
   `SpotLights`, `DirectionLights`, `ShadowUB`, `u_ShadowMap*`, `u_Env*` and `u_BRDFLUTTexture` into the
   frame set. This is mechanical but touches every lit shader.
3. **Reflection / layout**: `DShaderTool` + `VulkanShader` build descriptor layouts from reflection, so
   they must keep set 0 free for the renderer and stop folding those blocks into the material's layout.
4. **Renderers**: `MeshRenderer`, the deferred lighting pass, terrain, particles and the skybox bind the
   frame set at the start of their passes; the `MaterialPBRBase::Update*` helpers become
   `SceneRenderer::UpdateFrameState( ... )` and stop taking a `MaterialInstance*`.
5. **Delete the ownership warning** above, and re-enable the Details live preview
   (`git show 3744d4a^:Editor/Source/Editor/Widgets/PreviewViewport.cpp`) as the proof that it works: two
   renderers, two pictures, neither disturbing the other.

Order matters: 1–3 can land behind the existing behaviour (the frame set can be written *and* the old
material path kept) so the switch in 4 is a single, revertible commit.

## What it buys

The Details live preview, correct multi-scene editing, asset thumbnails rendered on demand without
disturbing the viewport, and any future picture-in-picture (a camera preview, a render-target view).
