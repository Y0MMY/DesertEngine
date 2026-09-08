#pragma once

// THE REGISTER: for every RAW pointer member the scan finds, the answer to the two questions.
//
// The forms answer for themselves and get no row here:
//
//   * `unique_ptr` — Q1 is in the type. One owner, named, and the destructor is not optional. Q2 cannot
//     arise because there is no observer.
//   * `weak_ptr`   — Q2 is in the type. The observed may die whenever it likes; `lock()` is the answer.
//   * `shared_ptr` — Q1 says "several owners and their deaths are unordered". Whether that claim is TRUE
//     is a separate question and it is a question of COST, not of correctness: a `shared_ptr` where a
//     `unique_ptr` would do costs an atomic pair and a false impression, never a crash. Those are counted
//     and argued in the suite, not here.
//
// A RAW POINTER MEMBER IS THE ONLY FORM THAT ANSWERS NEITHER QUESTION, so every one of them owes a row,
// and a row that cannot name a guard is a defect rather than a style. That is the whole register.

#include <string>
#include <vector>

namespace Desert::Tests::PointerCensus
{
    // The guarantees this tree actually has. Each is a FACT about the code, not a preference, and each
    // is stated so that the next reader can check it rather than trust it.
    enum class Guard
    {
        // The pointee has static storage duration — a string literal, or a table of them. It outlives
        // every object in the process, so Q2 is closed by the language.
        StaticStorage,

        // The pointee is owned by a smart pointer (or an arena) that is a MEMBER OF THE SAME OBJECT.
        // Observer and observed are destroyed together and in that order, so Q2 is closed by the layout.
        // The row must also say why the address is stable: a `vector<shared_ptr<T>>` does not move its
        // pointees when it grows; a `vector<T>` does, and would make this guard false.
        OwnedByThisObject,

        // A BACK-POINTER: the pointee owns (directly or through one link) the object holding it. It
        // cannot be destroyed without destroying the holder first, so Q2 is closed by containment.
        ObservedContainsUs,

        // AN ARGUMENT PACK. The struct is built at a call site and consumed inside that call; every
        // pointer in it comes from a local or a member of the caller, which outlives the call by
        // construction. Q2 is closed by the call stack.
        CallScoped,

        // Recorded during one frame and consumed before that frame ends, with the pointee owned for at
        // least the frame by a source the row names. Q2 is closed by the frame's own structure — and
        // this guard is only honest when NOTHING inside the window can free the pointee, which is
        // exactly where the register's `Debt` rows come from.
        FrameScoped,

        // NEVER DEREFERENCED. The pointer's VALUE is an identity — a cache key, a batch discriminator.
        // Q2 does not arise for a value. But identity is not free: an address is REUSED after a free,
        // so a row with this guard must also say what stops a recycled address from being mistaken for
        // the old object.
        IdentityOnly,

        // THIS RAW POINTER OWNS. Named rather than converted, because the thing it owns is not a C++
        // object with a destructor (a VMA allocation handle, a byte range inside a mapping). Q1 is "this
        // class", and the row must name where the release happens.
        OwningRaw,

        // The holder re-points it from a live owner before every dereference. Q2 is closed by the
        // CALLER'S DISCIPLINE and not by the type — which is a weaker guarantee than the ones above and
        // is recorded as such: it is one refactor away from being false, and the row says what would
        // break it.
        ReboundBeforeEveryUse,

        // Q2 IS "YES, AND NOTHING STOPS IT". A defect, not a taste. The row MUST name the task that owns
        // the fix — an exception with no task name is unreadable in a month, which is why
        // ConfigOwnership's debt register carries the same rule and the same check.
        Debt
    };

    struct Row
    {
        const char* File;   // repository-relative, as the scan reports it
        const char* Class;  // the class or struct the member is declared in
        const char* Member; // the member's name
        Guard       How;
        const char* Why;    // the argument, in one sentence; the long form is the paragraph it cites
        const char* Task = ""; // required, and only meaningful, for Guard::Debt
    };

    // ----------------------------------------------------------------------------------------------
    // The arguments that more than one row rests on. A family shares one argument because it IS one
    // argument; writing it out 46 times would not make it truer and would hide the rows that differ.
    // ----------------------------------------------------------------------------------------------

