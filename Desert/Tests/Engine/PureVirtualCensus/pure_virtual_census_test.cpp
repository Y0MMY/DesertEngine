// THE CENSUS OF PURE VIRTUALS EVERY CLASS IMPLEMENTS AND NOBODY CALLS.
//
// WHY IT EXISTS. Two of them turned up on one day, in unrelated subsystems, with nobody looking for
// either: `Texture2DProperty::Clone()` (closed by М9) and `RenderSystem::Shutdown()` — a pure virtual
// declared beside `Initialize()`, overridden by twenty render systems, carefully commented in
// MeshRenderer, and reached from no call site in the repository. Two in a day without searching is a
// habit, not an accident: somebody writes the "correct" virtual Init/Shutdown pair, wires up the first
// half, and nothing anywhere says the second half never runs. Г8 removed `Shutdown()` and then asked the
// question the two instances raised — HOW MANY MORE? — mechanically, over the tree, once.
//
// The answer is the register below, and the point of writing it down is the same as DeviceLostCensus's:
// it can only shrink, and a NEW dead pure virtual cannot appear without somebody typing a reason next to
// it.
//
// HOW A CALL IS ATTRIBUTED, AND WHY IT IS NOT BY NAME. `Shutdown` is declared pure-virtual by three
// unrelated bases — RenderSystem, RendererAPI and RendererContext — and the last two ARE called. A
// census that counted `->Shutdown(` by name would therefore have reported the very defect it was written
// for as alive. So a call site counts for `Base::Method` only when the call's translation unit SEES
// Base: the header declaring Base must be in that file's transitive include closure. That is sound in
// the direction that matters — a forward declaration cannot call a member — and it is blind to name
// collisions by construction. It found two rows the name-based version had hidden.
//
// WHAT IS NOT CLAIMED. A dead pure virtual is not automatically a defect to delete today; several of
// these rows are a question for the owner (does the engine want asset eviction at all?) rather than a
// cleanup. What the row must carry is WHO decides, so that a year from now the entry is readable.

