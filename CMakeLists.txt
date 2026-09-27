cmake_minimum_required(VERSION 3.21)

project(PreyHFR VERSION 1.0.1 LANGUAGES CXX)

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

add_library(PreyHFRHook SHARED
    src/hook/dllmain.cpp
)

add_executable(PreyHFRHookProbe
    src/hook_probe/main.cpp
)

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

target_compile_definitions(PreyHFRHook PRIVATE
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

target_compile_options(PreyHFRHook PRIVATE
    /W4
    /permissive-
    /EHsc
)

target_compile_options(PreyHFRHookProbe PRIVATE
    /W4
    /permissive-
    /EHsc
)

target_link_libraries(PreyRuntimeDump PRIVATE bcrypt)
target_link_libraries(PreyHFRLauncher PRIVATE bcrypt)
target_link_libraries(PreyHFRHook PRIVATE winmm)
target_link_libraries(PreyHFRHookProbe PRIVATE gdi32)

set_target_properties(PreyRuntimeDump PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(PreyHFRLauncher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(PreyHFRHook PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(PreyHFRHookProbe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS PreyHFRLauncher PreyHFRHook
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
)

install(FILES
    PreyHFR.ini
    uninstall-preyhfr.cmd
    DESTINATION .
)
install(FILES README.md DESTINATION . RENAME PreyHFR-README.md)
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
    DEPENDS PreyHFRLauncher PreyHFRHook
    COMMENT "Staging and verifying PreyHFR ${PREYHFR_PACKAGE_VERSION}"
    VERBATIM
)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME PreyHFRConfigValidation
        COMMAND PreyHFRLauncher
                --config "${CMAKE_SOURCE_DIR}/PreyHFR.ini"
                --validate-config
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
    set_tests_properties(PreyHFRDisplayConflictRejection
        PROPERTIES WILL_FAIL TRUE)
    set_tests_properties(PreyHFRCompatibilityDependencyRejection
        PROPERTIES WILL_FAIL TRUE)
    set_tests_properties(
        PreyHFRConfigValidation
        PreyHFRHookProbe144
        PreyHFRDisplayOptionsValidation
        PreyHFRConfigOverrideValidation
        PreyHFRDisplayConflictRejection
        PreyHFRRC1CompatibilityValidation
        PreyHFRCompatibilityDependencyRejection
        PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set_tests_properties(PreyHFRConfigValidation PROPERTIES
        PASS_REGULAR_EXPRESSION
            "presentation_fps=[0-9]+.*display_mode=borderless")
    set_tests_properties(PreyHFRConfigOverrideValidation PROPERTIES
        PASS_REGULAR_EXPRESSION
            "presentation_fps=144.*display_mode=windowed")
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
