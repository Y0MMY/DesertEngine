#include <Engine/Uniforms/UniformManager.hpp>

namespace Desert::Uniforms
{
    void UniformManager::AddBuffer( std::shared_ptr<UniformBuffer>&& buffer, const std::string& name )
    {
        const auto index = m_Data.Buffers.size();
        m_Data.Buffers.push_back( std::move( buffer ) );
        m_UniformNames.Buffers[name] = index;
    }

    void UniformManager::AddImageCube( std::shared_ptr<UniformImageCube>&& buffer, const std::string& name )
    {
        const auto index = m_Data.ImageCubes.size();
        m_Data.ImageCubes.push_back( std::move( buffer ) );
        m_UniformNames.ImageCubes[name] = index;
    }

    void UniformManager::AddImage2D( std::shared_ptr<UniformImage2D>&& buffer, const std::string& name )
    {
        const auto index = m_Data.Image2D.size();
        m_Data.Image2D.push_back( std::move( buffer ) );
        m_UniformNames.Image2D[name] = index;
    }

    Common::Result<std::shared_ptr<UniformBuffer>>
    UniformManager::GetUniformBuffer( const std::string& name ) const
    {
        auto it = m_UniformNames.Buffers.find( name );
        if ( it == m_UniformNames.Buffers.end() )
        {

            return Common::MakeFormattedError<std::shared_ptr<UniformBuffer>>(
                 "Uniform '{}' not found in material", name );
        }

        return Common::MakeSuccess( m_Data.Buffers[it->second] );
    }

    Common::Result<std::shared_ptr<UniformImageCube>>
    UniformManager::GetUniformImageCube( const std::string& name ) const
    {
        auto it = m_UniformNames.ImageCubes.find( name );
        if ( it == m_UniformNames.ImageCubes.end() )
        {

            return Common::MakeFormattedError<std::shared_ptr<UniformImageCube>>(
                 "Uniform '{}' not found in material", name );
        }

        return Common::MakeSuccess( m_Data.ImageCubes[it->second] );
    }

    Common::Result<std::shared_ptr<Desert::Uniforms::UniformImage2D>>
    UniformManager::GetUniformImage2D( const std::string& name ) const
    {
        auto it = m_UniformNames.Image2D.find( name );
        if ( it == m_UniformNames.Image2D.end() )
        {

            return Common::MakeFormattedError<std::shared_ptr<UniformImage2D>>(
                 "Uniform '{}' not found in material", name );
        }

        return Common::MakeSuccess( m_Data.Image2D[it->second] );
    }

    std::shared_ptr<UniformManager> UniformManager::Create( const std::string_view                  debugName,
                                                            const std::shared_ptr<Graphic::Shader>& shader )
    {
        return std::make_shared<UniformManager>( debugName, shader );
    }

    UniformManager::UniformManager( const std::string_view                  debugName,
                                    const std::shared_ptr<Graphic::Shader>& shader )
         : m_DebugName( debugName )
    {

        // Buffers
        {
            const auto& models = shader->GetUniformBufferModels();
            m_Data.Buffers.reserve( models.size() );
            m_UniformNames.Buffers.reserve( models.size() );

            for ( const auto& model : models )
            {
                AddBuffer( UniformBuffer::Create( debugName, model.Size, model.BindingPoint ), model.Name );
            }
        }

        // ImageCube
        {
            const auto& models = shader->GetUniformImageCubeModels();
            m_Data.ImageCubes.reserve( models.size() );
            m_UniformNames.ImageCubes.reserve( models.size() );

            for ( const auto& model : models )
            {
                AddImageCube( UniformImageCube::Create( debugName, model.BindingPoint ), model.Name );
            }
        }

        // Image2D
        {
            const auto& models = shader->GetUniformImage2DModels();
            m_Data.Image2D.reserve( models.size() );
            m_UniformNames.Image2D.reserve( models.size() );

            for ( const auto& model : models )
            {
                AddImage2D( UniformImage2D::Create( debugName, model.BindingPoint ), model.Name );
            }
        }
    }

} // namespace Desert::Uniforms