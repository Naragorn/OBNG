cmake_minimum_required(VERSION 3.20)

# The plugin's tests on their own, built natively but 32-bit, for working on
# Linux: none of them needs Windows, and the DLL build (clang-cl against
# xwin's winsysroot) makes Windows executables that cannot run here.
#
#   cmake -S plugin/tests -B build-plugin-tests -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32
#   cmake --build build-plugin-tests && ctest --test-dir build-plugin-tests --output-on-failure

project(OBNGPluginTests LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "The tests must be built 32-bit (clang++ with -m32): the mirrored engine structures hold for that pointer size.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

add_executable(ShadowLogicTest ShadowLogicTest.cpp)
target_include_directories(ShadowLogicTest PRIVATE ../src ../../src ../../tests)
target_compile_options(ShadowLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME ShadowLogicTest COMMAND ShadowLogicTest)

add_executable(ProbeLogicTest ProbeLogicTest.cpp)
target_include_directories(ProbeLogicTest PRIVATE ../src ../../tests)
target_compile_options(ProbeLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME ProbeLogicTest COMMAND ProbeLogicTest)

add_executable(PluginEntryTest PluginEntryTest.cpp ../src/PluginEntry.cpp)
target_include_directories(PluginEntryTest PRIVATE ../src ../../tests)
target_compile_options(PluginEntryTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME PluginEntryTest COMMAND PluginEntryTest)

add_executable(EntryDetourTest EntryDetourTest.cpp ../src/CodeWriter.cpp ../src/EntryDetour.cpp)
target_include_directories(EntryDetourTest PRIVATE ../src)
target_compile_options(EntryDetourTest PRIVATE -Wall -Wextra)
add_test(NAME EntryDetourTest COMMAND EntryDetourTest)

add_executable(ShadowRangeWalkLogicTest ShadowRangeWalkLogicTest.cpp)
target_include_directories(ShadowRangeWalkLogicTest PRIVATE ../src ../../src ../../tests)
target_compile_options(ShadowRangeWalkLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME ShadowRangeWalkLogicTest COMMAND ShadowRangeWalkLogicTest)

add_executable(RouteStereoDrawLogicTest RouteStereoDrawLogicTest.cpp)
target_include_directories(RouteStereoDrawLogicTest PRIVATE ../src ../../src ../../tests)
target_compile_options(RouteStereoDrawLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME RouteStereoDrawLogicTest COMMAND RouteStereoDrawLogicTest)

add_executable(RouteRangeWalkLogicTest RouteRangeWalkLogicTest.cpp)
target_include_directories(RouteRangeWalkLogicTest PRIVATE ../src ../../src ../../tests)
target_compile_options(RouteRangeWalkLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME RouteRangeWalkLogicTest COMMAND RouteRangeWalkLogicTest)

add_executable(StereoEyeSwitchLogicTest StereoEyeSwitchLogicTest.cpp)
target_include_directories(StereoEyeSwitchLogicTest PRIVATE ../src ../../src ../../tests)
target_compile_options(StereoEyeSwitchLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME StereoEyeSwitchLogicTest COMMAND StereoEyeSwitchLogicTest)

add_executable(OcclusionProbeLogicTest OcclusionProbeLogicTest.cpp)
target_include_directories(OcclusionProbeLogicTest PRIVATE ../src ../../tests)
target_compile_options(OcclusionProbeLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME OcclusionProbeLogicTest COMMAND OcclusionProbeLogicTest)

add_executable(UnroutedDrawLogicTest UnroutedDrawLogicTest.cpp)
target_include_directories(UnroutedDrawLogicTest PRIVATE ../src ../../tests)
target_compile_options(UnroutedDrawLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME UnroutedDrawLogicTest COMMAND UnroutedDrawLogicTest)
