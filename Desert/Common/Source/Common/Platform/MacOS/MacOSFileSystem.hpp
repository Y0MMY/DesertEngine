#pragma once

#include <Common/Utilities/FileSystem.hpp>

namespace Common::Utils
{
	class MacOSFileSystem
	{
	public:
		// filter is the Windows-style "Description\0*.ext\0" string; the native
		// NSOpenPanel does its own type filtering UI, so it is ignored here.
		static std::filesystem::path OpenFileDialog(const char* filter);
	};
}
