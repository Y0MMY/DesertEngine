#include "AnimGraph.hpp"

#include <rflcpp/rfl/json.hpp>
#include <rflcpp/rfl/DefaultIfMissing.hpp>

#include <format>

// reflect-cpp round-trip for the AnimGraph (all plain structs). DefaultIfMissing tolerates graphs saved by an
// older build that lacked a field, so adding fields never breaks existing .animgraph files.
namespace Desert::Animation::Graph
{
    std::string Serialize( const AnimGraph& graph )
    {
        return rfl::json::write( graph );
    }

    Common::ResultStr<AnimGraph> Deserialize( const std::string& json )
    {
        auto parsed = rfl::json::read<AnimGraph, rfl::DefaultIfMissing>( json );
        if ( !parsed )
            return Common::MakeError<AnimGraph>( std::format( "bad .animgraph: {}", parsed.error().what() ) );
        return Common::MakeSuccess( parsed.value() );
    }
} // namespace Desert::Animation::Graph
