cmake_minimum_required(VERSION 3.19)
project(hypr-edgehover VERSION "0.1.0")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
include(GNUInstallDirs)

add_compile_definitions(WLR_USE_UNSTABLE)
add_compile_options(
  -g
  -Wall
  -Wextra
  -Wno-unused-parameter
  -Wno-unused-value
  -Wno-missing-field-initializers
  -Wno-narrowing
  -Wno-pointer-arith
  --no-gnu-unique
)

if(CMAKE_EXPORT_COMPILE_COMMANDS)
  set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
      ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPR_DEPS REQUIRED hyprland)

add_library(hypr-edgehover SHARED
  src/main.cpp
  src/edgehover.cpp
  src/geometry.cpp
)

target_include_directories(hypr-edgehover PRIVATE ${HYPR_DEPS_INCLUDE_DIRS} src)

foreach(inc ${HYPR_DEPS_INCLUDE_DIRS})
  if(EXISTS "${inc}/hyprland/src")
    target_include_directories(hypr-edgehover PRIVATE "${inc}/hyprland/src")
  endif()
endforeach()

target_link_directories(hypr-edgehover PRIVATE ${HYPR_DEPS_LIBRARY_DIRS})
target_link_libraries(hypr-edgehover PRIVATE ${HYPR_DEPS_LIBRARIES})

if(BUILD_TESTING)
  add_executable(hypr-edgehover-geometry-test
    tools/geometry_test.cpp
    src/geometry.cpp
  )

  target_include_directories(hypr-edgehover-geometry-test PRIVATE src)
  add_test(NAME hypr-edgehover-geometry-test COMMAND hypr-edgehover-geometry-test)
endif()

install(TARGETS hypr-edgehover LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
