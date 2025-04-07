cmake_minimum_required(VERSION 3.16...3.26)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)


#set(FFMPEG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-2024-03-19-x64/include/")
#set(FFMPEG_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-2024-03-19-x64/lib/")

#set(FFmpeg_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-2024-03-19-x64/include/")
#set(FFmpeg_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-2024-03-19-x64/lib/")

#find_library(AVUTIL_LIBRARY NAMES avutil PATHS "${FFMPEG_LIB_DIR}" NO_DEFAULT_PATH)
#if (AVUTIL_LIBRARY)
#    message(STATUS "Found avutil library: ${AVUTIL_LIBRARY}")
#else()
#    message(FATAL_ERROR "avutil library not found!")
#endif()

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

include_directories(${FFMPEG_INCLUDE_DIR})
#target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE ${FFMPEG_LIB_DIR})

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE )

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                  -Wno-comma>)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON
               AUTOUIC ON
               AUTORCC ON)
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
