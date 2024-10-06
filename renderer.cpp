#include "renderer.hpp"
#include "engine.hpp"
#include "swapchain.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>

using namespace eng;

template<typename DescriptorSetBindingsArrayType>
static vk::raii::DescriptorSetLayout createDescriptorSetLayout(const vk::raii::Device& device, DescriptorSetBindingsArrayType&& bindings)
{
    return vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo {
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        });
}

static std::vector<vk::raii::DescriptorSetLayout> createDescriptorSetLayouts(const vk::raii::Device& device, const uint32_t numBindlessTextures)
{
    std::vector<vk::raii::DescriptorSetLayout> descriptorSetLayouts;
    descriptorSetLayouts.push_back(createDescriptorSetLayout(device, std::array {
                vk::DescriptorSetLayoutBinding {
                    .binding = 0,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .descriptorCount = numBindlessTextures,
                    .stageFlags = vk::ShaderStageFlagBits::eFragment,
                }
            }));
    descriptorSetLayouts.push_back(createDescriptorSetLayout(device, std::array {
                vk::DescriptorSetLayoutBinding {
                    .binding = 0,
                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                    .descriptorCount = 1,
                    .stageFlags = vk::ShaderStageFlagBits::eVertex,
                },
            }));
    descriptorSetLayouts.push_back(createDescriptorSetLayout(device, std::array {
                vk::DescriptorSetLayoutBinding {
                    .binding = 0,
                    .descriptorType = vk::DescriptorType::eStorageBuffer,
                    .descriptorCount = 1,
                    .stageFlags = vk::ShaderStageFlagBits::eVertex,
                }
            }));

    return descriptorSetLayouts;
}

static vk::raii::PipelineLayout createPipelineLayout(const vk::raii::Device& device, const std::vector<vk::raii::DescriptorSetLayout>& descriptorSetLayouts)
{
    std::vector<vk::DescriptorSetLayout> rawDescriptorSetLayouts;
    rawDescriptorSetLayouts.reserve(descriptorSetLayouts.size());
    for (const auto& descriptorSetLayout : descriptorSetLayouts)
    {
        rawDescriptorSetLayouts.push_back(*descriptorSetLayout);
    }

    return vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo {
            .setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size()),
            .pSetLayouts = rawDescriptorSetLayouts.data(),
        });
}

static vk::raii::ShaderModule loadShaderModule(const vk::raii::Device& device, const std::string& filePath)
{
    if (auto fileStream = std::ifstream(filePath, std::ios::binary))
    {
        std::vector<char> code { std::istreambuf_iterator<char>(fileStream), {} };
        return vk::raii::ShaderModule(device, vk::ShaderModuleCreateInfo {
                .codeSize = code.size(),
                .pCode = reinterpret_cast<const uint32_t*>(code.data()),
            });
    }
    throw std::runtime_error("Failed to open file: " + filePath);
}

