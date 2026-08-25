#pragma once

#include "Common.hpp"

namespace Desert::Assets
{
    struct AssetMetadata
    {
        // Every field has a default, and the defaults spell "no asset". IsValid() below tests the handle
        // against 0; leaving Handle without an initializer made a default-constructed metadata report
        // itself VALID back when UUID's default was random, and left Priority and AssetType reading
        // uninitialized memory.
        Common::UUID     Handle = Common::UUID::Null();
        Common::Filepath Filepath;
        AssetPriority    Priority  = AssetPriority::Low;
        AssetTypeID      AssetType = AssetTypeID::Unknown;
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