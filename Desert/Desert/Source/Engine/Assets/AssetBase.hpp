#pragma once

#include <Common/Core/Result.hpp>
#include <Common/Core/Core.hpp>
#include <Common/Core/UUID.hpp>

namespace Desert::Assets
{
    using AssetHandle = Common::UUID;

    template <typename T>
    using Asset = std::shared_ptr<T>;

    using NullAsset = nullptr_t;

    enum class AssetPriority
    {
        Low    = 0,
        Medium = 1,
        High   = 2,
    };

    enum class AssetTypeID
    {
        Unknown = 0,
        Mesh,
        Material,
    };

    class MeshAsset;

    class AssetBase
    {
    public:
        virtual ~AssetBase() = default;

        virtual const AssetPriority GetPriority() const final
        {
            return m_Priority;
        }

        virtual const AssetHandle GetHandle() const final
        {
            return m_Handle;
        }

        virtual const Common::Filepath& GetFilepath() const final
        {
            return m_Filepath;
        }

        virtual Common::BoolResult Load()   = 0;
        virtual Common::BoolResult Unload() = 0;

        virtual bool IsReadyForUse() const = 0;

        explicit AssetBase( const AssetPriority priority, const Common::Filepath& filepath )
             : m_Handle( Common::UUID() ), m_Priority( priority ), m_Filepath( filepath )
        {
        }

    protected:
        AssetHandle      m_Handle;
        AssetPriority    m_Priority;
        Common::Filepath m_Filepath;
    };

} // namespace Desert::Assets