#pragma once

#include "vulkan_includes.hpp"

namespace eng
{
    struct Swapchain
    {
        explicit Swapchain(const vk::raii::Device& device, const vk::raii::PhysicalDevice& physicalDevice, const vk::raii::SurfaceKHR& surface, const vk::SurfaceFormatKHR& surfaceFormat, const vk::Extent2D& extent);

        vk::raii::SwapchainKHR swapchain;
        std::vector<vk::Image> images;
        std::vector<vk::raii::ImageView> imageViews;
        vk::Extent2D extent;
    };
}
