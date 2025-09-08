#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Materials/Properties/StorageBufferProperty.hpp>

namespace Desert::Graphic::MaterialHelper
{
    template <typename MaterialUB>
    class MaterialWrapperStorage
    {
    public:
        explicit MaterialWrapperStorage( const std::shared_ptr<MaterialExecutor>& baseMaterial,
                                         std::string&&                            storageName )
             : m_MaterialExecutor( baseMaterial ), m_StorageName( std::move( storageName ) )
        {
            m_StorageProperty = m_MaterialExecutor->GetStorageBufferProperty( m_StorageName );
        }

        virtual ~MaterialWrapperStorage() = default;

        virtual const std::shared_ptr<MaterialExecutor>& GetMaterialExecutor() const final
        {
            return m_MaterialExecutor;
        }

        virtual void Update( const MaterialUB& props, const std::size_t size ) final
        {
            m_Data = props;
            m_StorageProperty->SetData( &m_Data, size );
        }

    private:
        MaterialUB m_Data;

    protected:
        std::shared_ptr<MaterialExecutor>      m_MaterialExecutor;
        std::string                            m_StorageName;
        std::shared_ptr<StorageBufferProperty> m_StorageProperty;
    };
} // namespace Desert::Graphic::MaterialHelper

#define DEFINE_MATERIAL_WRAPPER_STORAGE( className, templateParam, stringParam )                                  \
    class className final : public Desert::Graphic::MaterialHelper::MaterialWrapperStorage<templateParam>         \
    {                                                                                                             \
    public:                                                                                                       \
        explicit className( const std::shared_ptr<Desert::Graphic::MaterialExecutor>& material )                  \
             : MaterialWrapperStorage( material, ##stringParam )                                                  \
        {                                                                                                         \
        }                                                                                                         \
    };