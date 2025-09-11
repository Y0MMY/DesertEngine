#pragma once

namespace Desert::Engine
{
    class Device
    {
    public:
        virtual ~Device() = default;

        static std::shared_ptr<Device> Create();
    };

} // namespace Desert::Engine