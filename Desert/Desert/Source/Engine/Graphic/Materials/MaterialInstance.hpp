#pragma once

#include "MaterialProperty.hpp"
#include <memory>
#include <vector>

namespace Desert::Graphic
{
    class Material;
    class MaterialInstance;

    using MaterialInstancePtr = std::shared_ptr<MaterialInstance>;
    using MaterialPtr         = std::shared_ptr<Material>;

    class MaterialInstance : public std::enable_shared_from_this<MaterialInstance>
    {
    public:
        MaterialInstance( Material* parentMaterial, const std::string& name = "" );
        virtual ~MaterialInstance() = default;

        // Getters with type safety
        float     GetFloat( const std::string& name, float defaultValue = 0.0f ) const;
        int       GetInt( const std::string& name, int defaultValue = 0 ) const;
        bool      GetBool( const std::string& name, bool defaultValue = false ) const;
        glm::vec2 GetVec2( const std::string& name, const glm::vec2& defaultValue = glm::vec2( 0.0f ) ) const;
        glm::vec3 GetVec3( const std::string& name, const glm::vec3& defaultValue = glm::vec3( 0.0f ) ) const;
        glm::vec4 GetVec4( const std::string& name, const glm::vec4& defaultValue = glm::vec4( 0.0f ) ) const;
        glm::mat4 GetMat4( const std::string& name, const glm::mat4& defaultValue = glm::mat4( 1.0f ) ) const;
        void*     GetTexture( const std::string& name ) const;

        // Setters with dirty tracking
        void SetFloat( const std::string& name, float value );
        void SetInt( const std::string& name, int value );
        void SetBool( const std::string& name, bool value );
        void SetVec2( const std::string& name, const glm::vec2& value );
        void SetVec3( const std::string& name, const glm::vec3& value );
        void SetVec4( const std::string& name, const glm::vec4& value );
        void SetMat4( const std::string& name, const glm::mat4& value );
        void SetTexture( const std::string& name, void* texture );

        // Batch operations
        void SetParameters( const std::vector<std::pair<std::string, MaterialPropertyValue>>& params );
        void SetParameters( const MaterialPropertySet& properties );

        // Instance management
        MaterialInstancePtr CreateChildInstance( const std::string& name = "" );
        Material*           GetParentMaterial() const
        {
            return m_ParentMaterial;
        }
        MaterialInstancePtr GetParentInstance() const
        {
            return m_ParentInstance.lock();
        }
        const std::vector<MaterialInstancePtr>& GetChildInstances() const
        {
            return m_ChildInstances;
        }

        // Property access
        bool                       HasParameter( const std::string& name ) const;
        const MaterialPropertySet& GetPropertySet() const
        {
            return m_Properties;
        }
        MaterialPropertySet& GetPropertySetMutable()
        {
            return m_Properties;
        }

        // GPU update
        void MarkNeedsApply()
        {
            m_bNeedsApply = true;
        }
        void Apply();
        bool NeedsApply() const
        {
            return m_bNeedsApply;
        }

        // Debug
        const std::string& GetName() const
        {
            return m_Name;
        }
        void SetName( const std::string& name )
        {
            m_Name = name;
        }

    private:
        void                  PropagateToChildren();
        MaterialPropertyValue ResolveProperty( const std::string& name ) const;

        Material*                        m_ParentMaterial;
        std::string                      m_Name;
        MaterialPropertySet              m_Properties;
        std::weak_ptr<MaterialInstance>  m_ParentInstance;
        std::vector<MaterialInstancePtr> m_ChildInstances;
        bool                             m_bNeedsApply    = true;
        uint32_t                         m_LastApplyFrame = 0;
    };
} // namespace Desert::Graphic