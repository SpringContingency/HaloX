cmake_minimum_required (VERSION 3.10)

project(HaloX
	LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)

string(TIMESTAMP COMPILE_TIME %Y-%m-%dT%H:%M:%SZ)

add_compile_definitions(
	UNICODE _UNICODE
	PROJECT_NAME=${PROJECT_NAME}
	COMPILE_TIME=${COMPILE_TIME}
)

if(COMMIT_HASH)
	add_compile_definitions(COMMIT_HASH=${COMMIT_HASH})
endif()

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
find_package(ZLIB REQUIRED)
find_package(Freetype REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(imgui REQUIRED)
find_package(libmcc REQUIRED)

#					Sources
file(GLOB TOOL_SOURCES "src/tool/*.cpp")
file(GLOB_RECURSE HALOX_SOURCES "src/*.cpp" "src/*.c" )
list(FILTER HALOX_SOURCES EXCLUDE REGEX "src/tool/")

#					Executables
project(halox)

add_executable(halox
	WIN32 # Tell clang to build using subsystem windows for winMain
	${HALOX_SOURCES}
)

target_link_libraries(halox PRIVATE
	DbgHelp d3d11 Xinput
	ZLIB::ZLIB
	nlohmann_json::nlohmann_json
	Freetype::Freetype
	libmcc::libmcc
	ImGui::ImGui
)

install(TARGETS halox
    EXPORT haloxTargets
    RUNTIME  DESTINATION .
)

project(tool)

add_executable(tool
	${TOOL_SOURCES}
	"src/text/font_package.cpp"
)

target_link_libraries(tool PRIVATE
	ZLIB::ZLIB 
	Freetype::Freetype 
	nlohmann_json::nlohmann_json 
	libmcc::libmcc
)

install(TARGETS tool
    EXPORT toolTargets
    RUNTIME  DESTINATION .
)

# optional: install dependency dll
