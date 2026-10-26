cmake_minimum_required(VERSION 3.20)

# The tests of the rebuilt engine code. A standalone project, built 32-bit
# with clang, as OBVR's tests are: the layout structs mirror a 32-bit binary
# and their static_asserts only hold for a 4-byte pointer.
#
#   cmake -B build-tests tests -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32 -G Ninja
#   cmake --build build-tests
#   ctest --test-dir build-tests --output-on-failure
#
# Every tests/<Name>Test.cpp becomes one executable and one ctest entry, so a
# new test needs no edit here - the slice workflow's workers add tests in
# parallel and must not share a file.

project(OBNGTests LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR
		"The OBNG tests must be built 32-bit, found: ${CMAKE_SIZEOF_VOID_P}*8 bit.\n"
		"cmake -B build-tests tests -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	message(FATAL_ERROR
		"GCC cannot build the OBNG tests: the engine-facing declarations use the "
		"__thiscall, __stdcall and __cdecl keywords, which GCC only knows as attributes.\n"
		"cmake -B build-tests tests -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32")
endif()

set(CMAKE_CXX_STANDARD 20)
# For clangd: build-tests/compile_commands.json, linked from the repository root.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

file(GLOB_RECURSE engine_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../src/engine/*.cpp)
if(engine_sources)
	add_library(obng_engine STATIC ${engine_sources})
	target_include_directories(obng_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
	target_compile_options(obng_engine PRIVATE -Wall -Wextra -Werror)
endif()

file(GLOB test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*Test.cpp)
foreach(source ${test_sources})
	get_filename_component(name ${source} NAME_WE)
	add_executable(${name} ${source})
	target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR})
	target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
	if(engine_sources)
		target_link_libraries(${name} PRIVATE obng_engine)
	endif()
	add_test(NAME ${name} COMMAND ${name})
endforeach()
