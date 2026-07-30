#pragma once

#include <Common/Core/ResultStr.hpp>

#include <string>
#include <unordered_map>
#include <vector>

// Data-driven animation state machine ("AnimGraph"): a set of STATES (each playing a named clip) connected by
// TRANSITIONS whose CONDITIONS compare live PARAMETERS. The runtime Evaluator walks it each frame and reports
// which clip should be playing (+ blend duration on a change); the ECS then drives the existing Animator with
// that result. Plain structs so reflect-cpp round-trips the graph to/from JSON with no boilerplate (see
// AnimGraphSerialization.cpp); enums are stored as int for stable, tolerant serialization.
namespace Desert::Animation::Graph
{
    enum class ParamType : int
    {
        Bool  = 0,
        Int   = 1,
        Float = 2,
    };

    // Comparison of a parameter against a constant. IsTrue/IsFalse treat the parameter as a bool (!= 0).
    enum class CompareOp : int
    {
        Greater      = 0,
        Less         = 1,
        GreaterEqual = 2,
        LessEqual    = 3,
        Equals       = 4,
        NotEquals    = 5,
        IsTrue       = 6,
        IsFalse      = 7,
    };

    struct Parameter
    {
        std::string Name;
        int         Type    = static_cast<int>( ParamType::Float );
        float       Default = 0.0f; // bool encoded as 0/1, int as a whole number
    };

    struct Condition
    {
        std::string Parameter;
        int         Op    = static_cast<int>( CompareOp::Greater );
        float       Value = 0.0f;
    };

    struct Transition
    {
        std::string            To;                 // target state name
        float                  Blend       = 0.2f; // cross-fade seconds when this transition fires
        bool                   HasExitTime = false;
        float                  ExitTime    = 1.0f; // require the source clip to reach this fraction [0,1] first
        std::vector<Condition> Conditions;         // ALL must hold (logical AND); empty + exit-time = auto-advance
    };

    struct State
    {
        std::string             Name;
        std::string             Clip; // clip name, resolved against the AnimationLibrary at runtime
        bool                    Loop  = true;
        float                   Speed = 1.0f;
        float                   X     = 0.0f; // editor canvas position (persisted, unused at runtime)
        float                   Y     = 0.0f;
        std::vector<Transition> Transitions;
    };

    struct AnimGraph
    {
        std::string            Name = "AnimGraph";
        std::string            Entry; // entry state name (defaults to the first state if empty)
        std::vector<Parameter> Parameters;
        std::vector<State>     States;
    };

    // JSON round-trip (reflect-cpp). Serialize never fails; Deserialize returns an error string on bad JSON.
    std::string                  Serialize( const AnimGraph& graph );
    Common::ResultStr<AnimGraph> Deserialize( const std::string& json );

    // Runtime state-machine evaluator: owns a copy of the graph + live parameter values + the current state.
    // Parameters are all held as float (bool = 0/1, int as a whole number) for a single uniform store.
    class Evaluator
    {
    public:
        explicit Evaluator( AnimGraph graph );

        void Reset(); // jump to the entry state and seed parameters to their defaults

        // Replaces the graph in place while PRESERVING the running state (matched by name) and live parameter
        // values (new parameters get their defaults). Lets the editor edit a live graph without the state
        // machine snapping back to entry. Falls back to entry only if the active state was removed/renamed.
        void SyncGraph( AnimGraph graph );

        void  SetFloat( const std::string& name, float value );
        void  SetBool( const std::string& name, bool value );
        void  SetInt( const std::string& name, int value );
        float GetFloat( const std::string& name ) const;

        struct Result
        {
            const State* Current = nullptr; // current state after this tick (null only if the graph has no states)
            bool         Changed = false;   // a transition fired this tick
            float        Blend   = 0.0f;    // cross-fade seconds for the change (valid when Changed)
        };

        // Advances one tick. normalizedTime = the current clip's playback fraction [0,1] (for exit-time gating;
        // pass 0 if unknown). Fires at most one transition per tick (the first eligible one, in list order).
        Result Update( float normalizedTime );

        [[nodiscard]] const AnimGraph& Graph() const
        {
            return m_Graph;
        }
        [[nodiscard]] const State* CurrentState() const;

    private:
        [[nodiscard]] int  FindState( const std::string& name ) const;
        [[nodiscard]] bool EvaluateCondition( const Condition& c ) const;

        AnimGraph                              m_Graph;
        std::unordered_map<std::string, float> m_Params;
        int                                    m_Current = -1;
    };
} // namespace Desert::Animation::Graph
