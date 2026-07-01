#pragma once

#include <Engine/Desert.hpp>
#include <Common/Core/Math/Ray.hpp>

#include <glm/glm.hpp>

#include <functional>
#include <string>

namespace Desert::Editor::Tools
{
    // UE5-style foliage painting, extracted from ViewportPanel (god-object split). The tool is stateless —
    // brush/selection live in Editor::Core::FoliagePaint; the host supplies the scene, asset manager, the
    // cursor ray and a mesh-resolver (so the tool doesn't duplicate the panel's mesh resolution).
    class FoliagePaintTool
    {
    public:
        // Floating panel: Paint/Erase tabs, brush sliders, type list (multi-select), per-type settings.
        // viewportPos = the scene-image top-left (for placing the floating window).
        void DrawPanel( ::Desert::Core::Scene& scene, const Assets::AssetManager* assetManager,
                        const glm::vec2& viewportPos );

        // Paint/erase every CHECKED foliage type under the cursor ray (one dab). Surface hit comes from the
        // engine-owned Scene::Raycast (no duplicated raycast / mesh resolution).
        void Paint( ::Desert::Core::Scene& scene, const Common::Math::Ray& ray );

    private:
        void CreateType( ::Desert::Core::Scene& scene, const Assets::AssetManager* assetManager,
                         const std::string& meshSourcePath );
    };
} // namespace Desert::Editor::Tools
