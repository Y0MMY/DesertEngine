# The per-slot material route, fixed

Stage 1 proved the shader-graph path reaches a mesh and disproved that it is usable: a graph material
with any parameter rendered black on two frames in three, and a graph that failed to compile killed the
editor. `STAGE1_END_TO_END.md` is the evidence for both. This is what was done about them.

## 1. The strobe

### What was wrong

`UniformBufferProperty::UpdateFields()` copies a field's CPU-side local data into the uniform buffer,
and `VulkanUniformBuffer::SetData` resolves **one** copy — the one belonging to the (frame in flight ×
renderer slot) pair that is recording. There are three frame copies. One call serves one of them.

The per-slot dirty counter (`PropertyDirty::DirtyLifetime()`) is standing permission to keep writing
until every copy has been served. Permission is not the write. Somebody has to come back on the
following frames and perform it, and for generic draws nobody did:

| route | who applied the parameters | who flushed on later frames |
|---|---|---|
| `MaterialComponent` shader override | `MeshRenderer`, every draw | nobody — but `SetParamRaw` calls `UpdateFields` as a side effect, so every frame flushed by accident |
| `.demat` per-slot material | `MaterialFactory::ApplyShaderAsset`, once, when the asset loads | **nobody** |

Materials bound through a `MaterialInstance` get the flush from `Material::Bind`. Generic draws submit
a `MaterialExecutor` and never call `Bind`, so the per-slot route — the only route a shader graph can
take — wrote one copy at load time and never again. The other two copies stayed as the allocator left
them, and `VulkanUniformBuffer::RT_Invalidate` memsets its mappings to zero. Zero albedo is black.

### What changed

Every generic draw now ends in `DataDrivenMaterial::FlushParameterBuffers()`
(`MeshRenderer.cpp`, end of the `m_GenericQueue` loop). Unconditional, and self-limiting: once every
copy has been served the fields go clean and it does nothing.

The branch that remains above it chooses the **source** of the values, not whether they reach the GPU:

* the shader-override material is keyed by shader name in `m_GenericMaterials`, so several entities
  share one object within a frame and each draw must restate its own values;
* a per-slot material **is** the asset, and restating would overwrite it with schema defaults.

That distinction is real and is named at the call site. What is no longer conditional is the flush.

### The version of this fix that was wrong, and why it is worth writing down

The first attempt flushed **every** uniform buffer the material owned, reusing the loop inside
`Material::Bind`. The probe scene then rendered as bare sky — no spheres, no ground.

`CameraUB`, `TimeUB` and `DirectionLightsUB` are engine-filled: `MeshRenderer` writes them whole with
`UniformBufferProperty::SetRawData`, which memcpys into the mapped buffer and **never touches
`FieldProperty`**. Their field local data is therefore never initialised — `Buffer::Allocate` is a bare
`new std::byte[size]` — and every field is dirty from construction. Flushing them copied uninitialised
bytes over the camera matrices the renderer had just written, one draw after writing them.

So a uniform buffer in this engine has two possible sources of truth and no marker saying which one it
is on. `FlushParameterBuffers` only touches the buffers the schema's parameters map to, collected in
`ApplyDefaults`. **This is a trap for the next person and the comment says so at the point of use.**

### Proof

`Editor/Resources/Assets/Scenes/MAT_Probe.desce`, twelve consecutive frames, identical arguments before
and after, `--shot-every 1`. Frames 1–2 are startup; the steady state is frames 3–12.

```
scripts/MacOS/RunEditor.sh Debug --project Desert.deproj \
    --scene Resources/Assets/Scenes/MAT_Probe.desce \
    --shot <out>.png --shot-sequence <dir> --shot-every 1 --shot-frames 12 \
    --camera 100,60,900 --look 0,-0.02,-1
```

Frames 3–12, counted by sha256 rather than by eye — one frame cannot show a period-3 strobe:

| | distinct hashes among the ten steady frames |
|---|---|
| before | **two**: `021f2fbe…` ×3 (correct) and `61f46b7f…` ×7 (black) — period 3, which is `MaxFramesInFlight` |
| after | **one**: `021f2fbe…` ×10 |

The single after-hash **is** the before-run's correct hash. So the fix did not merely stop the flicker,
it made every frame byte-identical to the frame that was already right — nothing else about the picture
moved. That is also why no "after" frame is committed: it would be a byte-for-byte duplicate of
`Shots/MAT_params_correct_frame5.png`, which is already here. The two before-frames are
`Shots/MAT_params_correct_frame5.png` and `Shots/MAT_params_black_frame6.png` — consecutive, nothing
moving, three spheres flipping to black.

`Starter.desce` renders **byte-identical to the pre-fix binary** (sha256
`bd51a908fecc328231f22ad0f856ea5eee004c867b285d5f39c9038c2917bffb`), which is the guard that a scene
with no graph materials is untouched.

## 2. The crash on a shader that did not compile

`VulkanShader::CompileProgram` is transactional, so a failed **recompile** already keeps the previous
working modules — hot reload was never the problem. A shader whose **first** compile fails is a
different story: it is still constructed (the constructor calls `Reload()` and discards the result),
still registered under its name, still returned by `GetByName`. The pipeline built from it has
`stageCount = 0`, and the process died in a validation storm before presenting a frame.

