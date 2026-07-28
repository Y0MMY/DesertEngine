#pragma once

#include <Common/Core/ResultStr.hpp>
#include <memory>
#include <optional>
#include <string>

namespace Desert::Engine
{
    /**
     * @brief Represents the hardware graphics/compute device capabilities.
     */
    struct DeviceCapabilities
    {
        uint64_t MaxStorageBufferSize;
        uint64_t StorageBufferAlignment;
        bool     SupportsWideLines;
        float    MaxLineWidth;
        bool     SupportsAnisotropy = false;
        float    MaxAnisotropy      = 1.0f;
        // VK fillModeNonSolid: wireframe (VK_POLYGON_MODE_LINE) pipelines. Independent from wideLines —
        // MoltenVK supports non-solid fill but NOT wide lines.
        bool     SupportsNonSolidFill = false;
    };

    /**
     * @brief Abstract interface for a graphics/compute device (Physical and Logical).
     */
    class Device
    {
    public:
        virtual ~Device() = default;

        /**
         * @brief Returns the device's hardware capabilities.
         */
        [[nodiscard]] virtual const DeviceCapabilities& GetCapabilities() const = 0;

        /**
         * @brief Wait for all device operations to complete.
         */
        virtual void WaitIdle() const = 0;

        /**
         * @brief Platform/API specific name of the device.
         */
        [[nodiscard]] virtual std::string GetName() const = 0;

        static std::shared_ptr<Device> Create();
    };

} // namespace Desert::Engine
