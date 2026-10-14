cmake_minimum_required(VERSION 3.20)
project(RizomUVChinese LANGUAGES CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "RizomUVChinese requires Windows x64")
endif()
if(MSVC AND DEFINED MSVC_CXX_ARCHITECTURE_ID AND NOT MSVC_CXX_ARCHITECTURE_ID STREQUAL "x64")
  message(FATAL_ERROR "RizomUVChinese requires the AMD64/x64 toolchain, not ARM64")
endif()
# Do not require a newer VC runtime than an older RizomUV installation ships.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Match the other localizers: build/out contains deliverable binaries while
# build/obj contains CMake/MSBuild projects, objects, caches and import libs.
set(RIZOMUV_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/out" CACHE PATH "Native binaries output directory")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${RIZOMUV_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${RIZOMUV_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build/obj/lib")
foreach(CONFIG Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${CONFIG}" CONFIG_UPPER)
  set("CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER}" "${RIZOMUV_OUTPUT_DIR}")
  set("CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER}" "${RIZOMUV_OUTPUT_DIR}")
  set("CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER}"
      "${CMAKE_CURRENT_SOURCE_DIR}/build/obj/lib")
endforeach()

set(LOCALIZER_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source")

function(configure_native_target target standard)
  target_include_directories(${target} PRIVATE "${LOCALIZER_INCLUDE_DIR}")
  target_compile_features(${target} PRIVATE cxx_std_${standard})
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
  endif()
endfunction()

add_library(RizomUVChineseRuntime SHARED
  source/hook.cpp
  source/translation_dictionary.cpp
  source/tooltip_html.cpp
  source/tooltip_hook.cpp
  source/gdi_iat_hooks.cpp
  source/native_menu_localizer.cpp
  source/runtime_log.cpp)
configure_native_target(RizomUVChineseRuntime 20)
target_link_libraries(RizomUVChineseRuntime PRIVATE user32 gdi32 shell32 bcrypt)
set_target_properties(RizomUVChineseRuntime PROPERTIES PREFIX "")

set(RIZOMUV_EXECUTABLE_ICON_RESOURCE
  icon/app_icon.rc)

add_executable(RizomUVChineseLauncher WIN32
  source/launcher.cpp
  ${RIZOMUV_EXECUTABLE_ICON_RESOURCE})
configure_native_target(RizomUVChineseLauncher 17)
target_link_libraries(RizomUVChineseLauncher PRIVATE user32 shell32)

add_executable(RizomUVTooltipTests EXCLUDE_FROM_ALL
  source/tests/tooltip.cpp source/tooltip_html.cpp source/translation_dictionary.cpp)
configure_native_target(RizomUVTooltipTests 20)
target_link_libraries(RizomUVTooltipTests PRIVATE bcrypt psapi)

add_custom_command(TARGET RizomUVChineseRuntime POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/translations/dictionary_zh.json"
    "$<TARGET_FILE_DIR:RizomUVChineseRuntime>/dictionary_zh.json")

add_custom_target(RizomUVOneClickPackage
  COMMAND cmd /c "${CMAKE_CURRENT_SOURCE_DIR}/source/build_inno.bat" --package-only
  DEPENDS RizomUVChineseLauncher RizomUVChineseRuntime
  COMMENT "Packaging with source/inno/package.py (Inno Setup)"
  VERBATIM)

add_executable(RizomUVCompatibilityTests EXCLUDE_FROM_ALL
  source/tests/compatibility.cpp source/translation_dictionary.cpp source/runtime_log.cpp)
configure_native_target(RizomUVCompatibilityTests 20)
target_link_libraries(RizomUVCompatibilityTests PRIVATE user32 gdi32 shell32)

add_executable(RizomUVPerformanceTests EXCLUDE_FROM_ALL
  source/tests/performance.cpp source/tooltip_html.cpp source/translation_dictionary.cpp source/runtime_log.cpp)
configure_native_target(RizomUVPerformanceTests 20)
target_link_libraries(RizomUVPerformanceTests PRIVATE user32 gdi32)

add_executable(RizomUVInstallerSupportTests EXCLUDE_FROM_ALL source/tests/installer_support.cpp)
configure_native_target(RizomUVInstallerSupportTests 17)
