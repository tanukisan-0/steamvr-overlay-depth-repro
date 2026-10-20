cmake_minimum_required(VERSION 3.20)
project(steamvr_overlay_depth_repro CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(overlay_depth_repro main.cpp)
target_link_libraries(overlay_depth_repro PRIVATE d3d11)

if(MINGW)
  # MSYS2 package mingw-w64-ucrt-x86_64-openvr (openvr_mingw.hpp and the static libopenvr_api.a).
  find_path(OPENVR_INCLUDE_DIR NAMES openvr_mingw.hpp PATH_SUFFIXES openvr REQUIRED)
  find_library(OPENVR_LIBRARY NAMES libopenvr_api.a REQUIRED)
  target_include_directories(overlay_depth_repro SYSTEM PRIVATE ${OPENVR_INCLUDE_DIR})
  target_link_libraries(overlay_depth_repro PRIVATE ${OPENVR_LIBRARY})
  target_compile_definitions(overlay_depth_repro PRIVATE OPENVR_BUILD_STATIC)
  target_link_options(overlay_depth_repro PRIVATE -static)
else()
  # MSVC: set OPENVR_SDK_DIR to a checkout of https://github.com/ValveSoftware/openvr
  set(OPENVR_SDK_DIR "" CACHE PATH "Path to the OpenVR SDK")
  if(NOT OPENVR_SDK_DIR)
    message(FATAL_ERROR "Set OPENVR_SDK_DIR to the OpenVR SDK folder (with headers/, lib/ and bin/).")
  endif()
  target_include_directories(overlay_depth_repro PRIVATE ${OPENVR_SDK_DIR}/headers)
  target_link_libraries(overlay_depth_repro PRIVATE ${OPENVR_SDK_DIR}/lib/win64/openvr_api.lib)
  add_custom_command(TARGET overlay_depth_repro POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENVR_SDK_DIR}/bin/win64/openvr_api.dll
            $<TARGET_FILE_DIR:overlay_depth_repro>)
endif()
