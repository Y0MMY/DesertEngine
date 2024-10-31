#pragma once

#include <Common/Core/Events/Event.hpp>
#include <Common/Core/Memory/Shared.hpp>

#include <string>

namespace Common
{
	struct WindowSpecification
	{
		std::string Title = "Sandbox";
		uint32_t Width = 1600;
		uint32_t Height = 900;
		bool Decorated = true;
		bool Fullscreen = false;
		bool VSync = true;
	};

	class Window : public Common::Memory::RefCounted
	{
	public:
		virtual ~Window() = default;

		using EventCallbackFn = std::function<void(Common::Event&)>;

		[[nodiscard]] virtual const std::string& GetTitle() const = 0;
		virtual void SetTitle(const std::string& title) = 0;

		virtual void SetSize(uint32_t width, uint32_t height) = 0;
		[[nodiscard]] virtual uint32_t GetWidth() const = 0;
		[[nodiscard]] virtual uint32_t GetHeight() const = 0;
		virtual void SetVSync(bool enabled) const = 0;
		[[nodiscard]] virtual const void* GetNativeWindow() const = 0;

		virtual bool IsWindowMaximized() const = 0;
		virtual bool IsWindowMinimized() const = 0;
		virtual void Maximize() const = 0;

		virtual void SetEventCallback(const EventCallbackFn& e) = 0;

		static Common::Memory::Shared<Window> Create(const WindowSpecification& specification);
	};
}