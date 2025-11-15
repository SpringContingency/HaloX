#[[
DependencyManagement
--------------------
Find project dependencies

]]#

include(cmake/CPM.cmake)

# Make cpm download everything into this subdir.
set(CPM_SOURCE_CACHE ${CMAKE_SOURCE_DIR}/cmake/cache)

CPMAddPackage(
  # URI   "gh:madler/zlib#v1.3.1"
  NAME  zlib
  URL   "https://github.com/madler/zlib/archive/refs/tags/v1.3.1.zip"
)

if (zlib_ADDED)
  add_library(ZLIB::ZLIB INTERFACE IMPORTED)
  target_link_libraries(ZLIB::ZLIB INTERFACE zlibstatic)
  target_include_directories(ZLIB::ZLIB INTERFACE ${zlib_SOURCE_DIR})
endif()

CPMAddPackage(
  # URI   "gh:nlohmann/json#v3.12.0"
  NAME  nlohmann_json
  URL   "https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.zip"
)

if (nlohmann_json_ADDED)
  add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
endif()

CPMAddPackage(
  # URI   "gh:freetype/freetype#VER-2-14-1"
  NAME  freetype
  URL   "https://github.com/freetype/freetype/archive/refs/tags/VER-2-14-1.zip"
)

if (freetype_ADDED)
  add_library(Freetype::Freetype ALIAS freetype)
endif()

CPMAddPackage(
  URI   "gh:SpringContingency/libmcc#a8910a6737c234338cb4018912dbaf066af75214"
  NAME  libmcc
)

if (libmcc_ADDED)
  add_library(libmcc::libmcc ALIAS libmcc)
endif()

CPMAddPackage(
  # URI   "gh:ocornut/imgui#v1.92.4-docking"
  NAME  imgui
  URL   "https://github.com/ocornut/imgui/archive/refs/tags/v1.92.4-docking.zip"
)

# Imgui does not have its own cmake setup.
if (imgui_ADDED)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends/)
target_compile_definitions(imgui PUBLIC -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS) # optional imgui setting
#alias
add_library(ImGui::ImGui ALIAS imgui) 
endif ()