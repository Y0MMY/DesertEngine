// THE OTHER HALF OF "AFTER THE FIRST DEVICE LOSS THE ENGINE ISSUES NO VULKAN CALL".
//
// `DeviceLostGate` proves the latch behaves. It cannot prove that the production code ASKS it — that is a
// statement about the source text, and this suite makes it one. Without this half, the gate is a correct
// component nobody has to use, and the first entry point somebody adds without it silently reopens the
// defect. The verification skill has a whole section on that shape: both sides individually right, the
// defect living only in the disagreement.
//
// THREE THINGS ARE PINNED HERE.
//
//  1. EVERY WORK-ISSUING ENTRY POINT ASKS THE GATE. A named list, because the set is small and each member
//     is a deliberate decision rather than a pattern match.
//
//  2. VK_CHECK_RESULT CANNOT ABORT ON DEVICE LOSS. That macro's DESERT_VERIFY is where the process died —
//     `VkResult is 'VK_ERROR_DEVICE_LOST' in VulkanSwapChain.cpp:165`, treating an expected event as an
//     impossible one. The macro must route device loss to the latch and abort only on everything else.
//
//  3. THE DROPPED-RESULT CENSUS, AS A NUMBER. Every Vulkan call in the backend that sits in statement
//     position — where nothing can be reading what it returned — is listed below with what it returns and
//     why that is acceptable. The census was 18 dropped VkResults before this change and is 13 after; the
//     point of writing it down is that it can only shrink, and that a NEW one cannot be added without
//     somebody typing a reason next to it.
//
// WHY A SOURCE SCAN AND NOT A COMPILE-TIME CHECK. Vulkan's C headers do not mark VkResult [[nodiscard]],
// so the compiler has nothing to say about any of this; the project's own 330 NO_DISCARD guards cover our
// wrappers and stop at the API boundary. A scan is what is available, so it is made precise: comments and
// string literals are stripped first, and "statement position" is decided from the preceding token rather
// than from indentation.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    // Walks up from the working directory looking for a file only the repository has. Copied in shape from
    // AssetReferenceCensus, which needs the same thing for the same reason: the runner's working directory
    // is not fixed. (The suites share no header; copy-paste is the convention this directory follows.)
    std::string RepoRoot()
    {
        std::string prefix = "./";
        for ( int up = 0; up < 6; ++up )
        {
            std::ifstream probe( prefix + "Desert/Desert/Source/Engine/Graphic/DeviceLost.hpp" );
            if ( probe )
                return prefix;
            prefix += "../";
        }
        return {};
    }

    std::string ReadAll( const fs::path& path )
    {
        std::ifstream      in( path, std::ios::binary );
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    // Replaces comments and string literals with spaces, keeping every newline so line numbers survive. A
    // census that counted the calls named in comments would report defects that are prose, and this file is
    // full of prose naming Vulkan calls.
    std::string StripCommentsAndStrings( const std::string& src )
    {
        std::string out;
        out.reserve( src.size() );
        for ( std::size_t i = 0; i < src.size(); )
        {
            if ( src.compare( i, 2, "//" ) == 0 )
            {
                while ( i < src.size() && src[i] != '\n' )
                {
                    out.push_back( ' ' );
                    ++i;
                }
            }
            else if ( src.compare( i, 2, "/*" ) == 0 )
            {
                const std::size_t end = src.find( "*/", i + 2 );
                const std::size_t to  = end == std::string::npos ? src.size() : end + 2;
                for ( ; i < to; ++i )
                    out.push_back( src[i] == '\n' ? '\n' : ' ' );
            }
            else if ( src[i] == '"' )
            {
                out.push_back( ' ' );
                ++i;
                while ( i < src.size() && src[i] != '"' )
                {
                    out.push_back( src[i] == '\n' ? '\n' : ' ' );
                    i += src[i] == '\\' ? 2 : 1;
                }
                if ( i < src.size() )
                {
                    out.push_back( ' ' );
                    ++i;
                }
            }
            else
            {
                out.push_back( src[i] );
                ++i;
            }
        }
        return out;
    }

    bool IsIdentChar( char c )
    {
        return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_';
    }

    // The body of `Class::Method`, by brace matching from the first '{' after the name. Returns empty when
    // the name is not there at all, which the caller reports as a distinct failure: a census row naming a
    // function that no longer exists is worse than a missing guard, because it passes.
    std::string BodyOf( const std::string& src, const std::string& qualifiedName )
    {
        std::size_t at = 0;
        while ( ( at = src.find( qualifiedName, at ) ) != std::string::npos )
        {
            const std::size_t open = src.find( '{', at );
            if ( open == std::string::npos )
                return {};
            // A declaration in a header ends at ';' before it ever reaches a '{'.
            const std::size_t semi = src.find( ';', at );
            if ( semi != std::string::npos && semi < open )
            {
                at += qualifiedName.size();
                continue;
            }
            int         depth = 0;
            std::size_t i     = open;
            for ( ; i < src.size(); ++i )
            {
                if ( src[i] == '{' )
                    ++depth;
                else if ( src[i] == '}' && --depth == 0 )
                    break;
            }
            return src.substr( open, i - open );
        }
        return {};
    }

    struct GatedEntryPoint
    {
        const char* File;
        const char* QualifiedName;
        const char* WhatItWouldIssue;
    };

    // EVERY POINT THROUGH WHICH THE ENGINE ISSUES GPU WORK. Adding one without a gate is the way this
    // defect comes back, so the list is here and not inferred.
    //
    // vkCmd* recording is deliberately ABSENT and is not an omission: nothing records without
    // VulkanRendererAPI::m_CurrentCommandBuffer, and BeginFrame — which is on this list — is the only
    // function that ever sets it. Forty recording entry points are covered by one gate because of that,
    // and the day someone gives m_CurrentCommandBuffer a second writer, this comment is the thing they
    // have to argue with.
    constexpr GatedEntryPoint k_Gated[] = {
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanQueue.cpp", "VulkanQueue::PrepareFrame",
           "vkResetFences + the acquire" },
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanQueue.cpp", "VulkanQueue::Submit",
           "vkQueueSubmit" },
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanQueue.cpp", "VulkanQueue::Present",
           "vkQueuePresentKHR + vkWaitForFences" },
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanSwapChain.cpp",
           "VulkanSwapChain::CreateSwapChain", "vkCreateSwapchainKHR -- the line that aborted" },
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanSwapChain.cpp", "VulkanSwapChain::OnResize",
           "the whole teardown-and-rebuild" },
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanSwapChain.cpp",
           "VulkanSwapChain::AcquireNextImage", "vkAcquireNextImageKHR" },
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanSwapChain.cpp",
           "VulkanSwapChain::RecordFrameCapture", "a staging allocation and an image copy" },
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanRenderer.cpp", "VulkanRendererAPI::BeginFrame",
           "vkBeginCommandBuffer -- and every vkCmd* after it" },
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanRenderer.cpp", "VulkanRendererAPI::EndFrame",
           "vkEndCommandBuffer" },
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanRenderer.cpp",
           "VulkanRendererAPI::PresentFinalImage", "the submit and the present" },
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanRenderer.cpp",
           "VulkanRendererAPI::WaitDeviceIdle", "vkDeviceWaitIdle" },
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanContext.cpp", "VulkanContext::BeginFrame",
           "the acquire, on the live path the window actually calls" },
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/CommandBufferAllocator.cpp",
           "CommandBufferAllocator::RT_AllocateCommandBufferGraphic", "vkAllocateCommandBuffers" },
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/CommandBufferAllocator.cpp",
           "CommandBufferAllocator::RT_GetCommandBufferCompute", "vkAllocateCommandBuffers" },
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanDevice.cpp", "VulkanLogicalDevice::WaitIdle",
           "vkDeviceWaitIdle" },
         { "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanDevice.cpp",
           "VulkanLogicalDevice::SavePipelineCache", "vkGetPipelineCacheData" },
    };

    struct DroppedResult
    {
        const char* File;
        const char* Call;
        int         Count;
        const char* Verdict;
    };

    // THE CENSUS. Every Vulkan/VMA call in Engine/Graphic that sits in statement position — nothing can be
    // reading what it returned. `vkCmd*`, `vkDestroy*`, `vkFree*` and `vma*Destroy/Free` are excluded by
    // PREFIX and not by name, so this exclusion cannot fall behind the API the way a typed list would.
    //
    // "void" rows return nothing and are here only so the count is complete: a call that changes from void
    // to VkResult in some future header is a thing this suite should notice.
    constexpr DroppedResult k_Census[] = {
         // ---- returns void: nothing to check, listed for completeness -------------------------------
         { "VulkanAllocator.cpp", "vmaUnmapMemory", 1, "void" },
         { "VulkanDevice.cpp", "vkGetPhysicalDeviceProperties", 3, "void" },
         { "VulkanDevice.cpp", "vkGetPhysicalDeviceFeatures", 1, "void" },
         { "VulkanDevice.cpp", "vkGetPhysicalDeviceFormatProperties", 3, "void" },
         { "VulkanDevice.cpp", "vkGetPhysicalDeviceMemoryProperties", 1, "void" },
         { "VulkanDevice.cpp", "vkGetPhysicalDeviceQueueFamilyProperties", 2, "void" },
         { "VulkanDevice.cpp", "vkGetDeviceQueue", 3, "void" },
         { "VulkanGpuProfiler.cpp", "vkGetPhysicalDeviceQueueFamilyProperties", 2, "void" },
         { "VulkanMaterialBackend.cpp", "vkUpdateDescriptorSets", 1, "void" },
         { "VulkanPipelineCompute.cpp", "vkUpdateDescriptorSets", 1, "void" },

         // ---- returns VkResult and the result is DROPPED. Thirteen, each with its reason ------------
         // The count-then-fill enumeration idiom, at startup. Both halves can only fail with
         // OUT_OF_HOST_MEMORY or an unusable driver, and in every case the caller's next line already
         // refuses on the count being zero -- VulkanDevice's "no physical devices" verify, and
         // GetImageFormatAndColorSpace's "null format count" error. Worth checking one day; not worth
         // pretending it is this task.
         { "VulkanContext.cpp", "vkEnumerateInstanceLayerProperties", 2, "dropped: startup enumeration" },
         { "VulkanDevice.cpp", "vkEnumeratePhysicalDevices", 2, "dropped: startup enumeration" },
         { "VulkanDevice.cpp", "vkEnumerateDeviceExtensionProperties", 2, "dropped: startup enumeration" },
         { "VulkanSwapChain.cpp", "vkGetPhysicalDeviceSurfacePresentModesKHR", 2, "dropped: startup enumeration" },
         { "VulkanSwapChain.cpp", "vkGetPhysicalDeviceSurfaceFormatsKHR", 2, "dropped: startup enumeration" },

         // Teardown waits. All three are already behind DeviceLost::AllowWork(), so on a lost device they
         // are not issued at all; on a live one the only failure they can report is a loss that the next
         // call would report anyway, and there is nothing an exiting process would do differently.
         { "VulkanDevice.cpp", "vkDeviceWaitIdle", 1, "dropped: teardown wait, already gated" },
         { "VulkanSwapChain.cpp", "vkDeviceWaitIdle", 1, "dropped: teardown wait, already gated" },
         { "VulkanImGuiLayer.cpp", "vkDeviceWaitIdle", 1, "dropped: teardown wait, already gated" },
    };

    struct Found
    {
        std::string File;
        std::string Call;
        int         Line;
    };

    bool StartsWithAny( const std::string& s, std::initializer_list<const char*> prefixes )
    {
        for ( const char* p : prefixes )
            if ( s.rfind( p, 0 ) == 0 )
                return true;
        return false;
    }

    // Every `vk*`/`vma*` call in statement position under Engine/Graphic, minus the excluded prefixes.
    std::vector<Found> ScanStatementPositionCalls( const std::string& root )
    {
        std::vector<Found> found;
        const fs::path     base = fs::path( root ) / "Desert/Desert/Source/Engine/Graphic";
        std::error_code    ec;
        for ( auto it = fs::recursive_directory_iterator( base, ec ); it != fs::recursive_directory_iterator();
              ++it )
        {
            if ( ec )
                break;
            const fs::path& p = it->path();
            // lightweightvk is a vendored third-party tree that happens to live under our Vulkan folder.
            if ( p.string().find( "lightweightvk" ) != std::string::npos )
                continue;
            if ( p.extension() != ".cpp" && p.extension() != ".hpp" )
                continue;

            const std::string src = StripCommentsAndStrings( ReadAll( p ) );
            for ( std::size_t i = 0; i + 3 < src.size(); ++i )
            {
                const bool vk  = src.compare( i, 2, "vk" ) == 0 && src[i + 2] >= 'A' && src[i + 2] <= 'Z';
                const bool vma = src.compare( i, 3, "vma" ) == 0 && src[i + 3] >= 'A' && src[i + 3] <= 'Z';
                if ( !vk && !vma )
                    continue;
                if ( i > 0 && IsIdentChar( src[i - 1] ) )
                    continue;

                std::size_t e = i;
                while ( e < src.size() && IsIdentChar( src[e] ) )
                    ++e;
                std::size_t paren = e;
                while ( paren < src.size() && ( src[paren] == ' ' || src[paren] == '\n' ) )
                    ++paren;
                if ( paren >= src.size() || src[paren] != '(' )
                    continue;

                const std::string name = src.substr( i, e - i );
                if ( StartsWithAny( name, { "vkCmd", "vkDestroy", "vkFree", "vmaDestroy", "vmaFree",
                                            "vkGetInstanceProcAddr", "vkGetDeviceProcAddr" } ) )
                    continue;

                // Statement position: the previous non-space token ends a statement, a block, or a
                // control-flow condition (`if ( x ) vkFoo();` has no braces and is still statement
                // position). Anything else means somebody is consuming the value.
                std::size_t k = i;
                while ( k > 0 &&
                        ( src[k - 1] == ' ' || src[k - 1] == '\n' || src[k - 1] == '\t' || src[k - 1] == '\r' ) )
                    --k;
                if ( k == 0 || src[k - 1] == ';' || src[k - 1] == '{' || src[k - 1] == '}' || src[k - 1] == ')' ||
                     ( k >= 4 && src.compare( k - 4, 4, "else" ) == 0 ) )
                {
                    found.push_back(
                         { p.filename().string(), name,
                           1 + static_cast<int>( std::count( src.begin(), src.begin() + i, '\n' ) ) } );
                }
                i = e;
            }
        }
        return found;
    }
} // namespace

