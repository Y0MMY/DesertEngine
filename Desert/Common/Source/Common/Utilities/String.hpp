#pragma once

#include <string>

#include <Common/Core/Core.hpp>

namespace Common::Utils
{
    class String
    {
    public:
        static const std::string BytesToString( const uint64_t bytes );
        static const std::string GetPathHash(const Common::Filepath& path);
    };
} // namespace Common::Utils