---@diagnostic disable: undefined-global
set_project('sp')
set_license('GPL-3.0')

add_rules('mode.debug', 'mode.release')
includes('packages/*/xmake.lua')

add_requires('ffmpeg 5.1.2', { system = false, debug = true })
add_requires('glfw')

target('base')
set_kind('phony')
add_links('openal', 'pthread', 'glfw', { public = true })
add_includedirs('packages/glad/include', { public = true })
add_packages('glfw', { public = true })
add_packages('ffmpeg', {
    public = true,
    system = false,
    configs = {
        shared = true,
        debug = true,
        libx264 = true,
        libx265 = true,
        zlib = true,
        vulkan = false,
    }
})

target('sp')
set_kind('binary')
add_deps('base')
add_files('src/*.c', 'packages/glad/src/glad.c')
set_rundir(projectdir)

target('sp-test')
set_kind('binary')
add_deps('sp')
add_files('test/*.c', 'src/*.c', 'packages/glad/src/glad.c')
remove_files('src/main.c')
