#pragma once

#include <Engine/Graphic/RenderPhase.hpp>
#include <Common/Core/Singleton.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Desert::Graphic
{
    // Maintains the set of known render phases and their declaration order.
    //
    // Lifecycle:
    //   - Created via CreateInstance() before any render graph is built
    //     (SceneRenderer::Init does this automatically).
    //   - Built-in phases are pre-registered in the constructor.
    //   - RT-layer code registers user phases via Register() before
    //     SceneRenderer::RebuildRenderGraph() is called.
    class RenderPhaseRegistry final : public Common::Singleton<RenderPhaseRegistry>
    {
    public:
        RenderPhaseRegistry();

        // Registers a new user-defined phase and returns its unique ID.
        // The returned ID is stable for the lifetime of the registry.
        // Thread safety: not thread-safe; call before render graph is built.
        RenderPhaseID Register( std::string_view name );

        // Returns the human-readable name, or "Unknown" for unregistered IDs.
        std::string_view GetName( RenderPhaseID id ) const;

        // Registration order: built-in phases first (in engine declaration order),
        // then user phases in the order Register() was called.
        // This is the seed sequence fed into the topological sort tie-breaker.
        const std::vector<RenderPhaseID>& GetDeclarationOrder() const
        {
            return m_DeclOrder;
        }

    private:
        std::unordered_map<RenderPhaseID, std::string> m_Names;
        std::vector<RenderPhaseID>                     m_DeclOrder;
        RenderPhaseID                                  m_NextUserID = RenderPhase::k_UserBase;
    };

} // namespace Desert::Graphic
