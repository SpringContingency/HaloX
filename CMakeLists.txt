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

#Manage Dependencies
include(cmake/dependency-management.cmake)

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
