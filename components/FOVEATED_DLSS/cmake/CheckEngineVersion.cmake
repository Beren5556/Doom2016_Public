# Also runnable with cmake -D... -P for focused mismatch regression checks.
if(NOT DLSS_FOVEADO_SOURCE_DIR OR NOT DLSS_FOVEADO_EXPECTED_VERSION)
  message(FATAL_ERROR "Engine version check requires source directory and CMake version")
endif()
file(READ "${DLSS_FOVEADO_SOURCE_DIR}/provenance.json" _foveation_provenance)
string(JSON _foveation_manifest_version GET "${_foveation_provenance}" engine_version)
file(READ "${DLSS_FOVEADO_SOURCE_DIR}/include/dlss_foveado/Foveation.h" _foveation_header)
string(REGEX MATCH "EngineVersion[ \t]*=[ \t]*\"([0-9]+\\.[0-9]+\\.[0-9]+)\""
  _foveation_version_declaration "${_foveation_header}")
set(_foveation_header_version "${CMAKE_MATCH_1}")
if(NOT "${_foveation_manifest_version}" STREQUAL "${DLSS_FOVEADO_EXPECTED_VERSION}" OR
   NOT "${_foveation_header_version}" STREQUAL "${DLSS_FOVEADO_EXPECTED_VERSION}")
  message(FATAL_ERROR "Engine version mismatch: CMake=${DLSS_FOVEADO_EXPECTED_VERSION}, provenance=${_foveation_manifest_version}, header=${_foveation_header_version}")
endif()
message(STATUS "Engine identity verified: ${DLSS_FOVEADO_EXPECTED_VERSION}")
