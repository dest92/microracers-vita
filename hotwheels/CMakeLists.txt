cmake_minimum_required(VERSION 3.24)
project(hotwheels_vita LANGUAGES C CXX)

# Build HOST (tests). El build Vita NO pasa por acá: ver tools/build_vita.sh.
# winx86 se referencia desde D2Vita (no se copia). Spec: specs/001-skeleton.md.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(WINX86_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../d2vita/D2Vita/third_party/winx86"
    CACHE PATH "Raíz del motor winx86")
if(NOT EXISTS "${WINX86_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "winx86 no encontrado en ${WINX86_DIR}")
endif()
add_subdirectory("${WINX86_DIR}" winx86 EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../winx86-dx" winx86_dx)

# hw_boot: carga del PE + cableado del Bridge hasta el entry point (portable).
add_library(hw_boot STATIC
  src/boot/pe_file.cpp
  src/boot/boot.cpp
  src/runtime/hw_layout.cpp)
target_include_directories(hw_boot PUBLIC src "${WINX86_DIR}/src")

# Piezas portables de winx86 que winx86_core no incluye pero bridge.cpp usa.
# Referencias cíclicas entre archivos estáticos -> grupo con re-escaneo.
add_library(winx86_host_extra STATIC
  "${WINX86_DIR}/src/runtime/cpu.cpp"
  "${WINX86_DIR}/src/runtime/gil.cpp")
target_include_directories(winx86_host_extra PUBLIC "${WINX86_DIR}/src")
target_link_libraries(hw_boot PUBLIC
  "$<LINK_GROUP:RESCAN,winx86_core,winx86_host_extra>")

enable_testing()
add_executable(test_skeleton tests/test_skeleton.cpp tests/host_stubs.cpp)
target_link_libraries(test_skeleton PRIVATE hw_boot)
target_compile_definitions(test_skeleton PRIVATE
  HW_EXTRACTED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/extracted")
add_test(NAME skeleton COMMAND test_skeleton)
set_tests_properties(skeleton PROPERTIES SKIP_RETURN_CODE 77)

# HW-013: núcleo de superficies, adaptador COM y backend PNG host.
add_library(hw_ddraw STATIC
  src/ddraw/bridge.cpp
  src/ddraw/png.cpp)
target_include_directories(hw_ddraw PUBLIC src "${WINX86_DIR}/src")
target_link_libraries(hw_ddraw PUBLIC winx86_dx)
add_executable(test_ddraw tests/test_ddraw.cpp)
target_link_libraries(test_ddraw PRIVATE hw_ddraw)
add_test(NAME ddraw COMMAND test_ddraw)
add_executable(test_winx86_dx_ddraw tests/test_winx86_dx_ddraw.cpp)
target_link_libraries(test_winx86_dx_ddraw PRIVATE winx86_dx)
add_test(NAME winx86_dx_ddraw COMMAND test_winx86_dx_ddraw)
add_executable(test_winx86_dx_d3d tests/test_winx86_dx_d3d.cpp)
target_link_libraries(test_winx86_dx_d3d PRIVATE winx86_dx)
add_test(NAME winx86_dx_d3d COMMAND test_winx86_dx_d3d)
add_executable(test_winx86_dx_shims tests/test_winx86_dx_shims.cpp)
target_link_libraries(test_winx86_dx_shims PRIVATE winx86_dx)
add_test(NAME winx86_dx_shims COMMAND test_winx86_dx_shims)
add_executable(test_ddraw_bridge tests/test_ddraw_bridge.cpp tests/host_stubs.cpp)
target_link_libraries(test_ddraw_bridge PRIVATE hw_ddraw hw_boot hw_d3d)
add_test(NAME ddraw_bridge COMMAND test_ddraw_bridge)
add_test(NAME ddraw_bridge_sized COMMAND test_ddraw_bridge sized)
add_executable(test_dinput_bridge tests/test_dinput_bridge.cpp tests/host_stubs.cpp
  src/dinput/dinput.cpp src/integration/dinput_bridge.cpp src/integration/scripted_input.cpp)
target_link_libraries(test_dinput_bridge PRIVATE hw_boot)
add_test(NAME dinput_bridge COMMAND test_dinput_bridge)
set_tests_properties(ddraw PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(test_runtime tests/test_runtime.cpp tests/host_stubs.cpp)
target_link_libraries(test_runtime PRIVATE hw_boot)
add_test(NAME runtime COMMAND test_runtime)
add_executable(test_runtime_pmu tests/test_runtime_pmu.cpp)
target_include_directories(test_runtime_pmu PRIVATE "${WINX86_DIR}/src")
add_test(NAME runtime_pmu COMMAND test_runtime_pmu)
add_executable(test_watchdog_shutdown_gil tests/test_watchdog_shutdown_gil.cpp)
target_link_libraries(test_watchdog_shutdown_gil PRIVATE hw_boot)
add_test(NAME watchdog_shutdown_gil COMMAND test_watchdog_shutdown_gil)
add_executable(test_release_profile tests/test_release_profile.cpp)
target_include_directories(test_release_profile PRIVATE src)
add_test(NAME release_profile COMMAND test_release_profile)
add_executable(test_heartbeat tests/test_heartbeat.cpp)
target_include_directories(test_heartbeat PRIVATE src)
add_test(NAME heartbeat COMMAND test_heartbeat)
add_executable(test_vgl_pools tests/test_vgl_pools.cpp)
target_include_directories(test_vgl_pools PRIVATE src)
add_test(NAME vgl_pools COMMAND test_vgl_pools)
# HW-072 (SPEC-055 CA2): muestreo del dt del invitado desde el host.
add_executable(test_guest_dt_profile tests/test_guest_dt_profile.cpp src/runtime/guest_dt_profile.cpp)
target_include_directories(test_guest_dt_profile PRIVATE src)
add_test(NAME guest_dt_profile COMMAND test_guest_dt_profile)
add_executable(test_hw_intrin tests/test_hw_intrin.cpp src/runtime/hw_intrin.cpp src/hle/span_ft3.c tests/intrin_stub.c)
target_include_directories(test_hw_intrin PRIVATE src ${WINX86_DIR}/src/dynarec86)
add_test(NAME hw_intrin COMMAND test_hw_intrin)
add_executable(test_fist64_fast tests/test_fist64_fast.c)
target_link_libraries(test_fist64_fast PRIVATE m)
add_test(NAME fist64_fast COMMAND test_fist64_fast)
add_executable(test_div32_fast tests/test_div32_fast.c)
add_test(NAME div32_fast COMMAND test_div32_fast)

# HW-006 (re): shims base (spec 006). install.cpp usa servicios de winx86.
add_library(hw_shims STATIC src/io/read_stream.cpp src/shims/core.cpp src/shims/shims.cpp src/shims/install.cpp)
target_include_directories(hw_shims PUBLIC src "${WINX86_DIR}/src")
target_link_libraries(hw_shims PUBLIC hw_boot)
add_executable(test_shims tests/test_shims.cpp tests/host_stubs.cpp)
target_link_libraries(test_shims PRIVATE hw_shims)
add_test(NAME shims COMMAND test_shims)
target_compile_definitions(test_shims PRIVATE HW_SHIMS_LINK_INSTALL=1)

# HW-010 (re): renderer D3D6 (spec 003): núcleo + rasterizador de referencia.
add_library(hw_d3d STATIC src/d3d/core.cpp src/d3d/refraster.cpp src/d3d/bridge.cpp src/d3d/frame_queue.cpp
  src/fps/interp60.cpp)
target_include_directories(hw_d3d PUBLIC src "${WINX86_DIR}/src")
target_link_libraries(hw_d3d PUBLIC hw_boot winx86_dx)
target_compile_definitions(hw_d3d PRIVATE HW_D3D_LINK_WINX86_DX=1)
add_executable(test_d3d tests/test_d3d.cpp)
target_link_libraries(test_d3d PRIVATE hw_d3d)
add_test(NAME d3d COMMAND test_d3d WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(test_d3d_frame_queue tests/test_d3d_frame_queue.cpp)
target_link_libraries(test_d3d_frame_queue PRIVATE hw_d3d)
add_test(NAME d3d_frame_queue COMMAND test_d3d_frame_queue)
add_executable(test_d3d_bridge tests/test_d3d_bridge.cpp tests/host_stubs.cpp)
target_link_libraries(test_d3d_bridge PRIVATE hw_d3d)
add_test(NAME d3d_bridge COMMAND test_d3d_bridge)
add_executable(test_image_ring tests/test_image_ring.cpp)
target_link_libraries(test_image_ring PRIVATE hw_d3d)
add_test(NAME image_ring COMMAND test_image_ring)
add_executable(image_ring_replay tools/image_ring_replay.cpp)
target_link_libraries(image_ring_replay PRIVATE hw_d3d)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME image_ring_replay_input
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_image_ring_replay.py")
# El binario es un insumo externo: se inyecta por env para que la suite se
# salte (no falle) cuando se corre sin build (BUG-049).
set_tests_properties(image_ring_replay_input PROPERTIES
  ENVIRONMENT "HW_IMAGE_RING_REPLAY=$<TARGET_FILE:image_ring_replay>")
add_executable(test_interp60 tests/test_interp60.cpp)
target_link_libraries(test_interp60 PRIVATE hw_d3d)
add_test(NAME interp60 COMMAND test_interp60)
add_executable(test_fps_runtime tests/test_fps_runtime.cpp)
target_include_directories(test_fps_runtime PRIVATE src)
add_test(NAME fps_runtime COMMAND test_fps_runtime)
