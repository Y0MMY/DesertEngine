#pragma once

#include "Common.hpp"

namespace Desert::Assets
{
    struct AssetMetadata
    {
        Common::UUID     Handle;
        Common::Filepath Filepath;
        AssetPriority    Priority;
        AssetTypeID      AssetType;
        /*std::unordered_map<std::string, std::variant<int, float, std::string>> AdditionalData;

        template <typename T>
        void SetAdditionalData( const std::string& key, const T& value )
        {
            AdditionalData[key] = value;
        }

        template <typename T>
        T GetAdditionalData( const std::string& key, const T& defaultValue = T{} ) const
        {

        }*/

        bool IsValid() const
        {
            return Handle != 0 && ( !Filepath.empty() ) && AssetType != AssetTypeID::Unknown;
        }

        bool IsEquivalent( const AssetMetadata& other ) const
        {
            return Filepath == other.Filepath && AssetType == other.AssetType;
        }
    };
} // namespace Desert::Assets