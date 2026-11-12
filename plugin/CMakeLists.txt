cmake_minimum_required(VERSION 3.20)
project(OBNGPlugin CXX)

# OBNG.dll, the xOBSE plugin that runs the rebuilt functions beside the
# game's own and compares (docs/re-workflow.md, "the gate outside the
# workflow", layer 2). 32-bit, static runtime, no dependency beyond KERNEL32.
#
#   cmake -S plugin -B build-plugin -G Ninja -DCMAKE_BUILD_TYPE=Release --toolchain plugin/cmake/toolchain-linux-clang-cl.cmake
#   cmake --build build-plugin
# or tools/build-plugin.sh, which runs the tests first and deploys.

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "Oblivion is 32-bit: configure with a 32-bit toolchain (vcvars32.bat, -A Win32, or plugin/cmake/toolchain-linux-clang-cl.cmake).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_options(/W4 /permissive- /utf-8)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

# The rebuilt engine code (header-only logic under src/engine) and the
# plugin's own sources.
add_library(detour STATIC src/CodeWriter.cpp src/EntryDetour.cpp)
target_include_directories(detour PUBLIC src)

add_library(shadow STATIC src/Memory.cpp src/ShadowRecorder.cpp src/ShadowTimeInfo.cpp src/ShadowLoadingMenu.cpp src/RangeWalkGame.cpp src/ShadowRangeWalk.cpp src/RouteRangeWalk.cpp src/RouteStereoDraw.cpp src/Probes.cpp src/OcclusionProbe.cpp src/OcclusionCull.cpp src/UnroutedDrawProbe.cpp src/GrassProbe.cpp src/GrassFadeFix.cpp src/FastSqrt.cpp src/FastSceneUpdate.cpp src/FastVolatilePack.cpp src/ActorBatch.cpp src/HeapProbe.cpp src/LockProbe.cpp src/ProcessProbe.cpp src/ActorCensus.cpp src/ActorFarUpdate.cpp src/ReferenceAnimRate.cpp src/ActorShadowRest.cpp src/PassListKeep.cpp src/ShadowReceiverFilter.cpp src/StereoDrawTimer.cpp src/DrawInstancing.cpp src/DrawInstancingDevice.cpp src/GpuPassTimer.cpp src/TimedCalls.cpp src/FastConstantArrange.cpp)
target_include_directories(shadow PUBLIC src ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(shadow PUBLIC detour)

add_library(plugin_entry STATIC src/PluginEntry.cpp)
target_include_directories(plugin_entry PUBLIC src)

add_library(OBNG SHARED src/Plugin.cpp)
target_compile_definitions(OBNG PRIVATE OBNG_BUILDING_DLL)
target_link_libraries(OBNG PRIVATE plugin_entry shadow)
# A link map beside the DLL (OBNG.map): the render sampler's addresses inside OBNG.dll are
# resolved to its functions with it (tools/re/dll-samples.py). It changes nothing in the DLL.
target_link_options(OBNG PRIVATE /MAP:${CMAKE_BINARY_DIR}/OBNG.map)