#include "../SettingConsumers/setting_consumers_reader.hpp"

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

    // Walks up from the working directory looking for a file only the repository has, as
    // DeviceLostCensus and AssetReferenceCensus do for the same reason: the runner's working directory
    // is not fixed. THIS ONE is still copy-pasted on purpose: each census probes a DIFFERENT sentinel
    // file, so a shared version would need the sentinel as a parameter and would say less than the
    // three lines it replaced. The text READER is a different matter and is shared (Д33).
    std::string RepoRoot()
    {
        std::string prefix = "./";
        for ( int up = 0; up < 6; ++up )
        {
            std::ifstream probe( prefix + "Desert/Desert/Source/Engine/Graphic/Systems/RenderSystem.hpp" );
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
        std::string text = buffer.str();
        // A UTF-8 BOM sits in front of the first `#include` in several editor sources, and an include
        // scan anchored to the start of a line does not see past it. The first include of a .cpp is
        // usually its own header, so losing it loses most of that file's closure — which showed up as
        // two rows of this census that were not dead at all.
        if ( text.compare( 0, 3, "\xEF\xBB\xBF" ) == 0 )
            text.erase( 0, 3 );
        return text;
    }

    // Comments and literals become spaces, newlines survive so line numbers do. Without this the census
    // counts the calls named in prose — and this file is full of prose naming them.
    //
    // Д33: THIS WAS A PRIVATE COPY, AND THE COPY WAS BLIND. It knew about `"…"` and about comments and
    // about nothing else, so `c.Peek() == '"'` — ordinary C++, present in DShaderParser.cpp — opened a
    // string literal for it that closed at the next quote hundreds of lines away, and every call site in
    // between was invisible to a census whose whole product is a list of things nothing calls. The
    // shared reader next door had exactly the same hole and it has been fixed there; this file now uses
    // it rather than carrying a third opinion about what a literal is. (Its own escape handling was
    // wrong in a second way: it emitted one space for a two-byte escape, so the output was SHORTER than
    // the input and the line numbers it promised drifted.)
    std::string StripCommentsAndStrings( const std::string& src )
    {
        return Desert::Tests::ConsumerText::StripCommentsAndLiterals( src );
    }

    bool IsIdentChar( char c )
    {
        return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_';
    }

    // Every source the engine, the editor, the runtime and the tools are built from. Third-party trees
    // are excluded, `lightweightvk` explicitly because it is vendored INSIDE our Vulkan folder.
    std::vector<fs::path> ProjectSources( const std::string& root )
    {
        const char* trees[] = { "Desert/Desert/Source", "Desert/Common/Source", "Editor/Source", "Runtime/Source",
                                "Tools" };
        std::vector<fs::path> files;
        for ( const char* tree : trees )
        {
            const fs::path  base = fs::path( root ) / tree;
            std::error_code ec;
            if ( !fs::exists( base, ec ) )
                continue;
            for ( auto it = fs::recursive_directory_iterator( base, ec ); it != fs::recursive_directory_iterator();
                  ++it )
            {
                if ( ec )
                    break;
                const fs::path&   p = it->path();
                const std::string s = p.string();
                if ( s.find( "ThirdParty" ) != std::string::npos ||
                     s.find( "lightweightvk" ) != std::string::npos )
                    continue;
                const std::string ext = p.extension().string();
                if ( ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".inl" )
                    files.push_back( p );
            }
        }
        std::sort( files.begin(), files.end() );
        return files;
    }

    struct Tree
    {
        std::vector<fs::path>                        Files;
        std::map<std::string, std::string>           Code;    // path -> comment/string-stripped text
        std::map<std::string, std::string>           Raw;     // path -> as read (the include lines live here)
        std::map<std::string, std::set<std::string>> Closure; // path -> every project header it sees
    };

    // #include "..." / <...>, resolved against the four include roots premake gives every target plus
    // the including file's own directory. Anything that does not resolve is a standard or third-party
    // header and cannot declare one of our pure virtuals.
    void BuildIncludeClosure( const std::string& root, Tree& tree )
    {
        const char* roots[] = { "Desert/Desert/Source", "Desert/Common/Source", "Editor/Source",
                                "Runtime/Source" };

        std::set<std::string> known;
        for ( const auto& f : tree.Files )
            known.insert( fs::weakly_canonical( f ).string() );

        std::map<std::string, std::set<std::string>> direct;
        for ( const auto& f : tree.Files )
        {
            const std::string  self = fs::weakly_canonical( f ).string();
            const std::string& raw  = tree.Raw[f.string()];
            for ( std::size_t at = 0; at < raw.size(); )
            {
                const std::size_t hash = raw.find( "#include", at );
                if ( hash == std::string::npos )
                    break;
                at = hash + 8;
                // ONLY AT THE START OF A LINE. `#include` also appears inside string literals — the
                // shader compiler quotes one — and taking those produced a "filename" hundreds of
                // characters long that made weakly_canonical throw ENAMETOOLONG.
                std::size_t lineStart = raw.rfind( '\n', hash );
                lineStart             = lineStart == std::string::npos ? 0 : lineStart + 1;
                bool atLineStart      = true;
                for ( std::size_t k = lineStart; k < hash; ++k )
                    atLineStart = atLineStart && ( raw[k] == ' ' || raw[k] == '\t' );
                if ( !atLineStart )
                    continue;

                std::size_t open = at;
                while ( open < raw.size() && ( raw[open] == ' ' || raw[open] == '\t' ) )
                    ++open;
                if ( open >= raw.size() || ( raw[open] != '"' && raw[open] != '<' ) )
                    continue;
                const char        close = raw[open] == '"' ? '"' : '>';
                const std::size_t end   = raw.find( close, open + 1 );
                if ( end == std::string::npos )
                    continue;
                const std::string name = raw.substr( open + 1, end - open - 1 );
                if ( name.empty() || name.size() > 200 || name.find( '\n' ) != std::string::npos )
                    continue;

                std::vector<fs::path> candidates;
                candidates.push_back( f.parent_path() / name );
                for ( const char* r : roots )
                    candidates.push_back( fs::path( root ) / r / name );
                for ( const auto& c : candidates )
                {
                    const std::string resolved = fs::weakly_canonical( c ).string();
                    if ( known.count( resolved ) == 1 )
                    {
                        direct[self].insert( resolved );
                        break;
                    }
                }
            }
        }

        // Transitive closure by repeated widening. The graph is small (a few thousand edges) and this
        // terminates on cycles without a visited stack, which `#pragma once` headers do form.
        for ( const auto& f : tree.Files )
            tree.Closure[fs::weakly_canonical( f ).string()] = direct[fs::weakly_canonical( f ).string()];

        bool grew = true;
        while ( grew )
        {
            grew = false;
            for ( auto& entry : tree.Closure )
            {
                std::set<std::string> widened = entry.second;
                for ( const auto& dep : entry.second )
                {
                    const auto it = tree.Closure.find( dep );
                    if ( it != tree.Closure.end() )
                        widened.insert( it->second.begin(), it->second.end() );
                }
                if ( widened.size() != entry.second.size() )
                {
                    entry.second = std::move( widened );
                    grew         = true;
                }
            }
        }
    }

    Tree ReadTree( const std::string& root )
    {
        Tree tree;
        tree.Files = ProjectSources( root );
        for ( const auto& f : tree.Files )
        {
            const std::string raw = ReadAll( f );
            tree.Raw[f.string()]  = raw;
            tree.Code[f.string()] = StripCommentsAndStrings( raw );
        }
        BuildIncludeClosure( root, tree );
        return tree;
    }

    struct ClassSpan
    {
        std::string Name;
        std::string BaseClause; // the text between the name and the '{', empty when there is none
        std::size_t Open  = 0;
        std::size_t Close = 0;
    };

    // The base names in `: public A, B, private virtual C::D` — unqualified, because that is how the
    // derived class's own declaration spells the name this census keys on.
    std::vector<std::string> BaseNames( const std::string& clause )
    {
        std::vector<std::string> names;
        const std::size_t        colon = clause.find( ':' );
        if ( colon == std::string::npos )
            return names;
        // Template arguments hide commas that are not base separators: `: public Base<A, B>`.
        int         depth = 0;
        std::string current;
        for ( std::size_t i = colon + 1; i < clause.size(); ++i )
        {
            const char c = clause[i];
            if ( c == '<' )
                ++depth;
            else if ( c == '>' )
                --depth;
            if ( c == ',' && depth == 0 )
            {
                names.push_back( current );
                current.clear();
                continue;
            }
            if ( depth == 0 )
                current.push_back( c );
        }
        names.push_back( current );

        std::vector<std::string> cleaned;
        for ( std::string& one : names )
        {
            // Drop the access/virtual keywords and any template argument list, keep the last
            // `::`-qualified component.
            for ( const char* keyword : { "public", "private", "protected", "virtual" } )
            {
                std::size_t at = one.find( keyword );
                while ( at != std::string::npos )
                {
                    one.erase( at, std::string( keyword ).size() );
                    at = one.find( keyword );
                }
            }
            const std::size_t qualifier = one.rfind( "::" );
            if ( qualifier != std::string::npos )
                one.erase( 0, qualifier + 2 );

            std::string ident;
            for ( char c : one )
            {
                if ( IsIdentChar( c ) )
                    ident.push_back( c );
                else if ( !ident.empty() )
                    break;
            }
            if ( !ident.empty() )
                cleaned.push_back( ident );
        }
        return cleaned;
    }

    // `class X {`, `class X final {`, `struct X : public Y {` — the name and the extent of its body.
    std::vector<ClassSpan> ClassSpans( const std::string& code )
    {
        std::vector<ClassSpan> spans;
        for ( std::size_t i = 0; i + 6 < code.size(); ++i )
        {
            const bool isClass  = code.compare( i, 5, "class" ) == 0 && !IsIdentChar( code[i + 5] );
            const bool isStruct = code.compare( i, 6, "struct" ) == 0 && !IsIdentChar( code[i + 6] );
            if ( !isClass && !isStruct )
                continue;
            if ( i > 0 && IsIdentChar( code[i - 1] ) )
                continue;

            std::size_t at = i + ( isClass ? 5u : 6u );
            while ( at < code.size() && ( code[at] == ' ' || code[at] == '\n' || code[at] == '\t' ) )
                ++at;
            const std::size_t nameAt = at;
            while ( at < code.size() && IsIdentChar( code[at] ) )
                ++at;
            if ( at == nameAt )
                continue;
            const std::string name = code.substr( nameAt, at - nameAt );

            // Up to the '{' there may be `final` and a base-clause; a ';' first means a forward
            // declaration and a '(' means this was not a class-head at all.
            std::size_t scan = at;
            while ( scan < code.size() && code[scan] != '{' && code[scan] != ';' && code[scan] != '(' )
                ++scan;
            if ( scan >= code.size() || code[scan] != '{' )
                continue;

            int         depth = 0;
            std::size_t j     = scan;
            for ( ; j < code.size(); ++j )
            {
                if ( code[j] == '{' )
                    ++depth;
                else if ( code[j] == '}' && --depth == 0 )
                    break;
            }
            spans.push_back( { name, code.substr( at, scan - at ), scan, j } );
            i = at;
        }
        return spans;
    }

    // The innermost class whose body contains @p pos.
    std::string EnclosingClass( const std::vector<ClassSpan>& spans, std::size_t pos )
    {
        std::string best;
        std::size_t bestExtent = 0;
        for ( const auto& span : spans )
        {
            if ( span.Open >= pos || pos >= span.Close )
                continue;
            const std::size_t extent = span.Close - span.Open;
            if ( best.empty() || extent < bestExtent )
            {
                best       = span.Name;
                bestExtent = extent;
            }
        }
        return best;
    }

    struct PureVirtual
    {
        std::string Class;
        std::string Method;
        std::string Header; // repository-relative
        int         Line = 0;
    };

    // `virtual <ret> Name( args ) [const] = 0;`
    std::vector<PureVirtual> PureVirtuals( const std::string& root, const Tree& tree )
    {
        std::vector<PureVirtual> found;
        for ( const auto& file : tree.Files )
        {
            const std::string&           code  = tree.Code.at( file.string() );
            const std::vector<ClassSpan> spans = ClassSpans( code );

            for ( std::size_t at = 0; ( at = code.find( "virtual", at ) ) != std::string::npos; at += 7 )
            {
                if ( at > 0 && IsIdentChar( code[at - 1] ) )
                    continue;
                if ( at + 7 < code.size() && IsIdentChar( code[at + 7] ) )
                    continue;

                const std::size_t semi = code.find( ';', at );
                if ( semi == std::string::npos )
                    continue;
                const std::size_t brace = code.find_first_of( "{}", at );
                if ( brace != std::string::npos && brace < semi )
                    continue; // an inline body, so not a pure virtual
                const std::string decl = code.substr( at, semi - at );

                // THE `= 0` MUST BE THE LAST THING IN THE DECLARATION, not merely present in it. A
                // default argument spells the same two characters — `SetData( ..., uint32_t offset = 0 )
                // override;` — and matching anywhere reported four Vulkan overrides as pure virtuals.
                std::size_t tail = decl.size();
                while ( tail > 0 && ( decl[tail - 1] == ' ' || decl[tail - 1] == '\n' || decl[tail - 1] == '\t' ||
                                      decl[tail - 1] == '\r' ) )
                    --tail;
                if ( tail == 0 || decl[tail - 1] != '0' )
                    continue;
                --tail;
                while ( tail > 0 && ( decl[tail - 1] == ' ' || decl[tail - 1] == '\n' ) )
                    --tail;
                if ( tail == 0 || decl[tail - 1] != '=' )
                    continue;

                // The method name is the identifier in front of the LAST '(' before the '='.
                const std::size_t paren = decl.rfind( '(' );
                if ( paren == std::string::npos )
                    continue;
                std::size_t end = paren;
                while ( end > 0 && ( decl[end - 1] == ' ' || decl[end - 1] == '\n' ) )
                    --end;
                std::size_t start = end;
                while ( start > 0 && IsIdentChar( decl[start - 1] ) )
                    --start;
                if ( start == end )
                    continue;
                const std::string name = decl.substr( start, end - start );
                if ( name == "operator" )
                    continue;

                PureVirtual entry;
                entry.Class  = EnclosingClass( spans, at );
                entry.Method = name;
                entry.Header = fs::relative( file, fs::path( root ) ).generic_string();
                entry.Line   = 1 + static_cast<int>( std::count( code.begin(), code.begin() + at, '\n' ) );
                if ( !entry.Class.empty() )
                    found.push_back( entry );
                at = semi - 7 > at ? semi - 7 : at;
            }
        }
        return found;
    }

    // Every `->Method(` / `.Method(` in the tree, with the file it was written in.
    std::map<std::string, std::vector<std::string>> DispatchedCalls( const Tree& tree )
    {
        std::map<std::string, std::vector<std::string>> calls;
        for ( const auto& file : tree.Files )
        {
            const std::string& code = tree.Code.at( file.string() );
            for ( std::size_t i = 1; i + 1 < code.size(); ++i )
            {
                std::size_t at = 0;
                if ( code[i] == '.' && !IsIdentChar( code[i + 1] ) )
                    continue;
                if ( code[i] == '.' )
                    at = i + 1;
                else if ( code.compare( i, 2, "->" ) == 0 )
                    at = i + 2;
                else
                    continue;
                if ( code[i] == '.' && ( code[i - 1] == '.' || IsIdentChar( code[i - 1] ) == false ) )
                {
                    // `.5f`, `...`, `a . b` — a member access needs an identifier or a `)` `]` in front.
                    if ( code[i - 1] != ')' && code[i - 1] != ']' && !IsIdentChar( code[i - 1] ) )
                        continue;
                }
                while ( at < code.size() && ( code[at] == ' ' || code[at] == '\n' ) )
                    ++at;
                const std::size_t nameAt = at;
                while ( at < code.size() && IsIdentChar( code[at] ) )
                    ++at;
                if ( at == nameAt )
                    continue;
                std::size_t paren = at;
                while ( paren < code.size() && ( code[paren] == ' ' || code[paren] == '\n' ) )
                    ++paren;
                if ( paren >= code.size() || code[paren] != '(' )
                    continue;
                calls[code.substr( nameAt, at - nameAt )].push_back( fs::weakly_canonical( file ).string() );
            }
        }
        return calls;
    }

    struct CensusRow
    {
        const char* Class;
        const char* Method;
        const char* Header;
        const char* Verdict;
    };

    // THE REGISTER. Thirty-two pure virtuals that every implementer overrides and no translation unit
    // that can see the base ever calls. Each row says WHO decides, because an entry with no owner is
    // unreadable in a month — the rule ConfigOwnership's debt register already runs on.
    //
    // Г8 closed four rows rather than listing them: `RenderSystem::Shutdown` (the task); the orphan
    // duplicate of ImGuiLayer.hpp that declared a second `Desert::ImGui::ImGuiLayer` with the same
    // fully-qualified name and was included by nothing; and `Editor::Render::IRender::Init`/`::Render`,
    // an interface nothing derived from (see NoAbstractBaseIsLeftWithoutASingleImplementation).
    //
    // М9 closed the fifth: `MaterialProperty::Clone` is gone with all four of its implementations. The
    // comment left in its place (Properties/MaterialProperty.hpp) records why re-adding it in the shape
    // it had would be worse than not having it — every commented-out body was an ALIAS of the original's
    // GPU object, not a copy of it.
    constexpr CensusRow k_Census[] = {
         // ---- ONE ROW THAT IS A DESIGN QUESTION, NOT A CLEANUP -------------------------------------
         // Thirteen asset types implement Unload() and NOTHING in this engine ever evicts an asset. The
         // bodies are not wrong; the caller was never written. Deleting them decides that the engine
         // will not have eviction, which is the owner's call and not a tidy-up.
         { "AssetBase", "Unload", "Desert/Desert/Source/Engine/Assets/AssetBase.hpp",
           "OWNER DECIDES: does this engine want asset eviction? 13 implementations, no caller." },

         // ---- THE VULKAN BACKEND'S BIND VOCABULARY AND DEAD ACCESSORS --------------------------------
         // `Use`/`RT_Use` is the OpenGL "bind this object" idiom; a Vulkan backend binds through
         // descriptor sets and never calls it. Eight declarations across five bases, all with live
         // implementations underneath them. The accessors below are the same shape: written to complete
         // an interface, read by nobody.
         //
         // THEY ARE NOT REMOVED HERE BECAUSE Engine/Graphic/API/Vulkan WAS FENCED FOR THIS TASK — it had
         // just been rebuilt for device loss — and every implementation lives inside that fence.
         { "Image", "Use", "Desert/Desert/Source/Engine/Graphic/Image.hpp",
           "FENCED (API/Vulkan): the OpenGL bind idiom, unused by a descriptor-set backend." },
         { "Shader", "Use", "Desert/Desert/Source/Engine/Graphic/Shader.hpp",
           "FENCED (API/Vulkan): same bind idiom." },
         { "Shader", "RT_Use", "Desert/Desert/Source/Engine/Graphic/Shader.hpp",
           "FENCED (API/Vulkan): same bind idiom, render-thread spelling." },
         { "VertexBuffer", "Use", "Desert/Desert/Source/Engine/Graphic/VertexBuffer.hpp",
           "FENCED (API/Vulkan): same bind idiom." },
         { "VertexBuffer", "RT_Use", "Desert/Desert/Source/Engine/Graphic/VertexBuffer.hpp",
           "FENCED (API/Vulkan): same bind idiom, render-thread spelling." },
         { "IndexBuffer", "Use", "Desert/Desert/Source/Engine/Graphic/IndexBuffer.hpp",
           "FENCED (API/Vulkan): same bind idiom." },
         { "IndexBuffer", "RT_Use", "Desert/Desert/Source/Engine/Graphic/IndexBuffer.hpp",
           "FENCED (API/Vulkan): same bind idiom, render-thread spelling." },
         { "Framebuffer", "Use", "Desert/Desert/Source/Engine/Graphic/Framebuffer.hpp",
           "FENCED (API/Vulkan): same bind idiom." },
         { "Image", "GetImageFormat", "Desert/Desert/Source/Engine/Graphic/Image.hpp",
           "FENCED (API/Vulkan): the format is read off the specification instead." },
         { "Image", "IsLoaded", "Desert/Desert/Source/Engine/Graphic/Image.hpp",
           "FENCED (API/Vulkan): three backends keep the flag, nobody asks for it." },
         { "Image", "GetImagePixels", "Desert/Desert/Source/Engine/Graphic/Image.hpp",
           "FENCED (API/Vulkan): the DEAD half of a pair -- readback goes through "
           "Image2D::ReadPixelsRGBA8, which is called." },
         { "Image3D", "GetDepth", "Desert/Desert/Source/Engine/Graphic/Image.hpp",
           "FENCED (API/Vulkan): volume depth is read off the specification instead." },
         { "ComputePipeline", "GetInput", "Desert/Desert/Source/Engine/Graphic/Pipeline.hpp",
           "FENCED (API/Vulkan): inputs are set, never read back." },
         { "ComputePipeline", "GetOutput", "Desert/Desert/Source/Engine/Graphic/Pipeline.hpp",
           "FENCED (API/Vulkan): outputs are set, never read back." },
         { "Device", "IsFormatSupported", "Desert/Desert/Source/Engine/Core/Device.hpp",
           "FENCED (API/Vulkan), and the sharpest row here: its own comment says 'Prefer this over "
           "adding another Supports<Feature> bool' -- the RECOMMENDED question is the one nobody asks. "
           "Every caller still reads the cached DeviceCapabilities flags beside it." },
         { "MaterialBackend", "ApplyPushConstants",
           "Desert/Desert/Source/Engine/Graphic/Materials/MaterialBackend.hpp",
           "FENCED (API/Vulkan): the renderer pushes them itself at draw time out of "
           "MaterialExecutor::GetPushConstantBuffer(); this second route was never taken." },
         { "UniformImage2D", "GetImageHash", "Desert/Desert/Source/Engine/ShaderResources/UniformImage2D.hpp",
           "FENCED (API/Vulkan): the descriptor caches key off Image::GetHash directly." },
         { "UniformImageCube", "GetImageHash", "Desert/Desert/Source/Engine/ShaderResources/UniformImageCube.hpp",
           "FENCED (API/Vulkan): same as UniformImage2D." },

         // ---- THE PLATFORM WINDOW'S TITLE AND SIZE SURFACE -------------------------------------------
         // Six methods, implemented twice each (MacOSWindow, WindowsWindow), called nowhere. They read
         // like the API a custom title bar would need, and whether that is coming is the owner's call.
         { "Window", "GetTitle", "Desert/Desert/Source/Engine/Core/Window.hpp",
           "OWNER DECIDES: the custom-title-bar surface, implemented on both platforms, called nowhere." },
         { "Window", "SetTitle", "Desert/Desert/Source/Engine/Core/Window.hpp", "OWNER DECIDES: same surface." },
         { "Window", "SetWindowSize", "Desert/Desert/Source/Engine/Core/Window.hpp",
           "OWNER DECIDES: same surface." },
         { "Window", "Maximize", "Desert/Desert/Source/Engine/Core/Window.hpp", "OWNER DECIDES: same surface." },
         { "Window", "IsWindowMaximized", "Desert/Desert/Source/Engine/Core/Window.hpp",
           "OWNER DECIDES: same surface." },
         { "Window", "IsWindowMinimized", "Desert/Desert/Source/Engine/Core/Window.hpp",
           "OWNER DECIDES: same surface." },

         // ---- EDITOR INTERFACES WHOSE CALLER WAS NEVER WRITTEN ---------------------------------------
         { "IComponentWidget", "EntityHasComponent",
           "Editor/Source/Editor/Panels/SceneProperties/ComponentWidgets/IComponentWidget.hpp",
           "UNASSIGNED, and the most useful row of the three: the CRTP ComponentWidget<T> implements all "
           "three `override final` for every widget, and ScenePropertiesPanel still asks "
           "`entity.HasComponent<T>()` in a hand-written if-chain per component type. The generic route "
           "exists and is bypassed, so every new component type costs another branch. A task should "
           "either route Details through it or delete it." },
         { "IComponentWidget", "AddComponentToEntity",
           "Editor/Source/Editor/Panels/SceneProperties/ComponentWidgets/IComponentWidget.hpp",
           "UNASSIGNED: same interface." },
         { "IComponentWidget", "RemoveComponentFromEntity",
           "Editor/Source/Editor/Panels/SceneProperties/ComponentWidgets/IComponentWidget.hpp",
           "UNASSIGNED: same interface." },

         // ---- MATERIAL PROPERTY METADATA -------------------------------------------------------------
         { "IProperty", "GetTypeTag", "Desert/Desert/Source/Engine/Graphic/Materials/Properties/TProperty.hpp",
           "UNASSIGNED: the property type/editor-metadata surface, implemented by both property "
           "templates and asked by nothing. Same family as the `Clone` М9 removed." },
         { "IProperty", "GetEditorMeta", "Desert/Desert/Source/Engine/Graphic/Materials/Properties/TProperty.hpp",
           "UNASSIGNED: same surface." },
         { "IProperty", "SetEditorMeta", "Desert/Desert/Source/Engine/Graphic/Materials/Properties/TProperty.hpp",
           "UNASSIGNED: same surface." },

         // ---- ONE LEFTOVER ---------------------------------------------------------------------------
         { "MeshAsset", "GetMaterialHandle", "Desert/Desert/Source/Engine/Assets/Mesh/MeshAsset.hpp",
           "UNASSIGNED: the SINGULAR of a pair. Both mesh assets implement it, and every caller in the "
           "engine and the editor uses the plural GetMaterialHandles() beside it." },
    };

    std::string Key( const std::string& cls, const std::string& method )
    {
        return cls + "::" + method;
    }
} // namespace

