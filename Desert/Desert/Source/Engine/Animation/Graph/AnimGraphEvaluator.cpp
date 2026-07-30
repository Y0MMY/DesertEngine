#include "AnimGraph.hpp"

namespace Desert::Animation::Graph
{
    Evaluator::Evaluator( AnimGraph graph ) : m_Graph( std::move( graph ) )
    {
        Reset();
    }

    void Evaluator::Reset()
    {
        m_Params.clear();
        for ( const auto& p : m_Graph.Parameters )
            m_Params[p.Name] = p.Default;

        m_Current = m_Graph.Entry.empty() ? ( m_Graph.States.empty() ? -1 : 0 ) : FindState( m_Graph.Entry );
        if ( m_Current < 0 && !m_Graph.States.empty() )
            m_Current = 0; // entry named a missing state -> fall back to the first
    }

    void Evaluator::SetFloat( const std::string& name, float value )
    {
        m_Params[name] = value;
    }

    void Evaluator::SetBool( const std::string& name, bool value )
    {
        m_Params[name] = value ? 1.0f : 0.0f;
    }

    void Evaluator::SetInt( const std::string& name, int value )
    {
        m_Params[name] = static_cast<float>( value );
    }

    float Evaluator::GetFloat( const std::string& name ) const
    {
        const auto it = m_Params.find( name );
        return it == m_Params.end() ? 0.0f : it->second;
    }

    int Evaluator::FindState( const std::string& name ) const
    {
        for ( size_t i = 0; i < m_Graph.States.size(); ++i )
            if ( m_Graph.States[i].Name == name )
                return static_cast<int>( i );
        return -1;
    }

    const State* Evaluator::CurrentState() const
    {
        return ( m_Current >= 0 && m_Current < static_cast<int>( m_Graph.States.size() ) )
                    ? &m_Graph.States[m_Current]
                    : nullptr;
    }

    bool Evaluator::EvaluateCondition( const Condition& c ) const
    {
        const float v = GetFloat( c.Parameter );
        switch ( static_cast<CompareOp>( c.Op ) )
        {
            case CompareOp::Greater:
                return v > c.Value;
            case CompareOp::Less:
                return v < c.Value;
            case CompareOp::GreaterEqual:
                return v >= c.Value;
            case CompareOp::LessEqual:
                return v <= c.Value;
            case CompareOp::Equals:
                return v == c.Value;
            case CompareOp::NotEquals:
                return v != c.Value;
            case CompareOp::IsTrue:
                return v != 0.0f;
            case CompareOp::IsFalse:
                return v == 0.0f;
        }
        return false;
    }

    Evaluator::Result Evaluator::Update( float normalizedTime )
    {
        Result result;

        if ( m_Current < 0 )
            m_Current = m_Graph.States.empty() ? -1 : 0;
        result.Current = CurrentState();
        if ( !result.Current )
            return result;

        for ( const auto& t : result.Current->Transitions )
        {
            if ( t.HasExitTime && normalizedTime < t.ExitTime )
                continue;

            // Empty condition set is valid: a pure exit-time (or unconditional) transition auto-advances.
            bool pass = true;
            for ( const auto& c : t.Conditions )
            {
                if ( !EvaluateCondition( c ) )
                {
                    pass = false;
                    break;
                }
            }
            if ( !pass )
                continue;

            const int target = FindState( t.To );
            if ( target < 0 || target == m_Current )
                continue; // dangling target / self-loop -> ignore (never "changes")

            m_Current      = target;
            result.Current = CurrentState();
            result.Changed = true;
            result.Blend   = t.Blend;
            break; // one transition per tick
        }

        return result;
    }
} // namespace Desert::Animation::Graph