static vk::raii::Pipeline createPipeline(const vk::raii::Device& device, const std::string& vertexShaderPath, const std::string& fragmentShaderPath, const vk::Format colorAttachmentFormat, vk::PipelineLayout&& layout)
{
    auto vertexShaderModule = loadShaderModule(device, vertexShaderPath);
    auto fragmentShaderModule = loadShaderModule(device, fragmentShaderPath);
    const std::array stages = {
        vk::PipelineShaderStageCreateInfo {
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = vertexShaderModule,
            .pName = "main",
        },
        vk::PipelineShaderStageCreateInfo {
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = fragmentShaderModule,
            .pName = "main",
        }
    };

    const vk::PipelineVertexInputStateCreateInfo vertexInputState {
    };

    const vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState {
        .topology = vk::PrimitiveTopology::eTriangleStrip,
    };

    const vk::PipelineViewportStateCreateInfo viewportState {
        .viewportCount = 1,
        .scissorCount = 1,
    };

    const vk::PipelineRasterizationStateCreateInfo rasterizationState {
        .cullMode = vk::CullModeFlagBits::eNone,
        .lineWidth = 1.0f,
    };

    const vk::PipelineMultisampleStateCreateInfo multisampleState {
    };

    const vk::PipelineDepthStencilStateCreateInfo depthStencilState {
    };

    const std::array colorBlendAttachments = {
        vk::PipelineColorBlendAttachmentState {
            .blendEnable = vk::True,
            .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
            .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
            .colorBlendOp = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = vk::BlendFactor::eOne,
            .dstAlphaBlendFactor = vk::BlendFactor::eZero,
            .alphaBlendOp = vk::BlendOp::eAdd,
            .colorWriteMask = vk::FlagTraits<vk::ColorComponentFlagBits>::allFlags,
        },
    };
    const vk::PipelineColorBlendStateCreateInfo colorBlendState {
        .attachmentCount = colorBlendAttachments.size(),
        .pAttachments = colorBlendAttachments.data(),
    };

    const std::array dynamicStates { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    const vk::PipelineDynamicStateCreateInfo dynamicState {
        .dynamicStateCount = dynamicStates.size(),
        .pDynamicStates = dynamicStates.data(),
    };

    const vk::PipelineRenderingCreateInfo renderingInfo {
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorAttachmentFormat,
    };

    return vk::raii::Pipeline(device, nullptr, vk::GraphicsPipelineCreateInfo {
            .pNext = &renderingInfo,
            .stageCount = stages.size(),
            .pStages = stages.data(),
            .pVertexInputState = &vertexInputState,
            .pInputAssemblyState = &inputAssemblyState,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizationState,
            .pMultisampleState = &multisampleState,
            .pDepthStencilState = &depthStencilState,
            .pColorBlendState = &colorBlendState,
            .pDynamicState = &dynamicState,
            .layout = layout
        });
}

static vk::raii::DescriptorPool createDescriptorPool(const vk::raii::Device& device, const uint32_t numBindlessTextures, const uint32_t numFramesInFlight)
{
    const std::array poolSizes = {
        vk::DescriptorPoolSize { vk::DescriptorType::eUniformBuffer, numFramesInFlight },
        vk::DescriptorPoolSize { vk::DescriptorType::eCombinedImageSampler, numBindlessTextures },
        vk::DescriptorPoolSize { vk::DescriptorType::eStorageBuffer, numFramesInFlight },
    };

    return vk::raii::DescriptorPool(device, vk::DescriptorPoolCreateInfo {
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = 1 + 2 * numFramesInFlight,
            .poolSizeCount = poolSizes.size(),
            .pPoolSizes = poolSizes.data(),
        });
}

static vk::raii::DescriptorSet createTextureDescriptorSet(const vk::raii::Device& device, const vk::DescriptorPool& descriptorPool, const vk::DescriptorSetLayout& descriptorSetLayout, const vk::Sampler& textureSampler, const std::vector<std::tuple<vma::UniqueImage, vma::UniqueAllocation, vk::raii::ImageView>>& textures)
{
    auto descriptorSet = std::move(device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo {
                .descriptorPool = descriptorPool,
                .descriptorSetCount = 1,
                .pSetLayouts = &descriptorSetLayout,
            }).front());

    std::vector<vk::DescriptorImageInfo> imageInfos;
    imageInfos.reserve(textures.size());
    for (auto&& [image, allocation, imageView] : textures)
    {
        imageInfos.push_back(vk::DescriptorImageInfo {
                .sampler = textureSampler,
                .imageView = imageView,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            });
    }

    device.updateDescriptorSets(vk::WriteDescriptorSet {
            .dstSet = descriptorSet,
            .dstBinding = 0,
            .descriptorCount = static_cast<uint32_t>(imageInfos.size()),
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = imageInfos.data(),
        }, {});

    return descriptorSet;
}

