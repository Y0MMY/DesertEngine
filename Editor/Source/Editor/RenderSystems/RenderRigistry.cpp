#include "RenderRigistry.hpp"

namespace Desert::Editor::Render
{
    RenderRegistry::RenderRegistry( const std::shared_ptr<Core::Scene>& scene ) : m_Scene( scene )
    {
     //   (m_RenderGrid = std::make_unique<Grid>( scene ))->Init();
    }

    void RenderRegistry::Render()
    {
        
    }

} // namespace Desert::Editor::Render