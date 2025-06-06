#pragma once

#include <Common/Core/Result.hpp>

#include <Engine/Graphic/Shader.hpp>

namespace Desert::Graphic
{
    class UniformBuffer;

    /*
      in the startup phase already prepares all the UB information from the shader. To use the UB, it is enough
      just to get it from GetUniformBuffer()
     */
    class UniformBufferManager final
    {
    public:
        explicit UniformBufferManager( const std::string_view debugName, const std::shared_ptr<Shader>& shader );

        // maybe std::reference_wrapper? 
        Common::Result<std::shared_ptr<UniformBuffer>> GetUniformBuffer( const std::string& name ) const;

        static std::shared_ptr<UniformBufferManager> Create( const std::string_view         debugName,
                                                             const std::shared_ptr<Shader>& shader );

    private:
        void AddBuffer( std::shared_ptr<UniformBuffer>&& buffer, const std::string& name );

    private:
        std::vector<std::shared_ptr<UniformBuffer>> m_UniformBuffers;
        std::unordered_map<std::string, uint32_t>   m_NameMap;
        const std::string_view                      m_DebugName;
    };

    class UniformBuffer
    {
    public:
        virtual ~UniformBuffer() = default;

        virtual void SetData( const void* data, uint32_t size, uint32_t offset = 0 )    = 0;
        virtual void RT_SetData( const void* data, uint32_t size, uint32_t offset = 0 ) = 0;

        virtual const uint32_t GetBinding() const = 0;
        virtual const uint32_t GetSize() const    = 0;

    private:
        static std::shared_ptr<UniformBuffer> Create( uint32_t size, uint32_t binding );

        friend class UniformBufferManager;
    };
} // namespace Desert::Graphic