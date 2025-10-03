#pragma once

#include <Common/Core/ResultStr.hpp>

#include <Engine/Graphic/Shader.hpp>
#include "BaseBuffer.hpp"

namespace Desert::Uniforms
{
    class UniformBuffer : public BaseBuffer
    {
    public:
        explicit UniformBuffer( const Core::Models::UniformBuffer& uniform );
        virtual ~UniformBuffer() = default;

        virtual const uint32_t GetBinding() const override final
        {
            return m_UniformModel.BindingPoint;
        }

        virtual const uint32_t GetSize() const override final
        {
            return m_UniformModel.Size;
        }

        virtual const std::vector<Core::Models::Common::Field>& GetFields() const override final
        {
            return m_UniformModel.Fields;
        }

    protected:
        Core::Models::UniformBuffer m_UniformModel;

    private:
        static std::shared_ptr<UniformBuffer> Create( const Core::Models::UniformBuffer& uniform );

        friend class UniformManager;
    };

} // namespace Desert::Uniforms