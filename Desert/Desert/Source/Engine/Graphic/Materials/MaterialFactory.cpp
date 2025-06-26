#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Shader.hpp>

#include <Engine/Assets/Mapper.hpp>

namespace Desert::Graphic
{
    MaterialAssetLink MaterialFactory::CreateFromAsset( const Assets::Asset<Assets::MaterialAsset>& asset )
    {
        auto material = Material::Create( "MaterialFromAsset_StaticPBR.glsl",
                                          ShaderLibrary::Get( "StaticPBR.glsl", {} ).GetValue() );

        for ( const auto& [type, iterator] : asset->GetTextureLookup() )
        {
            material->GetTexture2DProperty( Assets::Mapper::GetTextureType( type ) )
                 ->SetTexture( asset->GetTexture( type ) );
        }

        return MaterialAssetLink{ material, asset->GetHandle() };
    }

} // namespace Desert::Graphic