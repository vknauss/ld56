#include "texture_loader.hpp"
#include <stb_image.h>

using eng::TextureLoader;

TextureLoader::TextureLoader(const vk::raii::Device& device, const vk::raii::Queue& queue, const uint32_t queueFamilyIndex, const vma::Allocator& allocator) :
    device(device),
    queue(queue),
    allocator(allocator),
    commandPool(device, vk::CommandPoolCreateInfo {
            // .flags = vk::CommandPoolCreateFlagBits::eTransient,
            .queueFamilyIndex = queueFamilyIndex,
        }),
    commandBuffer(std::move(device.allocateCommandBuffers(vk::CommandBufferAllocateInfo {
                .commandPool = commandPool,
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1,
            }).front())),
    fence(device, vk::FenceCreateInfo {})
{
    commandBuffer.begin(vk::CommandBufferBeginInfo {
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
        });
}

std::tuple<vma::UniqueImage, vma::UniqueAllocation, vk::raii::ImageView> TextureLoader::loadTexture(const std::string& filePath, const vk::Format format, const int channels, const int bytesPerPixel)
{
    int width, height, components;
    stbi_uc* textureData = stbi_load(filePath.c_str(), &width, &height, &components, channels);
    if (!textureData)
    {
        throw std::runtime_error("Failed to load texture: " + filePath);
    }

    const vk::DeviceSize textureDataSize = width * height * bytesPerPixel;
    vma::AllocationInfo allocationInfo;
    auto& [stagingBuffer, stagingBufferAllocation] = stagingBuffers.emplace_back(allocator.createBufferUnique(vk::BufferCreateInfo {
            .size = textureDataSize,
            .usage = vk::BufferUsageFlagBits::eTransferSrc,
        }, vma::AllocationCreateInfo {
            .flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
            .usage = vma::MemoryUsage::eAuto,
        }, allocationInfo));

    std::memcpy(allocationInfo.pMappedData, textureData, textureDataSize);
    stbi_image_free(textureData);

    auto [image, allocation] = allocator.createImageUnique(vk::ImageCreateInfo {
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = vk::Extent3D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 },
            .mipLevels = 1, /* TODO: mip mapping */
            .arrayLayers = 1,
            .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            .initialLayout = vk::ImageLayout::eUndefined,
        }, vma::AllocationCreateInfo {
            .usage = vma::MemoryUsage::eAuto,
        });

    const vk::ImageMemoryBarrier2 initialImageMemoryBarrier {
        .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
        .srcAccessMask = {},
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .image = *image,
        .subresourceRange = vk::ImageSubresourceRange {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount= 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    commandBuffer.pipelineBarrier2(vk::DependencyInfo {
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &initialImageMemoryBarrier,
        });

    commandBuffer.copyBufferToImage(*stagingBuffer, *image, vk::ImageLayout::eTransferDstOptimal, vk::BufferImageCopy {
            .imageSubresource = vk::ImageSubresourceLayers {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageExtent = vk::Extent3D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 },
        });

    const vk::ImageMemoryBarrier2 finalImageMemoryBarrier {
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .image = *image,
        .subresourceRange = vk::ImageSubresourceRange {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount= 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    commandBuffer.pipelineBarrier2(vk::DependencyInfo {
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &finalImageMemoryBarrier,
        });

    vk::raii::ImageView imageView(device, vk::ImageViewCreateInfo {
            .image = *image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
        });

    return { std::move(image), std::move(allocation), std::move(imageView) };
}

void TextureLoader::commit()
{
    commandBuffer.end();
    queue.submit(vk::SubmitInfo {
            .commandBufferCount = 1,
            .pCommandBuffers = &*commandBuffer,
        }, fence);
}

void TextureLoader::finalize()
{
    if (auto result = device.waitForFences(*fence, vk::True, std::numeric_limits<uint64_t>::max()); result != vk::Result::eSuccess)
    {
        throw std::runtime_error("Unexpected return from waitForFences");
    }

    stagingBuffers.clear();
    device.resetFences(*fence);
    commandPool.reset();
}
