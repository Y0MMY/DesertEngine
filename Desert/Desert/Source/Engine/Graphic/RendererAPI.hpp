#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/Mesh.hpp>

namespace Desert::Graphic
{
    enum class RendererAPIType : uint8_t
    {
        None   = 0,
        Vulkan = 1,
    };

    class RendererAPI
    {
    public:
        virtual ~RendererAPI() = default;

    public:
        virtual void Init() = 0;

        virtual Common::BoolResult BeginFrame()                                                     = 0;
        virtual Common::BoolResult EndFrame()                                                       = 0;
        virtual Common::BoolResult PresentFinalImage()                                              = 0;
        virtual Common::BoolResult BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass ) = 0;
        virtual Common::BoolResult BeginSwapChainRenderPass()                                       = 0;
        virtual Common::BoolResult EndRenderPass()                                                  = 0;
        virtual void RenderMesh( const std::shared_ptr<Pipeline>& pipeline, const std::shared_ptr<Mesh>& mesh,
                                 const glm::mat4& mvp /*TEMP*/ )                                    = 0;
        virtual void SubmitFullscreenQuad( const std::shared_ptr<Pipeline>& pipeline,
                                           const std::shared_ptr<Material>& material )              = 0;

        virtual void ResizeWindowEvent( uint32_t width, uint32_t height,
                                        const std::vector<std::shared_ptr<Framebuffer>>& framebuffers ) = 0;
#ifdef DESERT_CONFIG_DEBUG
        virtual std::shared_ptr<ImageCube> ConvertPanoramaToCubeMap_4x3( const Common::Filepath& filepath )  = 0;
        virtual std::shared_ptr<ImageCube> CreateDiffuseIrradiance( const Common::Filepath& filepath )       = 0;
        virtual Common::BoolResult         CreatePrefilteredMap( const std::shared_ptr<ImageCube>& imageCube ) = 0;
        virtual std::shared_ptr<ImageCube> CreateEnvironmentMap( const Common::Filepath& filepath )          = 0;

        // NOTE: In the release version we do not support direct generation of mips, they have to be uploaded
        virtual [[nodiscard]] Common::BoolResult
        GenerateMipMaps( const std::shared_ptr<Image2D>& image ) const = 0;
#endif
        virtual std::shared_ptr<Framebuffer> GetCompositeFramebuffer() const = 0;

    public:
        static const RendererAPIType GetAPIType()
        {
            return s_RenderingAPI;
        }

    private:
        static inline RendererAPIType s_RenderingAPI = RendererAPIType::Vulkan;
    };

} // namespace Desert::Graphic