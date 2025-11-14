include(cmake/CPM.cmake)
# Make cpm download everything into this subdir.
set(CPM_SOURCE_CACHE ${CMAKE_SOURCE_DIR}/cmake/cache)

#					Packages
# [zlib]
# url = https://github.com/madler/zlib.git
# commit = 51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf
# tag = v3.12.0
#
# [freetype]
# url = https://github.com/freetype/freetype.git
# commit = 526ec5c47b9ebccc4754c85ac0c0cdf7c85a5e9b
# tag = VER-2-14-1
#
# [json]
# url = https://github.com/nlohmann/json.git
# commit = e7d2d636affeeac1fc7175fb13472ca02d1bc0d1
# tag = v3.12.0
#
# [imgui]
# url = https://github.com/ocornut/imgui.git
# commit = e7d2d636affeeac1fc7175fb13472ca02d1bc0d1
# tag = v1.92.4-docking
#
# [libmcc]
# url = https://github.com/SpringContingency/libmcc.git
# commit = a8910a6737c234338cb4018912dbaf066af75214

CPMAddPackage(
  NAME zlib
  GITHUB_REPOSITORY madler/zlib
  GIT_TAG 51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf
)

if (zlib_ADDED)
  add_library(ZLIB::ZLIB INTERFACE IMPORTED)
  target_link_libraries(ZLIB::ZLIB INTERFACE zlibstatic)
  target_include_directories(ZLIB::ZLIB INTERFACE ${zlib_SOURCE_DIR})
endif()

CPMAddPackage(
  NAME nlohmann_json
  GITHUB_REPOSITORY nlohmann/json
  GIT_TAG v3.12.0
)

if (nlohmann_json_ADDED)
  add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
endif()

CPMAddPackage(
  NAME freetype
  GITHUB_REPOSITORY freetype/freetype
  GIT_TAG VER-2-14-1
)

if (freetype_ADDED)
  add_library(Freetype::Freetype ALIAS freetype)
endif()

CPMAddPackage(
  NAME libmcc
  GITHUB_REPOSITORY SpringContingency/libmcc
  GIT_TAG a8910a6737c234338cb4018912dbaf066af75214
)

if (libmcc_ADDED)
  add_library(libmcc::libmcc ALIAS libmcc)
endif()

CPMAddPackage(
  NAME imgui
  GITHUB_REPOSITORY ocornut/imgui
  GIT_TAG v1.92.4-docking
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