TEST( DeviceLostCensus, EveryWorkIssuingEntryPointAsksTheGateFirst )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "could not locate the repository from the working directory";

    for ( const auto& row : k_Gated )
    {
        const std::string src  = StripCommentsAndStrings( ReadAll( fs::path( root ) / row.File ) );
        const std::string body = BodyOf( src, row.QualifiedName );

        ASSERT_FALSE( body.empty() ) << row.QualifiedName << " is not in " << row.File
                                     << " any more. A census row naming a function that no longer exists "
                                        "passes without checking anything -- fix the row or the code.";
        EXPECT_NE( body.find( "DeviceLost::AllowWork()" ), std::string::npos )
             << row.QualifiedName << " issues " << row.WhatItWouldIssue
             << " without asking DeviceLost::AllowWork() first, so a lost device would receive it.";
    }
}

TEST( DeviceLostCensus, VkCheckResultCannotAbortOnALostDevice )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() );

    const std::string helper = ReadAll(
         fs::path( root ) / "Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp" );

    // The exact condition, not merely "the file mentions NoteIfDeviceLost somewhere". This is the line the
    // process died on, and the shape of it is the fix: a failed result reaches DESERT_VERIFY only when it
    // is NOT a device loss.
    EXPECT_NE( helper.find( "res != VK_SUCCESS && !NoteIfDeviceLost(" ), std::string::npos )
         << "VK_CHECK_RESULT must route VK_ERROR_DEVICE_LOST to the latch and abort only on the errors "
            "that really are invariants. As written it can abort on a device loss again.";

    // The three error-RETURNING macros latch too: whichever meets the loss first is the one that names it,
    // and which one that is depends on where in the frame the GPU died.
    for ( const char* macroName : { "VK_CHECK_RESULT_BOOL", "VK_RETURN_RESULT_IF_FALSE",
                                    "VK_RETURN_RESULT_IF_FALSE_TYPE", "VK_RETURN_RESULT" } )
    {
        const std::size_t at = helper.find( std::string( "#define " ) + macroName + "(" );
        ASSERT_NE( at, std::string::npos ) << macroName << " is gone from VulkanHelper.hpp";
        const std::size_t next = helper.find( "#define ", at + 8 );
        const std::string body = helper.substr( at, ( next == std::string::npos ? helper.size() : next ) - at );
        EXPECT_NE( body.find( "NoteIfDeviceLost(" ), std::string::npos )
             << macroName
             << " swallows a device loss without latching it, so the first place to meet the "
                "loss would not be the place that reports it.";
    }
}

