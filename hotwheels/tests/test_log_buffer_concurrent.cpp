#include "io/log_buffer.h"

#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

int main() {
    std::fputs("THREAD_SANITIZER_TEST_STARTED\n", stderr);
    std::fflush(stderr);
    constexpr int writers = 4;
    constexpr int lines = 1000;
    std::mutex sink_mutex;
    std::vector<std::string> chunks;
    hw::io::LogBuffer log([&](const std::string& batch) {
        std::lock_guard<std::mutex> lock(sink_mutex);
        chunks.push_back(batch);
    }, 4096, 250, 0);

    std::vector<std::thread> producers;
    for (int writer = 0; writer < writers; ++writer) {
        producers.emplace_back([&, writer] {
            for (int line = 0; line < lines; ++line) {
                char record[64];
                std::snprintf(record, sizeof record, "FATAL writer=%d line=%04d\n", writer, line);
                log.write(record, static_cast<uint32_t>(line));
            }
        });
    }
    for (auto& producer : producers) producer.join();
    log.flush();

    std::vector<std::vector<bool>> seen(writers, std::vector<bool>(lines, false));
    int records = 0;
    for (const auto& chunk : chunks) {
        size_t start = 0;
        while (start < chunk.size()) {
            size_t end = chunk.find('\n', start);
            if (end == std::string::npos) return 1;
            int writer = -1, line = -1;
            const std::string record = chunk.substr(start, end - start);
            if (std::sscanf(record.c_str(), "FATAL writer=%d line=%d", &writer, &line) != 2 ||
                writer < 0 || writer >= writers || line < 0 || line >= lines ||
                seen[writer][line]) return 1;
            seen[writer][line] = true;
            ++records;
            start = end + 1;
        }
    }
    if (records != writers * lines) return 1;
    for (const auto& writer : seen)
        for (bool found : writer) if (!found) return 1;
    std::puts("LogBuffer concurrent producers: PASS");
    return 0;
}
