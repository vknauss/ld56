#include "engine.hpp"
#include "input_manager.hpp"
#include "renderer.hpp"
#include "swapchain.hpp"
#include "texture_loader.hpp"
#include "vulkan_includes.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <stdexcept>
#include <sstream>
#include <vector>

using namespace eng;

struct GLFWWindowWrapper
{
    explicit GLFWWindowWrapper(GLFWwindow* window)
        : window(window)
    {
    }

    GLFWWindowWrapper(GLFWWindowWrapper&& rh)
        : window(rh.window)
    {
        rh.window = nullptr;
    }

    GLFWWindowWrapper(const GLFWWindowWrapper&) = delete;

    ~GLFWWindowWrapper()
    {
        glfwDestroyWindow(window);
    }

    GLFWWindowWrapper& operator=(GLFWWindowWrapper&& rh)
    {
        window = rh.window;
        rh.window = nullptr;
        return *this;
    }

    GLFWWindowWrapper& operator=(const GLFWWindowWrapper&) = delete;

    operator GLFWwindow*() const
    {
        return window;
    }

    GLFWwindow* window;
};

struct GLFWWrapper
{
    GLFWWrapper()
    {
        if (glfwInit() != GLFW_TRUE)
        {
            throw std::runtime_error("Failed to initialize GLFW");
        }
    }

    ~GLFWWrapper()
    {
        glfwTerminate();
    }

    GLFWWindowWrapper CreateWindow(const int width, const int height, const char* title) const
    {
        GLFWwindow* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (!window)
        {
            const char* errorMessage;
            int error = glfwGetError(&errorMessage);
            throw std::runtime_error((std::stringstream{} << "Failed to create GLFW window: Error: " << error << ": " << errorMessage).str());
        }
        return GLFWWindowWrapper(window);
    }
};

static auto createInstance(const vk::raii::Context& context, const ApplicationInfo& applicationInfo)
{
    uint32_t numRequiredInstanceExtensions;
    const char* const* requiredInstanceExtensions = glfwGetRequiredInstanceExtensions(&numRequiredInstanceExtensions);
    if (!requiredInstanceExtensions)
    {
        throw std::runtime_error("Vulkan not supported for window surface creation");
    }
    const std::array validationLayers = { "VK_LAYER_KHRONOS_validation" };
    std::vector<const char*> extensionNames(numRequiredInstanceExtensions + 1);
    std::copy(requiredInstanceExtensions, requiredInstanceExtensions + numRequiredInstanceExtensions, extensionNames.begin());
    extensionNames.back() = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;

    const vk::ApplicationInfo instanceApplicationInfo {
        .pApplicationName = applicationInfo.appName.c_str(),
        .applicationVersion = applicationInfo.appVersion,
        .apiVersion = vk::ApiVersion13,
    };

    return vk::raii::Instance(context, vk::InstanceCreateInfo {
            .flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
            .pApplicationInfo = &instanceApplicationInfo,
            .enabledLayerCount = validationLayers.size(),
            .ppEnabledLayerNames = validationLayers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(extensionNames.size()),
            .ppEnabledExtensionNames = extensionNames.data(),
        });
}

static auto createSurface(const vk::raii::Instance& instance, GLFWwindow* window)
{
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(*instance, window, nullptr, &surface) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create window surface");
    }
    return vk::raii::SurfaceKHR(instance, surface);
}

static vk::raii::PhysicalDevice getPhysicalDevice(const vk::raii::Instance& instance)
{
    auto physicalDevices = instance.enumeratePhysicalDevices();
    if (!physicalDevices.empty())
    {
        return physicalDevices.front();
    }
    throw std::runtime_error("No Vulkan devices found");
}

static uint32_t getQueueFamilyIndex(const vk::raii::PhysicalDevice& physicalDevice, const vk::QueueFlags& flags)
{
    auto queueFamilies = physicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
        if (queueFamilies[i].queueFlags & flags)
        {
            return i;
        }
    }
    throw std::runtime_error("No suitable queue family found");
}

static vk::raii::Device createDevice(const vk::raii::PhysicalDevice& physicalDevice, uint32_t queueFamilyIndex)
{
    const float queuePriority = 1.0f;
    const vk::DeviceQueueCreateInfo queueCreateInfo {
        .queueFamilyIndex = queueFamilyIndex,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };

    // TODO: query extension support
    const std::array deviceExtensions {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        "VK_KHR_portability_subset",
        "VK_KHR_synchronization2",
        "VK_KHR_dynamic_rendering",
    };

    const vk::StructureChain deviceCreateInfoChain {
        vk::DeviceCreateInfo {
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCreateInfo,
            .enabledExtensionCount = deviceExtensions.size(),
            .ppEnabledExtensionNames = deviceExtensions.data(),
        },
        vk::PhysicalDeviceFeatures2 {},
        vk::PhysicalDeviceVulkan12Features {
            .shaderSampledImageArrayNonUniformIndexing = vk::True,
            .runtimeDescriptorArray = vk::True,
            .timelineSemaphore = vk::True,
        },
        vk::PhysicalDeviceSynchronization2Features {
            .synchronization2 = vk::True,
        },
        vk::PhysicalDeviceDynamicRenderingFeatures {
            .dynamicRendering = vk::True,
        },
    };

    return vk::raii::Device(physicalDevice, deviceCreateInfoChain.get<vk::DeviceCreateInfo>());
}

