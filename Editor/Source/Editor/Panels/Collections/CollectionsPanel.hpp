#pragma once

#include <Editor/Panels/IPanel.hpp>

#include <Editor/Widgets/UIHelper/ImGuiUI.hpp>
#include <Editor/Widgets/AssetThumbnailRenderer.hpp>
#include <Editor/Widgets/ThumbnailCache.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Desert::Assets
{
    class AssetManager;
}

namespace Desert::Editor
{
    class ImportManager;

    // Asset LIBRARY browser. Each "collection" is a folder under Resources/Collections/<Pack>/ with a
    // `collection.json` manifest (name + items: { name, category, mesh, thumbnail }). NOT a parallel asset
    // system — items just carry a mesh SOURCE path that's emitted as a "MESH_ASSET" drag payload, so dropping
    // a card onto the Foliage panel (or anywhere that accepts meshes) cooks+uses it through the normal
    // pipeline. UI = searchable, category-filtered grid of cards with rendered mesh previews (shared on-disk
    // thumbnail cache + offscreen renderer, same as the File Explorer).
    struct CollectionItem
    {
        std::string Name;
        std::string Category;
        std::string MeshPath;  // working-dir-relative source path, emitted verbatim as the DnD payload
        std::string Thumbnail; // optional
    };

    struct LoadedCollection
    {
        std::string                 Name;
        std::string                 Author;
        std::vector<CollectionItem> Items;
    };

    class CollectionsPanel final : public IPanel
    {
    public:
        explicit CollectionsPanel( Assets::AssetManager* assetManager );
        void OnUIRender() override;

    private:
        void Rescan();                                                  // scan Resources/Collections/*/collection.json
        void DrawCollectionList();                                      // top level: the collections themselves
        void DrawCollectionContents( const LoadedCollection& coll );    // inside one collection: its meshes
        void DrawCard( const CollectionItem& item, float cardW, float imgH ); // preview + name + DnD source

        std::vector<LoadedCollection> m_Collections;
        std::vector<std::string>      m_Categories;     // [0] = "All", then unique item categories
        char                          m_Search[128]    = { 0 };
        int                           m_CategoryFilter = 0;
        int                           m_OpenCollection = -1; // -1 = showing the collection list, else an index

        // Rendered mesh previews (mirrors the File Explorer): resolve+cook each mesh once, render offscreen to
        // the shared Cooked/Thumbnails cache, then decode the PNG for display.
        Assets::AssetManager*                              m_AssetManager = nullptr;
        std::unique_ptr<UI::UIHelper>                      m_UIHelper;
        std::unique_ptr<ImportManager>                     m_ImportManager; // resolves texture paths -> handles
        std::unique_ptr<AssetThumbnailRenderer>            m_ThumbRenderer;
        std::unique_ptr<ThumbnailCache>                    m_Thumbs;
        std::unordered_map<std::string, Assets::AssetHandle> m_ResolvedMesh; // mesh source path -> handle (cached)
    };
} // namespace Desert::Editor
