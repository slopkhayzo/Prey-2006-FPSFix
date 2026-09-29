cmake_minimum_required(VERSION 3.21)

project(PreyHFR VERSION 1.0.4 LANGUAGES CXX)

set(PREYHFR_PRERELEASE "")
if(PREYHFR_PRERELEASE)
    set(PREYHFR_PACKAGE_VERSION "${PROJECT_VERSION}-${PREYHFR_PRERELEASE}")
else()
    set(PREYHFR_PACKAGE_VERSION "${PROJECT_VERSION}")
endif()

if(NOT WIN32)
    message(FATAL_ERROR "PreyHFR currently targets the Windows retail build of Prey 1.4")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "PreyHFR must be configured with a 32-bit (x86) toolchain")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

configure_file(
    cmake/preyhfr_version.h.in
    "${CMAKE_BINARY_DIR}/generated/preyhfr_version.h"
    @ONLY
)

add_executable(PreyRuntimeDump
    src/runtime_dump/main.cpp
)

add_executable(PreyHFRLauncher
    src/launcher/main.cpp
)

target_include_directories(PreyHFRLauncher PRIVATE
    "${CMAKE_BINARY_DIR}/generated"
)

add_library(PreyHFRCommon STATIC
    src/common/config.cpp
    src/common/integrity.cpp
)

target_include_directories(PreyHFRCommon PUBLIC
    src/common
)

add_library(PreyHFR SHARED
    src/hook/dllmain.cpp
    src/hook/wait_gate_patch.cpp
)

target_include_directories(PreyHFR PRIVATE
    src/common
    src/hook
    "${CMAKE_BINARY_DIR}/generated"
)

add_executable(PreyHFRHookProbe
    src/hook_probe/main.cpp
)

add_executable(PreyHFRConfigTest
    src/config_test/main.cpp
)

target_link_libraries(PreyHFRConfigTest PRIVATE PreyHFRCommon)