Three changes, outermost last:

1. `Graphic::Shader::IsCompiled()` — reads the stage list rather than adding a bool, so it cannot
   disagree with what a pipeline would actually be built from.
2. `MeshRenderer` skips draws whose shader is not compiled, silently: the compile error was already
   logged once with file and line, and repeating it per mesh per frame would bury it.
3. `VulkanPipeline::Invalidate()` refuses by name and leaves the pipeline null. Callers already treat a
   null pipeline as "skip this draw", so the rest of the scene keeps rendering. This is the last line of
   defence rather than the intended path, but it is the one place every pipeline in the engine passes
   through.

`ShaderService::Register` still registers a failed shader — deliberately, so the material referencing it
does not silently fall back to the standard shader — and says so once, naming the shader a material will
ask for.

Proof: `Editor/Resources/Assets/Scenes/MAT_ProbeBroken.desce` used to take the editor down with no PNG
at all — reproduced twice. It now renders. `Shots/MAT_broken_shader_survives.png`: the reference PBR
sphere draws, the broken-shader sphere does not, and the log says why, once, by the name a material asks
for:

```
[error] Shader Compilation Error (Fragment): .../MatBroken.shader:25: error: '=' : cannot convert ...
[error] [ShaderService] 'MatBroken' registered but has no compiled stages — every material using it
        will not draw until it compiles (Resources/Shaders/Programs/Graph/MatBroken.shader).
```

No pink, and deliberately no fallback to the standard shader: the object does not draw, which is the
truth, and the log names the cause.

## 3. The copy arithmetic

`frame * slots + slot` was written out by hand in `VulkanUniformBuffer` and again in
`VulkanStorageBuffer`, with nothing checking the two agreed — and each reached for a different spelling
of the slot count (`EngineContext::kMaxRendererSlots` vs `Engine::kMaxRendererSlots`). It now lives in
`Engine/ShaderResources/BufferCopyLayout.hpp` as pure integer functions, for the reason
`GpuTimestampLayout.hpp` gives for the same move: neither buffer can be constructed without a device, so
the arithmetic was unassertable.

## 4. Cover

`Desert/Tests/Engine/MaterialParamUpload` — six tests, no GPU. The units under test are headers and
`ShaderResources::UniformBuffer` is abstract, so a recording buffer stands in for the allocation and
resolves its copies with the **production** `BufferCopyIndex`. A test that recomputed that arithmetic
would be the very defect class this engine keeps paying for.

The headline test is the relation: **one value, two routes, identical bytes in identical copies.**

Both sabotages were run, and one of them found a hole:

| sabotage | result |
|---|---|
| remove the per-draw flush (i.e. restore the shipped behaviour) | **red** — "asset route left frame copy 1 unwritten", "the two routes disagree on frame copy 1" |
| drop the slot term from `BufferCopyIndex` | **red** on both `BufferCopyLayout` tests |
| ↑ same sabotage | **GREEN** on `ASecondRendererSlotIsStillOwedTheValue` — a hole |

That third line is the finding. The multi-slot test checked only that both slots ended up holding the
value, which is true of a layout with **no slot dimension at all**. It now also asserts that slot 0's
writes do **not** reach slot 1 while only slot 0 has recorded, and that assertion is what turns it red.

## Left undone, deliberately

* **`SetRawData` and `FieldProperty` remain two sources of truth for one buffer**, with nothing marking
  which a given buffer is on. The safe fix is to have `SetRawData` refresh the field local data so the
  two can never disagree, after which a blanket flush would be correct. That touches every `SetRawData`
  caller in the engine and is not this task's scope; it is written down here because it is the trap that
  caught the first version of this fix.
* **`GetDescriptorBufferInfo` disagrees between the two buffer types.** `VulkanUniformBuffer` resolves
  the frame from its argument; `VulkanStorageBuffer` ignores its argument and uses the current frame.
  Nothing exercises a caller passing anything but the current frame today, so this is latent.
* **A material is still built from an uncompiled shader**, and its backend allocates an empty descriptor
  set — `vkCreateDescriptorPool(): maxSets is zero` and `vkAllocateDescriptorSets(): descriptorSetCount
  must be greater than 0`, ten of each before the validation layer's duplicate limit stops them. Noise,
  not a crash: the draws are skipped, so nothing binds those sets. Suppressing it means refusing to
  build the material at all, and the obvious way to do that (return nullptr from
  `MaterialFactory::CreateMaterial`) would drop the submesh back onto the PBR path — a silent fallback,
  which is worse than the noise. Left as it is, deliberately.
* **Graph materials still cast no shadows.** The generated `Pass "Depth"` has no consumer:
  `RegisterShadowPass` walks only the static and instanced queues.
* **The graph compiler still does no type checking.** The canvas refuses a bad link; the `.dgraph`
  format and `CompileToDShader` do not, so a hand-edited or future-generated graph can still emit
  invalid GLSL. It no longer crashes — that is what section 2 bought — but the error surfaces from
  shaderc rather than from the graph.
