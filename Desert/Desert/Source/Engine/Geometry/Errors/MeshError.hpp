#pragma once

namespace Desert
{
    enum class MeshError
    {
        ImportError,
        NoAnimationData,
        InvalidBoneStructure,
        NoBonesFound,
        // A vertex or index buffer could not be uploaded. Every `Mesh::Invalidate` used to return
        // success unconditionally while dropping both `RT_Invalidate` results, so a mesh whose GPU
        // allocation failed was indistinguishable from one that uploaded — it simply drew nothing.
        GpuUploadFailed,
    };

    namespace Mapper
    {

    }
} // namespace Desert