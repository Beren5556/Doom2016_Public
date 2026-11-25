set(KHARVOX_SPIRV_CROSS_SOURCE "" CACHE PATH "SPIRV-Cross vulkan-sdk-1.4.304.0 source directory")
set(KHARVOX_SPIRV_CROSS_LIB "" CACHE PATH "SPIRV-Cross Release static library directory")
set(KHARVOX_GLSLANG_ROOT "" CACHE PATH "Installed glslang headers and static libraries")
if(NOT EXISTS "${KHARVOX_SPIRV_CROSS_SOURCE}/spirv_glsl.hpp" OR NOT EXISTS "${KHARVOX_GLSLANG_ROOT}/include/glslang/Include/glslang_c_interface.h")
  message(FATAL_ERROR "SFS compiler requires explicit SPIRV-Cross source/library and glslang install paths")
endif()
add_library(KharvoxSfsCompiler STATIC ShaderCompiler.cpp)
target_compile_features(KharvoxSfsCompiler PUBLIC cxx_std_17)
target_include_directories(KharvoxSfsCompiler PRIVATE "${KHARVOX_SPIRV_CROSS_SOURCE}" "${KHARVOX_GLSLANG_ROOT}/include")
foreach(lib spirv-cross-glsl spirv-cross-core)
  target_link_libraries(KharvoxSfsCompiler PUBLIC "${KHARVOX_SPIRV_CROSS_LIB}/${lib}.lib")
endforeach()
foreach(lib glslang glslang-default-resource-limits MachineIndependent GenericCodeGen OSDependent SPIRV SPIRV-Tools-opt SPIRV-Tools)
  target_link_libraries(KharvoxSfsCompiler PUBLIC "${KHARVOX_GLSLANG_ROOT}/lib/${lib}.lib")
