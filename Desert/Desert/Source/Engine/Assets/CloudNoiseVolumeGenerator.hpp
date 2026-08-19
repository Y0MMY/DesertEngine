#pragma once

#include <Engine/Assets/CloudNoiseVolume.hpp>

#include <atomic>

namespace Desert::Assets
{
    /**
     * @brief Produces a cloud noise volume from its parameters, on the CPU.
     *
     * WHY THE CPU, GIVEN THE OLD PATH WAS A COMPUTE SHADER. Because the point of this work is that the
     * volume can be SAVED, and `Graphic::Image3D` has no CPU readback (Docs/Clouds/ANALYSIS_APPROACH.md
     * section 2.3): a compute pass can fill a volume but nothing can get it back out to write a file. The
     * arithmetic is the same arithmetic either way — `Common/CloudNoise.glslh` is compiled as C++ here,
     * exactly as `Graphic::SkyGroundTransmittance` compiles `Common/SkyMedium.glslh` — so moving it offline
     * costs nothing but the wall clock of a bake nobody watches per frame.
     *
     * PURE. Same parameters, same bytes, on every machine and in every configuration: the hash is an
     * integer finaliser rather than `fract(sin(...))` precisely so that this holds. That is what lets the
     * container store the recipe and mean it, and what lets the test compare a decoded file against a
     * freshly generated volume byte for byte.
     *
     * COST, MEASURED rather than assumed, for the default 128^3 on this machine: 9.6 s in an optimised
     * build and 82 s unoptimised, single-threaded — which is why the work is spread across the hardware
     * threads (the voxels are independent, so the split changes nothing but the wall clock) and why the
     * engine does NOT generate a volume at load time. A scene with an empty slot loads the default volume
     * ASSET; see Graphic::System::VolumetricCloudRenderer.
     *
     * @param params   validated by the function itself; an invalid set is an error, not a clamp.
     * @param progress optional, written with the fraction of slices finished so a panel can show a bar.
     *                 Read by another thread, hence atomic.
     */
    Common::ResultStr<CloudNoiseVolumeData> GenerateCloudNoiseVolume( const CloudNoiseVolumeParams& params,
                                                                      std::atomic<float>* progress = nullptr );

    /**
     * @brief The four channel values at one point of the unit cube, in [0, 1].
     *
     * The generator's own per-voxel function, exposed because the slice preview in the editor panel and
     * the test that asserts "what the generator writes is what the sampler reads" both need to evaluate a
     * single point without baking eight million of them.
     */
    void SampleCloudNoiseVolumeChannels( const CloudNoiseVolumeParams& params, float u, float v, float w,
                                         float outChannels[4] );
} // namespace Desert::Assets
