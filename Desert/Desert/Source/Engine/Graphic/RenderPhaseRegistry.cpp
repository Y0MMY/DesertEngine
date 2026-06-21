#include <Engine/Graphic/RenderPhaseRegistry.hpp>

namespace Desert::Graphic
{
    RenderPhaseRegistry::RenderPhaseRegistry()
    {
        // Pre-register all built-in phases in canonical order.
        struct { RenderPhaseID id; const char* name; } builtins[] = {
            { RenderPhase::DepthPrePass,  "DepthPrePass"  },
            { RenderPhase::Sky,           "Sky"           },
            { RenderPhase::Geometry,      "Geometry"      },
            { RenderPhase::Outline,       "Outline"       },
            { RenderPhase::Decals,        "Decals"        },
            { RenderPhase::Lighting,      "Lighting"      },
            { RenderPhase::Transparency,  "Transparency"  },
            { RenderPhase::PostProcess,   "PostProcess"   },
            { RenderPhase::Overlay,       "Overlay"       },
            { RenderPhase::UI,            "UI"            },
            { RenderPhase::Debug,         "Debug"         },
        };

        m_Names.emplace( RenderPhase::None, "None" );
        for ( const auto& b : builtins )
        {
            m_Names.emplace( b.id, b.name );
            m_DeclOrder.push_back( b.id );
        }
    }

    RenderPhaseID RenderPhaseRegistry::Register( std::string_view name )
    {
        const RenderPhaseID id = m_NextUserID++;
        m_Names.emplace( id, std::string( name ) );
        m_DeclOrder.push_back( id );
        return id;
    }

    std::string_view RenderPhaseRegistry::GetName( RenderPhaseID id ) const
    {
        auto it = m_Names.find( id );
        return it != m_Names.end() ? std::string_view( it->second ) : "Unknown";
    }

    // -----------------------------------------------------------------------
    // Free function declared in RenderPhase.hpp
    // -----------------------------------------------------------------------
    std::string_view RenderPhaseToString( RenderPhaseID id )
    {
        return RenderPhaseRegistry::GetInstance().GetName( id );
    }

} // namespace Desert::Graphic
