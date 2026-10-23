// host_stubs.cpp — símbolos que bridge.cpp toma del backend ARM
// (cpu_box86.cpp / sched_native.cpp) y que la tranche host winx86_core no
// trae. Sólo para tests host: en Vita los define libwinx86_vita.a.
#include <cstdint>
extern "C" {
int d2rt_wakeprof = 0;
uint32_t d2rt_timeprof_base = 0;
}
