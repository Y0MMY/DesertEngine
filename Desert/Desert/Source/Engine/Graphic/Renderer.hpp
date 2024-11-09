#pragma once

#include <Engine/Graphic/RendererContext.hpp>

namespace Desert::Graphic
{
	class Renderer : public Common::Singleton<Renderer> 
	{
	public:
		Common::BoolResult Init();
	private:
		void InitGraphicAPI();
	private:
		std::shared_ptr<RendererContext> m_RendererContext;
	};
}