    // THE MATERIAL PROPERTY CACHES (46 rows). A material subclass caches
    // `Get<Texture2DProperty>( "u_X" )`, which is `.get()` on a `shared_ptr` held by
    // `MaterialExecutor::m_Texture2DPropertiesStorage`. That executor is a `unique_ptr` MEMBER of the
    // same material (Material.hpp:156), so the observed cannot outlive the observer. Three facts make
    // the address stable, and `MaterialPropertyStorageIsAddressStable` checks all three:
    //   * the storage is `std::vector<std::shared_ptr<T>>` — growing it moves the handles, never the
    //     property objects;
    //   * `InitializeProperties()` has exactly one caller and it is the executor's constructor;
    //   * nothing anywhere clears, erases from or resizes a `*PropertiesStorage`.
    inline constexpr const char* kWhyMaterialProperty =
         "cached from the material's own MaterialExecutor (a unique_ptr member of this same object); the "
         "property lives in a vector<shared_ptr<T>> filled once in the executor's constructor and never "
         "cleared, so the pointee's address is stable and its death is this object's death";

    // THE PER-FRAME RENDER PAYLOADS. A struct filled by the collector and read by the pass, inside one
    // `Scene::UpdateSceneFrame`. Every pointer in it names a GPU resource owned by a render system of
    // the SAME SceneRenderer, or an asset held by a service for longer than the frame.
    inline constexpr const char* kWhyFramePayload =
         "filled by this frame's collector and read by this frame's pass; the pointee is owned for at "
         "least the frame by a render system of the same SceneRenderer";

    // THE ARGUMENT PACKS. Built at a call site, passed by const reference, dead at the semicolon.
    inline constexpr const char* kWhyArgumentPack =
         "an argument pack built at the call site and consumed inside that call; every pointer in it "
         "comes from a local or a member of the caller";

