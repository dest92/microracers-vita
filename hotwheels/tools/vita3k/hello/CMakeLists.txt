cmake_minimum_required(VERSION 3.15)
project(hello_vita3k C)

if(NOT DEFINED ENV{VITASDK})
    message(FATAL_ERROR "VITASDK no esta definido; correr: export VITASDK=<repo>/shared/vitasdk")
endif()

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "HW Hello Vita3K")
set(VITA_TITLEID "HWMR00001")
set(VITA_VERSION "01.00")

add_executable(hello.elf main.c)
target_link_libraries(hello.elf
    SceDisplay_stub
    SceLibKernel_stub
    SceKernelThreadMgr_stub
)

vita_create_self(hello.self hello.elf)

vita_create_vpk(hello.vpk ${VITA_TITLEID} hello.self
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
)
