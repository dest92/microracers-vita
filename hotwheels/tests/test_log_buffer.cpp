// tests/test_log_buffer.cpp — HW-PERF: logger por lotes.
#include "io/log_buffer.h"
#include <cstdio>
static int g_fail = 0;
#define CHECK(c, m) do { if (!(c)) { std::printf("FAIL %d %s\n", __LINE__, m); ++g_fail; } } while (0)
int main() {
    std::string out; int calls = 0;
    hw::io::LogBuffer lb([&](const std::string& s) { out += s; ++calls; }, 64, 250, 0);
    lb.write("a\n", 10); lb.write("b\n", 20);
    CHECK(calls == 0, "buffered");
    lb.write("c\n", 300);                       // venció el intervalo
    CHECK(calls == 1 && out == "a\nb\nc\n", "interval flush");
    for (int i = 0; i < 40; ++i) lb.write("0123456789\n", 301);
    CHECK(calls > 1, "cap flush");              // 11 B * 6 >= 64
    int c = calls; lb.write("x\n", 302);
    CHECK(calls == c, "no flush");
    lb.write("[hw] FATAL: fault en 0x1\n", 303);
    CHECK(calls == c + 1 && out.find("FATAL") != std::string::npos, "critical inmediato");
    lb.write("tail\n", 304); lb.flush();
    CHECK(out.size() >= 5 && out.substr(out.size() - 5) == "tail\n", "flush final, orden preservado");
    CHECK(lb.flushes() == (uint64_t)calls, "contador");
    { int n = 0; hw::io::LogBuffer sy([&](const std::string&) { ++n; }, 8192, 250, 3);
      sy.write("1\n", 1); sy.write("2\n", 2); sy.write("3\n", 3);
      CHECK(n == 3, "primeras líneas síncronas");
      sy.write("4\n", 4); CHECK(n == 3, "luego por lotes"); }
    { int n = 0; hw::io::LogBuffer tk([&](const std::string&) { ++n; }, 8192, 250, 0);
      tk.write("ráfaga\n", 10);
      CHECK(!tk.tick(100) && n == 0, "tick antes del intervalo no vuelca");
      CHECK(tk.tick(300) && n == 1, "tick vuelca la cola sin esperar otra línea");
      CHECK(!tk.tick(900) && n == 1, "tick con buffer vacío no llama al sink"); }
    if (g_fail) { std::printf("LB: %d FALLAS\n", g_fail); return 1; }
    std::printf("LB: PASS\n");
}
