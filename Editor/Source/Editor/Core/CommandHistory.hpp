#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace Desert::Editor
{
    // Generic byte-level edit history for the reflection-driven property editor. A command records a
    // reflected field's memory location plus its before/after bytes, so undo/redo works for ANY field
    // type with no per-type code. Cleared when the edited root object changes (bounds dangling ptrs).
    class CommandHistory
    {
    public:
        static CommandHistory& Get()
        {
            static CommandHistory s_Instance;
            return s_Instance;
        }

        void Push( void* target, const void* oldBytes, const void* newBytes, std::size_t size )
        {
            // A new edit invalidates the redo branch.
            m_Redo.clear();

            Command cmd;
            cmd.Target = target;
            cmd.Size   = size;
            cmd.Old.assign( static_cast<const uint8_t*>( oldBytes ), static_cast<const uint8_t*>( oldBytes ) + size );
            cmd.New.assign( static_cast<const uint8_t*>( newBytes ), static_cast<const uint8_t*>( newBytes ) + size );
            m_Undo.push_back( std::move( cmd ) );
        }

        bool Undo()
        {
            if ( m_Undo.empty() )
                return false;
            Command cmd = std::move( m_Undo.back() );
            m_Undo.pop_back();
            std::memcpy( cmd.Target, cmd.Old.data(), cmd.Size );
            m_Redo.push_back( std::move( cmd ) );
            return true;
        }

        bool Redo()
        {
            if ( m_Redo.empty() )
                return false;
            Command cmd = std::move( m_Redo.back() );
            m_Redo.pop_back();
            std::memcpy( cmd.Target, cmd.New.data(), cmd.Size );
            m_Undo.push_back( std::move( cmd ) );
            return true;
        }

        void Clear()
        {
            m_Undo.clear();
            m_Redo.clear();
        }

    private:
        struct Command
        {
            void*                Target = nullptr;
            std::size_t          Size   = 0;
            std::vector<uint8_t> Old;
            std::vector<uint8_t> New;
        };

        std::vector<Command> m_Undo;
        std::vector<Command> m_Redo;
    };
} // namespace Desert::Editor
