#include <Common/Core/HandlePool.hpp>

#include <Engine/Runtime/ImageHandle.hpp>
#include <Engine/Graphic/Image.hpp>

namespace Desert::Runtime
{
    class ImageService
    {
    public:
        [[nodiscard]] ImageHandle Register( std::shared_ptr<Graphic::Image>&& image, ImageHandle::Type type );
        void                      Unregister( const ImageHandle& handle );
        Graphic::Image*           Resolve( const ImageHandle& handle ) const;

    private:
        Common::Core::HandlePool m_HandlePool;

        std::vector<std::shared_ptr<Graphic::Image>> m_Images;
    };
} // namespace Desert::Runtime
