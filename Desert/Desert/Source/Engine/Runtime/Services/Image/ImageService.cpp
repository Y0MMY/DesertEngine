#include "ImageService.hpp"

namespace Desert::Runtime
{
    ImageHandle ImageService::Register( std::shared_ptr<Graphic::Image>&& image, ImageHandle::Type type )
    {
        const auto& handle = m_HandlePool.Allocate();

        if ( handle.Index >= m_Images.size() )
            m_Images.resize( handle.Index + 1 );

        m_Images[handle.Index] = std::move( image );

        return ImageHandle{ handle, type };
    }

    void ImageService::Unregister( const ImageHandle& handle )
    {
        m_HandlePool.Release( handle.Value );
        m_Images[handle.Value.Index].reset();
    }

    void ImageService::Clear()
    {
        // The vector is INDEXED by handle, so the handle pool has to be reset with it or the next
        // Allocate() would hand out an index into a table that no longer describes anything.
        m_Images.clear();
        m_HandlePool = Common::Core::HandlePool{};
    }

    Graphic::Image* ImageService::Resolve( const ImageHandle& handle ) const
    {
        if ( handle.Value.Index >= m_Images.size() )
            return nullptr;

        return m_Images[handle.Value.Index].get();
    }

} // namespace Desert::Runtime
