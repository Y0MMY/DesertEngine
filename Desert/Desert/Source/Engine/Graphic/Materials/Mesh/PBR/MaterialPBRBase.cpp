#include "MaterialPBRBase.hpp"

namespace Desert::Graphic
{
    MaterialPBRBase::MaterialPBRBase( std::string&& debugName, std::string&& shaderName )
         : Material( std::move( debugName ), std::move( shaderName ) )
    {
    }
} // namespace Desert::Graphic
