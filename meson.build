project('ld56', 'cpp',
  default_options: {
    'cpp_std': 'c++20',
    'default_library': 'static',
    'prefer_static': true,
  })

fs = import('fs')
cmake = import('cmake')

vma_dep = cmake.subproject('VulkanMemoryAllocator-Hpp').dependency('VulkanMemoryAllocator')
vma_hpp_dep = cmake.subproject('VulkanMemoryAllocator-Hpp').dependency('VulkanMemoryAllocator-Hpp')
glfw_dep = cmake.subproject('glfw').dependency('glfw')

configure_file(output: 'config.h',
  configuration: {
    'USE_VALIDATION_LAYERS': get_option('use_validation_layers'),
  })

executable('gubgub',
  dependencies: [
    dependency('glm'),
    dependency('vulkan'),
    glfw_dep,
    vma_dep,
    vma_hpp_dep,
  ],
  include_directories: [
    'subprojects/stb',
  ],
  sources: [
    'engine.cpp',
    'input_manager.cpp',
    'main.cpp',
    'renderer.cpp',
    'stb_image_implementation.cpp',
    'swapchain.cpp',
    'texture_loader.cpp',
    'vma_implementation.cpp',
  ])

subdir('shaders')
subdir('textures')
