#pragma once

#include <Engine/Core/Scene.hpp>

#include <Common/Core/ResultStr.hpp>

#include <glm/glm.hpp>

#include <string_view>

namespace Desert::Core
{
    class SceneSerializer
    {
    public:
        explicit SceneSerializer( const Scene* scene, const Assets::AssetManager* assetManager );

        std::string SerializeToJson() const;

        /// Loads a scene from the JSON text of a .desce file into the scene this serializer was made for.
        ///
        /// FAILS, rather than repairs, on a file that is not at the current generation of the format
        /// (Core::kSceneVersion / Core::kUnitVersion). The error names the file, what it is, what is
        /// required and the exact SceneMigrator command that converts it — and NOTHING is created for it:
        /// not an entity, not a setting, not the scene name. The scene is left exactly as it was.
        ///
        /// @param source what to call this file in that error. A PATH when there is one; the play-mode
        ///        snapshot has no file, so it says so. It is never used to open anything - this function
        ///        does not touch the disk, and passing the path is only how the message can name it.
        [[nodiscard]] Common::BoolResultStr DeserializeFromJson( const std::string& json,
                                                                 std::string_view   source ) const;

        /// Writes the scene to `Scene/<its name>.desce` and SAYS WHETHER THE BYTES LANDED.
        ///
        /// This used to return void, and so did everything above it — Scene::Serialize and the editor's
        /// Ctrl+S — which made a failed save indistinguishable from a successful one all the way up to
        /// the user: the "unsaved changes" star went out and a green "Saved 'X'" toast appeared for a
        /// scene that was still only in memory. The error names the destination and the step that
        /// failed; on failure the file on disk is byte-identical to what it was (the write primitive is
        /// write-then-rename), so the right thing for a caller to do is keep the scene dirty and say so.
        [[nodiscard]] Common::BoolResultStr SaveToFile() const;

        /// The path SaveToFile writes, derived from the scene's name. Exposed so a caller can NAME the
        /// file in its own message without recomputing the derivation (which is how two spellings of
        /// one path start to drift).
        [[nodiscard]] Common::Filepath TargetPath() const;

    private:
        Scene*                m_Scene;
        Assets::AssetManager* m_AssetManager;
    };

} // namespace Desert::Core