static vk::raii::Device createDeviceFallback(const vk::raii::PhysicalDevice& physicalDevice, uint32_t queueFamilyIndex)
{
    const float queuePriority = 1.0f;
    const vk::DeviceQueueCreateInfo queueCreateInfo {
        .queueFamilyIndex = queueFamilyIndex,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };
    const std::array deviceExtensions { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    const vk::StructureChain deviceCreateInfoChain {
        vk::DeviceCreateInfo {
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCreateInfo,
            .enabledExtensionCount = deviceExtensions.size(),
            .ppEnabledExtensionNames = deviceExtensions.data(),
        },
        vk::PhysicalDeviceFeatures2 {},
        vk::PhysicalDeviceVulkan12Features {
            .timelineSemaphore = vk::True,
        },
        vk::PhysicalDeviceSynchronization2Features {
            .synchronization2 = vk::True,
        },
        vk::PhysicalDeviceDynamicRenderingFeatures {
            .dynamicRendering = vk::True,
        },
    };

    return vk::raii::Device(physicalDevice, deviceCreateInfoChain.get<vk::DeviceCreateInfo>());
}

static vk::SurfaceFormatKHR getSurfaceFormat(const vk::raii::PhysicalDevice& physicalDevice, const vk::raii::SurfaceKHR& surface)
{
    auto surfaceFormats = physicalDevice.getSurfaceFormatsKHR(surface);
    std::optional<vk::Format> format;
    for (const auto& surfaceFormat : surfaceFormats)
    {
        if (surfaceFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
        {
            if (surfaceFormat.format == vk::Format::eB8G8R8A8Srgb || surfaceFormat.format == vk::Format::eR8G8B8A8Srgb)
            {
                return surfaceFormat;
            }
            format = format.value_or(surfaceFormat.format);
        }
    }
    if (format)
    {
        return vk::SurfaceFormatKHR{ *format, vk::ColorSpaceKHR::eSrgbNonlinear };
    }
    throw std::runtime_error("No suitable surface format found");
}

struct ResourceLoader final : ResourceLoaderInterface
{
    TextureLoader& textureLoader;
    std::vector<std::tuple<vma::UniqueImage, vma::UniqueAllocation, vk::raii::ImageView>>& textures;

    explicit ResourceLoader(TextureLoader& textureLoader, std::vector<std::tuple<vma::UniqueImage, vma::UniqueAllocation, vk::raii::ImageView>>& textures) :
        textureLoader(textureLoader),
        textures(textures)
    {
    }

    uint32_t loadTexture(const std::string& filePath) override
    {
        uint32_t index = textures.size();
        textures.push_back(textureLoader.loadTexture(filePath, vk::Format::eR8G8B8A8Srgb, 4, 4));
        return index;
    }
};

class Scene final : public SceneInterface
{
public:
    std::vector<Instance>& instances() override
    {
        return instances_;
    }

    glm::mat4& projection() override
    {
        return projection_;
    }

private:
    std::vector<Instance> instances_;
    glm::mat4 projection_;
};

void eng::run(GameLogicInterface& gameLogic, const ApplicationInfo& applicationInfo)
{
    GLFWWrapper glfwWrapper;
    if (!glfwVulkanSupported())
    {
        throw std::runtime_error("Vulkan not supported");
    }

    const vk::raii::Context context;
    const auto instance = createInstance(context, applicationInfo);
    const auto physicalDevice = getPhysicalDevice(instance);
    const auto queueFamilyIndex = getQueueFamilyIndex(physicalDevice, vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute);

    vk::raii::Device device = nullptr;
    bool usingFallback = false;
    try
    {
        device = createDevice(physicalDevice, queueFamilyIndex);
    }
    catch (const std::exception& exception)
    {
        device = createDeviceFallback(physicalDevice, queueFamilyIndex);    
        usingFallback = true;
    }

    const auto queue = device.getQueue(queueFamilyIndex, 0);
    const auto allocator = vma::createAllocatorUnique(vma::AllocatorCreateInfo {
            .physicalDevice = *physicalDevice,
            .device = *device,
            .instance = *instance,
            .vulkanApiVersion = vk::ApiVersion13,
        });

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    auto window = glfwWrapper.CreateWindow(applicationInfo.windowWidth, applicationInfo.windowHeight, applicationInfo.windowTitle.c_str());

    const auto surface = createSurface(instance, window);
    const auto surfaceFormat = getSurfaceFormat(physicalDevice, surface);

    int framebufferWidth, framebufferHeight;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

    Swapchain swapchain(device, physicalDevice, surface, surfaceFormat, vk::Extent2D{ static_cast<uint32_t>(framebufferWidth), static_cast<uint32_t>(framebufferHeight) });

    TextureLoader textureLoader(device, queue, queueFamilyIndex, *allocator);
    std::vector<std::tuple<vma::UniqueImage, vma::UniqueAllocation, vk::raii::ImageView>> textures;

    ResourceLoader resourceLoader(textureLoader, textures);
    Scene scene;
    InputManager inputManager(window);

    gameLogic.init(resourceLoader, scene, inputManager);
    textureLoader.commit();

    Renderer renderer(device, queue, queueFamilyIndex, *allocator, textures, 3, surfaceFormat.format);

    textureLoader.finalize();

    auto lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        auto time = glfwGetTime();
        gameLogic.runFrame(scene, inputManager, time - lastTime);
        lastTime = time;

        renderer.beginFrame();
        renderer.updateFrame(scene.instances(), scene.projection());
        renderer.drawFrame(swapchain, scene.instances().size());
        renderer.nextFrame();
        inputManager.nextFrame();
    }

    gameLogic.cleanup();

    queue.waitIdle();
}
