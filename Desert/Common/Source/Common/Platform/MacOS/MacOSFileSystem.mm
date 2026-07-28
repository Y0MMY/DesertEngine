#include "MacOSFileSystem.hpp"

#import <AppKit/AppKit.h>

namespace Common::Utils
{
	std::filesystem::path MacOSFileSystem::OpenFileDialog(const char* filter)
	{
		(void)filter;

		@autoreleasepool
		{
			NSOpenPanel* panel = [NSOpenPanel openPanel];
			panel.canChooseFiles          = YES;
			panel.canChooseDirectories    = NO;
			panel.allowsMultipleSelection = NO;
			panel.resolvesAliases         = YES;

			if ([panel runModal] == NSModalResponseOK)
			{
				NSURL* url = panel.URLs.firstObject;
				if (url != nil)
				{
					return std::filesystem::path([url.path UTF8String]);
				}
			}
		}

		return std::filesystem::path();
	}
}
