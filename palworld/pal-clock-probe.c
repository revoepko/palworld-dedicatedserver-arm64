#define _GNU_SOURCE

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int64_t timespec_to_ns(const struct timespec *value)
{
    return (int64_t)value->tv_sec * 1000000000LL + value->tv_nsec;
}

static long raw_syscall1(long number, long first)
{
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"(number), "D"(first)
        : "rcx", "r11", "memory");
    return result;
}

static long raw_syscall2(long number, long first, long second)
{
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"(number), "D"(first), "S"(second)
        : "rcx", "r11", "memory");
    return result;
}

static int raw_clock_gettime(clockid_t clock_id, struct timespec *value)
{
    long result = raw_syscall2(SYS_clock_gettime, (long)clock_id, (long)value);

    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}

static int raw_gettimeofday(struct timeval *value)
{
    long result = raw_syscall2(SYS_gettimeofday, (long)value, 0);

    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}

static long probe_iterations(void)
{
    const char *value = getenv("PAL_CLOCK_PROBE_ITERATIONS");
    char *end = NULL;
    long parsed;

    if (value == NULL || *value == '\0') {
        return 1;
    }
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < 1 || parsed > 10000000) {
        fprintf(stderr, "invalid PAL_CLOCK_PROBE_ITERATIONS: %s\n", value);
        exit(2);
    }
    return parsed;
}

int main(void)
{
    struct timespec realtime_first;
    struct timespec realtime_second;
    struct timespec monotonic;
    struct timespec direct_realtime;
    struct timespec instruction_realtime;
    struct timeval wall;
    struct timeval direct_wall;
    struct timeval instruction_wall;
    time_t seconds;
    time_t direct_seconds;
    time_t instruction_seconds;
    long iterations = probe_iterations();
    long negative_count = 0;
    long i;
    int64_t libc_to_syscall_delta_ns;
    int64_t syscall_to_instruction_delta_ns;
    int64_t instruction_to_libc_delta_ns;
    int strict = getenv("PAL_CLOCK_PROBE_STRICT") != NULL
        && strcmp(getenv("PAL_CLOCK_PROBE_STRICT"), "0") != 0;

    if (clock_gettime(CLOCK_REALTIME, &realtime_first) != 0
        || clock_gettime(CLOCK_MONOTONIC_RAW, &monotonic) != 0
        || gettimeofday(&wall, NULL) != 0) {
        perror("clock probe");
        return 1;
    }
    seconds = time(NULL);
    if (syscall(SYS_clock_gettime, CLOCK_REALTIME, &direct_realtime) != 0
        || syscall(SYS_gettimeofday, &direct_wall, NULL) != 0) {
        perror("direct clock probe");
        return 1;
    }
    if (raw_clock_gettime(CLOCK_REALTIME, &instruction_realtime) != 0
        || raw_gettimeofday(&instruction_wall) != 0) {
        perror("instruction clock probe");
        return 1;
    }
    direct_seconds = (time_t)syscall(SYS_time, NULL);
    if (direct_seconds == (time_t)-1) {
        perror("direct time probe");
        return 1;
    }
    instruction_seconds = (time_t)raw_syscall1(SYS_time, 0);
    if (instruction_seconds == (time_t)-1) {
        perror("instruction time probe");
        return 1;
    }
    usleep(50000);
    if (clock_gettime(CLOCK_REALTIME, &realtime_second) != 0) {
        perror("clock probe second read");
        return 1;
    }

    libc_to_syscall_delta_ns =
        timespec_to_ns(&direct_realtime) - timespec_to_ns(&realtime_first);
    syscall_to_instruction_delta_ns =
        timespec_to_ns(&instruction_realtime) - timespec_to_ns(&direct_realtime);
    instruction_to_libc_delta_ns =
        timespec_to_ns(&realtime_second) - timespec_to_ns(&instruction_realtime);
    if (libc_to_syscall_delta_ns < 0) {
        negative_count++;
    }
    if (syscall_to_instruction_delta_ns < 0) {
        negative_count++;
    }
    if (instruction_to_libc_delta_ns < 0) {
        negative_count++;
    }

    for (i = 1; i < iterations; i++) {
        if (clock_gettime(CLOCK_REALTIME, &realtime_first) != 0
            || syscall(SYS_clock_gettime, CLOCK_REALTIME, &direct_realtime) != 0
            || raw_clock_gettime(CLOCK_REALTIME, &instruction_realtime) != 0
            || clock_gettime(CLOCK_REALTIME, &realtime_second) != 0) {
            perror("mixed clock probe");
            return 1;
        }
        if (timespec_to_ns(&direct_realtime) < timespec_to_ns(&realtime_first)) {
            negative_count++;
        }
        if (timespec_to_ns(&instruction_realtime) < timespec_to_ns(&direct_realtime)) {
            negative_count++;
        }
        if (timespec_to_ns(&realtime_second) < timespec_to_ns(&instruction_realtime)) {
            negative_count++;
        }
    }

    printf("virtual_realtime_ns=%lld\n", (long long)timespec_to_ns(&realtime_first));
    printf("virtual_delta_ns=%lld\n",
        (long long)(timespec_to_ns(&realtime_second) - timespec_to_ns(&realtime_first)));
    printf("direct_realtime_ns=%lld\n", (long long)timespec_to_ns(&direct_realtime));
    printf("instruction_realtime_ns=%lld\n",
        (long long)timespec_to_ns(&instruction_realtime));
    printf("libc_to_syscall_delta_ns=%lld\n", (long long)libc_to_syscall_delta_ns);
    printf("syscall_to_instruction_delta_ns=%lld\n",
        (long long)syscall_to_instruction_delta_ns);
    printf("instruction_to_libc_delta_ns=%lld\n",
        (long long)instruction_to_libc_delta_ns);
    printf("mixed_iterations=%ld\n", iterations);
    printf("mixed_negative_count=%ld\n", negative_count);
    printf("monotonic_raw_ns=%lld\n", (long long)timespec_to_ns(&monotonic));
    printf("gettimeofday_seconds=%lld\n", (long long)wall.tv_sec);
    printf("direct_gettimeofday_seconds=%lld\n", (long long)direct_wall.tv_sec);
    printf("instruction_gettimeofday_seconds=%lld\n",
        (long long)instruction_wall.tv_sec);
    printf("time_seconds=%lld\n", (long long)seconds);
    printf("direct_time_seconds=%lld\n", (long long)direct_seconds);
    printf("instruction_time_seconds=%lld\n", (long long)instruction_seconds);

    if (strict && negative_count != 0) {
        fprintf(stderr, "mixed clock moved backwards %ld time(s)\n", negative_count);
        return 3;
    }
    return 0;
}