    // ----------------------------------------------------------------------------------------------
    // THE 145 ROWS. Sorted by file and line, which is the order the scan reports them in.
    // ----------------------------------------------------------------------------------------------
    inline const std::vector<Row>& Register()
    {
        static const std::vector<Row> rows = {
        { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanPipelineCompute.hpp",
          "OutputBinding", "Image", Guard::ReboundBeforeEveryUse,
          "an entry of m_BoundOutputs, which is never cleared -- not even by Release() -- so after a "
          "renderer resize it names last frame's image; every dispatch site re-Sets it in the same "
          "function immediately before dispatching, and that is the whole guarantee" },
        { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanPipelineCompute.hpp",
          "VulkanPipelineCompute", "m_BoundInputs", Guard::ReboundBeforeEveryUse,
          "the map is NEVER cleared and the pipelines are long-lived members, so a stale entry survives "
          "a resize that destroyed the image it names; it is dereferenced (dynamic_cast reads the vtable) "
          "only from RecordDescriptorsAndDispatch, and every call site re-Sets in the same function first" },
        { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanPipelineCompute.hpp",
          "VulkanPipelineCompute", "m_BoundStorageBuffers", Guard::ReboundBeforeEveryUse,
          "same as m_BoundInputs: never cleared, and safe only because every dispatch site re-Sets the "
          "binding from the live owner in the same function before dispatching" },
        { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanQueue.hpp",
          "VulkanQueue", "m_SwapChain", Guard::ObservedContainsUs,
          "the swapchain creates and owns the queue wrapper; the queue cannot outlive it" },
        { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp",
          "VulkanSwapChain", "m_CaptureAllocation", Guard::OwningRaw,
          "a VMA allocation handle this class allocates and frees itself (there is no C++ object to hold); "
          "released by TakeCapturedFrameRGBA8 and, since A8, by Release() as well -- before that a capture "
          "recorded on a frame that then resized or shut down leaked a full frame of GPU_TO_CPU memory" },
        { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp",
          "VulkanSwapChain", "m_VmaAllocation", Guard::OwningRaw,
          "VMA allocation handles for the capture buffers, allocated and freed by this class" },
        { "Desert/Desert/Source/Engine/Graphic/AtmosphereEnv.hpp",
          "AtmosphereEnv", "AerialPerspectiveVolume", Guard::FrameScoped,
          "an opaque per-frame handle; the SkyboxRenderer of THIS SceneRenderer owns the image and refills the struct every frame (documented at the member)" },
        { "Desert/Desert/Source/Engine/Graphic/AtmosphereEnv.hpp",
          "AtmosphereEnv", "DistantSkyLight", Guard::FrameScoped,
          "an opaque per-frame handle; the SkyboxRenderer of THIS SceneRenderer owns the image and refills the struct every frame (documented at the member)" },
        { "Desert/Desert/Source/Engine/Graphic/AtmosphereEnv.hpp",
          "AtmosphereEnv", "TransmittanceLut", Guard::FrameScoped,
          "an opaque per-frame handle; the SkyboxRenderer of THIS SceneRenderer owns the image and refills the struct every frame (documented at the member)" },
        { "Desert/Desert/Source/Engine/Graphic/Clouds/CloudEnvironmentBake.hpp",
          "CloudEnvironmentBake", "Modelling", Guard::FrameScoped,
          kWhyFramePayload },
        { "Desert/Desert/Source/Engine/Graphic/Clouds/CloudEnvironmentBake.hpp",
          "CloudEnvironmentBake", "AuthoredAtlas", Guard::FrameScoped,
          kWhyFramePayload },
        { "Desert/Desert/Source/Engine/Graphic/Clouds/CloudEnvironmentBake.hpp",
          "CloudEnvironmentBake", "SkyOcclusionVolume", Guard::FrameScoped,
          kWhyFramePayload },
        { "Desert/Desert/Source/Engine/Graphic/Clouds/CloudEnvironmentBake.hpp",
          "CloudBakeBinding", "Params", Guard::CallScoped,
          kWhyArgumentPack },
        { "Desert/Desert/Source/Engine/Graphic/Clouds/CloudEnvironmentBake.hpp",
          "CloudBakeBinding", "Authored", Guard::CallScoped,
          kWhyArgumentPack },
        { "Desert/Desert/Source/Engine/Graphic/Clouds/CloudEnvironmentBake.hpp",
          "CloudBakeBinding", "Modelling", Guard::CallScoped,
          kWhyArgumentPack },
        { "Desert/Desert/Source/Engine/Graphic/Clouds/CloudEnvironmentBake.hpp",
          "CloudBakeBinding", "AuthoredAtlas", Guard::CallScoped,
          kWhyArgumentPack },
        { "Desert/Desert/Source/Engine/Graphic/Clouds/CloudEnvironmentBake.hpp",
          "CloudBakeBinding", "SkyOcclusionVolume", Guard::CallScoped,
          kWhyArgumentPack },
        { "Desert/Desert/Source/Engine/Graphic/Clouds/CloudEnvironmentBake.hpp",
          "CloudBakeBinding", "DistantSkyLight", Guard::CallScoped,
          kWhyArgumentPack },
        { "Desert/Desert/Source/Engine/Graphic/Clouds/CloudShadowPayload.hpp",
          "CloudShadowInput", "Map", Guard::CallScoped,
          kWhyArgumentPack },
        { "Desert/Desert/Source/Engine/Graphic/ExternalRenderPass.hpp",
          "ExternalPassContext", "Camera", Guard::CallScoped,
          kWhyArgumentPack },
        { "Desert/Desert/Source/Engine/Graphic/ExternalRenderPass.hpp",
          "ExternalPassContext", "Target", Guard::CallScoped,
          kWhyArgumentPack },
        { "Desert/Desert/Source/Engine/Graphic/ExternalRenderPass.hpp",
          "ExternalPassContext", "Depth", Guard::CallScoped,
          kWhyArgumentPack },
        { "Desert/Desert/Source/Engine/Graphic/MappedMemory.hpp",
          "MappedMemory", "kNeverAttempted", Guard::StaticStorage,
          "a string literal held by a constexpr static" },
        { "Desert/Desert/Source/Engine/Graphic/MappedMemory.hpp",
          "MappedMemory", "kReleased", Guard::StaticStorage,
          "a string literal held by a constexpr static" },
        { "Desert/Desert/Source/Engine/Graphic/MappedMemory.hpp",
          "MappedMemory", "kMovedOut", Guard::StaticStorage,
          "a string literal held by a constexpr static" },
        { "Desert/Desert/Source/Engine/Graphic/MappedMemory.hpp",
          "MappedMemory", "m_Allocation", Guard::OwningRaw,
          "the VMA allocation this guard is holding open; released in Release() and in the destructor, which is the type's whole purpose" },
        { "Desert/Desert/Source/Engine/Graphic/MappedMemory.hpp",
          "MappedMemory", "m_Bytes", Guard::OwningRaw,
          "the mapped byte range, valid exactly while m_Allocation is mapped and nulled by the same Release()" },
        { "Desert/Desert/Source/Engine/Graphic/MappedMemory.hpp",
          "MappedMemory", "m_Reason", Guard::StaticStorage,
          "always one of the constexpr literals above, so the refusal text cannot outlive its own storage" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Clouds/MaterialCloudComposite.hpp",
          "MaterialCloudComposite", "m_ScatterTexture", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Clouds/MaterialCloudComposite.hpp",
          "MaterialCloudComposite", "m_GuideTexture", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Debug/MaterialOverdrawResolve.hpp",
          "MaterialOverdrawResolve", "m_Overdraw", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialCopy.hpp",
          "MaterialCopy", "m_Input", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialDeferredLighting.hpp",
          "DeferredShadowInput", "CascadeVP", Guard::CallScoped,
          kWhyArgumentPack },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialDeferredLighting.hpp",
          "DeferredEnvironmentInput", "Irradiance", Guard::CallScoped,
          kWhyArgumentPack },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialDeferredLighting.hpp",
          "DeferredEnvironmentInput", "Prefiltered", Guard::CallScoped,
          kWhyArgumentPack },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialDeferredLighting.hpp",
          "DeferredEnvironmentInput", "BrdfLut", Guard::CallScoped,
          kWhyArgumentPack },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialDeferredLighting.hpp",
          "MaterialDeferredLighting", "m_GBufferB", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialDeferredLighting.hpp",
          "MaterialDeferredLighting", "m_GBufferC", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialDeferredLighting.hpp",
          "MaterialDeferredLighting", "m_GBufferEmissive", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialDeferredLighting.hpp",
          "MaterialDeferredLighting", "m_SSAO", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialDeferredLighting.hpp",
          "MaterialDeferredLighting", "m_GI", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialDeferredLighting.hpp",
          "MaterialDeferredLighting", "m_EnvIrradiance", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialDeferredLighting.hpp",
          "MaterialDeferredLighting", "m_EnvSpecular", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialDeferredLighting.hpp",
          "MaterialDeferredLighting", "m_BrdfLut", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialGIResolve.hpp",
          "MaterialGIResolve", "m_Normal", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialGIResolve.hpp",
          "MaterialGIResolve", "m_WorldPos", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialGIResolve.hpp",
          "MaterialGIResolve", "m_RSMAlbedo", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialGIResolve.hpp",
          "MaterialGIResolve", "m_RSMNormal", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialGIResolve.hpp",
          "MaterialGIResolve", "m_RSMWorldPos", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialSSAO.hpp",
          "MaterialSSAO", "m_Pos", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialSSAO.hpp",
          "MaterialSSAO", "m_Normal", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialSSR.hpp",
          "MaterialSSR", "m_Albedo", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialSSR.hpp",
          "MaterialSSR", "m_Normal", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialSSR.hpp",
          "MaterialSSR", "m_WorldPos", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialSSR.hpp",
          "MaterialSSR", "m_SceneColor", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialSSR.hpp",
          "MaterialSSRResolve", "m_Trace", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialSSR.hpp",
          "MaterialSSRResolve", "m_History", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialSSR.hpp",
          "MaterialSSRResolve", "m_WorldPos", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialSSR.hpp",
          "MaterialSSRComposite", "m_SSR", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Deferred/MaterialSSR.hpp",
          "MaterialSSRComposite", "m_Normal", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Fog/MaterialHeightFog.hpp",
          "MaterialHeightFog", "m_FogTexture", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Material.hpp",
          "Material", "m_RegisteredProperties", Guard::OwnedByThisObject,
          "every entry is the address of an MPROPERTY member of this same object, registered by that member's own registrar sub-object at construction" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/MaterialInstance.hpp",
          "MaterialInstance", "m_ParentMaterial", Guard::ReboundBeforeEveryUse,
          "the parent CAN die first -- MaterialService::Invalidate graveyards it -- and the guard is the "
          "invalidation stamp: MeshECSSystem compares MaterialService's version and clears the "
          "component's RuntimeMaterialInstances before any use, so no instance outlives its parent. "
          "Clear() does not bump that stamp, but its only caller is Renderer::Shutdown" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Mesh/PBR/MaterialPBRBase.hpp",
          "MaterialPBRBase", "kShadowBlockName", Guard::StaticStorage,
          "a shader block name, a string literal in a constexpr static" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Mesh/PBR/MaterialPBRBase.hpp",
          "MaterialPBRBase", "kEnvIrradianceName", Guard::StaticStorage,
          "a shader block name, a string literal in a constexpr static" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Mesh/PBR/MaterialPBRBase.hpp",
          "MaterialPBRBase", "kEnvSpecularName", Guard::StaticStorage,
          "a shader block name, a string literal in a constexpr static" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Mesh/PBR/MaterialPBRBase.hpp",
          "MaterialPBRBase", "kBrdfLutName", Guard::StaticStorage,
          "a shader block name, a string literal in a constexpr static" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Mesh/PBR/PBRSceneFrame.hpp",
          "PBRSceneFrame", "Camera", Guard::CallScoped,
          "PBRSceneFrame is always a function-local consumed by ApplyTo in the same scope; every field points at a member of the SceneRenderer or MeshRenderer that built it" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Mesh/PBR/PBRSceneFrame.hpp",
          "PBRSceneFrame", "PointLights", Guard::CallScoped,
          "PBRSceneFrame is always a function-local consumed by ApplyTo in the same scope; every field points at a member of the SceneRenderer or MeshRenderer that built it" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Mesh/PBR/PBRSceneFrame.hpp",
          "PBRSceneFrame", "SpotLights", Guard::CallScoped,
          "PBRSceneFrame is always a function-local consumed by ApplyTo in the same scope; every field points at a member of the SceneRenderer or MeshRenderer that built it" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Mesh/PBR/PBRSceneFrame.hpp",
          "PBRSceneFrame", "DirectionLights", Guard::CallScoped,
          "PBRSceneFrame is always a function-local consumed by ApplyTo in the same scope; every field points at a member of the SceneRenderer or MeshRenderer that built it" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Mesh/PBR/PBRSceneFrame.hpp",
          "PBRSceneFrame", "CascadeViewProj", Guard::CallScoped,
          "PBRSceneFrame is always a function-local consumed by ApplyTo in the same scope; every field points at a member of the SceneRenderer or MeshRenderer that built it" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Mesh/PBR/PBRSceneFrame.hpp",
          "PBRSceneFrame", "IrradianceMap", Guard::CallScoped,
          "PBRSceneFrame is always a function-local consumed by ApplyTo in the same scope; every field points at a member of the SceneRenderer or MeshRenderer that built it" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Mesh/PBR/PBRSceneFrame.hpp",
          "PBRSceneFrame", "PrefilteredMap", Guard::CallScoped,
          "PBRSceneFrame is always a function-local consumed by ApplyTo in the same scope; every field points at a member of the SceneRenderer or MeshRenderer that built it" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Mesh/PBR/PBRSceneFrame.hpp",
          "PBRSceneFrame", "BrdfLut", Guard::CallScoped,
          "PBRSceneFrame is always a function-local consumed by ApplyTo in the same scope; every field points at a member of the SceneRenderer or MeshRenderer that built it" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/JumpFloodMaterials.hpp",
          "MaterialJFAInit", "m_MaskTexture", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/JumpFloodMaterials.hpp",
          "MaterialJFAStep", "m_InputTexture", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/MaterialFXAA.hpp",
          "MaterialFXAA", "m_InputTexture", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/MaterialJFAComposite.hpp",
          "MaterialJFAComposite", "m_SceneTexture", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/MaterialSMAA.hpp",
          "MaterialSMAAEdges", "m_Color", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/MaterialSMAA.hpp",
          "MaterialSMAAWeights", "m_Edges", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/MaterialSMAA.hpp",
          "MaterialSMAAWeights", "m_Area", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/MaterialSMAA.hpp",
          "MaterialSMAAWeights", "m_Search", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/MaterialSMAA.hpp",
          "MaterialSMAABlend", "m_Color", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/MaterialSMAA.hpp",
          "MaterialSMAABlend", "m_Blend", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/MaterialSMAA.hpp",
          "MaterialSMAABlend", "m_Edges", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/MaterialSMAA.hpp",
          "MaterialSMAABlend", "m_Area", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/MaterialTonemap.hpp",
          "MaterialTonemap", "m_BloomTexture", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/MaterialTonemap.hpp",
          "MaterialTonemap", "m_AvgLuminance", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/MaterialTonemap.hpp",
          "MaterialTonemap", "m_LightShaftTexture", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/PostProcessing/MaterialTonemap.hpp",
          "MaterialTonemap", "m_LensFlareTexture", Guard::OwnedByThisObject,
          kWhyMaterialProperty },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Properties/Texture2DProperty.hpp",
          "Texture2DProperty", "m_Texture", Guard::ReboundBeforeEveryUse,
          "the image is not owned here; its DESCRIPTOR is copied out by UniformImage2D at SetImage2D time, and every consumer re-binds from the live framebuffer attachment each frame before the material is applied" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Properties/TextureCubeProperty.hpp",
          "TextureCubeProperty", "m_Texture", Guard::ReboundBeforeEveryUse,
          "same as Texture2DProperty::m_Texture: not owned, re-bound from the live owner before every apply" },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Skybox/MaterialSkybox.hpp",
          "UpdateMaterialSkyboxInfo", "Camera", Guard::CallScoped,
          kWhyArgumentPack },
        { "Desert/Desert/Source/Engine/Graphic/Materials/Skybox/MaterialSkybox.hpp",
          "MaterialSkybox", "m_CubeMapTexture", Guard::OwnedByThisObject,
          "as the material-property family, except that MaterialSkybox holds its executor by shared_ptr "
          "rather than unique_ptr -- which only strengthens the argument: the property cannot outlive "
          "this object because this object holds a reference to the executor that owns it" },
        { "Desert/Desert/Source/Engine/Graphic/PipelineCache.hpp",
          "Key", "Shader", Guard::IdentityOnly,
          "the pipeline cache keys on the ADDRESS of the shader/framebuffer/render pass; a recycled address cannot collide because a pipeline is only reachable through the spec that still holds shared_ptrs to all three" },
        { "Desert/Desert/Source/Engine/Graphic/PipelineCache.hpp",
          "Key", "Framebuffer", Guard::IdentityOnly,
          "the pipeline cache keys on the ADDRESS of the shader/framebuffer/render pass; a recycled address cannot collide because a pipeline is only reachable through the spec that still holds shared_ptrs to all three" },
        { "Desert/Desert/Source/Engine/Graphic/PipelineCache.hpp",
          "Key", "Renderpass", Guard::IdentityOnly,
          "the pipeline cache keys on the ADDRESS of the shader/framebuffer/render pass; a recycled address cannot collide because a pipeline is only reachable through the spec that still holds shared_ptrs to all three" },
        { "Desert/Desert/Source/Engine/Graphic/Render/Commands/DrawGenericMeshCommand.hpp",
          "DrawGenericMeshCommand", "Mesh", Guard::FrameScoped,
          "the mesh is held by the ECS component, the primitive factory's process-wide table or MeshService for the whole frame" },
        { "Desert/Desert/Source/Engine/Graphic/Render/Commands/DrawGenericMeshCommand.hpp",
          "DrawGenericMeshCommand", "DirectTexture", Guard::FrameScoped,
          kWhyFramePayload },
        { "Desert/Desert/Source/Engine/Graphic/Render/Commands/DrawMeshCommand.hpp",
          "DrawStaticMeshCommand", "Mesh", Guard::FrameScoped,
          "the mesh is held by the ECS component, the primitive factory's process-wide table or MeshService for the whole frame" },
        { "Desert/Desert/Source/Engine/Graphic/Render/Commands/DrawMeshCommand.hpp",
          "DrawStaticMeshCommand", "MaterialSlots", Guard::Debt,
          "the address of a std::vector MEMBER of an entt component. The vendored entt stores components by value in a flat std::vector (entt.hpp:4733), so any AddComponent or DestroyEntity reallocates or swap-and-pops the pool -- and ScriptSystem, which runs Lua, is registered AFTER MeshECSSystem and before ExecuteAll", "A8-3" },
        { "Desert/Desert/Source/Engine/Graphic/Render/Commands/DrawMeshCommand.hpp",
          "DrawInstancedStaticMeshCommand", "Mesh", Guard::FrameScoped,
          "the mesh is held by the ECS component or the primitive factory's process-wide table for the whole frame" },
        { "Desert/Desert/Source/Engine/Graphic/Render/Commands/DrawMeshCommand.hpp",
          "DrawInstancedStaticMeshCommand", "Material", Guard::FrameScoped,
          "a heap MaterialInstance behind a shared_ptr in the component, so an entt pool move does not touch it" },
        { "Desert/Desert/Source/Engine/Graphic/Render/Commands/DrawMeshCommand.hpp",
          "DrawInstancedStaticMeshCommand", "Transforms", Guard::Debt,
          "the address of InstancedStaticMeshComponent::InstanceTransforms, invalidated by the same entt pool reallocation as DrawStaticMeshCommand::MaterialSlots and read two passes deeper", "A8-3" },
        { "Desert/Desert/Source/Engine/Graphic/Render/Commands/DrawSkinnedMeshCommand.hpp",
          "DrawSkinnedMeshCommand", "Mesh", Guard::FrameScoped,
          "held by the ECS component for the whole frame" },
        { "Desert/Desert/Source/Engine/Graphic/Render/Commands/DrawSkinnedMeshCommand.hpp",
          "DrawSkinnedMeshCommand", "MaterialSlot", Guard::OwnedByThisObject,
          "the vector is a member of the command itself, destroyed by RenderCommandBuffer::Clear which runs the virtual destructor; the pointers in it are MaterialInstances owned by the component" },
        { "Desert/Desert/Source/Engine/Graphic/Render/Commands/DrawSlotMaterialMeshCommand.hpp",
          "DrawSlotMaterialMeshCommand", "Mesh", Guard::FrameScoped,
          "held by the ECS component or the primitive factory for the whole frame" },
        { "Desert/Desert/Source/Engine/Graphic/Render/Commands/DrawSlotMaterialMeshCommand.hpp",
          "DrawSlotMaterialMeshCommand", "SlotMaterial", Guard::FrameScoped,
          "a Material owned by MaterialService or by the SceneRenderer for longer than the frame" },
        { "Desert/Desert/Source/Engine/Graphic/Render/Commands/DrawTerrainCommand.hpp",
          "DrawTerrainCommand", "SplatMap", Guard::FrameScoped,
          kWhyFramePayload },
        { "Desert/Desert/Source/Engine/Graphic/Render/RenderCommandBuffer.hpp",
          "RenderCommandBuffer", "m_Commands", Guard::OwnedByThisObject,
          "the commands are placement-new'd into this object's own paged arena; pages are never freed mid-frame and Clear() runs the virtual destructor of every one before rewinding" },
        { "Desert/Desert/Source/Engine/Graphic/Render2D/DrawList2D.hpp",
          "DrawCommand", "Texture", Guard::IdentityOnly,
          "a batch discriminator: consecutive primitives with the same value extend one draw. It is also the key of Render2D's executor caches, and THAT use is what makes address recycling matter -- see the Render2D rows" },
        { "Desert/Desert/Source/Engine/Graphic/Render2D/Render2D.hpp",
          "Render2D", "m_WhiteImage", Guard::FrameScoped,
          "resolved from ImageService, whose only release path is Renderer::Shutdown -- terminal, and after the last Flush" },
        { "Desert/Desert/Source/Engine/Graphic/Render2D/Render2D.hpp",
          "Render2D", "m_Backdrop", Guard::Debt,
          "BackdropBlurRenderer::Resize replaces its shared_ptr<Image2D> and destroys the old object while this pointer still equals it. The pointer itself is re-pointed by the UI pass before every Flush, but it is also the KEY of m_GlassExecutors, and that cache is never erased -- so each resize leaks a MaterialExecutor whose Texture2DProperty still holds the freed image, and a new image at the same heap address hits the stale entry", "A8-1" },
        { "Desert/Desert/Source/Engine/Graphic/SceneRenderer.cpp",
          "ExternalPassSystem", "m_Renderer", Guard::ObservedContainsUs,
          "the SceneRenderer owns its render systems, so it cannot be destroyed while one of them is alive" },
        { "Desert/Desert/Source/Engine/Graphic/SkyPresets.hpp",
          "SkyPresetEntry", "Name", Guard::StaticStorage,
          "a string literal in a constexpr preset table" },
        { "Desert/Desert/Source/Engine/Graphic/SwapChain.hpp",
          "SwapChain", "m_Window", Guard::ObservedContainsUs,
          "the platform window owns the swapchain and declares m_GLFWWindow BEFORE m_SwapChain, so the "
          "swapchain is destroyed first; nothing in the tree calls glfwDestroyWindow at all. Note the "
          "member's const is decorative -- the one use site casts it away for InitSurface" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/RenderSystem.hpp",
          "RenderSystem", "m_SceneRenderer", Guard::ObservedContainsUs,
          "the SceneRenderer owns its render systems" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/RenderSystem.hpp",
          "RenderSystem", "m_RenderGraphBuilder", Guard::ObservedContainsUs,
          "the builder is a member of the SceneRenderer that owns this system; note the sibling m_TargetFramebuffer is a weak_ptr, because THAT one is not owned by the renderer" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Clouds/VolumetricCloudRenderer.hpp",
          "VolumetricCloudRenderer", "m_AuthoredAtlas", Guard::Debt,
          "a raw pointer into CloudModellingService::m_Atlas, a PROCESS-WIDE slot; a second live SceneRenderer calling EnsureAtlas with a different body set frees the image this renderer is pointing at. The service declares GetGeneration() for exactly this check and the renderer never calls it", "A8-2" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "MeshRenderData", "Mesh", Guard::CallScoped,
          "a temporary aggregate consumed synchronously by MeshRenderer::SubmitMesh" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "MeshRenderData", "MaterialSlots", Guard::Debt,
          "carries the same ECS-component vector address as DrawStaticMeshCommand::MaterialSlots into the renderer", "A8-3" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "StaticMeshRenderData", "Mesh", Guard::FrameScoped,
          "held by the ECS component, the primitive factory or MeshService for the whole frame" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "StaticMeshRenderData", "MaterialSlots", Guard::Debt,
          "stored in m_StaticQueue and dereferenced in five passes AFTER Lua has run; the pointee is a vector member of an entt component whose pool can have moved", "A8-3" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "SkinnedMeshRenderData", "Mesh", Guard::FrameScoped,
          "held by the ECS component for the whole frame" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "SkinnedMeshRenderData", "Material", Guard::FrameScoped,
          "a Material owned by the MeshRenderer itself" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "SkinnedMeshRenderData", "Instance", Guard::FrameScoped,
          "a heap MaterialInstance behind a shared_ptr in the component" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "InstancedMeshRenderData", "Mesh", Guard::FrameScoped,
          "held by the ECS component or the primitive factory for the whole frame" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "InstancedMeshRenderData", "Material", Guard::FrameScoped,
          "a heap MaterialInstance behind a shared_ptr in the component" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "InstancedMeshRenderData", "Transforms", Guard::Debt,
          "stored in m_InstancedQueue and copied by iterator range in two passes after Lua has run; same entt pool instability", "A8-3" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "GenericMeshRenderData", "Mesh", Guard::FrameScoped,
          "held by the ECS component or the primitive factory for the whole frame" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "GenericMeshRenderData", "SlotMaterial", Guard::FrameScoped,
          "a Material owned by MaterialService or the SceneRenderer for longer than the frame" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "GenericMeshRenderData", "DirectTexture", Guard::FrameScoped,
          kWhyFramePayload },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "ObjDraw", "Obj", Guard::OwnedByThisObject,
          "points into m_StaticQueue, which is fully populated before the pass that builds these and is not pushed to during it" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "ObjDraw", "Inst", Guard::FrameScoped,
          "a heap MaterialInstance behind a shared_ptr in the component" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "InstancedDraw", "Mesh", Guard::FrameScoped,
          "held by the ECS component or the primitive factory for the whole frame" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "GenericDraw", "Data", Guard::OwnedByThisObject,
          "points into m_GenericQueue, fully populated before the pass that builds these" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "GenericDraw", "Material", Guard::FrameScoped,
          "a DataDrivenMaterial owned by MaterialService or the SceneRenderer for longer than the frame" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "ShadowBatch", "Mesh", Guard::FrameScoped,
          "held by the ECS component or the primitive factory for the whole frame" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "MeshRenderer", "m_ScratchShadowSingles", Guard::OwnedByThisObject,
          "a reused scratch vector of pointers into m_StaticQueue, cleared and refilled inside one pass" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp",
          "MeshRenderer", "m_ScratchGenericRows", Guard::OwnedByThisObject,
          "a reused scratch vector; the DataDrivenMaterial* in it are owned by MaterialService for longer than the frame" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Particles/ParticleRenderer.hpp",
          "FrameEmitter", "Gpu", Guard::OwnedByThisObject,
          "points into ParticleRenderer::m_Emitters, an unordered_map -- NODE-BASED, so the insert that "
          "PrepareFrame can perform while it is already pushing these pointers cannot move the pointee; "
          "the only erase is OnSceneReplaced's clear(), which clears m_FrameEmitters first" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Skybox/SkyboxRenderer.hpp",
          "SkyboxRenderer", "m_ActiveCamera", Guard::ReboundBeforeEveryUse,
          "re-pointed by PrepareCamera from SceneRenderer::BeginScene at the top of every frame, before any pass that reads it; the camera itself is a persistent member of Scene that Scene::Clear does not touch" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Terrain/TerrainRenderer.cpp",
          "Group", "Material", Guard::FrameScoped,
          "a per-frame grouping key; the DataDrivenMaterial is owned by MaterialService for longer than the frame" },
        { "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Terrain/TerrainRenderer.hpp",
          "TerrainDrawData", "SplatMap", Guard::FrameScoped,
          kWhyFramePayload },

        };
        return rows;
    }
} // namespace Desert::Tests::PointerCensus