static std::vector<FrameData> createFrameData(const vk::raii::Device& device, const uint32_t queueFamilyIndex, const vma::Allocator& allocator, const vk::raii::DescriptorPool& descriptorPool, const std::vector<vk::DescriptorSetLayout>& descriptorSetLayouts, const uint32_t numFramesInFlight)
{
    std::vector<FrameData> frameData;
    frameData.reserve(numFramesInFlight);

    for (uint32_t i = 0; i < numFramesInFlight; ++i)
    {
        vk::raii::CommandPool commandPool(device, vk::CommandPoolCreateInfo {
                .flags = vk::CommandPoolCreateFlagBits::eTransient,
                .queueFamilyIndex = queueFamilyIndex,
            });

        vk::raii::CommandBuffers commandBuffers(device, vk::CommandBufferAllocateInfo {
                .commandPool = commandPool,
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1,
            });

        vk::raii::DescriptorSets descriptorSets(device, vk::DescriptorSetAllocateInfo {
                .descriptorPool = descriptorPool,
                .descriptorSetCount = static_cast<uint32_t>(descriptorSetLayouts.size()),
                .pSetLayouts = descriptorSetLayouts.data(),
            });

        vma::AllocationInfo uniformBufferAllocationInfo;
        auto [uniformBuffer, uniformBufferAllocation] = allocator.createBufferUnique(vk::BufferCreateInfo {
                .size = sizeof(glm::mat4),
                .usage = vk::BufferUsageFlagBits::eUniformBuffer,
            }, vma::AllocationCreateInfo {
                .flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
                .usage = vma::MemoryUsage::eAuto,
            }, uniformBufferAllocationInfo);

        vma::AllocationInfo instanceBufferAllocationInfo;
        auto [instanceBuffer, instanceBufferAllocation] = allocator.createBufferUnique(vk::BufferCreateInfo {
                .size = 16777216,
                .usage = vk::BufferUsageFlagBits::eStorageBuffer,
            }, vma::AllocationCreateInfo {
                .flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
                .usage = vma::MemoryUsage::eAuto,
            }, instanceBufferAllocationInfo);

        const std::array bufferInfos {
            vk::DescriptorBufferInfo {
                .buffer = *uniformBuffer,
                .range = vk::WholeSize,
            },
            vk::DescriptorBufferInfo {
                .buffer = *instanceBuffer,
                .range = vk::WholeSize,
            },
        };

        device.updateDescriptorSets({
                vk::WriteDescriptorSet {
                    .dstSet = descriptorSets[0],
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                    .pBufferInfo = &bufferInfos[0],
                },
                vk::WriteDescriptorSet {
                    .dstSet = descriptorSets[1],
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eStorageBuffer,
                    .pBufferInfo = &bufferInfos[1],
                },
            }, {});

        frameData.push_back(FrameData {
                .inFlightFence = vk::raii::Fence(device, vk::FenceCreateInfo {
                        .flags = vk::FenceCreateFlagBits::eSignaled,
                    }),
                .imageAcquiredSemaphore = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo {}),
                .renderFinishedSemaphore = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo {}),
                .commandPool = std::move(commandPool),
                .commandBuffers = std::move(commandBuffers),
                .descriptorSets = std::move(descriptorSets),
                .uniformBuffer = std::move(uniformBuffer),
                .uniformBufferAllocation = std::move(uniformBufferAllocation),
                .uniformBufferAllocationInfo = std::move(uniformBufferAllocationInfo),
                .instanceBuffer = std::move(instanceBuffer),
                .instanceBufferAllocation = std::move(instanceBufferAllocation),
                .instanceBufferAllocationInfo = std::move(instanceBufferAllocationInfo),
            });
    }

    return frameData;
}

namespace
{
    template<typename T, typename Enable = void>
    struct WriteDataHelper
    {
        static void writeData(char*& writePointer, T&& value)
        {
            std::memcpy(writePointer, &value, sizeof(T));
            writePointer += sizeof(T);
        }
    };

    template<typename T>
    struct WriteDataHelper<T, decltype(glm::value_ptr(std::declval<T>()))>
    {
        static void writeData(char*& writePointer, T&& value)
        {
            std::memcpy(writePointer, glm::value_ptr(value), sizeof(T));
            writePointer += sizeof(T);
        }
    };
}

template<typename T> 
static void writeData(char*& writePointer, T&& value)
{
    WriteDataHelper<T>::writeData(writePointer, std::forward<T>(value));
}

Renderer::Renderer(const vk::raii::Device& device, const vk::raii::Queue& queue, const uint32_t queueFamilyIndex, const vma::Allocator& allocator, const std::vector<std::tuple<vma::UniqueImage, vma::UniqueAllocation, vk::raii::ImageView>>& textures, const uint32_t numFramesInFlight, const vk::Format colorAttachmentFormat) :
    device(device),
    queue(queue),
    descriptorSetLayouts(createDescriptorSetLayouts(device, textures.size())),
    pipelineLayout(createPipelineLayout(device, descriptorSetLayouts)),
    pipeline(createPipeline(device, "shaders/test.vs.spv", "shaders/test.fs.spv", colorAttachmentFormat, pipelineLayout)),
    textureSampler(device, vk::SamplerCreateInfo {}),
    descriptorPool(createDescriptorPool(device, textures.size(), numFramesInFlight)),
    textureDescriptorSet(createTextureDescriptorSet(device, descriptorPool, descriptorSetLayouts[0], textureSampler, textures)),
    frameData(createFrameData(device, queueFamilyIndex, allocator, descriptorPool, { *descriptorSetLayouts[1], *descriptorSetLayouts[2] }, numFramesInFlight))
{
}

void Renderer::beginFrame()
{
    if (auto result = device.waitForFences(*frameData[frameIndex].inFlightFence, vk::True, std::numeric_limits<uint64_t>::max()); result != vk::Result::eSuccess)
    {
        throw std::runtime_error("Unexpected return from waitForFences");
    }
    device.resetFences({ frameData[frameIndex].inFlightFence });

    frameData[frameIndex].commandPool.reset();
}

