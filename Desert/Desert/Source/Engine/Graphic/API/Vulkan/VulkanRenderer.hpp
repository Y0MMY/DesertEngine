#pragma once

#include <Engine/Graphic/RendererAPI.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanRendererAPI : public RendererAPI
    {
    public:
        virtual void Init() override;

        virtual [[nodiscard]] Common::BoolResult BeginFrame() override;
        virtual [[nodiscard]] Common::BoolResult EndFrame() override;
        virtual [[nodiscard]] Common::BoolResult PresentFinalImage() override;
        virtual [[nodiscard]] Common::BoolResult
                                   BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass ) override;
        virtual Common::BoolResult BeginSwapChainRenderPass() override;
        virtual [[nodiscard]] Common::BoolResult EndRenderPass() override;
        virtual void RenderMesh( const std::shared_ptr<Pipeline>& pipeline, const std::shared_ptr<Mesh>& mesh,
                                 const glm::mat4& mvp /*TEMP*/ ) override;

        virtual void SubmitFullscreenQuad( const std::shared_ptr<Pipeline>& pipeline,
                                           const std::shared_ptr<Material>& material ) override;

        virtual void ResizeWindowEvent( uint32_t width, uint32_t height,
                                        const std::vector<std::shared_ptr<Framebuffer>>& framebuffers ) override;

        virtual std::shared_ptr<Framebuffer> GetCompositeFramebuffer() const override;

        virtual std::shared_ptr<Image2D> ConvertPanoramaToCubeMap_4x3( const Common::Filepath& filepath ) override;
        virtual std::shared_ptr<Image2D> CreateDiffuseIrradiance( const Common::Filepath& filepath ) override;
        virtual std::shared_ptr<Image2D> CreatePrefilteredMap( const Common::Filepath& filepath ) override;
        virtual std::shared_ptr<Image2D> CreateEnvironmentMap( const Common::Filepath& filepath ) override;
#ifdef DESERT_CONFIG_DEBUG
        virtual [[nodiscard]] Common::BoolResult
        GenerateMipMaps( const std::shared_ptr<Image2D>& image ) const override;
#endif

    private:
        void SetViewportAndScissor();

#ifdef DESERT_CONFIG_DEBUG
        void GenerateMipmaps2D( const std::shared_ptr<Image2D>& image ) const;
        void GenerateMipmapsCubemap( const std::shared_ptr<Image2D>& image ) const;
#endif

    private:
        VkCommandBuffer m_CurrentCommandBuffer = nullptr;

        std::shared_ptr<Framebuffer> m_CompositeFramebuffer;
    };

} // namespace Desert::Graphic::API::Vulkan