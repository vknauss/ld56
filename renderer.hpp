#pragma once

#include "vulkan_includes.hpp"
#include <glm/glm.hpp>

namespace eng
{
    struct Instance;
    struct Swapchain;

    struct FrameData
    {
        vk::raii::Fence inFlightFence;
        vk::raii::Semaphore imageAcquiredSemaphore;
        vk::raii::Semaphore renderFinishedSemaphore;
        vk::raii::CommandPool commandPool;
        vk::raii::CommandBuffers commandBuffers;
        vk::raii::DescriptorSets descriptorSets;
        vma::UniqueBuffer uniformBuffer;
        vma::UniqueAllocation uniformBufferAllocation;
        vma::AllocationInfo uniformBufferAllocationInfo;
        vma::UniqueBuffer instanceBuffer;
        vma::UniqueAllocation instanceBufferAllocation;
        vma::AllocationInfo instanceBufferAllocationInfo;
    };

    struct Renderer
    {
        explicit Renderer(const vk::raii::Device& device, const vk::raii::Queue& queue, const uint32_t queueFamilyIndex, const vma::Allocator& allocator, const std::vector<std::tuple<vma::UniqueImage, vma::UniqueAllocation, vk::raii::ImageView>>& textures, const uint32_t numFramesInFlight, const vk::Format colorAttachmentFormat);

        void beginFrame();
        void updateFrame(const std::vector<Instance>& instances, const glm::mat4& projection);
        void drawFrame(const Swapchain& swapchain, const uint32_t numInstances);
        void nextFrame();

        const vk::raii::Device& device;
        const vk::raii::Queue& queue;
        const std::vector<vk::raii::DescriptorSetLayout> descriptorSetLayouts;
        const vk::raii::PipelineLayout pipelineLayout;
        const vk::raii::Pipeline pipeline;
        const vk::raii::Sampler textureSampler;
        const vk::raii::DescriptorPool descriptorPool;
        const vk::raii::DescriptorSet textureDescriptorSet;
        const std::vector<FrameData> frameData;
        uint32_t frameIndex = 0;
    };
}
