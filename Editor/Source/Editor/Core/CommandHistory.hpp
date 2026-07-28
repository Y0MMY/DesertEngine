#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace Desert::Editor
{
    // One undoable editor action. Commands should identify their targets by STABLE ids (entity UUIDs)
    // so an entry stays valid regardless of what happened to the scene in between; Undo/Redo return
    // false when the target no longer exists, and the stack silently discards such stale entries.
    //
    // Commands that instead hold raw pointers into live objects (the reflected-property byte edits)
    // must report IsVolatile() — the stack drops them whenever their target may have died (selection
    // change, entity creation/destruction), because a stale pointer can't even be *detected*, only
    // crashed into.
    class ICommand
    {
    public:
        virtual ~ICommand() = default;

        virtual bool Undo() = 0;
        virtual bool Redo() = 0;

        virtual bool IsVolatile() const
        {
            return false;
        }
    };

    // The single editor-wide undo/redo stack: reflected-property edits, gizmo transforms and structural
    // scene changes (create/delete/reparent/duplicate/rename) all land here, so Ctrl+Z walks back through
    // everything in the order it actually happened.
    class CommandHistory
    {
    public:
        static CommandHistory& Get()
        {
            static CommandHistory s_Instance;
            return s_Instance;
        }

        // Reflected-property byte edit (see PropertyEditorBuilder): before/after bytes at a raw field
        // address. Pointer-based -> volatile (dropped when the edited object may have died).
        void Push( void* target, const void* oldBytes, const void* newBytes, std::size_t size )
        {
            PushCommand( std::make_unique<ByteCommand>( target, oldBytes, newBytes, size ) );
        }

        void PushCommand( std::unique_ptr<ICommand> command )
        {
            // A new edit invalidates the redo branch.
            m_Redo.clear();
            m_Undo.push_back( std::move( command ) );
            if ( m_Undo.size() > kMaxEntries )
                m_Undo.erase( m_Undo.begin() );
            ++m_Revision;
        }

        bool Undo()
        {
            // Stale entries (target entity gone) report failure — discard them and keep walking down.
            while ( !m_Undo.empty() )
            {
                std::unique_ptr<ICommand> cmd = std::move( m_Undo.back() );
                m_Undo.pop_back();
                if ( cmd->Undo() )
                {
                    m_Redo.push_back( std::move( cmd ) );
                    ++m_Revision;
                    return true;
                }
            }
            return false;
        }

        bool Redo()
        {
            while ( !m_Redo.empty() )
            {
                std::unique_ptr<ICommand> cmd = std::move( m_Redo.back() );
                m_Redo.pop_back();
                if ( cmd->Redo() )
                {
                    m_Undo.push_back( std::move( cmd ) );
                    ++m_Revision;
                    return true;
                }
            }
            return false;
        }

        // Monotonic edit counter (bumped by every push/undo/redo). "Unsaved changes" = the revision moved
        // since the last save marker; Clear() does NOT bump it (loading a scene isn't an edit).
        uint64_t Revision() const
        {
            return m_Revision;
        }

        // Drops only the pointer-based (volatile) entries; UUID-addressed structural commands survive.
        void DropVolatile()
        {
            auto drop = []( std::vector<std::unique_ptr<ICommand>>& stack )
            {
                std::erase_if( stack, []( const std::unique_ptr<ICommand>& c ) { return c->IsVolatile(); } );
            };
            drop( m_Undo );
            drop( m_Redo );
        }

        void Clear()
        {
            m_Undo.clear();
            m_Redo.clear();
        }

    private:
        class ByteCommand final : public ICommand
        {
        public:
            ByteCommand( void* target, const void* oldBytes, const void* newBytes, std::size_t size )
                 : m_Target( target ), m_Size( size )
            {
                m_Old.assign( static_cast<const uint8_t*>( oldBytes ),
                              static_cast<const uint8_t*>( oldBytes ) + size );
                m_New.assign( static_cast<const uint8_t*>( newBytes ),
                              static_cast<const uint8_t*>( newBytes ) + size );
            }

            bool Undo() override
            {
                std::memcpy( m_Target, m_Old.data(), m_Size );
                return true;
            }

            bool Redo() override
            {
                std::memcpy( m_Target, m_New.data(), m_Size );
                return true;
            }

            bool IsVolatile() const override
            {
                return true;
            }

        private:
            void*                m_Target = nullptr;
            std::size_t          m_Size   = 0;
            std::vector<uint8_t> m_Old;
            std::vector<uint8_t> m_New;
        };

        static constexpr size_t kMaxEntries = 256;

        std::vector<std::unique_ptr<ICommand>> m_Undo;
        std::vector<std::unique_ptr<ICommand>> m_Redo;
        uint64_t                               m_Revision = 0;
    };
} // namespace Desert::Editor