void Renderer::updateFrame(const std::vector<Instance>& instances, const glm::mat4& projection)
{
    auto writePointer = static_cast<char*>(frameData[frameIndex].uniformBufferAllocationInfo.pMappedData);
    writeData(writePointer, projection);

    writePointer = static_cast<char*>(frameData[frameIndex].instanceBufferAllocationInfo.pMappedData);
    for (const auto& instance : instances)
    {
        writeData(writePointer, instance.position);
        writeData(writePointer, instance.scale);
        writeData(writePointer, glm::vec2(0, 0));
        writeData(writePointer, glm::vec2(1, 1));
        writeData(writePointer, glm::vec2(glm::cos(instance.angle), glm::sin(instance.angle)));
        writeData(writePointer, instance.textureIndex);
        writeData(writePointer, 0.0f); // padding
        writeData(writePointer, instance.tintColor);
    }
}

void Renderer::drawFrame(const Swapchain& swapchain, const glm::vec2& viewportOffset, const glm::vec2& viewportExtent, const uint32_t numInstances)
{
    auto [acquireResult, imageIndex] = swapchain.swapchain.acquireNextImage(std::numeric_limits<uint64_t>::max(), frameData[frameIndex].imageAcquiredSemaphore, nullptr);
    if (acquireResult != vk::Result::eSuccess && acquireResult != vk::Result::eSuboptimalKHR)
    {
        throw std::runtime_error("Unexpected return from acquireNextImage");
    }

    const auto& commandBuffer = frameData[frameIndex].commandBuffers.front();
    commandBuffer.begin(vk::CommandBufferBeginInfo {
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    });

    const vk::ImageMemoryBarrier2 initialImageMemoryBarrier {
        .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
        .srcAccessMask = {},
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .image = swapchain.images[imageIndex],
        .subresourceRange = vk::ImageSubresourceRange { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
    };
    commandBuffer.pipelineBarrier2(vk::DependencyInfo {
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &initialImageMemoryBarrier,
    });

    const vk::RenderingAttachmentInfo renderingAttachmentInfo {
        .imageView = swapchain.imageViews[imageIndex],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue({ 0.0f, 0.0f, 0.0f, 0.0f }),
    };
    commandBuffer.beginRendering(vk::RenderingInfo {
        .renderArea = vk::Rect2D { .extent = swapchain.extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &renderingAttachmentInfo,
    });

    commandBuffer.setViewport(0, vk::Viewport {
            .x = viewportOffset.x,
            .y = viewportOffset.y,
            .width = viewportExtent.x,
            .height = viewportExtent.y,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        });
    commandBuffer.setScissor(0, vk::Rect2D {
            .extent = swapchain.extent,
        });
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, {
            textureDescriptorSet,
            frameData[frameIndex].descriptorSets[0],
            frameData[frameIndex].descriptorSets[1],
        }, {});

    commandBuffer.draw(4, numInstances, 0, 0);

    commandBuffer.endRendering();

    const vk::ImageMemoryBarrier2 finalImageMemoryBarrier {
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe,
        .dstAccessMask = {},
        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .newLayout = vk::ImageLayout::ePresentSrcKHR,
        .image = swapchain.images[imageIndex],
        .subresourceRange = vk::ImageSubresourceRange { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
    };
    commandBuffer.pipelineBarrier2(vk::DependencyInfo {
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &finalImageMemoryBarrier,
        });

    commandBuffer.end();

    const vk::CommandBufferSubmitInfo commandBufferSubmitInfo {
        .commandBuffer = commandBuffer,
    };

    const vk::SemaphoreSubmitInfo waitSemaphoreInfo {
        .semaphore = frameData[frameIndex].imageAcquiredSemaphore,
        .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    };

    const vk::SemaphoreSubmitInfo signalSemaphoreInfo {
        .semaphore = frameData[frameIndex].renderFinishedSemaphore,
        .stageMask = vk::PipelineStageFlagBits2::eBottomOfPipe,
    };

    queue.submit2(vk::SubmitInfo2 {
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &waitSemaphoreInfo,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandBufferSubmitInfo,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &signalSemaphoreInfo,
        }, frameData[frameIndex].inFlightFence);

    if (auto result = queue.presentKHR(vk::PresentInfoKHR {
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &*frameData[frameIndex].renderFinishedSemaphore,
                .swapchainCount = 1,
                .pSwapchains = &*swapchain.swapchain,
                .pImageIndices = &imageIndex,
            });
            result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
    {
        throw std::runtime_error("Unexpected return from presentKHR");
    }
    else if (result == vk::Result::eSuboptimalKHR)
    {
        // TODO: recreate swapchain
    }
}

void Renderer::nextFrame()
{
    frameIndex = (frameIndex + 1) % frameData.size();
}
