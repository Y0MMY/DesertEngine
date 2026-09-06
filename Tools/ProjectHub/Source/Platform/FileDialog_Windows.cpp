// Windows half of Hub::FileDialog — IFileOpenDialog (the Vista-era COM dialog, not the
// long-deprecated GetOpenFileName). Ships with the OS; no dependency is added.

#include "../FileDialog.hpp"

#include <windows.h>

#include <shlobj.h>
#include <shobjidl.h>

#include "WindowsStrings.hpp"

#include <string>

namespace Hub::FileDialog
{
    namespace
    {
        using Hub::Platform::Narrow;
        using Hub::Platform::Widen;

        // One panel routine for both entry points; `folders` picks which of the two it is.
        std::string RunPanel( const std::string& title, const std::string& filterName,
                              const std::string& filterExtension, const std::string& startIn, bool folders )
        {
            // Apartment-threaded because the shell dialog is; already-initialised is a success the
            // caller must not undo, so the matching Uninit is conditional on THIS call having done it.
            const HRESULT initialised =
                 ::CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE );
            const bool weInitialised = SUCCEEDED( initialised );

            std::string      chosen;
            IFileOpenDialog* dialog = nullptr;
            if ( SUCCEEDED( ::CoCreateInstance( CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                                IID_PPV_ARGS( &dialog ) ) ) )
            {
                const std::wstring wideTitle = Widen( title );
                dialog->SetTitle( wideTitle.c_str() );

                DWORD options = 0;
                dialog->GetOptions( &options );
                dialog->SetOptions( options | FOS_FORCEFILESYSTEM | ( folders ? FOS_PICKFOLDERS : DWORD( 0 ) ) );

                std::wstring wideFilterName;
                std::wstring wideFilterSpec;
                if ( !folders )
                {
                    wideFilterName                   = Widen( filterName );
                    wideFilterSpec                   = L"*." + Widen( filterExtension );
                    const COMDLG_FILTERSPEC filter[] = { { wideFilterName.c_str(), wideFilterSpec.c_str() } };
                    dialog->SetFileTypes( 1, filter );
                    dialog->SetDefaultExtension( Widen( filterExtension ).c_str() );
                }

                if ( !startIn.empty() )
                {
                    IShellItem*        folder    = nullptr;
                    const std::wstring wideStart = Widen( startIn );
                    if ( SUCCEEDED( ::SHCreateItemFromParsingName( wideStart.c_str(), nullptr,
                                                                   IID_PPV_ARGS( &folder ) ) ) )
                    {
                        dialog->SetFolder( folder );
                        folder->Release();
                    }
                }

                if ( SUCCEEDED( dialog->Show( nullptr ) ) ) // a cancel lands here as a failed HRESULT
                {
                    IShellItem* item = nullptr;
                    if ( SUCCEEDED( dialog->GetResult( &item ) ) )
                    {
                        PWSTR filePath = nullptr;
                        if ( SUCCEEDED( item->GetDisplayName( SIGDN_FILESYSPATH, &filePath ) ) )
                        {
                            chosen = Narrow( filePath );
                            ::CoTaskMemFree( filePath );
                        }
                        item->Release();
                    }
                }
                dialog->Release();
            }

            if ( weInitialised )
                ::CoUninitialize();
            return chosen;
        }
    } // namespace

    std::string OpenFile( const std::string& title, const std::string& filterName,
                          const std::string& filterExtension )
    {
        return RunPanel( title, filterName, filterExtension, std::string(), false );
    }

    std::string PickDirectory( const std::string& title, const std::string& startIn )
    {
        return RunPanel( title, std::string(), std::string(), startIn, true );
    }
} // namespace Hub::FileDialog
