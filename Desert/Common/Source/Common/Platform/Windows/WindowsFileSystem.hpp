#pragma once

#include <Common/Utilities/FileSystem.hpp>

namespace Common::Utils
{
	class WindowsFileSystem
	{
	public:
		static std::filesystem::path OpenFileDialog(const char* filter);
	};
}