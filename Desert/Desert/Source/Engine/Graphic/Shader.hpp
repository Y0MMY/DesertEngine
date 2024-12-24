#pragma once

#include <Engine/Graphic/RendererTypes.hpp>

namespace Desert::Graphic
{
    class Shader
    {
    public:
        virtual ~Shader() = default;

        virtual void              Use( BindUsage use = BindUsage::Bind ) const    = 0;
        virtual void              RT_Use( BindUsage use = BindUsage::Bind ) const = 0;
        virtual Common::BoolResult              Reload()                                        = 0;
        virtual const std::string GetName() const                                 = 0;

        static std::shared_ptr<Shader> Create( const std::string& filename );
    };

} // namespace Desert::Graphic