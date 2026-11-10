cmake_minimum_required(VERSION 3.20)
project(IsleRender LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(GLEW REQUIRED)

# ---- Dear ImGui (vendored) -------------------------------------------------
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

# ---- Engine + demo ---------------------------------------------------------
file(GLOB_RECURSE ISLE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(IsleRender ${ISLE_SOURCES})
target_include_directories(IsleRender PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(IsleRender PRIVATE imgui glfw GLEW::GLEW OpenGL::GL)
target_compile_options(IsleRender PRIVATE -Wall -Wextra -Wno-unused-parameter)
# Source asset dir is preferred at runtime so shaders can be hot-reloaded while editing.
target_compile_definitions(IsleRender PRIVATE ISLE_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")

if(MINGW)
  target_link_options(IsleRender PRIVATE -static-libgcc -static-libstdc++)
endif()
