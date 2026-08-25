#pragma once

#include <Common/Core/ResultStr.hpp>
#include <Engine/Core/Formats/ImageFormat.hpp>

#include <memory>
#include <optional>
#include <string>

namespace Desert::Engine
{
    /// What a format is being asked about. Flags, because the interesting questions are combinations —
    /// an accumulation target needs ColorAttachment|Blendable|Sampled, and a compute target needs Storage.
    enum FormatUsage : uint32_t
    {
        FormatUsage_Sampled         = 0x1, ///< usable as a sampled texture
        FormatUsage_ColorAttachment = 0x2, ///< usable as a colour attachment
        FormatUsage_DepthAttachment = 0x4, ///< usable as a depth/stencil attachment
        FormatUsage_Blendable       = 0x8, ///< supports colour blending when used as an attachment
        FormatUsage_Storage         = 0x10, ///< usable as a storage image (compute writes)
        FormatUsage_LinearFilter    = 0x20, ///< supports linear filtering when sampled
    };

    constexpr FormatUsage operator|( FormatUsage a, FormatUsage b )
    {
        return static_cast<FormatUsage>( static_cast<uint32_t>( a ) | static_cast<uint32_t>( b ) );
    }

    enum class DeviceType
    {
        Unknown = 0,
        Integrated,
        Discrete,
        Virtual,
        CPU,
    };

    /**
     * @brief Static hardware capabilities of the graphics/compute device.
     *
     * This is the engine's SystemInfo: a snapshot of what the hardware can do, queried once at device
     * selection. It exists so features can ASK before they allocate instead of creating resources and
     * hoping — an SSR/GI pass that needs a blendable float target should find that out here, not from a
     * driver error three frames later.
     *
     * Deliberately kept to facts a renderer actually branches on. This struct is not a mirror of
     * VkPhysicalDeviceLimits: a field belongs here only once something reads it.
     */
    struct DeviceCapabilities
    {
        // --- Identity -------------------------------------------------------------------------------
        std::string Name;                              ///< Adapter name, e.g. "NVIDIA GeForce RTX 3070 Ti".
        std::string VendorName;                        ///< Decoded PCI vendor: NVIDIA / AMD / Intel / Apple.
        DeviceType  Type = DeviceType::Unknown;        ///< Discrete vs integrated — drives default quality.

        // --- Memory ---------------------------------------------------------------------------------
        /// Total device-local heap in bytes. 0 when it could not be determined. Used to decide whether the
        /// heavier screen-space passes (SSR trace + GI resolve + their ping-pong history) are affordable.
        uint64_t VideoMemory = 0;

        // --- Buffers --------------------------------------------------------------------------------
        uint64_t MaxStorageBufferSize   = 0;
        uint64_t StorageBufferAlignment = 0;
        uint32_t MaxPushConstantSize    = 0; ///< Per-object transforms ride a push constant; this caps them.

        // --- Raster / lines -------------------------------------------------------------------------
        bool  SupportsWideLines = false;
        float MaxLineWidth      = 1.0f;
        bool  SupportsAnisotropy = false;
        float MaxAnisotropy      = 1.0f;
        /// VK fillModeNonSolid: wireframe (VK_POLYGON_MODE_LINE) pipelines. Independent from wideLines —
        /// MoltenVK supports non-solid fill but NOT wide lines.
        bool SupportsNonSolidFill = false;

        // --- Texture / attachment limits ------------------------------------------------------------
        uint32_t MaxTexture2DSize      = 0;
        uint32_t MaxTextureArrayLayers = 0;
        uint32_t MaxColorAttachments   = 0; ///< The deferred G-buffer needs 4 + depth; RSM mirrors it.
        /// Bitmask of usable MSAA counts (bit N set = 2^N samples), colour AND depth both supported.
        uint32_t MSAASampleMask = 1;

        // --- Feature flags the renderer branches on -------------------------------------------------
        bool SupportsGeometryShaders     = false;
        bool SupportsTessellation        = false;
        bool SupportsMultiDrawIndirect   = false;
        bool SupportsTimestampQueries    = false; ///< GPU-side profiling; the profiler is CPU-only without it.
        /// Nanoseconds per timestamp tick — the factor that turns a query delta into real time. Read by
        /// the GPU profiler and by nothing else; it is 1.0 on MoltenVK (Metal counts in nanoseconds
        /// already) and e.g. 38.4 on some AMD parts, so a profiler that assumes either is wrong somewhere.
        float TimestampPeriodNs = 0.0f;
        /// Sampling + blending into 32-bit float colour targets. Every screen-space pass that accumulates
        /// (SSR trace/resolve, GI resolve, bloom) writes RGBA32F, so this gates them.
        bool SupportsFloatRenderTargets = false;

        [[nodiscard]] bool IsDiscrete() const
        {
            return Type == DeviceType::Discrete;
        }
        /// Highest usable MSAA sample count (1 when multisampling is unavailable).
        [[nodiscard]] uint32_t MaxMSAASamples() const
        {
            uint32_t best = 1;
            for ( uint32_t bit = 0; bit < 6; ++bit )
                if ( MSAASampleMask & ( 1u << bit ) )
                    best = 1u << bit;
            return best;
        }
    };

    /**
     * @brief The device INTROSPECTION layer — what the hardware is and what it can do.
     *
     * Scope is intentionally narrow, and the split is the point:
     *   - this interface       — capabilities, identity, whole-device synchronization;
     *   - Graphic::Renderer    — command submission (render passes, draws, dispatches);
     *   - the resource classes — creation (Framebuffer::Create, Image::Create, GraphicsPipeline::Create...).
     *
     * Resource creation deliberately does NOT live here. UE's FDynamicRHI owns it and grows to hundreds of
     * virtuals as a result; this engine follows the Godot/bgfx shape instead, where each resource type owns
     * its own factory. Adding RHICreate*-style methods here would duplicate those factories, not replace
     * them. If a caller needs to know whether something is possible, it asks GetCapabilities(); if it needs
     * to make something, it asks that resource's Create().
     */
    class Device
    {
    public:
        virtual ~Device() = default;

        /**
         * @brief Returns the device's hardware capabilities.
         */
        [[nodiscard]] virtual const DeviceCapabilities& GetCapabilities() const = 0;

        /**
         * @brief Can this format be used for ALL of the requested purposes?
         *
         * The general form of the question the cached DeviceCapabilities flags answer for the few cases
         * the renderer checks every frame. Prefer this over adding another Supports<Feature> bool: those
         * grow one per feature, this one answers for any format the engine has.
         *
         * @param format Engine image format.
         * @param usage  Bitwise-OR of FormatUsage — every requested bit must be supported.
         */
        // Fully qualified: inside Desert::Engine a bare "Core::" does not reach Desert::Core.
        [[nodiscard]] virtual bool IsFormatSupported( ::Desert::Core::Formats::ImageFormat format,
                                                     FormatUsage                          usage ) const = 0;

        /**
         * @brief Wait for all device operations to complete.
         */
        virtual void WaitIdle() const = 0;

        /**
         * @brief Platform/API specific name of the device.
         */
        [[nodiscard]] virtual std::string GetName() const = 0;

        static std::shared_ptr<Device> Create();
    };

} // namespace Desert::Engine
