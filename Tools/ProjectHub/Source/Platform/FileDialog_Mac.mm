// macOS half of Hub::FileDialog — NSOpenPanel, no dependency beyond AppKit, which GLFW already
// links. GLFW has created and finished launching the NSApplication by the time any of this runs,
// so a modal panel here behaves exactly as it does in a normal Cocoa app.

#include "../FileDialog.hpp"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace Hub::FileDialog
{
    namespace
    {
        // The panel is ordered front explicitly: the hub's own window is key while the frame loop
        // runs, and without this the panel can open behind it on a Space with other windows.
        std::string RunPanel( NSOpenPanel* panel )
        {
            [panel setLevel:NSModalPanelWindowLevel];
            const NSModalResponse response = [panel runModal];
            if ( response != NSModalResponseOK )
                return {}; // cancelled — not a failure
            NSURL* url = [[panel URLs] firstObject];
            if ( url == nil )
                return {};
            return std::string( [[url path] UTF8String] );
        }
    } // namespace

    std::string OpenFile( const std::string& title, const std::string& filterName,
                          const std::string& filterExtension )
    {
        @autoreleasepool
        {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            [panel setTitle:[NSString stringWithUTF8String:title.c_str()]];
            [panel setMessage:[NSString stringWithUTF8String:filterName.c_str()]];
            [panel setCanChooseFiles:YES];
            [panel setCanChooseDirectories:NO];
            [panel setAllowsMultipleSelection:NO];
            // allowedContentTypes, not the allowedFileTypes string array — that one has been
            // deprecated since macOS 12. `.deproj` is not a type the system knows and the hub
            // declares none in an Info.plist, so this is a DYNAMIC UTType derived from the
            // extension; it filters exactly the same way. A nil would mean the system could not
            // even invent one, and showing every file beats showing none.
            UTType* type =
                 [UTType typeWithFilenameExtension:[NSString stringWithUTF8String:filterExtension.c_str()]];
            if ( type != nil )
                [panel setAllowedContentTypes:@[ type ]];
            return RunPanel( panel );
        }
    }

    std::string PickDirectory( const std::string& title, const std::string& startIn )
    {
        @autoreleasepool
        {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            [panel setTitle:[NSString stringWithUTF8String:title.c_str()]];
            [panel setCanChooseFiles:NO];
            [panel setCanChooseDirectories:YES];
            [panel setCanCreateDirectories:YES];
            [panel setAllowsMultipleSelection:NO];
            if ( !startIn.empty() )
                [panel setDirectoryURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:startIn.c_str()]
                                                  isDirectory:YES]];
            return RunPanel( panel );
        }
    }
} // namespace Hub::FileDialog
