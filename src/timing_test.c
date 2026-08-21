#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

static inline uint64_t read_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000UL + ts.tv_nsec;
}

int main() {
    uint64_t start, end, diff;
    uint64_t min = 1000000000, max = 0, total = 0;
    int loops = 1000;

    for (int i = 0; i < loops; i++) {
        start = read_ns();
        syscall(SYS_getpid);
        end = read_ns();
        
        diff = end - start;
        if (diff < min) min = diff;
        if (diff > max) max = diff;
        total += diff;
    }

    printf("[*] 测试完成，循环 %d 次\n", loops);
    printf("[*] 最短耗时: %lu 纳秒\n", min);
    printf("[*] 最长耗时: %lu 纳秒\n", max);
    printf("[*] 平均耗时: %lu 纳秒\n", total / loops);
    printf("[*] 波动幅度 (最大 - 最小): %lu 纳秒\n", max - min);
    return 0;
}