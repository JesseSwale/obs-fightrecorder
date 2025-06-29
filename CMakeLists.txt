cmake_minimum_required(VERSION 3.16...3.26)


include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})


#if(WIN32)
#    set(WIN_SDK_INCLUDE_DIR "C:/Program Files (x86)/Windows Kits/10/Include/${CMAKE_SYSTEM_VERSION}")
#    include_directories(
#        "${WIN_SDK_INCLUDE_DIR}/um"
#        "${WIN_SDK_INCLUDE_DIR}/shared"
#        "${WIN_SDK_INCLUDE_DIR}/ucrt"
#    )
#endif()

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE 
    "${CMAKE_LIBRARY_PATH}/avformat.lib"
    "${CMAKE_LIBRARY_PATH}/avutil.lib"
    "${CMAKE_LIBRARY_PATH}/avcodec.lib")

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

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_INCLUDE_PATH})


set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
