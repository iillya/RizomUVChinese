cmake_minimum_required(VERSION 3.20)
project(RizomUVChinese LANGUAGES CXX)

set(LOCALIZER_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source")

add_library(RizomUVChineseRuntime SHARED
  source/hook.cpp
  source/translation_dictionary.cpp
  source/gdi_iat_hooks.cpp
  source/native_menu_localizer.cpp
  source/runtime_log.cpp)
target_include_directories(RizomUVChineseRuntime PRIVATE "${LOCALIZER_INCLUDE_DIR}")
target_compile_features(RizomUVChineseRuntime PRIVATE cxx_std_17)
target_compile_definitions(RizomUVChineseRuntime PRIVATE
  UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(RizomUVChineseRuntime PRIVATE user32 gdi32 shell32)
set_target_properties(RizomUVChineseRuntime PROPERTIES PREFIX "")

set(RIZOMUV_EXECUTABLE_ICON_RESOURCE
  icon/app_icon.rc)

add_executable(RizomUVChineseLauncher WIN32
  source/launcher.cpp
  ${RIZOMUV_EXECUTABLE_ICON_RESOURCE})
target_compile_features(RizomUVChineseLauncher PRIVATE cxx_std_17)
target_compile_definitions(RizomUVChineseLauncher PRIVATE
  UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(RizomUVChineseLauncher PRIVATE user32 shell32)

add_executable(RizomUVChineseInstaller WIN32
  source/installer.cpp
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
203 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/translations/dictionary_zh.json\"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/translations/dictionary_zh.json"
    "$<TARGET_FILE_DIR:RizomUVChineseRuntime>/dictionary_zh.json")

add_custom_target(RizomUVOneClickPackage
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/dist"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:RizomUVChineseInstaller>"
    "${CMAKE_CURRENT_SOURCE_DIR}/dist/安装RizomUV汉化.exe"
  DEPENDS RizomUVChineseInstaller RizomUVChineseLauncher RizomUVChineseRuntime
  VERBATIM)