TEST( DeviceLostCensus, TheDroppedResultCensusStillHoldsAndCanOnlyShrink )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() );

    const std::vector<Found> found = ScanStatementPositionCalls( root );

    // Fold the scan into (file, call) -> count, which is what the census rows are.
    std::map<std::pair<std::string, std::string>, int> actual;
    for ( const auto& f : found )
        ++actual[{ f.File, f.Call }];

    std::map<std::pair<std::string, std::string>, int> expected;
    for ( const auto& row : k_Census )
        expected[{ row.File, row.Call }] += row.Count;

    // BOTH DIRECTIONS. A new row that nobody wrote a reason for is the defect coming back; a row that no
    // longer matches anything is a census pinning a call that has been fixed or moved, which passes while
    // meaning nothing.
    for ( const auto& [key, count] : actual )
    {
        const auto it = expected.find( key );
        EXPECT_NE( it, expected.end() )
             << key.first << " calls " << key.second
             << " and throws away what it returns, with no row in the census saying why that is all right. "
                "Either read the result or add a row with a reason.";
        if ( it != expected.end() )
            EXPECT_EQ( count, it->second ) << key.first << " now has " << count << " statement-position calls to "
                                           << key.second << ", the census says " << it->second;
    }
    for ( const auto& [key, count] : expected )
    {
        (void)count;
        EXPECT_NE( actual.find( key ), actual.end() )
             << "the census claims " << key.first << " drops the result of " << key.second
             << ", and it does not any more. Delete the row -- a census that pins nothing passes silently.";
    }

    // THE NUMBER, stated so a regression is visible as a number and not only as a diff. Thirteen VkResults
    // are dropped in the whole Vulkan backend; it was eighteen before this change, and every survivor is
    // either a startup enumeration whose caller refuses on the count, or a teardown wait that is already
    // behind the gate.
    int droppedResults = 0;
    for ( const auto& row : k_Census )
        if ( std::string( row.Verdict ) != "void" )
            droppedResults += row.Count;
    EXPECT_EQ( droppedResults, 13 )
         << "the number of Vulkan calls whose result nobody reads has changed. Up is a regression; down is "
            "welcome, and this line moves with it.";
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
