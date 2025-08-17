#pragma once

#include <Engine/Desert.hpp>

namespace Desert::Editor
{
    class IComponentWidget
    {
    public:
        explicit IComponentWidget( std::string&& widgetName ) : m_WidgetName( std::move( widgetName ) )
        {
        }
        virtual ~IComponentWidget()                              = default;
        virtual void               Render( ECS::Entity& entity ) = 0;
        virtual const std::string& GetName() const final
        {
            return m_WidgetName;
        }
        virtual bool CanRemove() const
        {
            return true;
        }

        virtual bool EntityHasComponent( ECS::Entity& entity ) const  = 0;
        virtual void AddComponentToEntity( ECS::Entity& entity )      = 0;
        virtual void RemoveComponentFromEntity( ECS::Entity& entity ) = 0;

    protected:
        std::string m_WidgetName;
    };

    template <typename ComponentT>
    class ComponentWidget : public IComponentWidget
    {
    public:
        using IComponentWidget::IComponentWidget;

        bool EntityHasComponent( ECS::Entity& entity ) const override final
        {
            return entity.template HasComponent<ComponentT>();
        }

        void AddComponentToEntity( ECS::Entity& entity ) override final
        {
            entity.template AddComponent<ComponentT>();
        }

        void RemoveComponentFromEntity( ECS::Entity& entity ) override final
        {
            // entity.template RemoveComponent<ComponentT>();
        }
    };
} // namespace Desert::Editor