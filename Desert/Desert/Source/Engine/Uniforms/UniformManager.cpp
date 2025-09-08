#include <Engine/Uniforms/UniformManager.hpp>

namespace Desert::Uniforms
{
    void UniformManager::AddUniformBuffer( std::shared_ptr<UniformBuffer>&& buffer, const std::string& name )
    {
        const auto index = m_UniformBuffersData.Data.size();
        m_UniformBuffersData.Data.push_back( std::move( buffer ) );
        m_UniformBuffersData.Names[name] = index;
    }

    void UniformManager::AddStorageBuffer( std::shared_ptr<StorageBuffer>&& buffer, const std::string& name )
    {
        const auto index = m_SrorageBuffersData.Data.size();
        m_SrorageBuffersData.Data.push_back( std::move( buffer ) );
        m_SrorageBuffersData.Names[name] = index;
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
        auto it = m_UniformBuffersData.Names.find( name );
        if ( it == m_UniformBuffersData.Names.end() )
        {
            return Common::MakeFormattedError<std::shared_ptr<UniformBuffer>>(
                 "Uniform '{}' not found in material", name );
        }

        return Common::MakeSuccess( m_UniformBuffersData.Data[it->second] );
    }

    Common::Result<std::shared_ptr<Desert::Uniforms::StorageBuffer>>
    UniformManager::GetStorageBuffer( const std::string& name ) const
    {
        auto it = m_SrorageBuffersData.Names.find( name );
        if ( it == m_SrorageBuffersData.Names.end() )
        {
            return Common::MakeFormattedError<std::shared_ptr<StorageBuffer>>(
                 "Storage buffer '{}' not found in material", name );
        }

        return Common::MakeSuccess( m_SrorageBuffersData.Data[it->second] );
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
            m_UniformBuffersData.Data.reserve( models.size() + storageModels.size() );
            m_SrorageBuffersData.Names.reserve( models.size() + storageModels.size() );

            for ( const auto& model : models )
            {
                AddUniformBuffer( UniformBuffer::Create( model.Name, model.Size, model.BindingPoint ),
                                  model.Name );
            }

            // Maybe it should be separated?
            for ( const auto& model : storageModels )
            {
                AddStorageBuffer( StorageBuffer::Create( model.Name, 36, model.BindingPoint ),
                                  model.Name );
            }
        }

        // ImageCube
        {
            const auto& models = shader->GetUniformImageCubeModels();
            m_ImageCubeData.Data.reserve( models.size() );
            m_ImageCubeData.Names.reserve( models.size() );

            for ( const auto& model : models )
            {
                AddImageCube( UniformImageCube::Create( model.Name, model.BindingPoint ), model.Name );
            }
        }

        // Image2D
        {
            const auto& models = shader->GetUniformImage2DModels();
            m_Image2DData.Data.reserve( models.size() );
            m_Image2DData.Names.reserve( models.size() );

            for ( const auto& model : models )
            {
                AddImage2D( UniformImage2D::Create( model.Name, model.BindingPoint ), model.Name );
            }
        }
    }

} // namespace Desert::Uniforms