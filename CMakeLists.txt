cmake_minimum_required(VERSION 3.25)

project(wasmcut LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(imgui)

if(EMSCRIPTEN)
  if(NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
      INTERFACE_LINK_LIBRARIES SDL2
      INTERFACE_COMPILE_OPTIONS "-sUSE_SDL=2"
    )
  endif()
  set(WASMCUT_SDL2_TARGET SDL2::SDL2)
else()
  find_package(SDL2 REQUIRED)
  if(TARGET SDL2::SDL2)
    set(WASMCUT_SDL2_TARGET SDL2::SDL2)
  elseif(TARGET SDL2)
    set(WASMCUT_SDL2_TARGET SDL2)
  else()
    message(FATAL_ERROR "SDL2 target was not found")
  endif()
endif()

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_compile_features(imgui PUBLIC cxx_std_20)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

add_library(imgui_backends STATIC
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_link_libraries(imgui_backends PUBLIC imgui ${WASMCUT_SDL2_TARGET})
target_compile_features(imgui_backends PUBLIC cxx_std_20)

add_library(wasmcut_model STATIC
  src/model/project.cpp
  src/model/track.cpp
)

target_include_directories(wasmcut_model PUBLIC src)
target_compile_features(wasmcut_model PUBLIC cxx_std_20)

add_executable(wasmcut
  src/main.cpp
  src/platform/platform_bridge.cpp
)

target_include_directories(wasmcut PRIVATE src)
target_link_libraries(wasmcut PRIVATE imgui imgui_backends wasmcut_model ${WASMCUT_SDL2_TARGET})
target_compile_features(wasmcut PRIVATE cxx_std_20)

if(EMSCRIPTEN)
  target_link_libraries(wasmcut PRIVATE embind)
  set_target_properties(wasmcut PROPERTIES
    OUTPUT_NAME "wasmcut"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/web/generated"
    SUFFIX ".js"
  )

  target_link_options(wasmcut PRIVATE
    "-sUSE_SDL=2"
    "-sUSE_WEBGL2=1"
    "-sFULL_ES3=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sFORCE_FILESYSTEM=1"
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sEXPORT_NAME=createWasmcutModule"
    "-sEXPORTED_RUNTIME_METHODS=cwrap"
    "-sEXPORTED_FUNCTIONS=_main,_wasmcut_set_status,_wasmcut_set_media_info,_wasmcut_set_playback_time,_wasmcut_set_playback_state,_wasmcut_set_progress,_wasmcut_set_viewport_size,_wasmcut_add_media_to_timeline"
    "-sENVIRONMENT=web"
    "-sEXIT_RUNTIME=0"
  )

  add_executable(model_tests tests/model_tests.cpp)
  target_link_libraries(model_tests PRIVATE wasmcut_model)
  target_compile_features(model_tests PRIVATE cxx_std_20)
  set_target_properties(model_tests PROPERTIES
    OUTPUT_NAME "model_tests"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    SUFFIX ".cjs"
  )
  target_link_options(model_tests PRIVATE
    "-sENVIRONMENT=node"
    "-sEXIT_RUNTIME=1"
  )
endif()
