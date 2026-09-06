#pragma once

#include <Common/Core/ResultStr.hpp>

#include <rflcpp/rfl/Generic.hpp>
#include <rflcpp/rfl/json.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Desert::Editor::Control
{
    /**
     * @brief THE WIRE. One JSON object per line in, one JSON object per line out.
     *
     * WHY A CHANNEL AT ALL, and why this file is the whole of its vocabulary. This editor already had a
     * control surface: eighteen command-line flags, four of them purely about driving the interface
     * (`--select`, `--open-panel`, `--open-menu`, `--preview-orbit`). Nobody designed it — each flag was
     * added by whichever developer needed a picture that week, because macOS refuses synthetic input to
     * this machine and a click is therefore not available. It grew by one flag per task and could only
     * ever grow that way, since a flag is read once at boot and a session is not a boot.
     *
     * This replaces that growth with a channel, and the four control flags are DELETED rather than kept
     * beside it: two ways to open a panel is the defect shape this codebase spends its days removing, and
     * the losing side is always the one nobody remembers to update.
     *
     * THE PARSE IS TOTAL, exactly as Editor/Core/CommandLine.hpp is for argv, and for the same reason
     * written out there at length: a request this parser does not understand is an ERROR NAMING ITSELF,
     * never a request that quietly did nothing. A client that mistyped an operation and got silence would
     * conclude the editor had done what it asked.
     *
     * NOTHING HERE TOUCHES A SOCKET, ImGui, THE RENDERER OR A GLOBAL. Bytes in, a request out; a response
     * in, bytes out. That is what makes the whole protocol assertable by a suite — and it has to be,
     * because EditorLayer.cpp is compiled by no suite at all (scripts/CI/UnreachedSources.sh) and a rule
     * written there is a rule nothing can show going red.
     */

    /// What a request asks for. Deliberately a closed set: the channel's job is to reach the COMMAND
    /// PALETTE, not to grow an instruction set of its own — see ControlDispatch.hpp.
    enum class Op
    {
        Commands,     ///< list every command the palette offers this instant
        Run,          ///< run one of them, addressed by group + label
        State,        ///< read the editor's state as JSON
        ShotWindow,   ///< capture the WHOLE editor, interface included (swapchain readback)
        ShotViewport, ///< capture the 3D viewport only, no interface (the scene's final image)
        Quit,         ///< end the session with an exit status
    };

    /// One accepted operation. A table, for the same reason kCommandLineFlags is one: the message that
    /// lists the known operations is built FROM the set the parser accepts, so the two cannot drift.
    struct OpSpec
    {
        const char* Name;
        Op          Operation;
    };

    inline constexpr OpSpec kOps[] = {
         { "commands", Op::Commands },
         { "run", Op::Run },
         { "state", Op::State },
         { "shot.window", Op::ShotWindow },
         { "shot.viewport", Op::ShotViewport },
         { "quit", Op::Quit },
    };

    /// TWO CAPTURES THAT NEVER SUBSTITUTE FOR EACH OTHER, and that is why they are two operations rather
    /// than one with a flag. `shot.window` reads the presented swapchain image and therefore contains the
    /// panels, the menus and the dialogs; `shot.viewport` reads the scene's own final image and contains
    /// none of them, because ImGui is recorded into the swapchain pass (VulkanImGuiLayer::End).
    ///
    /// Until this channel existed, only the second capture existed at all — so no picture ever taken by
    /// this engine held one pixel of its interface, and `--open-menu`'s promise that "a capture can show
    /// what is in it" was a promise its own capture path could not keep. A window shot that silently
    /// fell back to the viewport would be that same failure with a new name: a picture of the wrong
    /// subject, delivered under the name of the right one.
    [[nodiscard]] constexpr bool IsShot( Op op ) noexcept
    {
        return op == Op::ShotWindow || op == Op::ShotViewport;
    }

    struct Request
    {
        /// Echoed in the response. A client that pipelines needs to know which answer is whose, and an id
        /// the client chose is the only thing that can tell it — the editor's own frame counter cannot.
        int64_t Id = 0;

        Op Operation = Op::Commands;

        /// Op::Run — the palette entry, addressed exactly as the palette shows it.
        std::string Group;
        std::string Label;

        /// Op::ShotWindow / Op::ShotViewport — where the PNG goes.
        std::string Path;

        /// Op::State — which sections to include. Empty means all of them.
        std::vector<std::string> Sections;

        /// Op::Quit — the process exit status.
        int32_t ExitCode = 0;
    };

    /// The known operations, comma separated, for the message a rejected one gets. Built from the table
    /// so an operation added above appears here without anyone remembering to add it.
    [[nodiscard]] inline std::string KnownOpList()
    {
        std::string list;
        for ( const OpSpec& spec : kOps )
        {
            if ( !list.empty() )
                list += ", ";
            list += spec.Name;
        }
        return list;
    }

    namespace ProtocolDetail
    {
        /// A string field, or empty when absent. Absent and empty-string are the same thing to every
        /// consumer here, so they are not distinguished — a client that omits `group` and a client that
        /// sends `"group": ""` have both failed to name a command, and get the same refusal.
        [[nodiscard]] inline std::string ReadString( const rfl::Generic::Object& object, const char* key )
        {
            const auto field = object.get( key );
            if ( !field )
                return {};
            const auto text = field.value().to_string();
            return text ? text.value() : std::string{};
        }

        /// An integer field, or @p fallback.
        ///
        /// BOTH SPELLINGS ARE ACCEPTED, and that is not defensive coding: reflect-cpp keeps a whole JSON
        /// number in the variant arm its text implies, so `7` arrives as an int64 and `7.0` as a double.
        /// Asking only for a double made every `"id": 7` read as the fallback — which meant every reply
        /// came back with id 0 and a client that pipelined could not match one answer to its question.
        /// Measured, not reasoned about: the round-trip test caught it on the first run.
        [[nodiscard]] inline int64_t ReadInt( const rfl::Generic::Object& object, const char* key,
                                              int64_t fallback )
        {
            const auto field = object.get( key );
            if ( !field )
                return fallback;

            if ( const auto whole = field.value().to_int64() )
                return whole.value();
            if ( const auto real = field.value().to_double() )
                return static_cast<int64_t>( real.value() );
            return fallback;
        }

        /// An array-of-strings field, or empty. Anything in the array that is not a string is DROPPED
        /// rather than stringified: a section name is matched against a known set downstream, so a number
        /// here would be reported as an unknown section, which is a clearer message than a coerced "3".
        [[nodiscard]] inline std::vector<std::string> ReadStringArray( const rfl::Generic::Object& object,
                                                                       const char*                 key )
        {
            std::vector<std::string> values;
            const auto               field = object.get( key );
            if ( !field )
                return values;

            const auto array = field.value().to_array();
            if ( !array )
                return values;

            for ( const auto& element : array.value() )
            {
                if ( const auto text = element.to_string() )
                    values.push_back( text.value() );
            }
            return values;
        }
    } // namespace ProtocolDetail

    /**
     * @brief One line of JSON -> a request, or a NAMED failure.
     *
     * Every failure mode below used to be a silent no-op in the flag family this replaces:
     *   - text that is not a JSON object at all;
     *   - no `op` field;
     *   - an `op` this parser does not know;
     *   - `run` without a group or without a label;
     *   - a shot without a path to write to.
     */
    [[nodiscard]] inline Common::ResultStr<Request> ParseRequest( std::string_view line )
    {
        using namespace ProtocolDetail;

        const auto parsed = rfl::json::read<rfl::Generic>( std::string( line ) );
        if ( !parsed )
        {
            return Common::MakeFormattedError<Request>( "the request is not JSON: {}", parsed.error().what() );
        }

        const auto object = parsed.value().to_object();
        if ( !object )
        {
            return Common::MakeError<Request>(
                 "the request is JSON but not an object; every request is a single object with an 'op' "
                 "field." );
        }

        const rfl::Generic::Object& fields = object.value();

        const std::string opName = ReadString( fields, "op" );
        if ( opName.empty() )
        {
            return Common::MakeFormattedError<Request>(
                 "the request names no operation ('op' is missing or empty). Known operations: {}",
                 KnownOpList() );
        }

        const OpSpec* spec = nullptr;
        for ( const OpSpec& candidate : kOps )
        {
            if ( opName == candidate.Name )
            {
                spec = &candidate;
                break;
            }
        }

        if ( spec == nullptr )
        {
            return Common::MakeFormattedError<Request>(
                 "'{}' is not an operation this editor knows. An operation dropped in silence would look "
                 "exactly like an editor that ignored it. Known operations: {}",
                 opName, KnownOpList() );
        }

        Request request;
        request.Id        = ReadInt( fields, "id", 0 );
        request.Operation = spec->Operation;

        switch ( spec->Operation )
        {
            case Op::Run:
            {
                request.Group = ReadString( fields, "group" );
                request.Label = ReadString( fields, "label" );
                if ( request.Group.empty() || request.Label.empty() )
                {
                    return Common::MakeError<Request>(
                         "'run' addresses a palette entry by BOTH its group and its label, and one of them "
                         "is missing. Ask 'commands' for the pairs this editor offers right now." );
                }
                break;
            }
            case Op::ShotWindow:
            case Op::ShotViewport:
            {
                request.Path = ReadString( fields, "path" );
                if ( request.Path.empty() )
                {
                    return Common::MakeError<Request>(
                         "a shot needs a 'path' to write the PNG to. A capture with nowhere to go would "
                         "report success and leave no evidence." );
                }
                break;
            }
            case Op::State:
                request.Sections = ReadStringArray( fields, "sections" );
                break;
            case Op::Quit:
                request.ExitCode = static_cast<int32_t>( ReadInt( fields, "code", 0 ) );
                break;
            case Op::Commands:
                break;
        }

        return Common::MakeSuccess( std::move( request ) );
    }

    /**
     * @brief What goes back. ALWAYS carries an outcome, and a refusal ALWAYS carries a reason.
     *
     * The two factories are the only way to build one, and that is the point: a default-constructed
     * response would be a silent failure — `ok` false with nothing said — which is precisely the shape
     * this project keeps finding and removing. Here the type cannot express it.
     */
    class Response
    {
    public:
        [[nodiscard]] static Response Success( int64_t id, rfl::Generic::Object payload = {} )
        {
            Response response;
            response.m_Id      = id;
            response.m_Ok      = true;
            response.m_Payload = std::move( payload );
            return response;
        }

        [[nodiscard]] static Response Failure( int64_t id, std::string reason )
        {
            Response response;
            response.m_Id = id;
            response.m_Ok = false;
            // A refusal with nothing said is the defect this class exists to prevent, so an empty reason
            // is not quietly accepted — it becomes a message that names the channel itself as the fault.
            // Loud and wrong beats silent and wrong: someone reads this and fixes the call site.
            response.m_Error = reason.empty()
                                    ? std::string( "a refusal was produced with no reason given; that is a "
                                                   "defect in the control channel, not in the request." )
                                    : std::move( reason );
            return response;
        }

        [[nodiscard]] int64_t Id() const noexcept
        {
            return m_Id;
        }
        [[nodiscard]] bool Ok() const noexcept
        {
            return m_Ok;
        }
        [[nodiscard]] const std::string& Error() const noexcept
        {
            return m_Error;
        }
        [[nodiscard]] const rfl::Generic::Object& Payload() const noexcept
        {
            return m_Payload;
        }

    private:
        Response() = default;

        int64_t              m_Id = 0;
        bool                 m_Ok = false;
        std::string          m_Error;
        rfl::Generic::Object m_Payload;
    };

    /// The three keys the outcome owns. A payload may not carry them, whatever it thinks it is doing.
    inline constexpr const char* kReservedResponseKeys[] = { "id", "ok", "error" };

    /**
     * @brief The response as ONE LINE of JSON.
     *
     * ONE LINE, because the framing is one message per line: a payload carrying a newline would split one
     * reply into two, and the second half would be read as the answer to the NEXT request.
     *
     * THE RESERVED KEYS ARE REBUILT, NOT OVERWRITTEN. Copying the payload and then assigning `ok` over it
     * is not enough, and the difference is the whole of a defect this had: a SUCCESS whose payload happens
     * to carry a key called `error` would keep it, and every client convention in existence reads an
     * `error` field as a failure. So the payload is filtered first and the outcome written into a clean
     * object — the three fields every reader depends on come from the response and from nowhere else.
     */
    [[nodiscard]] inline std::string FormatResponse( const Response& response )
    {
        rfl::Generic::Object object;

        for ( const auto& [key, value] : response.Payload() )
        {
            const bool reserved =
                 std::any_of( std::begin( kReservedResponseKeys ), std::end( kReservedResponseKeys ),
                              [&key]( const char* name ) { return key == name; } );
            if ( !reserved )
                object[key] = value;
        }

        // AN INTEGER, NOT A DOUBLE. reflect-cpp keeps a number in the variant arm its type implies, so a
        // double comes back out as `1.0` — and a client that matched its request id against the reply's
        // would have been comparing 1 with 1.0. The request side accepts both spellings (ReadInt); the
        // reply side emits the one the request used.
        object["id"] = rfl::Generic( response.Id() );
        object["ok"] = rfl::Generic( response.Ok() );
        if ( !response.Ok() )
            object["error"] = rfl::Generic( response.Error() );

        std::string text = rfl::json::write( rfl::Generic( object ) );
        std::erase( text, '\n' );
        std::erase( text, '\r' );
        return text;
    }
} // namespace Desert::Editor::Control
