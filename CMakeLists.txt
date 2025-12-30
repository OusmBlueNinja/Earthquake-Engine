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
    GIT_TAG "master"
)
FetchContent_MakeAvailable(meshoptimizer)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
)
FetchContent_MakeAvailable(imgui)

set(IMGUI_SRC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_library(imgui STATIC ${IMGUI_SRC})
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC
    glfw
    OpenGL::GL
)

file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/core/*.c"
    "${CMAKE_SOURCE_DIR}/vendor/*.c"
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
    "${CMAKE_SOURCE_DIR}/vendor/"
    "${meshoptimizer_SOURCE_DIR}/src"
)

target_link_libraries(core PUBLIC
    glfw
    OpenGL::GL
    GLEW::GLEW
    meshoptimizer
)

file(GLOB_RECURSE EDITOR_SRC CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/editor/*.cpp"
    "${CMAKE_SOURCE_DIR}/editor/*.cc"
    "${CMAKE_SOURCE_DIR}/editor/*.cxx"
    "${CMAKE_SOURCE_DIR}/editor/*.c"
)

add_library(editor STATIC ${EDITOR_SRC})
set_property(TARGET editor PROPERTY LINKER_LANGUAGE CXX)

target_include_directories(editor PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/editor"
    "${CMAKE_SOURCE_DIR}/editor/include"
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(editor PUBLIC
    core
    imgui
)

file(GLOB_RECURSE APP_SRC CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/app/*.cpp"
    "${CMAKE_SOURCE_DIR}/app/*.cc"
    "${CMAKE_SOURCE_DIR}/app/*.cxx"
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