TEST( PureVirtualCensus, TheScanSeesTheTreeAtAll )
{
    // AN EMPTY SUCCESSFUL ANSWER IS A SILENT WRONG ANSWER (contract §1.4). Every assertion below is of
    // the form "nothing unexpected was found", so a scan that found NOTHING passes all of them while
    // checking nothing at all. These are the floors under that.
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "could not locate the repository from " << fs::current_path().string();

    const Tree tree = ReadTree( root );
    ASSERT_GT( tree.Files.size(), 800u )
         << "only " << tree.Files.size() << " sources walked -- the walk, not the engine, is what is wrong";

    const std::vector<PureVirtual> pure = PureVirtuals( root, tree );
    EXPECT_GT( pure.size(), 150u ) << "only " << pure.size()
                                   << " pure virtuals parsed; the declaration scan has stopped working";

    // The include closure is what attributes a call to a base. A tree whose closures are empty would
    // report every pure virtual in the engine as dead.
    std::size_t widest = 0;
    for ( const auto& entry : tree.Closure )
        widest = std::max( widest, entry.second.size() );
    EXPECT_GT( widest, 100u ) << "the widest include closure is " << widest
                              << " headers; include resolution is broken and every row would read dead";
}

TEST( PureVirtualCensus, EveryDeadPureVirtualIsInTheRegisterAndEveryRegisterRowIsStillDead )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() );

    const Tree                                            tree  = ReadTree( root );
    const std::vector<PureVirtual>                        pure  = PureVirtuals( root, tree );
    const std::map<std::string, std::vector<std::string>> calls = DispatchedCalls( tree );

    std::map<std::string, PureVirtual> dead;
    for ( const auto& entry : pure )
    {
        const std::string declaring = fs::weakly_canonical( fs::path( root ) / entry.Header ).string();
        const auto        it        = calls.find( entry.Method );

        bool reached = false;
        if ( it != calls.end() )
        {
            for ( const std::string& caller : it->second )
            {
                if ( caller == declaring )
                {
                    reached = true;
                    break;
                }
                const auto closure = tree.Closure.find( caller );
                if ( closure != tree.Closure.end() && closure->second.count( declaring ) == 1 )
                {
                    reached = true;
                    break;
                }
            }
        }
        if ( !reached )
            dead.emplace( Key( entry.Class, entry.Method ), entry );
    }

    std::set<std::string> registered;
    for ( const auto& row : k_Census )
        registered.insert( Key( row.Class, row.Method ) );

    // BOTH DIRECTIONS, as DeviceLostCensus does. A new dead pure virtual nobody wrote a reason for is
    // the habit coming back; a row that is no longer dead is a census pinning something that has been
    // fixed, which passes while meaning nothing.
    for ( const auto& entry : dead )
    {
        EXPECT_EQ( registered.count( entry.first ), 1u )
             << entry.first << " (" << entry.second.Header << ":" << entry.second.Line
             << ") is a pure virtual that every implementer overrides and no translation unit able to "
                "see it ever calls. Either give it a caller, delete the whole virtual pair, or add a "
                "row to k_Census saying who decides.";
    }
    for ( const auto& row : k_Census )
    {
        EXPECT_EQ( dead.count( Key( row.Class, row.Method ) ), 1u )
             << Key( row.Class, row.Method ) << " has a census row (" << row.Verdict
             << ") and is NOT dead any more -- it is called, or it is gone. Delete the row: a census "
                "that pins nothing passes silently.";
    }
}

