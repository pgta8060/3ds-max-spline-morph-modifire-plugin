cmake_minimum_required(VERSION 3.20)
project(TopoWeave VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TOPOWEAVE_BUILD_TESTS "Build the core unit tests" ON)
option(TOPOWEAVE_BUILD_DEMO "Build the offline demo / renderer" ON)
option(TOPOWEAVE_BUILD_MAX "Build the 3ds Max plug-in (Windows + 3ds Max SDK)" OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- SDK independent geometry engine --------------------------------------
add_library(topoweave_core STATIC
  core/src/Surface.cpp
  core/src/Polyline.cpp
  core/src/MonotoneMap.cpp
  core/src/PolyMesh.cpp
  core/src/MeshOps.cpp
  core/src/Stroke.cpp
  core/src/Bridge.cpp
)
target_include_directories(topoweave_core PUBLIC core/include)
set_target_properties(topoweave_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  # The Max plug-in links the dynamic CRT; keep the core consistent.
  set_property(TARGET topoweave_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(TOPOWEAVE_BUILD_TESTS OR TOPOWEAVE_BUILD_DEMO)
  add_library(topoweave_testkit STATIC tools/common/Shapes.cpp tools/common/Render.cpp tools/common/ObjIO.cpp)
  target_include_directories(topoweave_testkit PUBLIC tools/common)
  target_link_libraries(topoweave_testkit PUBLIC topoweave_core)
  find_package(ZLIB QUIET)
  if(ZLIB_FOUND)
    target_compile_definitions(topoweave_testkit PRIVATE TOPOWEAVE_HAVE_ZLIB)
    target_link_libraries(topoweave_testkit PRIVATE ZLIB::ZLIB)
  endif()
endif()

if(TOPOWEAVE_BUILD_TESTS)
  enable_testing()
  add_executable(topoweave_tests
    tests/main.cpp
    tests/test_math.cpp
    tests/test_surface.cpp
    tests/test_polyline.cpp
    tests/test_polymesh.cpp
    tests/test_stroke.cpp
    tests/test_bridge.cpp
  )
  target_link_libraries(topoweave_tests PRIVATE topoweave_testkit)
  add_test(NAME topoweave_tests COMMAND topoweave_tests)
endif()

if(TOPOWEAVE_BUILD_DEMO)
  add_executable(topoweave_demo tools/demo/demo.cpp)
  target_link_libraries(topoweave_demo PRIVATE topoweave_testkit)
endif()

if(TOPOWEAVE_BUILD_MAX)
  add_subdirectory(max)
endif()
