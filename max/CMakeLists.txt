# TopoWeave 3ds Max plug-in (.dlm). Windows + Visual Studio 2022 + 3ds Max SDK.
#
#   cmake -S . -B build-max -G "Visual Studio 17 2022" -A x64 ^
#         -DTOPOWEAVE_BUILD_MAX=ON -DMAXSDK_DIR="C:/Program Files/Autodesk/3ds Max 2025 SDK/maxsdk"
#   cmake --build build-max --config Release --target TopoWeave
if(NOT WIN32)
  message(FATAL_ERROR "The 3ds Max plug-in can only be built on Windows")
endif()

set(MAXSDK_DIR "" CACHE PATH "Path to the 3ds Max SDK folder that contains include/ and lib/")
if(NOT MAXSDK_DIR)
  foreach(ver 2027 2026 2025 2024 2023)
    if(DEFINED ENV{ADSK_3DSMAX_SDK_${ver}})
      set(MAXSDK_DIR "$ENV{ADSK_3DSMAX_SDK_${ver}}/maxsdk")
      break()
    endif()
  endforeach()
endif()
if(NOT EXISTS "${MAXSDK_DIR}/include/max.h")
  message(FATAL_ERROR "3ds Max SDK not found. Set MAXSDK_DIR to the 'maxsdk' folder (contains include/max.h).")
endif()
message(STATUS "Using 3ds Max SDK: ${MAXSDK_DIR}")

set(MAX_PLUGIN_DIR "" CACHE PATH "Optional: copy the built .dlm into this 3ds Max plugins folder")

add_library(TopoWeave SHARED
  src/DllEntry.cpp
  src/Params.cpp
  src/TopoWeaveMod.cpp
  src/Tools.cpp
  src/Palette.cpp
  src/Actions.cpp
  src/TopoWeave.rc
  src/TopoWeave.def
)

target_include_directories(TopoWeave PRIVATE src "${MAXSDK_DIR}/include")
target_link_directories(TopoWeave PRIVATE "${MAXSDK_DIR}/lib/x64/Release")
target_link_libraries(TopoWeave PRIVATE
  topoweave_core
  core geom gfx mesh maxutil maxscrpt paramblk2 mnmath poly bmm
  comctl32
)
target_compile_definitions(TopoWeave PRIVATE UNICODE _UNICODE _USRDLL WIN32 _WINDOWS)
# Max SDK headers are not /permissive- clean and are noisy at /W4.
target_compile_options(TopoWeave PRIVATE /permissive /W3 /MP)
set_target_properties(TopoWeave PROPERTIES
  SUFFIX ".dlm"
  MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
)

if(MAX_PLUGIN_DIR)
  add_custom_command(TARGET TopoWeave POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:TopoWeave>" "${MAX_PLUGIN_DIR}"
    COMMENT "Copying TopoWeave.dlm to ${MAX_PLUGIN_DIR}")
endif()