TEST( PureVirtualCensus, NoAbstractBaseIsLeftWithoutASingleImplementation )
{
    // THE SHAPE THE INCLUDE-CLOSURE RULE ABOVE CANNOT SEE, and it was live in the tree.
    // `Editor::Render::IRender` declared `Init()` and `Render()`, was included by exactly one header, and
    // NOTHING derived from it — so the interface could never be instantiated and its two pure virtuals
    // could never be called. The rule above missed it for a reason worth writing down: the header sits in
    // the include closure of half the editor, and `->Init(` and `->Render(` are two of the commonest calls
    // in that half, so every one of them was attributed to it. Method names collide; an inheritance edge
    // does not.
    //
    // This test needs no register: an abstract base with no derived class is dead by construction, there
    // are none left, and one appearing is always worth a conversation.
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() );

    const Tree tree = ReadTree( root );

    std::map<std::string, std::string> abstractBases; // class -> where it is declared
    std::set<std::string>              hasDerived;
    for ( const auto& file : tree.Files )
    {
        const std::string&           code  = tree.Code.at( file.string() );
        const std::vector<ClassSpan> spans = ClassSpans( code );
        for ( const auto& span : spans )
        {
            for ( const std::string& base : BaseNames( span.BaseClause ) )
                hasDerived.insert( base );
        }
    }

    const std::vector<PureVirtual> pure = PureVirtuals( root, tree );
    for ( const auto& entry : pure )
        abstractBases.emplace( entry.Class, entry.Header + ":" + std::to_string( entry.Line ) );

    ASSERT_GT( abstractBases.size(), 30u )
         << "only " << abstractBases.size() << " abstract bases found; the class scan has stopped working";
    ASSERT_GT( hasDerived.size(), 30u ) << "only " << hasDerived.size()
                                        << " base names parsed out of base-clauses; inheritance is not "
                                           "being read and every base would look orphaned";

    for ( const auto& base : abstractBases )
    {
        EXPECT_EQ( hasDerived.count( base.first ), 1u )
             << base.first << " (" << base.second
             << ") declares a pure virtual and NOTHING in the repository derives from it. It cannot be "
                "instantiated, so nothing it declares can ever run. Delete the interface, or give it the "
                "implementation it was written for.";
    }
}

TEST( PureVirtualCensus, TheNumberIsStatedSoAShrinkageIsVisible )
{
    // 32, and it was 35 when Г8 counted: `RenderSystem::Shutdown` and the orphan duplicate of
    // ImGuiLayer.hpp went with that task, and `MaterialProperty::Clone` with М9. Up is a regression;
    // down is welcome, and this line moves with it. The count is quoted because a per-row diff never
    // says "there are four more of these now".
    EXPECT_EQ( std::size( k_Census ), 32u )
         << "the number of pure virtuals implemented by everybody and called by nobody has changed";
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