target_compile_definitions(PreyRuntimeDump PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

target_compile_definitions(PreyHFRLauncher PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

target_compile_definitions(PreyHFRCommon PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

target_compile_definitions(PreyHFR PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

target_compile_definitions(PreyHFRHookProbe PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

target_compile_definitions(PreyHFRConfigTest PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

target_compile_options(PreyRuntimeDump PRIVATE
    /W4
    /permissive-
    /EHsc
)

target_compile_options(PreyHFRLauncher PRIVATE
    /W4
    /permissive-
    /EHsc
)

target_compile_options(PreyHFRCommon PRIVATE
    /W4
    /permissive-
    /EHsc
)

target_compile_options(PreyHFR PRIVATE
    /W4
    /permissive-
    /EHsc
)

target_compile_options(PreyHFRHookProbe PRIVATE
    /W4
    /permissive-
    /EHsc
)

target_compile_options(PreyHFRConfigTest PRIVATE
    /W4
    /permissive-
    /EHsc
)

target_link_libraries(PreyRuntimeDump PRIVATE bcrypt)
target_link_libraries(PreyHFRLauncher PRIVATE bcrypt)
target_link_libraries(PreyHFRCommon PRIVATE bcrypt)
target_link_libraries(PreyHFR PRIVATE PreyHFRCommon winmm)
target_link_libraries(PreyHFRHookProbe PRIVATE gdi32)

set_target_properties(PreyRuntimeDump PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(PreyHFRLauncher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(PreyHFR PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    PREFIX ""
    OUTPUT_NAME "PreyHFR"
    SUFFIX ".asi"
)

set_target_properties(PreyHFRHookProbe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(PreyHFRConfigTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS PreyHFR
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
)

install(FILES
    PreyHFR.ini
    uninstall-preyhfr.cmd
    DESTINATION .
)
install(FILES packaging/PreyHFR-README.txt DESTINATION .)
install(FILES LICENSE DESTINATION . RENAME PreyHFR-LICENSE.txt)

configure_file(
    cmake/GenerateReleaseManifest.cmake.in
    "${CMAKE_BINARY_DIR}/GenerateReleaseManifest.cmake"
    @ONLY
)
install(SCRIPT "${CMAKE_BINARY_DIR}/GenerateReleaseManifest.cmake")

set(PREYHFR_STAGE_DIR
    "${CMAKE_BINARY_DIR}/release/PreyHFR-${PREYHFR_PACKAGE_VERSION}-windows-x86")

add_custom_target(PreyHFRRelease
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${PREYHFR_STAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
            --prefix "${PREYHFR_STAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}"
            -DPACKAGE_DIR=${PREYHFR_STAGE_DIR}
            -DEXPECTED_VERSION=${PREYHFR_PACKAGE_VERSION}
            -P "${CMAKE_SOURCE_DIR}/cmake/VerifyRelease.cmake"
    DEPENDS PreyHFR
    COMMENT "Staging and verifying PreyHFR ${PREYHFR_PACKAGE_VERSION}"
    VERBATIM
)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME PreyHFRConfigValidation
        COMMAND PreyHFRConfigTest "${CMAKE_SOURCE_DIR}/PreyHFR.ini"
    )
    add_test(NAME PreyHFRHookProbe144
        COMMAND PreyHFRHookProbe 144 2
    )
    add_test(NAME PreyHFRDisplayOptionsValidation
        COMMAND PreyHFRLauncher --no-config --windowed --vsync off
                --resolution 1280x720 --validate-config
    )
    add_test(NAME PreyHFRConfigOverrideValidation
        COMMAND PreyHFRLauncher
                --config "${CMAKE_SOURCE_DIR}/PreyHFR.ini"
                --fps 144 --no-borderless --windowed
                --resolution 1280x720 --validate-config
    )
    add_test(NAME PreyHFRDisplayConflictRejection
        COMMAND PreyHFRLauncher --no-config --borderless --exclusive
                --validate-config
    )
    add_test(NAME PreyHFRRC1CompatibilityValidation
        COMMAND PreyHFRLauncher --no-config
                --no-continuous-snapshot-timing
                --no-multi-tic-entity-alignment
                --no-overdue-snapshot-fallback
                --validate-config
    )
    add_test(NAME PreyHFRCompatibilityDependencyRejection
        COMMAND PreyHFRLauncher --no-config
                --no-continuous-snapshot-timing
                --validate-config
    )
    add_test(NAME PreyHFRInterpolationTraceValidation
        COMMAND PreyHFRLauncher
                --config "${CMAKE_SOURCE_DIR}/PreyHFR.ini"
                --interpolation-trace --validate-config
    )
    add_test(NAME PreyHFRInterpolationTraceDependencyRejection
        COMMAND PreyHFRLauncher --no-config
                --interpolation-trace --validate-config
    )
    add_test(NAME PreyHFRTimelineResetKeyValidation
        COMMAND PreyHFRLauncher --no-config --camera-interp
                --timeline-reset-key F24 --validate-config
    )
    add_test(NAME PreyHFRTimelineResetKeyRejection
        COMMAND PreyHFRLauncher --no-config --camera-interp
                --timeline-reset-key F25 --validate-config
    )
    set_tests_properties(PreyHFRDisplayConflictRejection
        PROPERTIES WILL_FAIL TRUE)
    set_tests_properties(PreyHFRCompatibilityDependencyRejection
        PROPERTIES WILL_FAIL TRUE)
    set_tests_properties(PreyHFRInterpolationTraceDependencyRejection
        PROPERTIES WILL_FAIL TRUE)
    set_tests_properties(PreyHFRTimelineResetKeyRejection
        PROPERTIES WILL_FAIL TRUE)
    set_tests_properties(
        PreyHFRConfigValidation
        PreyHFRHookProbe144
        PreyHFRDisplayOptionsValidation
        PreyHFRConfigOverrideValidation
        PreyHFRDisplayConflictRejection
        PreyHFRRC1CompatibilityValidation
        PreyHFRCompatibilityDependencyRejection
        PreyHFRInterpolationTraceValidation
        PreyHFRInterpolationTraceDependencyRejection
        PreyHFRTimelineResetKeyValidation
        PreyHFRTimelineResetKeyRejection
        PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set_tests_properties(PreyHFRConfigValidation PROPERTIES
        PASS_REGULAR_EXPRESSION
            "presentation_fps=[0-9]+.*buffered_two_tic_interpolation=off.*display_mode=borderless")
    set_tests_properties(PreyHFRConfigOverrideValidation PROPERTIES
        PASS_REGULAR_EXPRESSION
            "presentation_fps=144.*display_mode=windowed")
    set_tests_properties(PreyHFRInterpolationTraceValidation PROPERTIES
        PASS_REGULAR_EXPRESSION "interpolation_trace=on")
    set_tests_properties(PreyHFRTimelineResetKeyValidation PROPERTIES
        PASS_REGULAR_EXPRESSION "timeline_reset_key=F24")
endif()

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "PreyHFR")
set(CPACK_PACKAGE_VENDOR "PreyHFR contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "High-frame-rate presentation fix for the supported Steam release of Prey 1.4")
set(CPACK_PACKAGE_VERSION "${PREYHFR_PACKAGE_VERSION}")
set(PREYHFR_BINARY_PACKAGE_FILE_NAME
    "PreyHFR-${PREYHFR_PACKAGE_VERSION}-windows-x86")
set(CPACK_PACKAGE_FILE_NAME "${PREYHFR_BINARY_PACKAGE_FILE_NAME}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/release")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(PREYHFR_ARCHIVE
    "${CPACK_PACKAGE_DIRECTORY}/${PREYHFR_BINARY_PACKAGE_FILE_NAME}.zip")
include(CPack)

add_custom_target(PreyHFRPackage
    COMMAND "${CMAKE_CPACK_COMMAND}" --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
    COMMAND "${CMAKE_COMMAND}"
            -DARCHIVE=${PREYHFR_ARCHIVE}
            -DEXPECTED_VERSION=${PREYHFR_PACKAGE_VERSION}
            -DVERIFY_SCRIPT=${CMAKE_SOURCE_DIR}/cmake/VerifyRelease.cmake
            -P "${CMAKE_SOURCE_DIR}/cmake/VerifyReleaseArchive.cmake"
    DEPENDS PreyHFRRelease
    COMMENT "Creating and verifying ${PREYHFR_BINARY_PACKAGE_FILE_NAME}.zip"
    VERBATIM
)

set(PREYHFR_LOADER_BUNDLE_DIR
    "${CMAKE_BINARY_DIR}/release/PreyHFR-${PREYHFR_PACKAGE_VERSION}-windows-x86-with-loader")
set(PREYHFR_LOADER_BUNDLE_ARCHIVE
    "${PREYHFR_LOADER_BUNDLE_DIR}.zip")
add_custom_target(PreyHFRBundlePackage
    COMMAND "${CMAKE_COMMAND}"
            -DPLUGIN_DIR=${PREYHFR_STAGE_DIR}
            -DBUNDLE_DIR=${PREYHFR_LOADER_BUNDLE_DIR}
            -DARCHIVE=${PREYHFR_LOADER_BUNDLE_ARCHIVE}
            -DLOADER_BINARY=${CMAKE_SOURCE_DIR}/vendor/ultimate-asi-loader/dinput.dll
            -DLOADER_CONFIG=${CMAKE_SOURCE_DIR}/packaging/dinput.ini
            -DLOADER_LICENSE=${CMAKE_SOURCE_DIR}/vendor/ultimate-asi-loader/LICENSE.txt
            -DLOADER_NOTICE=${CMAKE_SOURCE_DIR}/vendor/ultimate-asi-loader/NOTICE.txt
            -DEXPECTED_VERSION=${PREYHFR_PACKAGE_VERSION}
            -P "${CMAKE_SOURCE_DIR}/cmake/CreateLoaderBundle.cmake"
    DEPENDS PreyHFRRelease
    COMMENT "Creating loader-inclusive ${PREYHFR_BINARY_PACKAGE_FILE_NAME} bundle"
    VERBATIM
)
