#include "WindowsFileSystem.hpp"

#include <Windows.h>
#include <filesystem>
#include <commdlg.h>

namespace Common::Utils
{
	std::filesystem::path WindowsFileSystem::OpenFileDialog(const char* filter)
	{
		OPENFILENAMEA ofn;
		CHAR szFile[260] = { 0 }; 

		ZeroMemory(&ofn, sizeof(OPENFILENAME));
		ofn.lStructSize = sizeof(OPENFILENAME);
		ofn.lpstrFile = szFile;
		ofn.nMaxFile = sizeof(szFile);
		ofn.lpstrFilter = filter; 
		ofn.nFilterIndex = 1;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

		if (GetOpenFileNameA(&ofn) == TRUE)
		{
			std::string fp = ofn.lpstrFile;
			std::replace(fp.begin(), fp.end(), '\\', '/');
			return std::filesystem::path(fp);
		}

		return std::filesystem::path(); 
	}
}