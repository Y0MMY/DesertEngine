#pragma once

#include <Engine/Uniforms/UniformBuffer.hpp>
#include <Engine/Uniforms/UniformImageCube.hpp>
#include <Engine/Uniforms/UniformImage2D.hpp>

namespace Desert::Uniforms
{
    /*
      in the startup phase already prepares all the UB information from the shader. To use the UB, it is enough
      just to get it from GetUniformBuffer()
     */
    class UniformManager final
    {
    public:
        explicit UniformManager( const std::string_view                  debugName,
                                 const std::shared_ptr<Graphic::Shader>& shader );

        struct BuffersData
        {
            std::vector<std::shared_ptr<UniformBuffer>> Data;
            std::unordered_map<std::string, uint32_t>   Names;
        };

        struct ImageCubeData
        {
            std::vector<std::shared_ptr<UniformImageCube>> Data;
            std::unordered_map<std::string, uint32_t>      Names;
        };

        struct Image2DData
        {
            std::vector<std::shared_ptr<UniformImage2D>> Data;
            std::unordered_map<std::string, uint32_t>    Names;
        };

        // maybe std::reference_wrapper?
        Common::Result<std::shared_ptr<UniformBuffer>>    GetUniformBuffer( const std::string& name ) const;
        Common::Result<std::shared_ptr<UniformImageCube>> GetUniformImageCube( const std::string& name ) const;
        Common::Result<std::shared_ptr<UniformImage2D>>   GetUniformImage2D( const std::string& name ) const;

        const auto& GetUniformBufferTotal()
        {
            return m_BufferData;
        }

        const auto& GetUniformImageCubeTotal() const
        {
            return m_ImageCubeData;
        }

        const auto& GetUniformImage2DTotal() const
        {
            return m_Image2DData;
        }

        static std::shared_ptr<UniformManager> Create( const std::string_view                  debugName,
                                                       const std::shared_ptr<Graphic::Shader>& shader );

    private:
        void AddBuffer( std::shared_ptr<UniformBuffer>&& buffer, const std::string& name );
        void AddImageCube( std::shared_ptr<UniformImageCube>&& buffer, const std::string& name );
        void AddImage2D( std::shared_ptr<UniformImage2D>&& buffer, const std::string& name );

    private:
        BuffersData   m_BufferData;
        ImageCubeData m_ImageCubeData;
        Image2DData   m_Image2DData;

        const std::string_view m_DebugName;
    };
} // namespace Desert::Uniforms