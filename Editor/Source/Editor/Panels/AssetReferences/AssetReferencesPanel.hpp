#pragma once

#include "../IPanel.hpp"

#include <Editor/Core/AssetReferences.hpp>

#include <string>
#include <vector>

namespace Desert::Editor
{
    // "Asset References" — a read-only usage browser over the open project. Rebuild scans the Assets
    // tree into an AssetReferenceIndex; from there you can see who references a given asset and which
    // leaf assets (textures, materials) nothing references (cleanup candidates). Hidden by default;
    // enable via View -> Asset References.
    class AssetReferencesPanel final : public IPanel
    {
    public:
        AssetReferencesPanel() : IPanel( "Asset References", /*showPanel=*/false )
        {
        }

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 460.0f, 560.0f );
        }

        void OnUIRender() override;

    private:
        void Rebuild();

        AssetReferenceIndex      m_Index;
        bool                     m_Built = false;
        std::vector<std::string> m_AllPaths;   // sorted entry paths for the lookup combo
        int                      m_Selected = -1;
        std::vector<std::string> m_Orphans;    // cached from the last Rebuild
    };
} // namespace Desert::Editor
