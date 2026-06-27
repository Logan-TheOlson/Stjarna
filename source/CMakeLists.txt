cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
set(NAME Stjarna)
project(${NAME})

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
add_custom_target(Shaders DEPENDS ${SHADER_OUT}/circle.vert.spv ${SHADER_OUT}/circle.frag.spv)

if (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
elseif (LINUX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WAYLAND_KHR")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNOMINMAX")
if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
endif()

add_executable(${NAME}
    main.cpp
    app/App.cpp
    app/Window.cpp
    app/VulkanContext.cpp
    resource.rc
    Stjarna.manifest
        physics/collision_Engine.cpp
        physics/collision_Engine.h)

target_compile_definitions(${NAME} PRIVATE VK_NO_PROTOTYPES)
if (WIN32)
    target_compile_definitions(${NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
set_target_properties(${NAME} PROPERTIES DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(${NAME} PRIVATE cxx_std_20)
target_link_libraries(${NAME} PRIVATE ${SDL_LIBRARY})
add_dependencies(${NAME} Shaders)
