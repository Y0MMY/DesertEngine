#pragma once

// UTF-8 <-> UTF-16 for the Windows-only code paths. The hub keeps every string it owns in UTF-8
// (that is what the .deproj, the registry and ImGui all carry); Windows wants wide characters at
// its API boundary, and BOTH the spawn (Launch.cpp) and the native dialog
// (Platform/FileDialog_Windows.cpp) sit on that boundary. One definition rather than two copies of
// the same eight lines — a project path with a non-ASCII character in it must convert identically
// whether it is being launched or being picked.
//
// Included only under _WIN32; nothing outside these two call sites needs it.

#include <windows.h>

#include <string>

namespace Hub::Platform
{
    inline std::wstring Widen( const std::string& utf8 )
    {
        if ( utf8.empty() )
            return {};
        const int    size = ::MultiByteToWideChar( CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0 );
        std::wstring wide( (size_t)size, L'\0' );
        ::MultiByteToWideChar( CP_UTF8, 0, utf8.data(), (int)utf8.size(), wide.data(), size );
        return wide;
    }

    // Takes a NUL-terminated wide string (what the shell APIs hand back).
    inline std::string Narrow( const wchar_t* wide )
    {
        if ( wide == nullptr || wide[0] == L'\0' )
            return {};
        const int size = ::WideCharToMultiByte( CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr );
        if ( size <= 1 ) // just the terminator
            return {};
        std::string utf8( (size_t)size - 1, '\0' );
        ::WideCharToMultiByte( CP_UTF8, 0, wide, -1, utf8.data(), size, nullptr, nullptr );
        return utf8;
    }
} // namespace Hub::Platform
