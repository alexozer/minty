#include <cinttypes>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using u64 = uint64_t;
using f32 = float;

int main() {
    constexpr u64 ITERS = 10'000'000;
    constexpr u64 UNIQUE_KEYS = 10'000;

    std::unordered_map<std::string, u64> map;
    std::vector<std::string> strings;

    for (u64 i = 0; i < UNIQUE_KEYS; i++) {
        std::string key = "SlightlyLongerKey" + std::to_string(i);
        strings.push_back(key);
    }

    auto start = std::chrono::steady_clock::now();
    for (u64 i = 0; i < ITERS; i++) {
        map[strings[i % strings.size()]] = i;
    }
    auto end = std::chrono::steady_clock::now();

    std::chrono::duration<f32> duration_sec = end - start;
    f32 rate = (f32)ITERS / duration_sec.count();
    printf("Iters = %" PRIu64 ", Unique Keys = %" PRIu64
           ", Duration = %.2fs, Insertion rate = %.2f/sec\n",
           ITERS, UNIQUE_KEYS, duration_sec.count(), rate);
}
