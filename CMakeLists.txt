cmake_minimum_required(VERSION 3.20)

project(Earthquake LANGUAGES C CXX)
include(FetchContent)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.10
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG v1.0
)
FetchContent_MakeAvailable(meshoptimizer)

FetchContent_Declare(
    nuklear
    GIT_REPOSITORY https://github.com/Immediate-Mode-UI/Nuklear.git
    GIT_TAG master
)
FetchContent_MakeAvailable(nuklear)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/core/*.c"
)

add_library(core STATIC ${CORE_SRC})

if(WIN32)
    target_link_libraries(core PUBLIC ws2_32)
endif()

target_include_directories(core PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/core"
    "${CMAKE_SOURCE_DIR}/core/include"
    "${CMAKE_SOURCE_DIR}/core/managers"
    "${CMAKE_SOURCE_DIR}/core/systems"
    "${CMAKE_SOURCE_DIR}/core/types"
    "${CMAKE_SOURCE_DIR}/vendor"
    "${meshoptimizer_SOURCE_DIR}/src"
)

target_link_libraries(core PUBLIC
    glfw
    OpenGL::GL
    GLEW::GLEW
    meshoptimizer
)

file(GLOB_RECURSE EDITOR_SRC CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/editor/*.c"
)

add_library(editor STATIC ${EDITOR_SRC})
set_property(TARGET editor PROPERTY LINKER_LANGUAGE CXX)

target_include_directories(editor PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/editor"
    "${CMAKE_SOURCE_DIR}/editor/include"
    "${nuklear_SOURCE_DIR}"
    "${nuklear_SOURCE_DIR}/demo/glfw_opengl3"
)

target_compile_definitions(editor PUBLIC
    NK_INCLUDE_FIXED_TYPES
    NK_INCLUDE_STANDARD_IO
    NK_INCLUDE_STANDARD_VARARGS
    NK_INCLUDE_DEFAULT_ALLOCATOR
    NK_INCLUDE_FONT_BAKING
    NK_INCLUDE_DEFAULT_FONT
    NK_INCLUDE_VERTEX_BUFFER_OUTPUT
)

target_link_libraries(editor PUBLIC core)

file(GLOB_RECURSE APP_SRC CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/app/*.c"
)

add_executable(app ${APP_SRC})
set_property(TARGET app PROPERTY LINKER_LANGUAGE CXX)

target_include_directories(app PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/app"
    "${CMAKE_SOURCE_DIR}/app/include"
)

target_link_libraries(app PRIVATE core editor)

target_compile_definitions(core PRIVATE $<$<CONFIG:Debug>:_DEBUG>)
target_compile_definitions(editor PRIVATE $<$<CONFIG:Debug>:_DEBUG>)
target_compile_definitions(app PRIVATE $<$<CONFIG:Debug>:_DEBUG>)

add_compile_options($<$<CONFIG:Debug>:-g>)
