#pragma once

#include <Engine/ECS/VolumetricCloudsComponent.hpp>

#include <array>
#include <cstdint>

namespace Desert::Graphic
{
    /**
     * How many volumetric cloud layers one view can carry.
     *
     * TWO, and the number is a statement about the RAY rather than about memory: the march walks the
     * layers' shells in the order the ray meets them (Graphic::CloudPlanTwoShells builds that order), and
     * the plan a straight line through two disjoint concentric shells can produce has at most three
     * intervals — which is what CLOUD_MAX_MARCH_SEGMENTS in Common/CloudGeometry.glslh is. A third layer
     * is not a bigger array: it is a different plan builder, with its own proof that the intervals come
     * out ordered and disjoint. Raising this without writing that is how "near over far" quietly stops
     * being true.
     *
     * The reference's own wide frames (Nubis3 pp. 125/126/150) are all cumulus-plus-cirrus: a deck low
     * down and a thin sheet high above it. Two is what that picture needs.
     */
    inline constexpr uint32_t kCloudMaxLayers = 2;

    /**
     * This frame's cloud layers, in ALTITUDE ORDER — Layers[0] is the lowest.
     *
     * The order is fixed on the CPU, once, by ECS::VolumetricCloudsECSSystem, and two things depend on it:
     *
     *   * Layers[0] is the PRIMARY layer. The five settings that belong to the VIEW rather than to a
     *     layer — Resolution Scale, Temporal Mode, Temporal Blend Factor, Temporal Clamp Scale and
     *     Jitter Strength — are read from it and from it alone. There is one raymarch target, one
     *     history pair and one ray per pixel, so there is one answer to each of those questions; taking
     *     it from the lowest layer makes the answer deterministic instead of dependent on which entity
     *     happened to be created first. The Details panel says so on every layer that is not the primary
     *     (see the widget), so the fields are scoped rather than silently ignored.
     *
     *   * Every other setting is genuinely per layer, including the ones in the Quality group that
     *     describe THE MARCH THROUGH A SHELL rather than the frame: Max Steps, the step schedule, the
     *     light march, the multi-scatter octaves, the ambient occlusion and the cloud shadow map. A thin
     *     cirrus sheet 1.2 km thick and a 3.5 km cumulus deck want different numbers for every one of
     *     them, and each layer's Max Steps is its own budget so that a deck the ray crosses first cannot
     *     starve a sheet it crosses second.
     */
    struct CloudLayerSet
    {
        std::array<ECS::VolumetricCloudData, kCloudMaxLayers> Layers{};

        // How many entries of Layers are live. 0 means "this scene has no cloud layer at all", which is
        // said explicitly rather than by the command's absence — see Render::VolumetricCloudsCommand.
        uint32_t Count = 0;

        [[nodiscard]] bool Empty() const
        {
            return Count == 0;
        }

        // The layer whose view-wide settings the renderer obeys, and whose Enabled is the master switch
        // for the whole subsystem. Safe to call on an empty set: it answers with the default-constructed
        // row, which has Enabled = true but is never marched because Count is 0.
        [[nodiscard]] const ECS::VolumetricCloudData& Primary() const
        {
            return Layers[0];
        }
    };
} // namespace Desert::Graphic
