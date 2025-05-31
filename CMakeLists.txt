cmake_minimum_required (VERSION 3.0)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

set(SDL_STATIC ON)
set(SDL_SHARED OFF)
set(CMAKE_CXX_STANDARD 20)

add_subdirectory("./dep/libmcc")
add_subdirectory("./dep/freetype")
add_subdirectory("./dep/zlib")
add_subdirectory("./dep/json")

add_compile_definitions(UNICODE _UNICODE)

# imgui
project(imgui)

set(${PROJECT_NAME}_SRC 
	./dep/imgui/imgui.cpp
	./dep/imgui/imgui_demo.cpp
	./dep/imgui/imgui_draw.cpp
	./dep/imgui/imgui_tables.cpp
	./dep/imgui/imgui_widgets.cpp
	./dep/imgui/backends/imgui_impl_dx11.cpp
	./dep/imgui/backends/imgui_impl_win32.cpp
)

add_library(${PROJECT_NAME} STATIC ${${PROJECT_NAME}_SRC})

target_include_directories(${PROJECT_NAME} PUBLIC ./dep/imgui)

# tool
project("tool")

file(GLOB_RECURSE ${PROJECT_NAME}_SRC "${CMAKE_SOURCE_DIR}/src/tool/*.cpp")

add_executable(${PROJECT_NAME} 
	${${PROJECT_NAME}_SRC} 
	"${CMAKE_SOURCE_DIR}/src/text/font_package.cpp"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
	nlohmann_json freetype zlibstatic libmcc
)

target_link_options(${PROJECT_NAME} PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/ENTRY:mainCRTStartup>
)

# halox
project("halox")

add_compile_definitions(PROJECT_NAME=${PROJECT_NAME})

file(GLOB_RECURSE ${PROJECT_NAME}_SRC "${CMAKE_SOURCE_DIR}/src/*.cpp" "${CMAKE_SOURCE_DIR}/src/*.c")

add_executable(${PROJECT_NAME} ${${PROJECT_NAME}_SRC})

target_link_libraries(${PROJECT_NAME} 
	DbgHelp d3d11 Xinput
	nlohmann_json
	freetype zlibstatic
	imgui libmcc 
)

target_link_options(${PROJECT_NAME} PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/ENTRY:WinMainCRTStartup>
)

install(TARGETS ${PROJECT_NAME} DESTINATION $ENV{MCC_DIR})
