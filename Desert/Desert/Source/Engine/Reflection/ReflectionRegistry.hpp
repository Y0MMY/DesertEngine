#pragma once

#include <Engine/Reflection/ReflectionTypes.hpp>

#include <unordered_map>

namespace Desert::Reflection
{
    // Global registry of reflected types. Populated at static-initialization time by the generated
    // reflection code (DesertHeaderTool output) and queried by the editor's PropertyEditorBuilder and
    // by the automatic shader-upload path.
    class ReflectionRegistry
    {
    public:
        static ReflectionRegistry& Get();

        // Registers (or replaces) a type. Returns a stable pointer valid for the process lifetime.
        const TypeInfo* Register( TypeInfo info );

        [[nodiscard]] const TypeInfo* Find( const std::string& name ) const;

        [[nodiscard]] const std::unordered_map<std::string, TypeInfo>& All() const
        {
            return m_Types;
        }

        // Resolves FieldInfo::StructType pointers for every Struct field by name. Called once after all
        // generated registrations have run (types can reference each other regardless of init order).
        void ResolveStructLinks();

    private:
        ReflectionRegistry() = default;

        std::unordered_map<std::string, TypeInfo> m_Types;
    };

    // Defined in the generated Reflection.gen.cpp. Call once at engine startup to force the generated
    // translation unit (and therefore its static type registrations) to be linked in from the static
    // library. Without a reference the linker discards the object and the registry stays empty.
    void ForceLinkGeneratedReflection();
} // namespace Desert::Reflection
