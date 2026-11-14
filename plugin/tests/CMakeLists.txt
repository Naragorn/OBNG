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

add_executable(LogKeepLogicTest LogKeepLogicTest.cpp)
target_include_directories(LogKeepLogicTest PRIVATE ../src ../../tests)
target_compile_options(LogKeepLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME LogKeepLogicTest COMMAND LogKeepLogicTest)

add_executable(TimedCallLogicTest TimedCallLogicTest.cpp)
target_include_directories(TimedCallLogicTest PRIVATE ../src ../../tests)
target_compile_options(TimedCallLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME TimedCallLogicTest COMMAND TimedCallLogicTest)

add_executable(ActorCensusLogicTest ActorCensusLogicTest.cpp)
target_include_directories(ActorCensusLogicTest PRIVATE ../src ../../tests)
target_compile_options(ActorCensusLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME ActorCensusLogicTest COMMAND ActorCensusLogicTest)

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

add_executable(OcclusionCullLogicTest OcclusionCullLogicTest.cpp)
target_include_directories(OcclusionCullLogicTest PRIVATE ../src ../../tests)
target_compile_options(OcclusionCullLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME OcclusionCullLogicTest COMMAND OcclusionCullLogicTest)

add_executable(OcclusionCullGuardLogicTest OcclusionCullGuardLogicTest.cpp)
target_include_directories(OcclusionCullGuardLogicTest PRIVATE ../src ../../tests)
target_compile_options(OcclusionCullGuardLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME OcclusionCullGuardLogicTest COMMAND OcclusionCullGuardLogicTest)

add_executable(OcclusionCullActLogicTest OcclusionCullActLogicTest.cpp)
target_include_directories(OcclusionCullActLogicTest PRIVATE ../src ../../tests)
target_compile_options(OcclusionCullActLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME OcclusionCullActLogicTest COMMAND OcclusionCullActLogicTest)

add_executable(OcclusionCullQuickLogicTest OcclusionCullQuickLogicTest.cpp)
target_include_directories(OcclusionCullQuickLogicTest PRIVATE ../src ../../tests)
target_compile_options(OcclusionCullQuickLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME OcclusionCullQuickLogicTest COMMAND OcclusionCullQuickLogicTest)

add_executable(CodePatchLogicTest CodePatchLogicTest.cpp)
target_include_directories(CodePatchLogicTest PRIVATE ../src ../../tests)
target_compile_options(CodePatchLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME CodePatchLogicTest COMMAND CodePatchLogicTest)

add_executable(DrawGroupLogicTest DrawGroupLogicTest.cpp)
target_include_directories(DrawGroupLogicTest PRIVATE ../src ../../tests)
target_compile_options(DrawGroupLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME DrawGroupLogicTest COMMAND DrawGroupLogicTest)

add_executable(UnroutedDrawLogicTest UnroutedDrawLogicTest.cpp)
target_include_directories(UnroutedDrawLogicTest PRIVATE ../src ../../tests)
target_compile_options(UnroutedDrawLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME UnroutedDrawLogicTest COMMAND UnroutedDrawLogicTest)

add_executable(GrassProbeLogicTest GrassProbeLogicTest.cpp)
target_include_directories(GrassProbeLogicTest PRIVATE ../src ../../tests)
target_compile_options(GrassProbeLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME GrassProbeLogicTest COMMAND GrassProbeLogicTest)

add_executable(EyeConstantsTraceLogicTest EyeConstantsTraceLogicTest.cpp)
target_include_directories(EyeConstantsTraceLogicTest PRIVATE ../src ../../tests)
target_compile_options(EyeConstantsTraceLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME EyeConstantsTraceLogicTest COMMAND EyeConstantsTraceLogicTest)

add_executable(GrassFadePatchLogicTest GrassFadePatchLogicTest.cpp)
target_include_directories(GrassFadePatchLogicTest PRIVATE ../src ../../tests)
target_compile_options(GrassFadePatchLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME GrassFadePatchLogicTest COMMAND GrassFadePatchLogicTest)

# Against the installed game's shader package; skipped (77) where there is none.
add_executable(GrassFadePatchPackageTest GrassFadePatchPackageTest.cpp)
target_include_directories(GrassFadePatchPackageTest PRIVATE ../src ../../tests)
target_compile_options(GrassFadePatchPackageTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME GrassFadePatchPackageTest COMMAND GrassFadePatchPackageTest)
set_tests_properties(GrassFadePatchPackageTest PROPERTIES SKIP_RETURN_CODE 77)

# The CRT square root's dispatch (FastSqrtLogic.h): the decisions, the bytes,
# and the generated code run on the host's x87.
add_executable(FastSqrtLogicTest FastSqrtLogicTest.cpp ../src/CodeWriter.cpp)
target_include_directories(FastSqrtLogicTest PRIVATE ../src ../../tests)
target_compile_options(FastSqrtLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME FastSqrtLogicTest COMMAND FastSqrtLogicTest)

# Against the original's own bytes, mapped from the reference Oblivion.exe
# ($OBNG_ORACLE_EXE or Steam's default); skipped (77) where there is none.
add_executable(FastSqrtOracleTest FastSqrtOracleTest.cpp ../src/CodeWriter.cpp)
target_include_directories(FastSqrtOracleTest PRIVATE ../src ../../tests)
target_compile_options(FastSqrtOracleTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME FastSqrtOracleTest COMMAND FastSqrtOracleTest)
set_tests_properties(FastSqrtOracleTest PROPERTIES SKIP_RETURN_CODE 77)

add_executable(InstanceGroupLogicTest InstanceGroupLogicTest.cpp)
target_include_directories(InstanceGroupLogicTest PRIVATE ../src ../../tests)
target_compile_options(InstanceGroupLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME InstanceGroupLogicTest COMMAND InstanceGroupLogicTest)

add_executable(DrawInstancingLogicTest DrawInstancingLogicTest.cpp)
target_include_directories(DrawInstancingLogicTest PRIVATE ../src ../../tests)
target_compile_options(DrawInstancingLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME DrawInstancingLogicTest COMMAND DrawInstancingLogicTest)

add_executable(GpuPassTimerLogicTest GpuPassTimerLogicTest.cpp)
target_include_directories(GpuPassTimerLogicTest PRIVATE ../src ../../tests)
target_compile_options(GpuPassTimerLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME GpuPassTimerLogicTest COMMAND GpuPassTimerLogicTest)

add_executable(VoiceProbeLogicTest VoiceProbeLogicTest.cpp)
target_include_directories(VoiceProbeLogicTest PRIVATE ../src ../../tests)
target_compile_options(VoiceProbeLogicTest PRIVATE -Wall -Wextra -Werror)
add_test(NAME VoiceProbeLogicTest COMMAND VoiceProbeLogicTest)
