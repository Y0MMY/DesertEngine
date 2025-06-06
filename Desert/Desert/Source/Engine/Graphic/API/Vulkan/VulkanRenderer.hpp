#pragma once
#pragma once

#include <Engine/Graphic/RendererAPI.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanRendererAPI : public RendererAPI
    {
    public:
        virtual void Init() override;
        virtual void Shutdown() override;

        [[nodiscard]] virtual Common::BoolResult BeginFrame() override;
        [[nodiscard]] virtual Common::BoolResult EndFrame() override;
        [[nodiscard]] virtual Common::BoolResult PrepareNextFrame() override;
        [[nodiscard]] virtual Common::BoolResult PresentFinalImage() override;
        [[nodiscard]] virtual Common::BoolResult
                                   BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass ) override;
        virtual Common::BoolResult BeginSwapChainRenderPass() override;
        [[nodiscard]] virtual Common::BoolResult EndRenderPass() override;
        virtual void RenderMesh( const std::shared_ptr<Pipeline>& pipeline, const std::shared_ptr<Mesh>& mesh,
                                 const std::shared_ptr<Material>& material ) override;

        virtual void SubmitFullscreenQuad( const std::shared_ptr<Pipeline>& pipeline,
                                           const std::shared_ptr<Material>& material ) override;

        virtual void ResizeWindowEvent( uint32_t width, uint32_t height ) override;

        virtual std::shared_ptr<Framebuffer> GetCompositeFramebuffer() const override;

#ifdef DESERT_CONFIG_DEBUG
        virtual std::shared_ptr<ImageCube>
        ConvertPanoramaToCubeMap_4x3( const Common::Filepath& filepath ) override;
        virtual std::shared_ptr<ImageCube> CreateDiffuseIrradiance( const Common::Filepath& filepath ) override;
        virtual Common::BoolResult CreatePrefilteredMap( const std::shared_ptr<ImageCube>& imageCube ) override;
        virtual PBRTextures        CreateEnvironmentMap( const Common::Filepath& filepath ) override;
        [[nodiscard]] virtual Common::BoolResult
        GenerateMipMaps( const std::shared_ptr<Image2D>& image ) const override;
#endif
        VkCommandBuffer GetCurrentCmdBuffer() const;

    private:
        void SetViewportAndScissor();

#ifdef DESERT_CONFIG_DEBUG
        void GenerateMipmaps2D( const std::shared_ptr<Image2D>& image ) const;
#endif

    private:
        VkCommandBuffer m_CurrentCommandBuffer = nullptr;

        std::shared_ptr<Framebuffer> m_CompositeFramebuffer;
    };

} // namespace Desert::Graphic::API::Vulkan