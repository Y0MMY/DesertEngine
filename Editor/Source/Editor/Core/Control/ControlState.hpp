#pragma once

#include <Common/Core/ResultStr.hpp>

#include <Editor/Core/Control/ControlPipeline.hpp>

#include <rflcpp/rfl/Generic.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Desert::Editor::Control
{
    /**
     * @brief WHAT THE EDITOR LOOKS LIKE, as plain values, and its rendering as JSON.
     *
     * WHY A SNAPSHOT STRUCT AND NOT "SERIALISE IT WHERE IT LIVES". Every fact below is held somewhere in
     * EditorLayer, and EditorLayer.cpp is compiled by NO test suite at all (scripts/CI/UnreachedSources.sh).
     * A JSON writer over live editor state would be a writer nothing could ever show going red — and this
     * is the half of the channel a client REASONS about: a number here disagreeing with the picture beside
     * it is worse than no number, because a report quotes it.
     *
     * So EditorLayer does the one thing only it can do — read its own members — and everything after that
     * is pure and assertable: the section filter, the shapes, the names of the fields.
     *
     * THE SNAPSHOT IS TAKEN ALL AT ONCE, which is the other reason it is a value. A client asking for the
     * documents and the slot census in one request must get two halves of ONE instant; gathered lazily per
     * section they could straddle a close, and the answer would be a state the editor was never in.
     */

    struct DocumentSnapshot
    {
        std::string Name;    ///< the display half, as a person reads it on the tab
        std::string Type;    ///< the asset type's name
        std::string Subject; ///< the asset handle, decimal — an id survives a rename
        bool        HoldsRendererSlot  = false;
        bool        ClaimsRendererSlot = false;
        bool        Focused            = false;
    };

    struct ClosedDocumentSnapshot
    {
        std::string Name;
        std::string Type;
        std::string Subject;
    };

    struct PanelSnapshot
    {
        std::string Name;
        bool        Visible    = false;
        bool        Pinned     = false;
        bool        Contextual = false;
        bool        Relevant   = true;
    };

    struct EntitySnapshot
    {
        std::string Tag;
        std::string Uuid;
    };

    /// The whole picture, gathered in one pass. Every list is in the order the editor itself shows it —
    /// the documents most-recently-used first, exactly as Ctrl+Tab walks them — so a client reading this
    /// and a person reading the screen are reading the same sequence.
    struct EditorSnapshot
    {
        std::string SceneName;
        bool        SceneHasUnsavedChanges = false;
        bool        InPlayMode             = false;

        std::vector<EntitySnapshot> Selection; ///< last element is the primary selection

        std::vector<DocumentSnapshot>       Documents;      ///< most recently used first
        std::vector<ClosedDocumentSnapshot> RecentlyClosed; ///< newest first
        std::vector<PanelSnapshot>          Panels;         ///< tools only; a document is never here

        uint32_t RendererSlotsLive    = 0;
        uint32_t RendererSlotsPending = 0;
        uint32_t RendererSlotsMax     = 0;

        std::size_t              LogInfoCount    = 0;
        std::size_t              LogWarningCount = 0;
        std::size_t              LogErrorCount   = 0;
        std::vector<std::string> LogTail; ///< oldest first

        EditorQuiescence Quiescence;
    };

    /// The sections a client may ask for. A table, so the refusal that lists them is built from the set
    /// actually honoured — the same rule the command line follows for its flags, and for the same reason:
    /// a list written out by hand drifts, and the drift shows up as a section that silently returns nothing.
    inline constexpr const char* kStateSections[] = {
         "scene", "selection", "documents", "panels", "renderer_slots", "log", "quiescence",
    };

    [[nodiscard]] inline std::string KnownSectionList()
    {
        std::string list;
        for ( const char* section : kStateSections )
        {
            if ( !list.empty() )
                list += ", ";
            list += section;
        }
        return list;
    }

    /// Refuse a section nobody serves, naming the ones served. A section quietly missing from the answer
    /// would read as "the editor has none of those" — an empty document list is what a client sees either
    /// way, and one of those two readings is a lie.
    [[nodiscard]] inline Common::BoolResultStr ValidateSections( const std::vector<std::string>& sections )
    {
        for ( const std::string& wanted : sections )
        {
            bool known = false;
            for ( const char* section : kStateSections )
            {
                if ( wanted == section )
                {
                    known = true;
                    break;
                }
            }

            if ( !known )
            {
                return Common::MakeFormattedError<bool>(
                     "'{}' is not a section of the editor's state. Asking for one that does not exist would "
                     "come back empty, which reads exactly like a section that exists and is empty. Known "
                     "sections: {}",
                     wanted, KnownSectionList() );
            }
        }
        return Common::MakeSuccess( true );
    }

    namespace StateDetail
    {
        /// Was @p section asked for? An EMPTY request means all of them — a client that wants everything
        /// should not have to enumerate what "everything" is today.
        [[nodiscard]] inline bool Wanted( const std::vector<std::string>& sections, const char* section )
        {
            if ( sections.empty() )
                return true;
            for ( const std::string& asked : sections )
                if ( asked == section )
                    return true;
            return false;
        }

        [[nodiscard]] inline rfl::Generic Str( const std::string& text )
        {
            return rfl::Generic( text );
        }

        [[nodiscard]] inline rfl::Generic Num( double value )
        {
            return rfl::Generic( value );
        }
    } // namespace StateDetail

    /// The snapshot as JSON, restricted to @p sections (empty = all). Callers validate the sections
    /// first; anything unknown here is simply absent, because the refusal already happened.
    [[nodiscard]] inline rfl::Generic::Object ToJson( const EditorSnapshot&           snapshot,
                                                      const std::vector<std::string>& sections )
    {
        using namespace StateDetail;
        rfl::Generic::Object root;

        if ( Wanted( sections, "scene" ) )
        {
            rfl::Generic::Object scene;
            scene["name"]     = Str( snapshot.SceneName );
            scene["modified"] = rfl::Generic( snapshot.SceneHasUnsavedChanges );
            scene["playing"]  = rfl::Generic( snapshot.InPlayMode );
            root["scene"]     = rfl::Generic( scene );
        }

        if ( Wanted( sections, "selection" ) )
        {
            rfl::Generic::Array selection;
            for ( const EntitySnapshot& entity : snapshot.Selection )
            {
                rfl::Generic::Object item;
                item["tag"]  = Str( entity.Tag );
                item["uuid"] = Str( entity.Uuid );
                selection.push_back( rfl::Generic( item ) );
            }
            root["selection"] = rfl::Generic( selection );
        }

        if ( Wanted( sections, "documents" ) )
        {
            rfl::Generic::Array open;
            for ( const DocumentSnapshot& document : snapshot.Documents )
            {
                rfl::Generic::Object item;
                item["name"]       = Str( document.Name );
                item["type"]       = Str( document.Type );
                item["subject"]    = Str( document.Subject );
                item["holdsSlot"]  = rfl::Generic( document.HoldsRendererSlot );
                item["claimsSlot"] = rfl::Generic( document.ClaimsRendererSlot );
                item["focused"]    = rfl::Generic( document.Focused );
                open.push_back( rfl::Generic( item ) );
            }

            rfl::Generic::Array closed;
            for ( const ClosedDocumentSnapshot& document : snapshot.RecentlyClosed )
            {
                rfl::Generic::Object item;
                item["name"]    = Str( document.Name );
                item["type"]    = Str( document.Type );
                item["subject"] = Str( document.Subject );
                closed.push_back( rfl::Generic( item ) );
            }

            rfl::Generic::Object documents;
            documents["open"] = rfl::Generic( open );
            // The list the empty document well offers back, newest first. Named on the wire because it is
            // the one piece of document state that outlives the window it describes.
            documents["recentlyClosed"] = rfl::Generic( closed );
            root["documents"]           = rfl::Generic( documents );
        }

        if ( Wanted( sections, "panels" ) )
        {
            rfl::Generic::Array panels;
            for ( const PanelSnapshot& panel : snapshot.Panels )
            {
                rfl::Generic::Object item;
                item["name"]       = Str( panel.Name );
                item["visible"]    = rfl::Generic( panel.Visible );
                item["pinned"]     = rfl::Generic( panel.Pinned );
                item["contextual"] = rfl::Generic( panel.Contextual );
                item["relevant"]   = rfl::Generic( panel.Relevant );
                panels.push_back( rfl::Generic( item ) );
            }
            root["panels"] = rfl::Generic( panels );
        }

        if ( Wanted( sections, "renderer_slots" ) )
        {
            rfl::Generic::Object slots;
            slots["live"]          = Num( snapshot.RendererSlotsLive );
            slots["pending"]       = Num( snapshot.RendererSlotsPending );
            slots["max"]           = Num( snapshot.RendererSlotsMax );
            root["renderer_slots"] = rfl::Generic( slots );
        }

        if ( Wanted( sections, "log" ) )
        {
            rfl::Generic::Array tail;
            for ( const std::string& line : snapshot.LogTail )
                tail.push_back( Str( line ) );

            rfl::Generic::Object log;
            log["info"]    = Num( static_cast<double>( snapshot.LogInfoCount ) );
            log["warning"] = Num( static_cast<double>( snapshot.LogWarningCount ) );
            log["error"]   = Num( static_cast<double>( snapshot.LogErrorCount ) );
            log["tail"]    = rfl::Generic( tail );
            root["log"]    = rfl::Generic( log );
        }

        if ( Wanted( sections, "quiescence" ) )
        {
            rfl::Generic::Object quiescence;
            quiescence["settled"] = rfl::Generic( snapshot.Quiescence.Settled() );
            // What is outstanding, in the same words a settle timeout uses. One vocabulary, so a client
            // that read "asset documents are waiting to be opened" here recognises it in a refusal.
            quiescence["outstanding"] = Str( snapshot.Quiescence.Describe() );
            root["quiescence"]        = rfl::Generic( quiescence );
        }

        return root;
    }
} // namespace Desert::Editor::Control
