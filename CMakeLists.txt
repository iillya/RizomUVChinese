cmake_minimum_required(VERSION 3.20)
project(RizomUVChinese LANGUAGES CXX)

# Match the other localizers: build/out contains deliverable binaries while
# build/obj contains CMake/MSBuild projects, objects, caches and import libs.
set(RIZOMUV_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/out")
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

if(MSVC)
  target_compile_options(RizomUVChineseRuntime PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(RizomUVChineseLauncher PRIVATE /W4 /permissive- /utf-8)
endif()

add_custom_command(TARGET RizomUVChineseRuntime POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/translations/dictionary_zh.json"
    "$<TARGET_FILE_DIR:RizomUVChineseRuntime>/dictionary_zh.json")

add_custom_target(RizomUVOneClickPackage
  COMMAND cmd /c "${CMAKE_CURRENT_SOURCE_DIR}/source/build_inno.bat" --package-only
  DEPENDS RizomUVChineseLauncher RizomUVChineseRuntime
  COMMENT "Packaging with source/inno/package.py (Inno Setup)"
  VERBATIM)
