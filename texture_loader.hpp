#pragma  once

#include "vulkan_includes.hpp"

namespace eng
{
    struct TextureLoader
    {
        explicit TextureLoader(const vk::raii::Device& device, const vk::raii::Queue& queue, const uint32_t queueFamilyIndex, const vma::Allocator& allocator);

        std::tuple<vma::UniqueImage, vma::UniqueAllocation, vk::raii::ImageView> loadTexture(const std::string& filePath, const vk::Format format, const int channels, const int bytesPerPixel);
        void commit();
        void finalize();

        const vk::raii::Device& device;
        const vk::raii::Queue& queue;
        const vma::Allocator& allocator;
        const vk::raii::CommandPool commandPool;
        const vk::raii::CommandBuffer commandBuffer;
        const vk::raii::Fence fence;
        std::vector<std::pair<vma::UniqueBuffer, vma::UniqueAllocation>> stagingBuffers;
    };
}
