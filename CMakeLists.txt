cmake_minimum_required(VERSION 3.20)

project(Earthquake C)
include(FetchContent)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

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

find_package(OpenGL REQUIRED)

file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/core/*.c"
)

add_library(core STATIC ${CORE_SRC})

target_include_directories(core PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/core"
    "${CMAKE_SOURCE_DIR}/core/include"
)

file(GLOB_RECURSE APP_SRC CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/app/*.c"
)


target_link_libraries(core PUBLIC
    glfw
    OpenGL::GL
)

add_executable(app ${APP_SRC})

target_include_directories(app PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/app"
    "${CMAKE_SOURCE_DIR}/app/include"
)

target_link_libraries(app PRIVATE core)

target_compile_definitions(core PRIVATE $<$<CONFIG:Debug>:_DEBUG>)
target_compile_definitions(app PRIVATE $<$<CONFIG:Debug>:_DEBUG>)
add_compile_options($<$<CONFIG:Debug>:-g>)
