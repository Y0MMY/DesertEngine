#pragma once

#include <unordered_map>
#include "AnimationState.hpp"
#include "AnimationParameters.hpp"
#include "../Animator.hpp"

namespace Desert::Animation
{
    class AnimationStateMachine
    {
    public:
        explicit AnimationStateMachine( Animator& animator ) : m_Animator( animator )
        {
        }

        void AddState( AnimationState state )
        {
            m_States.emplace( state.Name, std::move( state ) );
        }

        void SetEntryState( const std::string& name )
        {
            m_CurrentState = &m_States.at( name );
        }

        void Update()
        {
            if ( !m_CurrentState )
            {
                return;
            }

            for ( auto& transition : m_CurrentState->Transitions )
            {
                if ( transition.Condition && transition.Condition() )
                {
                    ChangeState( transition.TargetState, transition.BlendDuration );
                    break;
                }
            }
        }

        AnimationParameters& GetParameters()
        {
            return m_Params;
        }

    private:
        void ChangeState( const std::string& newState, float blend )
        {
           /* auto& next = m_States.at( newState );

            if ( m_CurrentState == &next )
            {
                return;
            }

            m_CurrentState = &next;

            const AnimationClip* currentClip = m_Animator.GetCurrentClip();

            auto clip = m_Animator.GetSkeleton().GetClip( next.ClipName );
            if ( !clip )
                return;

            if ( currentClip )
                m_Animator.CrossFade( *clip, blend, next.Loop );
            else
                m_Animator.Play( *clip, next.Loop );*/
        }

    private:
        Animator& m_Animator;

        std::unordered_map<std::string, AnimationState> m_States;
        AnimationState*                                 m_CurrentState = nullptr;

        AnimationParameters m_Params;
    };
} // namespace Desert::Animation