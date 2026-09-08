#pragma once

#include <Common/Core/AssetHandle.hpp>
#include <Common/Core/Core.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Desert::Graphic
{
    /**
     * @brief WHO OWNS EVERY LIVE GPU OBJECT — one ledger, and the entry in it cannot outlive the object.
     *
     * WHY THIS EXISTS, IN THE ENGINE'S OWN WORDS. `DeviceLost.cpp` refuses device-loss recovery and names
     * the missing piece: "WHAT WOULD CHANGE THE ANSWER: a resource ownership registry that can enumerate
     * every live GPU object (wanted for other reasons too — teardown order, hot reload, and the 'which
     * slot is this?' question the renderer-slot work keeps asking)." Nothing enumerated them. There was no
     * counter of GPU objects, no counter of GPU memory, and `VulkanAllocator::CheckResourceLeaks()` had an
     * empty body. `ImageService` looked like the registry and is not: `Register()` is reached from eight
     * call sites, while every renderer LUT, framebuffer attachment, font atlas and panel image is created
     * through `Image2D::Create` and never seen again.
     *
     * ASSET EVICTION IS THE FIRST CONSUMER, NOT THE SUBJECT. An asset in this engine owns NOTHING on the
     * GPU — every `AssetBase` subclass holds CPU data and a path, and the GPU object built from it lives in
     * a `Runtime::*Service` keyed on the asset's handle. So a registry of ASSETS cannot answer "what is on
     * the device", and a registry that could was going to be written twice — once for eviction and once
     * for device loss. It is written once, here, and eviction reads it.
     *
     * ── THE SHAPE, AND WHY IT IS A TYPE RATHER THAN A PAIR OF CALLS ───────────────────────────────────
     *
     * `MappedMemory` (beside this file) is the model: the contract is guarded by the TYPE, not by checks
     * the caller has to remember. A `Register()`/`Unregister()` pair is thirteen places for a fourteenth
     * to be forgotten, and a forgotten `Unregister` is worse than no ledger at all — it reports objects
     * that are gone, so the number a decision rests on drifts upward for ever and nobody can tell that it
     * has. `ImageService::Unregister` is the existing proof: it is reached from four sites and `Clear()`
     * from one, and a fifth owner simply never gives its image back.
     *
     * So there is no `Unregister`. There is a move-only token, `ResourceOwnership`, that a resource holds
     * as a member: constructing it is the registration, destroying it is the removal, and a resource
     * cannot be alive without one or dead with one. It has no copy constructor, because two tokens for
     * one object would double-count and then double-remove.
     *
     * ── WHY THE OWNER IS CLAIMED AFTERWARDS AND "UNCLAIMED" IS AN ANSWER ──────────────────────────────
     *
     * The token is taken in the RESOURCE BASE CLASS's constructor, which is the only place that sees every
     * object of that kind whatever backend made it — and that constructor cannot possibly know who is
     * about to hold the pointer. `Image2D::Create` runs identically for a texture built from an asset, a
     * shadow cascade attachment and a panel's slice preview.
     *
     * Attribution therefore happens where the object is PUT somewhere: `Claim()`, called by the service,
     * the renderer or the cache that takes ownership of it. Anything nobody claims stays `Unclaimed`, and
     * that is deliberately a reportable state rather than a default bucket — §1.4 of the contract, applied
     * to a census: "nobody owns this" and "this owner has nothing" must not be the same number. The count
     * of unclaimed rows is the measurement device-loss recovery (Г7-A) has to plan against, and hiding it
     * behind a plausible default would make the ledger read complete while covering half the device.
     *
     * ── WHAT A ROW COSTS ─────────────────────────────────────────────────────────────────────────────
     *
     * 24 bytes and one mutex acquisition per GPU-object construction and destruction. GPU-object creation
     * already costs a driver round trip; this is not on any per-frame path, and the ledger is deliberately
     * NOT consulted while drawing — it answers questions between frames.
     */

    /// What KIND of device object a row stands for. A closed set with a name table below, for the reason
    /// `AssetTypeName` gives: a census that has to say WHICH kinds disagreed cannot print an integer.
    enum class ResourceKind : uint8_t
    {
        Image2D = 0,
        ImageCube,
        Image3D,
        VertexBuffer,
        IndexBuffer,
        UniformBuffer,
        StorageBuffer,
        GraphicsPipeline,
        ComputePipeline,
        Shader,
        Framebuffer,
        Material,

        // NOT a kind: the number of them. A new kind is added ABOVE this line and turns
        // Desert/Tests/Engine/ResourceLedger red until it is named in ResourceKindName.
        Count,
    };

    /// Deliberately a switch with NO `default:` — see `Assets::AssetTypeName`, which this copies for the
    /// same reason: a `default:` swallows a new enumerator and puts a wrong name in the one message whose
    /// whole job is to be trusted.
    constexpr const char* ResourceKindName( const ResourceKind kind )
    {
        switch ( kind )
        {
            case ResourceKind::Image2D:
                return "Image2D";
            case ResourceKind::ImageCube:
                return "ImageCube";
            case ResourceKind::Image3D:
                return "Image3D";
            case ResourceKind::VertexBuffer:
                return "VertexBuffer";
            case ResourceKind::IndexBuffer:
                return "IndexBuffer";
            case ResourceKind::UniformBuffer:
                return "UniformBuffer";
            case ResourceKind::StorageBuffer:
                return "StorageBuffer";
            case ResourceKind::GraphicsPipeline:
                return "GraphicsPipeline";
            case ResourceKind::ComputePipeline:
                return "ComputePipeline";
            case ResourceKind::Shader:
                return "Shader";
            case ResourceKind::Framebuffer:
                return "Framebuffer";
            case ResourceKind::Material:
                return "Material";
            case ResourceKind::Count:
                return "Count";
        }
        return "Unknown";
    }

    /// WHO holds the object. The categories are LIFETIMES, not classes: what matters to both consumers —
    /// eviction and device-loss recovery — is who would have to rebuild the thing, not what C++ type it is.
    enum class ResourceOwner : uint8_t
    {
        /// Nobody has claimed it. The number this ledger exists to make visible; never a resting state
        /// for a resource somebody does own.
        Unclaimed = 0,

        /// A `Runtime::*Service` holds it, keyed on an asset handle. THE ONLY CATEGORY ASSET EVICTION MAY
        /// TOUCH: the asset's file is the recipe, so releasing it is recoverable by re-reading that file.
        AssetService,

        /// A `SceneRenderer` and the render systems inside it: framebuffers, LUTs, pipelines, the
        /// materials the passes own. Rebuilt only by rebuilding the renderer.
        SceneRenderer,

        /// The sky/IBL bake — panorama, radiance, irradiance, prefiltered. Recipe is the sky settings.
        Environment,

        /// The editor's own pictures: thumbnails, previews, panel slices, icon and font atlases.
        EditorTool,

        /// The 2D/UI backend and the ImGui layer.
        UserInterface,

        /// Swapchain, command pools, query pools, default and fallback textures — the device's own.
        Device,

        /// Built from no file at all: procedural meshes, runtime-generated images. There is no recipe on
        /// disk, so releasing one is DATA LOSS and eviction must never consider it.
        Procedural,

        Count,
    };

    constexpr const char* ResourceOwnerName( const ResourceOwner owner )
    {
        switch ( owner )
        {
            case ResourceOwner::Unclaimed:
                return "Unclaimed";
            case ResourceOwner::AssetService:
                return "AssetService";
            case ResourceOwner::SceneRenderer:
                return "SceneRenderer";
            case ResourceOwner::Environment:
                return "Environment";
            case ResourceOwner::EditorTool:
                return "EditorTool";
            case ResourceOwner::UserInterface:
                return "UserInterface";
            case ResourceOwner::Device:
                return "Device";
            case ResourceOwner::Procedural:
                return "Procedural";
            case ResourceOwner::Count:
                return "Count";
        }
        return "Unknown";
    }

    /**
     * @brief A LIVE ROW IN THE LEDGER, held BY the resource it describes. Move-only; destroying it is the
     *        removal.
     *
     * There is no `Register` and no `Unregister` anywhere in this header. `Take()` is the only way in and
     * `~ResourceOwnership` is the only way out, so the pair cannot be mismatched by anybody's discipline.
     *
     * A default-constructed token accounts for NOTHING and says so (`IsAccounted() == false`) rather than
     * pretending to be a row. That is what a resource constructed before the ledger's first use, or moved
     * out of, holds — and it is distinguishable from a live row, which is the whole of §1.4's rule.
     */
    class ResourceOwnership final
    {
    public:
        /// Accounts for nothing. Not a row; asking it anything says so.
        ResourceOwnership() = default;

        /// Open a row for a live object of @p kind. @p bytes is what the object costs on the device where
        /// the caller knows it and 0 where it does not — a COUNT is the number both consumers need, and a
        /// guessed size would be worse than an absent one.
        [[nodiscard]] static ResourceOwnership Take( ResourceKind kind, std::size_t bytes = 0 );

        ~ResourceOwnership();

        ResourceOwnership( ResourceOwnership&& other ) noexcept;
        ResourceOwnership& operator=( ResourceOwnership&& other ) noexcept;

        ResourceOwnership( const ResourceOwnership& )            = delete;
        ResourceOwnership& operator=( const ResourceOwnership& ) = delete;

        /// Is this token a row in the ledger?
        [[nodiscard]] bool IsAccounted() const noexcept
        {
            return m_Row != 0;
        }

        /// Say who holds the object, and — for an `AssetService` row — which asset's file is its recipe.
        /// Called by whoever takes ownership; see the header note on why this is not a constructor
        /// argument. Claiming an unaccounted token is a no-op, not a crash: a resource built before the
        /// first `Take()` is legal.
        void Claim( ResourceOwner owner, Common::AssetHandle asset = Common::AssetHandle{} );

        /// Correct the size once the object knows it (a Vulkan image learns its footprint only after the
        /// allocator answers). Silently ignored on an unaccounted token.
        void RecordBytes( std::size_t bytes );

        [[nodiscard]] ResourceOwner       GetOwner() const;
        [[nodiscard]] Common::AssetHandle GetAsset() const;

    private:
        explicit ResourceOwnership( uint64_t row ) noexcept : m_Row( row )
        {
        }

        /// The row's id, or 0 for "accounts for nothing". An id and not a pointer, because the ledger's
        /// storage is allowed to move and a token outliving a reallocation must not be holding an address
        /// into it.
        uint64_t m_Row = 0;
    };

    /**
     * @brief WHILE THIS OBJECT IS ALIVE, ANY ROW OPENED ON THIS THREAD BELONGS TO @p owner.
     *
     * The reason it exists is arithmetic. One `SceneRenderer` builds roughly a hundred and twenty device
     * objects across twenty render systems — framebuffers, LUTs, pipelines, the materials the passes own —
     * and every one of them is created by a different file. Claiming them one at a time means touching
     * twenty files to answer one question, and the twenty-first system, written next month, is attributed
     * by nobody and silently swells the "Unclaimed" figure that the whole ledger exists to make trustworthy.
     * One scope at the top of `EnsureRendererResources` attributes all of them, and a new system inside it
     * is attributed the day it is written.
     *
     * AN EXPLICIT `Claim()` ALWAYS WINS, because it happens later: a service that builds a texture inside
     * somebody's scope names the asset behind it afterwards, and that is the more specific truth. The scope
     * is a DEFAULT for rows nobody speaks for, not an override of the rows somebody does.
     *
     * Thread-local, and nested scopes restore the enclosing one — a bake inside a renderer's scope is still
     * the bake's.
     */
    class ResourceAttributionScope final
    {
    public:
        explicit ResourceAttributionScope( ResourceOwner owner ) noexcept;
        ~ResourceAttributionScope();

        ResourceAttributionScope( const ResourceAttributionScope& )            = delete;
        ResourceAttributionScope& operator=( const ResourceAttributionScope& ) = delete;
        ResourceAttributionScope( ResourceAttributionScope&& )                 = delete;
        ResourceAttributionScope& operator=( ResourceAttributionScope&& )      = delete;

    private:
        ResourceOwner m_Previous;
    };

    /// A whole-ledger answer, taken in ONE pass under ONE lock. A caller that asked for the total and then
    /// for the per-owner split in two calls would be handed two halves of two different instants, and the
    /// difference between them reads exactly like a leak.
    struct ResourceCensus
    {
        /// Every live row.
        uint32_t Live = 0;
        /// Rows nobody has claimed. THE number Г7-A plans against.
        uint32_t Unclaimed = 0;
        /// Rows with no asset behind them — everything asset eviction can never reach, claimed or not.
        uint32_t WithoutAsset = 0;
        /// Bytes, summed over the rows that reported a size. Rows that did not are counted in `Live` and
        /// contribute nothing here; `BytesKnownFor` says how many did, so a reader can tell a small total
        /// from an unmeasured one.
        uint64_t Bytes         = 0;
        uint32_t BytesKnownFor = 0;

        /// Live rows per kind and per owner, indexed by the enumerators above.
        uint32_t PerKind[static_cast<std::size_t>( ResourceKind::Count )]   = {};
        uint32_t PerOwner[static_cast<std::size_t>( ResourceOwner::Count )] = {};

        /// Live rows per (owner x kind) — the table that answers "who owns the objects nobody claimed".
        uint32_t PerOwnerKind[static_cast<std::size_t>( ResourceOwner::Count )]
                             [static_cast<std::size_t>( ResourceKind::Count )] = {};
    };

    class ResourceLedger final
    {
    public:
        /// The census, at this instant.
        [[nodiscard]] static ResourceCensus Take();

        /// The census as lines a person reads, owners first and then the kinds under each. Written for the
        /// log and for the control channel; the numbers are the same object as `Take()`.
        [[nodiscard]] static std::string Report();

        /// Asset handles the ledger currently has an `AssetService` row for. Asset eviction's INPUT: the
        /// set it is allowed to consider, which is by construction the set that can be rebuilt from a file.
        [[nodiscard]] static std::vector<Common::AssetHandle> AssetBackedHandles();

        /// How many live rows name @p asset. Zero for an asset whose GPU objects have been released — and
        /// that is how a test tells eviction happened from outside the services.
        [[nodiscard]] static uint32_t RowsFor( Common::AssetHandle asset );

    private:
        friend class ResourceOwnership;

        static uint64_t Open( ResourceKind kind, std::size_t bytes );
        static void     Close( uint64_t row );
        static void     Attribute( uint64_t row, ResourceOwner owner, Common::AssetHandle asset );
        static void     SetBytes( uint64_t row, std::size_t bytes );
        static bool     Read( uint64_t row, ResourceOwner& owner, Common::AssetHandle& asset );
    };

} // namespace Desert::Graphic