endforeach()
add_executable(KharvoxSfsCompile ../../tools/sfs_compile.cpp)
target_link_libraries(KharvoxSfsCompile PRIVATE KharvoxSfsCompiler)
if(BUILD_TESTING AND WIN32)
  add_executable(KharvoxMappedUniformsTests ../../tests/mapped_uniforms_tests.cpp)
  target_compile_features(KharvoxMappedUniformsTests PRIVATE cxx_std_17)
  target_include_directories(KharvoxMappedUniformsTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
  add_test(NAME sfs-mapped-uniforms COMMAND KharvoxMappedUniformsTests)
  set(KHARVOX_DOOM_TEMPORAL_REPLAY "" CACHE FILEPATH "Associated engine/TAA temporal parameter replay")
  if(KHARVOX_DOOM_TEMPORAL_REPLAY)
    add_executable(KharvoxDoomTemporalParametersTests ../../tests/doom_temporal_parameters_tests.cpp)
    target_compile_features(KharvoxDoomTemporalParametersTests PRIVATE cxx_std_17)
    target_include_directories(KharvoxDoomTemporalParametersTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include" "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include")
    add_test(NAME doom-temporal-parameters COMMAND KharvoxDoomTemporalParametersTests "${KHARVOX_DOOM_TEMPORAL_REPLAY}")
  endif()
  set(KHARVOX_DOOM_OBJECT_VERTEX "" CACHE FILEPATH "Fingerprint-checked original object motion vertex shader")
  set(KHARVOX_DOOM_OBJECT_FRAGMENT "" CACHE FILEPATH "Fingerprint-checked original object motion fragment shader")
  if(KHARVOX_DOOM_OBJECT_VERTEX AND KHARVOX_DOOM_OBJECT_FRAGMENT)
    add_executable(KharvoxDoomObjectMotionGpuTests ../../tests/doom_object_motion_gpu_tests.cpp)
    target_link_libraries(KharvoxDoomObjectMotionGpuTests PRIVATE KharvoxSfsCompiler)
    target_compile_definitions(KharvoxDoomObjectMotionGpuTests PRIVATE VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(KharvoxDoomObjectMotionGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include" "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include")
    add_test(NAME doom-object-motion-gpu COMMAND KharvoxDoomObjectMotionGpuTests "${KHARVOX_DOOM_OBJECT_VERTEX}" "${KHARVOX_DOOM_OBJECT_FRAGMENT}")
    set_tests_properties(doom-object-motion-gpu PROPERTIES TIMEOUT 30)
    get_filename_component(object_shader_directory "${KHARVOX_DOOM_OBJECT_VERTEX}" DIRECTORY)
    if(EXISTS "${object_shader_directory}/d271df747727c7ef.spv" AND EXISTS "${object_shader_directory}/1cd7637afafff064.spv")
      add_test(NAME doom-object-motion-opaque-gpu COMMAND KharvoxDoomObjectMotionGpuTests "${object_shader_directory}/d271df747727c7ef.spv" "${object_shader_directory}/1cd7637afafff064.spv")
      set_tests_properties(doom-object-motion-opaque-gpu PROPERTIES TIMEOUT 30)
    endif()
  endif()
  set(KHARVOX_DOOM_GEOMETRY_ORIGINAL "" CACHE FILEPATH "Fingerprint-checked original geometry vertex SPIR-V")
  set(KHARVOX_DOOM_GEOMETRY_PROFILE "" CACHE FILEPATH "Fingerprint-checked installed geometry vertex profile")
  if(KHARVOX_DOOM_GEOMETRY_ORIGINAL)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/geometry-audit.frag.spv"
      COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
        "${PROJECT_SOURCE_DIR}/tests/shaders/geometry_audit.frag" -o "${CMAKE_CURRENT_BINARY_DIR}/geometry-audit.frag.spv"
      DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/geometry_audit.frag" VERBATIM)
    add_executable(KharvoxDoomGeometryAuditGpuTests ../../tests/doom_geometry_audit_gpu_tests.cpp NativeSfs.cpp "${CMAKE_CURRENT_BINARY_DIR}/geometry-audit.frag.spv")
    target_link_libraries(KharvoxDoomGeometryAuditGpuTests PRIVATE KharvoxSfsCompiler)
    target_compile_definitions(KharvoxDoomGeometryAuditGpuTests PRIVATE KHARVOX_HAVE_SFS_COMPILER VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(KharvoxDoomGeometryAuditGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include" "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include")
    add_test(NAME doom-geometry-audit-runtime-gpu COMMAND KharvoxDoomGeometryAuditGpuTests
      "${KHARVOX_DOOM_GEOMETRY_ORIGINAL}" "${CMAKE_CURRENT_BINARY_DIR}/geometry-audit.frag.spv")
    set_tests_properties(doom-geometry-audit-runtime-gpu PROPERTIES TIMEOUT 60)
    if(KHARVOX_DOOM_GEOMETRY_PROFILE)
      add_test(NAME doom-geometry-audit-profile-gpu COMMAND KharvoxDoomGeometryAuditGpuTests
        "${KHARVOX_DOOM_GEOMETRY_ORIGINAL}" "${CMAKE_CURRENT_BINARY_DIR}/geometry-audit.frag.spv" "${KHARVOX_DOOM_GEOMETRY_PROFILE}")
      set_tests_properties(doom-geometry-audit-profile-gpu PROPERTIES TIMEOUT 60)
    endif()
  endif()
  set(KHARVOX_DOOM_JITTER_REPLAY "" CACHE PATH "Explicit offline geometry jitter replay directory")
  if(KHARVOX_DOOM_JITTER_REPLAY)
    add_executable(KharvoxDoomRasterJitterGpuTests ../../tests/doom_raster_jitter_gpu_tests.cpp)
    target_compile_features(KharvoxDoomRasterJitterGpuTests PRIVATE cxx_std_17)
    target_compile_definitions(KharvoxDoomRasterJitterGpuTests PRIVATE VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(KharvoxDoomRasterJitterGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include" "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include")
    add_test(NAME doom-raster-jitter-gpu COMMAND KharvoxDoomRasterJitterGpuTests
      "${KHARVOX_DOOM_JITTER_REPLAY}/geometry-replay.vert.spv" "${KHARVOX_DOOM_JITTER_REPLAY}/geometry-replay.frag.spv" "${KHARVOX_DOOM_JITTER_REPLAY}/cases.bin")
    set_tests_properties(doom-raster-jitter-gpu PROPERTIES TIMEOUT 30)
  endif()
  add_executable(KharvoxTemporalContextTests ../../tests/temporal_context_tests.cpp)
  target_compile_features(KharvoxTemporalContextTests PRIVATE cxx_std_17)
  target_include_directories(KharvoxTemporalContextTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
  add_test(NAME doom-temporal-context COMMAND KharvoxTemporalContextTests)

  set(KHARVOX_DOOM_TAA_PROFILE "" CACHE FILEPATH "Explicit installed TAA SPIR-V for opt-in private guide GPU qualification")
  if(KHARVOX_DOOM_TAA_PROFILE)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv"
      COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
        "${PROJECT_SOURCE_DIR}/tests/shaders/temporal_guides.vert" -o "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv"
      DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/temporal_guides.vert" VERBATIM)
    add_executable(KharvoxDoomTemporalGuidesGpuTests ../../tests/doom_temporal_guides_gpu_tests.cpp "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv")
    target_link_libraries(KharvoxDoomTemporalGuidesGpuTests PRIVATE KharvoxSfsCompiler)
    target_compile_definitions(KharvoxDoomTemporalGuidesGpuTests PRIVATE VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(KharvoxDoomTemporalGuidesGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
    add_test(NAME doom-temporal-guides-gpu COMMAND KharvoxDoomTemporalGuidesGpuTests "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv" "${KHARVOX_DOOM_TAA_PROFILE}")
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/temporal-resolve-readback.frag.spv"
      COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
        "${PROJECT_SOURCE_DIR}/tests/shaders/temporal_resolve_readback.frag" -o "${CMAKE_CURRENT_BINARY_DIR}/temporal-resolve-readback.frag.spv"
      DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/temporal_resolve_readback.frag" VERBATIM)
    add_executable(KharvoxDoomTemporalResolveGpuTests ../../tests/doom_temporal_resolve_gpu_tests.cpp
      "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv" "${CMAKE_CURRENT_BINARY_DIR}/temporal-resolve-readback.frag.spv")
    target_link_libraries(KharvoxDoomTemporalResolveGpuTests PRIVATE KharvoxSfsCompiler)
    target_compile_definitions(KharvoxDoomTemporalResolveGpuTests PRIVATE VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(KharvoxDoomTemporalResolveGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
    add_test(NAME doom-temporal-resolve-gpu COMMAND KharvoxDoomTemporalResolveGpuTests
      "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv" "${KHARVOX_DOOM_TAA_PROFILE}" "${CMAKE_CURRENT_BINARY_DIR}/temporal-resolve-readback.frag.spv")
    set_tests_properties(doom-temporal-resolve-gpu PROPERTIES TIMEOUT 30)
    add_executable(KharvoxDoomTemporalBridgeGpuTests ../../tests/doom_temporal_bridge_gpu_tests.cpp
      "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv" "${CMAKE_CURRENT_BINARY_DIR}/temporal-resolve-readback.frag.spv")
    target_link_libraries(KharvoxDoomTemporalBridgeGpuTests PRIVATE KharvoxSfsCompiler)
    target_compile_definitions(KharvoxDoomTemporalBridgeGpuTests PRIVATE VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(KharvoxDoomTemporalBridgeGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
    add_test(NAME doom-temporal-bridge-gpu COMMAND KharvoxDoomTemporalBridgeGpuTests
      "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv" "${KHARVOX_DOOM_TAA_PROFILE}" "${CMAKE_CURRENT_BINARY_DIR}/temporal-resolve-readback.frag.spv")
    set_tests_properties(doom-temporal-bridge-gpu PROPERTIES TIMEOUT 30)
    if(EXISTS "${KHARVOX_RENDERDOC_DIRECTORY}/renderdoc_app.h")
      add_executable(KharvoxRenderDocTemporalCaptureTests ../../tests/renderdoc_temporal_capture_tests.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv" "${CMAKE_CURRENT_BINARY_DIR}/temporal-resolve-readback.frag.spv")
      target_compile_features(KharvoxRenderDocTemporalCaptureTests PRIVATE cxx_std_17)
      target_compile_definitions(KharvoxRenderDocTemporalCaptureTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
      target_include_directories(KharvoxRenderDocTemporalCaptureTests PRIVATE "${KHARVOX_RENDERDOC_DIRECTORY}" "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
      target_link_libraries(KharvoxRenderDocTemporalCaptureTests PRIVATE KharvoxSfsCompiler)
      add_test(NAME renderdoc-temporal-capture COMMAND KharvoxRenderDocTemporalCaptureTests
        "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv" "${KHARVOX_DOOM_TAA_PROFILE}"
        "${CMAKE_CURRENT_BINARY_DIR}/temporal-resolve-readback.frag.spv" "${KHARVOX_RENDERDOC_DIRECTORY}"
        "${CMAKE_CURRENT_BINARY_DIR}/renderdoc-temporal")
      set_tests_properties(renderdoc-temporal-capture PROPERTIES TIMEOUT 90)
    endif()
    if(TARGET KharvoxVulkanDlss)
      add_executable(KharvoxDoomDlssChainGpuTests ../../tests/doom_dlss_chain_gpu_tests.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv" "${CMAKE_CURRENT_BINARY_DIR}/temporal-resolve-readback.frag.spv")
      target_link_libraries(KharvoxDoomDlssChainGpuTests PRIVATE KharvoxSfsCompiler KharvoxVulkanDlss)
      target_include_directories(KharvoxDoomDlssChainGpuTests PRIVATE "${KHARVOX_NGX_SDK}/include")
      add_test(NAME doom-dlss-chain-gpu COMMAND KharvoxDoomDlssChainGpuTests
        "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv" "${KHARVOX_DOOM_TAA_PROFILE}" "${CMAKE_CURRENT_BINARY_DIR}/temporal-resolve-readback.frag.spv"
        "${KHARVOX_DLSS_RUNTIME}" "${CMAKE_CURRENT_BINARY_DIR}/ngx-chain-cache" "${KHARVOX_DLSS_EXPECTED_MODULE}")
      set_tests_properties(doom-dlss-chain-gpu PROPERTIES TIMEOUT 120)
      add_test(NAME doom-dlss-bridge-gpu COMMAND KharvoxDoomDlssChainGpuTests
        "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv" "${KHARVOX_DOOM_TAA_PROFILE}" "${CMAKE_CURRENT_BINARY_DIR}/temporal-resolve-readback.frag.spv"
        "${KHARVOX_DLSS_RUNTIME}" "${CMAKE_CURRENT_BINARY_DIR}/ngx-bridge-cache" "${KHARVOX_DLSS_EXPECTED_MODULE}" bridge)
      set_tests_properties(doom-dlss-bridge-gpu PROPERTIES TIMEOUT 120)
      add_executable(KharvoxDoomDlssSessionGpuTests ../../tests/doom_dlss_session_gpu_tests.cpp)
      target_link_libraries(KharvoxDoomDlssSessionGpuTests PRIVATE KharvoxSfsCompiler KharvoxVulkanDlss)
      target_include_directories(KharvoxDoomDlssSessionGpuTests PRIVATE "${KHARVOX_NGX_SDK}/include" "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include")
      add_test(NAME doom-dlss-session-gpu COMMAND KharvoxDoomDlssSessionGpuTests
        "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv" "${KHARVOX_DOOM_TAA_PROFILE}" "${CMAKE_CURRENT_BINARY_DIR}/temporal-resolve-readback.frag.spv"
        "${KHARVOX_DLSS_RUNTIME}" "${CMAKE_CURRENT_BINARY_DIR}/ngx-session-cache" "${KHARVOX_DLSS_EXPECTED_MODULE}")
      set_tests_properties(doom-dlss-session-gpu PROPERTIES TIMEOUT 120)
    endif()
    add_executable(KharvoxDoomTemporalAuditGpuTests ../../tests/doom_temporal_audit_gpu_tests.cpp "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv")
    target_link_libraries(KharvoxDoomTemporalAuditGpuTests PRIVATE KharvoxSfsCompiler)
    target_compile_definitions(KharvoxDoomTemporalAuditGpuTests PRIVATE VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(KharvoxDoomTemporalAuditGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
    add_test(NAME doom-temporal-audit-gpu COMMAND KharvoxDoomTemporalAuditGpuTests "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv" "${KHARVOX_DOOM_TAA_PROFILE}")
    set_tests_properties(doom-temporal-audit-gpu PROPERTIES TIMEOUT 60)
    set(KHARVOX_DOOM_TAA_ORIGINAL "" CACHE FILEPATH "Locally traced original DOOM TAA shader, not redistributed")
    if(KHARVOX_DOOM_TAA_ORIGINAL)
      add_executable(KharvoxDoomTemporalBridgeRuntimeTests ../../tests/doom_temporal_bridge_runtime_tests.cpp NativeSfs.cpp "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv")
      target_link_libraries(KharvoxDoomTemporalBridgeRuntimeTests PRIVATE KharvoxSfsCompiler)
      target_compile_definitions(KharvoxDoomTemporalBridgeRuntimeTests PRIVATE VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX KHARVOX_HAVE_SFS_COMPILER)
      target_include_directories(KharvoxDoomTemporalBridgeRuntimeTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include" "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include")
      add_test(NAME doom-temporal-bridge-runtime-gpu COMMAND KharvoxDoomTemporalBridgeRuntimeTests "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv" "${KHARVOX_DOOM_TAA_PROFILE}" "${KHARVOX_DOOM_TAA_ORIGINAL}")
      set_tests_properties(doom-temporal-bridge-runtime-gpu PROPERTIES TIMEOUT 60)
      if(TARGET KharvoxVulkanDlss AND KHARVOX_DOOM_TEMPORAL_REPLAY)
        add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/dlss-restore-compute.comp.spv"
          COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V "${PROJECT_SOURCE_DIR}/tests/shaders/dlss_restore_compute.comp" -o "${CMAKE_CURRENT_BINARY_DIR}/dlss-restore-compute.comp.spv"
          DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/dlss_restore_compute.comp" VERBATIM)
        add_executable(KharvoxDoomDlssRuntimeTests ../../tests/doom_temporal_bridge_runtime_tests.cpp NativeSfs.cpp "${CMAKE_CURRENT_BINARY_DIR}/dlss-restore-compute.comp.spv")
        target_link_libraries(KharvoxDoomDlssRuntimeTests PRIVATE KharvoxSfsCompiler KharvoxVulkanDlss)
        target_compile_definitions(KharvoxDoomDlssRuntimeTests PRIVATE VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX KHARVOX_HAVE_SFS_COMPILER KHARVOX_HAVE_DLSS_STARTUP KHARVOX_DLSS_TEST_READBACK)
        target_include_directories(KharvoxDoomDlssRuntimeTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include" "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include")
        add_test(NAME doom-dlss-runtime-gpu COMMAND KharvoxDoomDlssRuntimeTests "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv" "${KHARVOX_DOOM_TAA_PROFILE}" "${KHARVOX_DOOM_TAA_ORIGINAL}"
          "${KHARVOX_DLSS_RUNTIME}" "${CMAKE_CURRENT_BINARY_DIR}/ngx-native-cache" "${KHARVOX_DOOM_TEMPORAL_REPLAY}")
        set_tests_properties(doom-dlss-runtime-gpu PROPERTIES TIMEOUT 120)
        if(TARGET KharvoxVulkanReconstruction)
          add_executable(KharvoxDoomReconstructionRuntimeTests ../../tests/doom_temporal_bridge_runtime_tests.cpp NativeSfs.cpp "${CMAKE_CURRENT_BINARY_DIR}/dlss-restore-compute.comp.spv")
          target_link_libraries(KharvoxDoomReconstructionRuntimeTests PRIVATE KharvoxSfsCompiler KharvoxVulkanReconstruction)
          target_compile_definitions(KharvoxDoomReconstructionRuntimeTests PRIVATE VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX KHARVOX_HAVE_SFS_COMPILER KHARVOX_HAVE_DLSS_STARTUP KHARVOX_DLSS_TEST_READBACK KHARVOX_HAVE_RECONSTRUCTION)
          target_compile_options(KharvoxDoomReconstructionRuntimeTests PRIVATE /Zc:char8_t-)
          target_include_directories(KharvoxDoomReconstructionRuntimeTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include" "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include")
          set_target_properties(KharvoxDoomReconstructionRuntimeTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native-reconstruction")
        endif()
      endif()
      add_executable(KharvoxDoomTemporalRuntimeGpuTests ../../tests/doom_temporal_audit_gpu_tests.cpp NativeSfs.cpp "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv")
      target_link_libraries(KharvoxDoomTemporalRuntimeGpuTests PRIVATE KharvoxSfsCompiler)
      target_compile_definitions(KharvoxDoomTemporalRuntimeGpuTests PRIVATE VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX KHARVOX_AUDIT_RUNTIME KHARVOX_HAVE_SFS_COMPILER)
      target_include_directories(KharvoxDoomTemporalRuntimeGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include" "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include")
      add_test(NAME doom-temporal-audit-runtime-gpu COMMAND KharvoxDoomTemporalRuntimeGpuTests "${CMAKE_CURRENT_BINARY_DIR}/temporal-guides.vert.spv" "${KHARVOX_DOOM_TAA_PROFILE}" "${KHARVOX_DOOM_TAA_ORIGINAL}")
      set_tests_properties(doom-temporal-audit-runtime-gpu PROPERTIES TIMEOUT 60)
    endif()
    set_tests_properties(doom-temporal-guides-gpu PROPERTIES TIMEOUT 30)
  endif()
  add_executable(KharvoxSfsPushReplayTests ../../tests/sfs_push_replay_tests.cpp)
  target_compile_features(KharvoxSfsPushReplayTests PRIVATE cxx_std_17)
  target_include_directories(KharvoxSfsPushReplayTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
  add_test(NAME sfs-push-replay COMMAND KharvoxSfsPushReplayTests)
  add_executable(KharvoxSfsSourceRingGpuTests ../../tests/sfs_source_ring_gpu_tests.cpp)
  target_compile_features(KharvoxSfsSourceRingGpuTests PRIVATE cxx_std_17)
  target_compile_definitions(KharvoxSfsSourceRingGpuTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(KharvoxSfsSourceRingGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
  add_test(NAME sfs-source-ring-gpu COMMAND KharvoxSfsSourceRingGpuTests)
  set_tests_properties(sfs-source-ring-gpu PROPERTIES TIMEOUT 30)
  add_executable(KharvoxSfsSourceRuntimeGpuTests ../../tests/sfs_source_ring_gpu_tests.cpp NativeSfs.cpp)
  target_compile_features(KharvoxSfsSourceRuntimeGpuTests PRIVATE cxx_std_17)
  target_link_libraries(KharvoxSfsSourceRuntimeGpuTests PRIVATE KharvoxSfsCompiler)
  target_compile_definitions(KharvoxSfsSourceRuntimeGpuTests PRIVATE KHARVOX_SFS_RING_RUNTIME KHARVOX_HAVE_SFS_COMPILER WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(KharvoxSfsSourceRuntimeGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include" "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include")
  add_test(NAME sfs-source-runtime-gpu COMMAND KharvoxSfsSourceRuntimeGpuTests)
  set_tests_properties(sfs-source-runtime-gpu PROPERTIES TIMEOUT 30)

  add_executable(KharvoxSfsGroupVoteTests ../../tests/sfs_group_vote_tests.cpp)
  target_link_libraries(KharvoxSfsGroupVoteTests PRIVATE KharvoxSfsCompiler)
  target_include_directories(KharvoxSfsGroupVoteTests PRIVATE "${KHARVOX_SPIRV_CROSS_SOURCE}")
  add_test(NAME sfs-legacy-group-vote COMMAND KharvoxSfsGroupVoteTests)
  add_executable(KharvoxSfsGameplayContractTests ../../tests/sfs_gameplay_contract_tests.cpp)
  target_compile_features(KharvoxSfsGameplayContractTests PRIVATE cxx_std_17)
  add_test(NAME sfs-gameplay-contract COMMAND KharvoxSfsGameplayContractTests)
  add_executable(KharvoxSfsHandDepthGpuTests ../../tests/hand_scene_depth_gpu_tests.cpp)
  target_compile_features(KharvoxSfsHandDepthGpuTests PRIVATE cxx_std_17)
  target_compile_definitions(KharvoxSfsHandDepthGpuTests PRIVATE KHARVOX_TEST_DEPTH_LAYER=1)
  target_include_directories(KharvoxSfsHandDepthGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
  add_test(NAME sfs-hand-depth-right-layer-gpu COMMAND KharvoxSfsHandDepthGpuTests)
  set_tests_properties(sfs-hand-depth-right-layer-gpu PROPERTIES TIMEOUT 30)
  add_executable(KharvoxSfsProjectionTests ../../tests/sfs_projection_tests.cpp)
  target_compile_features(KharvoxSfsProjectionTests PRIVATE cxx_std_17)
  target_include_directories(KharvoxSfsProjectionTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include")
  target_include_directories(KharvoxSfsProjectionTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
  add_test(NAME sfs-frame-projection COMMAND KharvoxSfsProjectionTests)
  set(sfs_fixtures "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
  add_custom_command(OUTPUT "${sfs_fixtures}/indirect.comp.spv"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${sfs_fixtures}"
    COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
      "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_indirect.comp" -o "${sfs_fixtures}/indirect.comp.spv"
    DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_indirect.comp" VERBATIM)
  add_executable(KharvoxSfsIndirectGpuTests ../../tests/sfs_source_ring_gpu_tests.cpp NativeSfs.cpp "${sfs_fixtures}/indirect.comp.spv")
  target_compile_features(KharvoxSfsIndirectGpuTests PRIVATE cxx_std_17)
  target_link_libraries(KharvoxSfsIndirectGpuTests PRIVATE KharvoxSfsCompiler)
  target_compile_definitions(KharvoxSfsIndirectGpuTests PRIVATE KHARVOX_SFS_RING_RUNTIME KHARVOX_SFS_TEST_INDIRECT KHARVOX_HAVE_SFS_COMPILER WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(KharvoxSfsIndirectGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include" "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include")
  add_test(NAME sfs-indirect-both-eyes-gpu COMMAND KharvoxSfsIndirectGpuTests "${sfs_fixtures}/indirect.comp.spv")
  set_tests_properties(sfs-indirect-both-eyes-gpu PROPERTIES TIMEOUT 30)
  add_custom_command(OUTPUT "${sfs_fixtures}/indirect-shared.comp.spv"
    COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
      "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_indirect_shared.comp" -o "${sfs_fixtures}/indirect-shared.comp.spv"
    DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_indirect_shared.comp" VERBATIM)
  target_sources(KharvoxSfsIndirectGpuTests PRIVATE "${sfs_fixtures}/indirect-shared.comp.spv")
  foreach(stage vert frag)
    add_custom_command(OUTPUT "${sfs_fixtures}/sampling.${stage}.spv"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${sfs_fixtures}"
      COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
        "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_sampling.${stage}" -o "${sfs_fixtures}/sampling.${stage}.spv"
      DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_sampling.${stage}" VERBATIM)
  endforeach()
  add_executable(KharvoxSfsSamplingGpuTests ../../tests/sfs_sampling_gpu_tests.cpp
    "${sfs_fixtures}/sampling.vert.spv" "${sfs_fixtures}/sampling.frag.spv")
  target_link_libraries(KharvoxSfsSamplingGpuTests PRIVATE KharvoxSfsCompiler)
  target_include_directories(KharvoxSfsSamplingGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
  add_test(NAME sfs-typed-sampling-gpu COMMAND KharvoxSfsSamplingGpuTests
    "${sfs_fixtures}/sampling.vert.spv" "${sfs_fixtures}/sampling.frag.spv")
  set_tests_properties(sfs-typed-sampling-gpu PROPERTIES TIMEOUT 30)
  add_custom_command(OUTPUT "${sfs_fixtures}/texture-feedback.frag.spv"
    COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
      "${PROJECT_SOURCE_DIR}/tests/shaders/doom_texture_feedback.frag" -o "${sfs_fixtures}/texture-feedback.frag.spv"
    DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/doom_texture_feedback.frag" VERBATIM)
  add_executable(KharvoxDoomTextureFeedbackGpuTests ../../tests/doom_texture_feedback_gpu_tests.cpp
    "${sfs_fixtures}/sampling.vert.spv" "${sfs_fixtures}/texture-feedback.frag.spv")
  target_link_libraries(KharvoxDoomTextureFeedbackGpuTests PRIVATE KharvoxSfsCompiler)
  target_compile_definitions(KharvoxDoomTextureFeedbackGpuTests PRIVATE VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(KharvoxDoomTextureFeedbackGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
  add_test(NAME doom-texture-feedback-gpu COMMAND KharvoxDoomTextureFeedbackGpuTests
    "${sfs_fixtures}/sampling.vert.spv" "${sfs_fixtures}/texture-feedback.frag.spv")
  set_tests_properties(doom-texture-feedback-gpu PROPERTIES TIMEOUT 30)
  add_executable(KharvoxSfsRuntimeGpuTests ../../tests/sfs_sampling_gpu_tests.cpp NativeSfs.cpp
    "${sfs_fixtures}/sampling.vert.spv" "${sfs_fixtures}/sampling.frag.spv")
  target_link_libraries(KharvoxSfsRuntimeGpuTests PRIVATE KharvoxSfsCompiler)
  target_compile_definitions(KharvoxSfsRuntimeGpuTests PRIVATE KHARVOX_SFS_TEST_RUNTIME KHARVOX_HAVE_SFS_COMPILER WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(KharvoxSfsRuntimeGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include" "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
  add_test(NAME sfs-runtime-gpu COMMAND KharvoxSfsRuntimeGpuTests
    "${sfs_fixtures}/sampling.vert.spv" "${sfs_fixtures}/sampling.frag.spv")
  set_tests_properties(sfs-runtime-gpu PROPERTIES TIMEOUT 30)
  add_custom_command(OUTPUT "${sfs_fixtures}/affine.frag.spv"
    COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
      "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_affine.frag" -o "${sfs_fixtures}/affine.frag.spv"
    DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_affine.frag" VERBATIM)
  add_executable(KharvoxSfsAffineGpuTests ../../tests/sfs_sampling_gpu_tests.cpp NativeSfs.cpp
    "${sfs_fixtures}/sampling.vert.spv" "${sfs_fixtures}/affine.frag.spv")
  target_link_libraries(KharvoxSfsAffineGpuTests PRIVATE KharvoxSfsCompiler)
  target_compile_definitions(KharvoxSfsAffineGpuTests PRIVATE KHARVOX_SFS_TEST_RUNTIME KHARVOX_SFS_TEST_AFFINE KHARVOX_HAVE_SFS_COMPILER WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(KharvoxSfsAffineGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include" "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
  add_test(NAME sfs-affine-ipd-gpu COMMAND KharvoxSfsAffineGpuTests
    "${sfs_fixtures}/sampling.vert.spv" "${sfs_fixtures}/affine.frag.spv")
  set_tests_properties(sfs-affine-ipd-gpu PROPERTIES TIMEOUT 30)
  foreach(kind lighting shadow)
    add_custom_command(OUTPUT "${sfs_fixtures}/${kind}.frag.spv"
      COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
        "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_${kind}.frag" -o "${sfs_fixtures}/${kind}.frag.spv"
      DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_${kind}.frag" VERBATIM)
    add_executable(KharvoxSfs${kind}GpuTests ../../tests/sfs_sampling_gpu_tests.cpp NativeSfs.cpp
      "${sfs_fixtures}/sampling.vert.spv" "${sfs_fixtures}/${kind}.frag.spv")
    target_link_libraries(KharvoxSfs${kind}GpuTests PRIVATE KharvoxSfsCompiler)
    string(TOUPPER "${kind}" kind_upper)
    target_compile_definitions(KharvoxSfs${kind}GpuTests PRIVATE KHARVOX_SFS_TEST_RUNTIME KHARVOX_SFS_TEST_AFFINE KHARVOX_SFS_TEST_${kind_upper} KHARVOX_HAVE_SFS_COMPILER WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(KharvoxSfs${kind}GpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include" "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
    add_test(NAME sfs-${kind}-gpu COMMAND KharvoxSfs${kind}GpuTests "${sfs_fixtures}/sampling.vert.spv" "${sfs_fixtures}/${kind}.frag.spv")
    set_tests_properties(sfs-${kind}-gpu PROPERTIES TIMEOUT 30)
  endforeach()
  add_test(NAME sfs-lighting-asymmetric-gpu COMMAND KharvoxSfslightingGpuTests "${sfs_fixtures}/sampling.vert.spv" "${sfs_fixtures}/lighting.frag.spv" asymmetric)
  set_tests_properties(sfs-lighting-asymmetric-gpu PROPERTIES TIMEOUT 30)
  add_custom_command(OUTPUT "${sfs_fixtures}/ssr.frag.spv"
    COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
      "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_ssr.frag" -o "${sfs_fixtures}/ssr.frag.spv"
    DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_ssr.frag" VERBATIM)
  target_sources(KharvoxSfslightingGpuTests PRIVATE "${sfs_fixtures}/ssr.frag.spv")
  add_test(NAME sfs-ssr-projection-gpu COMMAND KharvoxSfslightingGpuTests "${sfs_fixtures}/sampling.vert.spv" "${sfs_fixtures}/ssr.frag.spv")
  add_test(NAME sfs-ssr-asymmetric-gpu COMMAND KharvoxSfslightingGpuTests "${sfs_fixtures}/sampling.vert.spv" "${sfs_fixtures}/ssr.frag.spv" asymmetric)
  set_tests_properties(sfs-ssr-projection-gpu sfs-ssr-asymmetric-gpu PROPERTIES TIMEOUT 30)
  add_custom_command(OUTPUT "${sfs_fixtures}/shadow-projection.vert.spv"
    COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
      "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_shadow_projection.vert" -o "${sfs_fixtures}/shadow-projection.vert.spv"
    DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_shadow_projection.vert" VERBATIM)
  add_executable(KharvoxSfsShadowProjectionGpuTests ../../tests/sfs_sampling_gpu_tests.cpp NativeSfs.cpp
    "${sfs_fixtures}/shadow-projection.vert.spv" "${sfs_fixtures}/sampling.frag.spv")
  target_link_libraries(KharvoxSfsShadowProjectionGpuTests PRIVATE KharvoxSfsCompiler)
  target_compile_definitions(KharvoxSfsShadowProjectionGpuTests PRIVATE KHARVOX_SFS_TEST_RUNTIME KHARVOX_SFS_TEST_AFFINE KHARVOX_SFS_TEST_SHADOW_PROJECTION KHARVOX_HAVE_SFS_COMPILER WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(KharvoxSfsShadowProjectionGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/openxr-sdk/include" "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
  add_test(NAME sfs-shadow-projection-gpu COMMAND KharvoxSfsShadowProjectionGpuTests "${sfs_fixtures}/shadow-projection.vert.spv" "${sfs_fixtures}/sampling.frag.spv")
  add_test(NAME sfs-camera-depth-projection-gpu COMMAND KharvoxSfsShadowProjectionGpuTests "${sfs_fixtures}/shadow-projection.vert.spv" "${sfs_fixtures}/sampling.frag.spv" camera)
  set_tests_properties(sfs-shadow-projection-gpu sfs-camera-depth-projection-gpu PROPERTIES TIMEOUT 30)
  add_test(NAME sfs-unshifted-profile-camera-gpu COMMAND KharvoxSfsShadowProjectionGpuTests "${sfs_fixtures}/shadow-projection.vert.spv" "${sfs_fixtures}/sampling.frag.spv" profile-camera)
  add_test(NAME sfs-unshifted-profile-shadow-gpu COMMAND KharvoxSfsShadowProjectionGpuTests "${sfs_fixtures}/shadow-projection.vert.spv" "${sfs_fixtures}/sampling.frag.spv" profile-shadow)
  set_tests_properties(sfs-unshifted-profile-camera-gpu sfs-unshifted-profile-shadow-gpu PROPERTIES TIMEOUT 30)
  add_custom_command(OUTPUT "${sfs_fixtures}/particle-projection.vert.spv"
    COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
      "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_particle_projection.vert" -o "${sfs_fixtures}/particle-projection.vert.spv"
    DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_particle_projection.vert" VERBATIM)
  target_sources(KharvoxSfsShadowProjectionGpuTests PRIVATE "${sfs_fixtures}/particle-projection.vert.spv")
  add_test(NAME sfs-particle-projection-gpu COMMAND KharvoxSfsShadowProjectionGpuTests "${sfs_fixtures}/particle-projection.vert.spv" "${sfs_fixtures}/sampling.frag.spv" camera)
  set_tests_properties(sfs-particle-projection-gpu PROPERTIES TIMEOUT 30)
  add_custom_command(OUTPUT "${sfs_fixtures}/material-atlas.vert.spv"
    COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
      "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_material_atlas.vert" -o "${sfs_fixtures}/material-atlas.vert.spv"
    DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_material_atlas.vert" VERBATIM)
  add_custom_command(OUTPUT "${sfs_fixtures}/packed-world.vert.spv"
    COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
      "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_packed_world.vert" -o "${sfs_fixtures}/packed-world.vert.spv"
    DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_packed_world.vert" VERBATIM)
  add_custom_command(OUTPUT "${sfs_fixtures}/inactive-projection.vert.spv"
    COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
      "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_inactive_projection.vert" -o "${sfs_fixtures}/inactive-projection.vert.spv"
    DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_inactive_projection.vert" VERBATIM)
  add_custom_command(OUTPUT "${sfs_fixtures}/ui-semantic.vert.spv"
    COMMAND "${KHARVOX_GLSLANG_ROOT}/bin/glslang.exe" -V --target-env vulkan1.1
      "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_ui_semantic.vert" -o "${sfs_fixtures}/ui-semantic.vert.spv"
    DEPENDS "${PROJECT_SOURCE_DIR}/tests/shaders/sfs_ui_semantic.vert" VERBATIM)
  add_executable(KharvoxSfsLightingRewriteTests ../../tests/sfs_lighting_rewrite_tests.cpp "${sfs_fixtures}/material-atlas.vert.spv" "${sfs_fixtures}/shadow-projection.vert.spv" "${sfs_fixtures}/packed-world.vert.spv" "${sfs_fixtures}/inactive-projection.vert.spv" "${sfs_fixtures}/ui-semantic.vert.spv")
  target_link_libraries(KharvoxSfsLightingRewriteTests PRIVATE KharvoxSfsCompiler)
  target_include_directories(KharvoxSfsLightingRewriteTests PRIVATE "${PROJECT_SOURCE_DIR}/third-party/vulkan-headers/include")
  add_test(NAME sfs-lighting-rewrite COMMAND KharvoxSfsLightingRewriteTests "${sfs_fixtures}/material-atlas.vert.spv" "${sfs_fixtures}/shadow-projection.vert.spv" "${sfs_fixtures}/packed-world.vert.spv" "${sfs_fixtures}/inactive-projection.vert.spv" "${sfs_fixtures}/ui-semantic.vert.spv")
endif()
