cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
set(NAME Stjarna)
project(${NAME})

include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

if (NOT IS_DIRECTORY $ENV{VULKAN_SDK})
    message(WARNING "Vulkan SDK not detected, if installed please set VULKAN_SDK env var or manually provide libraries")
else()
    message(STATUS "Found Vulkan SDK in $ENV{VULKAN_SDK}")
endif()

include_directories(SYSTEM $ENV{VULKAN_SDK}/include)

find_package(Vulkan REQUIRED)
find_library(SDL_LIBRARY NAMES SDL3 HINTS "$ENV{VULKAN_SDK}/lib" REQUIRED)
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" REQUIRED)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders)
add_custom_command(
    OUTPUT  ${SHADER_OUT}/circle.vert.spv ${SHADER_OUT}/circle.frag.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT}
    COMMAND ${GLSLC} ${SHADER_DIR}/circle.vert -o ${SHADER_OUT}/circle.vert.spv
    COMMAND ${GLSLC} ${SHADER_DIR}/circle.frag -o ${SHADER_OUT}/circle.frag.spv
    DEPENDS ${SHADER_DIR}/circle.vert ${SHADER_DIR}/circle.frag
    VERBATIM
)
add_custom_command(
    OUTPUT  ${SHADER_OUT}/rect.vert.spv ${SHADER_OUT}/rect.frag.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT}
    COMMAND ${GLSLC} ${SHADER_DIR}/rect.vert -o ${SHADER_OUT}/rect.vert.spv
    COMMAND ${GLSLC} ${SHADER_DIR}/rect.frag -o ${SHADER_OUT}/rect.frag.spv
    DEPENDS ${SHADER_DIR}/rect.vert ${SHADER_DIR}/rect.frag
    VERBATIM
)
add_custom_target(Shaders DEPENDS
    ${SHADER_OUT}/circle.vert.spv ${SHADER_OUT}/circle.frag.spv
    ${SHADER_OUT}/rect.vert.spv   ${SHADER_OUT}/rect.frag.spv
)

if (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
elseif (LINUX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WAYLAND_KHR")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNOMINMAX")
if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
endif()

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    $ENV{VULKAN_SDK}/include
)
target_compile_definitions(imgui_lib PRIVATE
    IMGUI_IMPL_VULKAN_USE_VOLK
    "IMGUI_IMPL_VULKAN_VOLK_FILENAME=<volk/volk.h>"
    VK_NO_PROTOTYPES
)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(imgui_lib PRIVATE -Wno-stringop-overflow)
endif()
target_link_libraries(imgui_lib PRIVATE ${SDL_LIBRARY})

add_executable(${NAME}
    src/main.cpp       # user scene: Init() + Update()
    src/Engine.cpp     # engine loop + object management
    src/app/App.cpp
    src/app/Window.cpp
    src/app/VulkanContext.cpp
    src/Profiler.cpp
    src/Object.cpp
    src/physics/Physics.cpp
    resource.rc
    Stjarna.manifest
)

target_include_directories(${NAME} PRIVATE include)
target_compile_definitions(${NAME} PRIVATE VK_NO_PROTOTYPES)
if (WIN32)
    target_compile_definitions(${NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
set_target_properties(${NAME} PROPERTIES DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(${NAME} PRIVATE cxx_std_20)
target_link_libraries(${NAME} PRIVATE ${SDL_LIBRARY} imgui_lib)
add_dependencies(${NAME} Shaders)
