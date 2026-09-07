#include "SubjectEditorRegistry.hpp"

#include <Common/Core/Logger.hpp>

namespace Desert::Editor
{
    void SubjectEditorRegistry::Register( SubjectTypeKey type, Registration editor )
    {
        const std::string& typeName = editor.TypeName;

        if ( type.Domain == SubjectDomain::Unknown || type.Facet == 0u )
        {
            LOG_ERROR( "[SubjectEditorRegistry] refusing to register the editor '{}' under an empty subject "
                       "type (domain '{}', facet {}) — nothing could ever ask for it.",
                       typeName, SubjectDomainName( type.Domain ), type.Facet );
            return;
        }

        if ( !editor.Make )
        {
            LOG_ERROR( "[SubjectEditorRegistry] refusing an empty factory for subject type '{}' — the type "
                       "would read as having an editor and open nothing.",
                       typeName );
            return;
        }

        if ( !editor.Icon )
        {
            LOG_ERROR( "[SubjectEditorRegistry] '{}' registered without an icon — the document well, the "
                       "Documents menu and the window title all draw one, and three call sites would each "
                       "have had to invent a fallback.",
                       typeName );
            return;
        }

        if ( const auto it = m_Editors.find( type ); it != m_Editors.end() )
        {
            // TWO NAMES UNDER ONE KEY IS A DIGEST COLLISION, and it is the one failure mode of deriving a
            // component facet from its type name (EditorSubject.hpp::ComponentFacet). Left alone it would
            // be a Details button that opens somebody else's editor, with nothing anywhere saying why. Said
            // out loud, with both names, it is a startup error a reader can act on.
            if ( it->second.TypeName != typeName )
            {
                LOG_ERROR( "[SubjectEditorRegistry] '{}' and '{}' both hash to subject type (domain '{}', "
                           "facet {}). One of the two names has to change — sharing a key means sharing an "
                           "editor, and the loser would open the winner's window with no error anywhere.",
                           it->second.TypeName, typeName, SubjectDomainName( type.Domain ), type.Facet );
                return;
            }

            // NAMED rather than overwritten quietly: the loser of a duplicate registration is decided by
            // startup order, so a reader of the log has to be told which editor actually won.
            LOG_WARN( "[SubjectEditorRegistry] subject type '{}' already had an editor — the later "
                      "registration replaces it.",
                      typeName );
        }

        m_Editors[type] = std::move( editor );
    }

    bool SubjectEditorRegistry::HasEditorFor( SubjectTypeKey type ) const noexcept
    {
        return m_Editors.find( type ) != m_Editors.end();
    }

    std::string SubjectEditorRegistry::TypeName( SubjectTypeKey type ) const
    {
        if ( const auto it = m_Editors.find( type ); it != m_Editors.end() )
            return it->second.TypeName;

        // The number is kept, not swallowed: this string is what a refusal and a census print, and a
        // subject nothing is registered for is exactly the case a reader has to be able to chase.
        return std::string( "unregistered " ) + SubjectDomainName( type.Domain ) + " subject (facet " +
               std::to_string( type.Facet ) + ")";
    }

    std::vector<SubjectTypeKey> SubjectEditorRegistry::RegisteredTypes() const
    {
        std::vector<SubjectTypeKey> types;
        types.reserve( m_Editors.size() );
        for ( const auto& [type, editor] : m_Editors )
            types.push_back( type );
        return types;
    }

    std::unique_ptr<ISubjectDocument> SubjectEditorRegistry::Create( const SubjectId& subject ) const
    {
        if ( subject.IsNull() )
        {
            LOG_ERROR( "[SubjectEditorRegistry] open request carries an empty subject ('{}') — whatever "
                       "resolved a file or an entity to a subject failed and said nothing.",
                       subject.ToString() );
            return nullptr;
        }

        const auto it = m_Editors.find( subject.Type() );
        if ( it == m_Editors.end() )
        {
            LOG_WARN( "[SubjectEditorRegistry] no editor is registered for subject '{}' — nothing opened.",
                      subject.ToString() );
            return nullptr;
        }

        auto document = it->second.Make( subject );
        if ( !document )
        {
            LOG_ERROR( "[SubjectEditorRegistry] the '{}' editor could not build a document for subject '{}'.",
                       it->second.TypeName, subject.ToString() );
        }
        return document;
    }

    const char* SubjectEditorRegistry::Icon( SubjectTypeKey type, const char* fallback ) const
    {
        if ( const auto it = m_Editors.find( type ); it != m_Editors.end() )
            return it->second.Icon;
        return fallback;
    }

    void SubjectEditorRegistry::RegisterPathOpener( PathOpener opener )
    {
        if ( !opener )
        {
            LOG_ERROR( "[SubjectEditorRegistry] refusing an empty path opener — every path would fall "
                       "through it silently and a double-click would do nothing." );
            return;
        }
        m_PathOpeners.push_back( std::move( opener ) );
    }

    SubjectEditorRegistry::PathOpenOutcome SubjectEditorRegistry::OpenPath( const std::string& path ) const
    {
        for ( const PathOpener& opener : m_PathOpeners )
        {
            // The FIRST opener that claims the path wins, and the rest are not consulted. Registration
            // order is therefore load-bearing only if two openers claim one extension, which is a
            // programming error either way — extensions are what they dispatch on and they are disjoint.
            if ( const PathOpenOutcome outcome = opener( path ); outcome != PathOpenOutcome::NotMine )
                return outcome;
        }
        return PathOpenOutcome::NotMine;
    }
} // namespace Desert::Editor
