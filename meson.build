project('ld56', 'cpp',
  default_options: {
    'cpp_std': 'c++20',
  })

fs = import('fs')
cmake = import('cmake')

vma_dep = cmake.subproject('VulkanMemoryAllocator-Hpp').dependency('VulkanMemoryAllocator')
vma_hpp_dep = cmake.subproject('VulkanMemoryAllocator-Hpp').dependency('VulkanMemoryAllocator-Hpp')

executable('gubgub',
  dependencies: [
    dependency('glfw3'),
    dependency('glm'),
    dependency('vulkan'),
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
