#pragma once

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

namespace Desert::Graphic
{
    class Material
    {
    public:
        explicit Material( std::string&& debugName, std::string&& shaderName )
             : m_MaterialExecutor(
                    Graphic::MaterialExecutor::Create( std::move( debugName ), std::move( shaderName ) ) )
        {
        }

        virtual ~Material() = default;

        virtual std::shared_ptr<MaterialExecutor> GetMaterialExecutor() const final
        {
            return m_MaterialExecutor;
        }

        bool IsDirty() const
        {
            return m_ParametersDirty;
        }

        void MarkDirty()
        {
            m_ParametersDirty = true;
        }

        void ClearDirty()
        {
            m_ParametersDirty = false;
        }

     /*   const auto& GetStorageData() const
        {
            return m_TMaterialOverrideData;
        }*/


    protected:
        std::shared_ptr<MaterialExecutor> m_MaterialExecutor;
        bool                              m_ParametersDirty = false;
    };
} // namespace Desert::Graphic