cmake_minimum_required(VERSION 3.20)
project(RizomUVChinese LANGUAGES CXX)

add_executable(RizomUVUiProbe EXCLUDE_FROM_ALL plugin/development/diagnostics/interface_probe.cpp)
target_compile_features(RizomUVUiProbe PRIVATE cxx_std_17)
target_compile_definitions(RizomUVUiProbe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
  _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
target_link_libraries(RizomUVUiProbe PRIVATE user32 ole32 oleaut32 uiautomationcore)

if(MSVC)
  target_compile_options(RizomUVUiProbe PRIVATE /W4 /permissive- /utf-8)
endif()

add_library(RizomUVTextSniffer SHARED EXCLUDE_FROM_ALL plugin/development/diagnostics/text_rendering_sniffer.cpp)
target_compile_features(RizomUVTextSniffer PRIVATE cxx_std_17)
target_compile_definitions(RizomUVTextSniffer PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(RizomUVTextSniffer PRIVATE user32 gdi32)
set_target_properties(RizomUVTextSniffer PROPERTIES PREFIX "")

add_executable(RizomUVSnifferLauncher EXCLUDE_FROM_ALL plugin/development/diagnostics/text_sniffer_launcher.cpp)
target_compile_features(RizomUVSnifferLauncher PRIVATE cxx_std_17)
target_compile_definitions(RizomUVSnifferLauncher PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(RizomUVSnifferLauncher PRIVATE user32)

if(MSVC)
  target_compile_options(RizomUVTextSniffer PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(RizomUVSnifferLauncher PRIVATE /W4 /permissive- /utf-8)
endif()

set(LOCALIZER_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/plugin/source")

add_library(RizomUVChineseRuntime SHARED
  plugin/source/rizomuv_chinese_runtime.cpp
  plugin/source/translation_dictionary.cpp
  plugin/source/gdi_iat_hooks.cpp
  plugin/source/native_menu_localizer.cpp
  plugin/source/runtime_log.cpp)
target_include_directories(RizomUVChineseRuntime PRIVATE "${LOCALIZER_INCLUDE_DIR}")
target_compile_features(RizomUVChineseRuntime PRIVATE cxx_std_17)
target_compile_definitions(RizomUVChineseRuntime PRIVATE
  UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(RizomUVChineseRuntime PRIVATE user32 gdi32 shell32)
set_target_properties(RizomUVChineseRuntime PROPERTIES PREFIX "")

set(RIZOMUV_EXECUTABLE_ICON_RESOURCE
  plugin/resources/rizomuv_executable_icon.rc)

add_executable(RizomUVChineseLauncher WIN32
  plugin/source/rizomuv_chinese_launcher.cpp
  ${RIZOMUV_EXECUTABLE_ICON_RESOURCE})
target_compile_features(RizomUVChineseLauncher PRIVATE cxx_std_17)
target_compile_definitions(RizomUVChineseLauncher PRIVATE
  UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(RizomUVChineseLauncher PRIVATE user32 shell32)

add_executable(RizomUVChineseInstaller WIN32
  plugin/source/rizomuv_chinese_installer.cpp
  ${RIZOMUV_EXECUTABLE_ICON_RESOURCE})
target_compile_features(RizomUVChineseInstaller PRIVATE cxx_std_17)
target_compile_definitions(RizomUVChineseInstaller PRIVATE
  UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(RizomUVChineseInstaller PRIVATE
  user32 shell32 ole32 comctl32 advapi32)
set(RIZOMUV_INSTALLER_PAYLOAD_RC
  "${CMAKE_CURRENT_BINARY_DIR}/installer_payload_$<CONFIG>.rc")
file(GENERATE OUTPUT "${RIZOMUV_INSTALLER_PAYLOAD_RC}" CONTENT
"201 RCDATA \"$<TARGET_FILE:RizomUVChineseLauncher>\"
202 RCDATA \"$<TARGET_FILE:RizomUVChineseRuntime>\"
203 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/plugin/translations/ui_zh-CN.json\"
")
target_sources(RizomUVChineseInstaller PRIVATE
  "${RIZOMUV_INSTALLER_PAYLOAD_RC}")
# Ninja may otherwise compile the generated resource while the payload
# binaries are still being linked.  Resource compilation needs the files,
# not merely target-level ordering.
set_source_files_properties("${RIZOMUV_INSTALLER_PAYLOAD_RC}" PROPERTIES
  OBJECT_DEPENDS "$<TARGET_FILE:RizomUVChineseLauncher>;$<TARGET_FILE:RizomUVChineseRuntime>")
add_dependencies(RizomUVChineseInstaller RizomUVChineseLauncher RizomUVChineseRuntime)

if(MSVC)
  target_compile_options(RizomUVChineseRuntime PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(RizomUVChineseLauncher PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(RizomUVChineseInstaller PRIVATE /W4 /permissive- /utf-8)
  target_link_options(RizomUVChineseInstaller PRIVATE
    "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()

add_custom_command(TARGET RizomUVChineseRuntime POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/plugin/translations/ui_zh-CN.json"
    "$<TARGET_FILE_DIR:RizomUVChineseRuntime>/ui_zh-CN.json")

add_custom_target(RizomUVOneClickPackage
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/dist"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:RizomUVChineseInstaller>"
    "${CMAKE_CURRENT_SOURCE_DIR}/dist/安装RizomUV汉化.exe"
  DEPENDS RizomUVChineseInstaller RizomUVChineseLauncher RizomUVChineseRuntime
  VERBATIM)
