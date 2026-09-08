#pragma once

#include <Common/Core/AssetHandle.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace Desert::Assets
{
    /**
     * @brief THE ASSETS SOMETHING STILL NEEDS, AND WHAT THAT SOMETHING IS.
     *
     * Eviction's whole difficulty is the question this type answers: an asset is referenced by a HANDLE —
     * a 64-bit number sitting in an ECS component, a scene setting or another asset's file — and a number
     * has no reference count. `shared_ptr::use_count()` on the registry's own pointer says nothing,
     * because the registry holds one for every asset for ever and a component holds none.
     *
     * SO THE ANSWER IS REACHABILITY, NOT COUNTING, and that is the same conclusion Unreal reached: its
     * garbage collector traces UPROPERTY-declared references from a root set and frees what the trace does
     * not touch. It reached it for our reason too — a raw pointer that the reflection cannot see does not
     * keep anything alive there either. What we do NOT copy is what happens next (see AssetEviction.hpp).
     *
     * EVERY MARK CARRIES A REASON, and that is not decoration. The reason is what a person reads when an
     * asset they expected to be released is still resident, and it is what a REFUSAL prints. Without it,
     * "this asset was kept" is unfalsifiable — which is how a root set that has quietly stopped visiting a
     * whole component type goes unnoticed for a month. The Desert/Tests/Engine/AssetRoots census exists for
     * the same reason from the other side.
     */
    class AssetRootSet final
    {
    public:
        /// Keep @p handle, because @p why. The null handle is IGNORED, not stored: it is the vocabulary
        /// for "this slot is empty", and an empty slot is not a reference to asset zero.
        void Mark( const Common::AssetHandle& handle, std::string why )
        {
            if ( static_cast<uint64_t>( handle ) == 0 )
                return;

            // FIRST reason wins. A popular asset is named by fifty entities and the fiftieth reason is no
            // more informative than the first; keeping the first also makes the message stable between
            // runs, which matters because these strings end up in a test's expectations.
            m_Reasons.emplace( static_cast<uint64_t>( handle ), std::move( why ) );
        }

        [[nodiscard]] bool Contains( const Common::AssetHandle& handle ) const
        {
            return m_Reasons.find( static_cast<uint64_t>( handle ) ) != m_Reasons.end();
        }

        /// Why @p handle is being kept, or an empty string when it is not in the set at all. The two are
        /// distinguishable through Contains; this deliberately does not conflate them.
        [[nodiscard]] const std::string& WhyKept( const Common::AssetHandle& handle ) const
        {
            static const std::string kNotKept;
            const auto               it = m_Reasons.find( static_cast<uint64_t>( handle ) );
            return it == m_Reasons.end() ? kNotKept : it->second;
        }

        [[nodiscard]] std::size_t Size() const noexcept
        {
            return m_Reasons.size();
        }

        [[nodiscard]] std::vector<Common::AssetHandle> Handles() const
        {
            std::vector<Common::AssetHandle> handles;
            handles.reserve( m_Reasons.size() );
            for ( const auto& [value, why] : m_Reasons )
                handles.emplace_back( value );
            return handles;
        }

    private:
        // Keyed on the raw uint64 rather than on AssetHandle so this header needs no hash specialisation
        // and can be included by the ECS layer, which is where most of the roots come from.
        std::unordered_map<uint64_t, std::string> m_Reasons;
    };
} // namespace Desert::Assets
