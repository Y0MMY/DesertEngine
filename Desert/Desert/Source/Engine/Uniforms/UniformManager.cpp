#include <Engine/Uniforms/UniformManager.hpp>

namespace Desert::Uniforms
{
    void UniformManager::AddBuffer( std::shared_ptr<UniformBuffer>&& buffer, const std::string& name )
    {
        const auto index = m_BufferData.Data.size();
        m_BufferData.Data.push_back( std::move( buffer ) );
        m_BufferData.Names[name] = index;
    }

    void UniformManager::AddImageCube( std::shared_ptr<UniformImageCube>&& buffer, const std::string& name )
    {
        const auto index = m_ImageCubeData.Data.size();
        m_ImageCubeData.Data.push_back( std::move( buffer ) );
        m_ImageCubeData.Names[name] = index;
    }

    void UniformManager::AddImage2D( std::shared_ptr<UniformImage2D>&& buffer, const std::string& name )
    {
        const auto index = m_Image2DData.Data.size();
        m_Image2DData.Data.push_back( std::move( buffer ) );
        m_Image2DData.Names[name] = index;
    }

    Common::Result<std::shared_ptr<UniformBuffer>>
    UniformManager::GetUniformBuffer( const std::string& name ) const
    {
        auto it = m_BufferData.Names.find( name );
        if ( it == m_BufferData.Names.end() )
        {

            return Common::MakeFormattedError<std::shared_ptr<UniformBuffer>>(
                 "Uniform '{}' not found in material", name );
        }

        return Common::MakeSuccess( m_BufferData.Data[it->second] );
    }

    Common::Result<std::shared_ptr<UniformImageCube>>
    UniformManager::GetUniformImageCube( const std::string& name ) const
    {
        auto it = m_ImageCubeData.Names.find( name );
        if ( it == m_ImageCubeData.Names.end() )
        {

            return Common::MakeFormattedError<std::shared_ptr<UniformImageCube>>(
                 "Uniform '{}' not found in material", name );
        }

        return Common::MakeSuccess( m_ImageCubeData.Data[it->second] );
    }

    Common::Result<std::shared_ptr<Desert::Uniforms::UniformImage2D>>
    UniformManager::GetUniformImage2D( const std::string& name ) const
    {
        auto it = m_Image2DData.Names.find( name );
        if ( it == m_Image2DData.Names.end() )
        {

            return Common::MakeFormattedError<std::shared_ptr<UniformImage2D>>(
                 "Uniform '{}' not found in material", name );
        }

        return Common::MakeSuccess( m_Image2DData.Data[it->second] );
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
            const auto& models        = shader->GetUniformBufferModels();
            const auto& storageModels = shader->GetStorageBufferModels();
            m_BufferData.Data.reserve( models.size() + storageModels.size() );
            m_BufferData.Names.reserve( models.size() + storageModels.size() );

            for ( const auto& model : models )
            {
                AddBuffer( UniformBuffer::Create( debugName, model.Size, model.BindingPoint ), model.Name );
            }

            for ( const auto& model : storageModels )
            {
                AddBuffer( UniformBuffer::Create( debugName, model.Size, model.BindingPoint ), model.Name );
            }
            // Maybe it should be separated?
        }

        // ImageCube
        {
            const auto& models = shader->GetUniformImageCubeModels();
            m_ImageCubeData.Data.reserve( models.size() );
            m_ImageCubeData.Names.reserve( models.size() );

            for ( const auto& model : models )
            {
                AddImageCube( UniformImageCube::Create( debugName, model.BindingPoint ), model.Name );
            }
        }

        // Image2D
        {
            const auto& models = shader->GetUniformImage2DModels();
            m_Image2DData.Data.reserve( models.size() );
            m_Image2DData.Names.reserve( models.size() );

            for ( const auto& model : models )
            {
                AddImage2D( UniformImage2D::Create( debugName, model.BindingPoint ), model.Name );
            }
        }
    }

} // namespace Desert::Uniforms