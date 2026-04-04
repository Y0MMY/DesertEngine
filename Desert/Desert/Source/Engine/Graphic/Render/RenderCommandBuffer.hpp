#pragma once

#include "RenderCommand.hpp"

namespace Desert::Graphic::Render
{
    class RenderCommandBuffer
    {
    public:
        template <typename T, typename... Args>
        void Emplace( Args&&... args )
        {
            m_Commands.emplace_back( std::make_unique<T>( std::forward<Args>( args )... ) );
        }

        void ExecuteAll( Graphic::SceneRenderer& renderer )
        {
            for ( auto& cmd : m_Commands )
            {
                cmd->Execute( renderer );
            }
        }

        void Clear()
        {
            m_Commands.clear();
        }

    private:
        std::vector<std::unique_ptr<RenderCommand>> m_Commands;
    };

} // namespace Desert::Graphic::Render