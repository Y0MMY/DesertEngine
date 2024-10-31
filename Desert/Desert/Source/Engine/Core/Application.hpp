#pragma once

#include <string>

#include <Common/Core/Singleton.hpp>

namespace Desert::Engine
{
    struct ApplicationInfo
    {
        std::string Title;
    };

    class Applicaton : public Common::Singleton<Applicaton>
    {
    public:
        Applicaton( const ApplicationInfo& appInfo );
    private:
        ApplicationInfo m_ApplicationInfo;
    };

    Applicaton* CreateApplicaton( int argc, char** argv );
} // namespace Desert::Engine
