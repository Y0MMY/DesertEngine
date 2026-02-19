#pragma once

#include <Common/Core/ResultStr.hpp>

#include <Engine/Graphic/Shader.hpp>
#include "BaseBuffer.hpp"

namespace Desert::ShaderResources
{
    class UniformBuffer : public BaseBuffer
    {
    public:
        explicit UniformBuffer( const ShaderLayout::UniformBuffer& uniform );
        virtual ~UniformBuffer() = default;

        virtual const uint32_t GetBinding() const override final
        {
            return m_UniformModel.BindingPoint;
        }

        virtual const uint32_t GetSize() const override final
        {
            return m_UniformModel.Size;
        }

        virtual const std::vector<ShaderLayout::ShaderFieldLayout>& GetFields() const override final
        {
            return m_UniformModel.Fields;
        }

    protected:
        ShaderLayout::UniformBuffer m_UniformModel;

    private:
        static std::shared_ptr<UniformBuffer> Create( const ShaderLayout::UniformBuffer& uniform );

        friend class ShaderResourcesManager;
    };

} // namespace Desert::ShaderResources