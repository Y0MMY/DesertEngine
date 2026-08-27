#pragma once

#include <Common/Core/ResultStr.hpp>

#include <Engine/Graphic/Shader.hpp>
#include "BaseBuffer.hpp"

namespace Desert::ShaderResources
{
    class UniformBuffer : public BaseBuffer
    {
    public:
        // Inline on purpose: this was the type's ONLY out-of-line symbol, and its .cpp pulls in
        // RendererAPI and VulkanUniformBuffer for Create(). A test that wants a device-free buffer to
        // drive UniformBufferProperty had to link that whole chain for a member-wise copy. Create() stays
        // where it is — the backend choice genuinely belongs to the .cpp.
        explicit UniformBuffer( const ShaderLayout::UniformBuffer& uniform ) : m_UniformModel( uniform )
        {
        }
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

        // The block name from shader reflection ("CameraUB", "MaterialUB", ...). Exists so a refusal can
        // say WHICH buffer refused: "a uniform buffer rejected a write" is not actionable, and the
        // defect this guards against was found by staring at a frame rather than at a message.
        const std::string& GetName() const
        {
            return m_UniformModel.Name;
        }

    protected:
        ShaderLayout::UniformBuffer m_UniformModel;

    private:
        static std::shared_ptr<UniformBuffer> Create( const ShaderLayout::UniformBuffer& uniform );

        friend class ShaderResourcesManager;
    };

} // namespace Desert::ShaderResources