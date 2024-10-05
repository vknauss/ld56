make sure to init submodules `git submodule update --init --recursive`

external dependencies:

- meson
- glfw
- glm
- vulkan sdk
- more may be needed depending on platform, if meson setup complains about dependencies then you will know

`meson setup build`
`cd build`
`meson compile`
`./gubgub`
