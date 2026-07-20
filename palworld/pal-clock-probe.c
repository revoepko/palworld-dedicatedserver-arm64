#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
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

struct thread_probe_result {
    long negative_count;
    long zero_count;
    int error_number;
};

struct thread_probe_context {
    long iterations;
    struct thread_probe_result *result;
};

static long probe_value(const char *name, long default_value, long maximum)
{
    const char *value = getenv(name);
    char *end = NULL;
    long parsed;

    if (value == NULL || *value == '\0') {
        return default_value;
    }
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < 1 || parsed > maximum) {
        fprintf(stderr, "invalid %s: %s\n", name, value);
        exit(2);
    }
    return parsed;
}

static void count_delta(int64_t current, int64_t previous, long *negative, long *zero)
{
    if (current < previous) {
        (*negative)++;
    } else if (current == previous) {
        (*zero)++;
    }
}

static void *thread_probe(void *argument)
{
    struct thread_probe_context *context = argument;
    struct thread_probe_result *result = context->result;
    struct timespec libc_before;
    struct timespec direct;
    struct timespec instruction;
    struct timespec libc_after;
    long i;

    for (i = 0; i < context->iterations; i++) {
        if (clock_gettime(CLOCK_REALTIME, &libc_before) != 0
            || syscall(SYS_clock_gettime, CLOCK_REALTIME, &direct) != 0
            || raw_clock_gettime(CLOCK_REALTIME, &instruction) != 0
            || clock_gettime(CLOCK_REALTIME, &libc_after) != 0) {
            result->error_number = errno != 0 ? errno : EIO;
            return NULL;
        }
        count_delta(timespec_to_ns(&direct), timespec_to_ns(&libc_before),
            &result->negative_count, &result->zero_count);
        count_delta(timespec_to_ns(&instruction), timespec_to_ns(&direct),
            &result->negative_count, &result->zero_count);
        count_delta(timespec_to_ns(&libc_after), timespec_to_ns(&instruction),
            &result->negative_count, &result->zero_count);
    }
    return NULL;
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
    long iterations = probe_value("PAL_CLOCK_PROBE_ITERATIONS", 1, 10000000);
    long thread_count = probe_value("PAL_CLOCK_PROBE_THREADS", 4, 64);
    long thread_iterations = probe_value(
        "PAL_CLOCK_PROBE_THREAD_ITERATIONS", iterations, 10000000);
    long negative_count = 0;
    long zero_count = 0;
    long thread_negative_count = 0;
    long thread_zero_count = 0;
    long i;
    int thread_error;
    pthread_t *threads;
    struct thread_probe_context *thread_contexts;
    struct thread_probe_result *thread_results;
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
    } else if (libc_to_syscall_delta_ns == 0) {
        zero_count++;
    }
    if (syscall_to_instruction_delta_ns < 0) {
        negative_count++;
    } else if (syscall_to_instruction_delta_ns == 0) {
        zero_count++;
    }
    if (instruction_to_libc_delta_ns < 0) {
        negative_count++;
    } else if (instruction_to_libc_delta_ns == 0) {
        zero_count++;
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
        } else if (timespec_to_ns(&direct_realtime) == timespec_to_ns(&realtime_first)) {
            zero_count++;
        }
        if (timespec_to_ns(&instruction_realtime) < timespec_to_ns(&direct_realtime)) {
            negative_count++;
        } else if (timespec_to_ns(&instruction_realtime) == timespec_to_ns(&direct_realtime)) {
            zero_count++;
        }
        if (timespec_to_ns(&realtime_second) < timespec_to_ns(&instruction_realtime)) {
            negative_count++;
        } else if (timespec_to_ns(&realtime_second) == timespec_to_ns(&instruction_realtime)) {
            zero_count++;
        }
    }

    threads = calloc((size_t)thread_count, sizeof(*threads));
    thread_contexts = calloc((size_t)thread_count, sizeof(*thread_contexts));
    thread_results = calloc((size_t)thread_count, sizeof(*thread_results));
    if (threads == NULL || thread_contexts == NULL || thread_results == NULL) {
        fprintf(stderr, "failed to allocate thread probe state\n");
        free(threads);
        free(thread_contexts);
        free(thread_results);
        return 1;
    }
    for (i = 0; i < thread_count; i++) {
        thread_contexts[i].iterations = thread_iterations;
        thread_contexts[i].result = &thread_results[i];
        thread_error = pthread_create(&threads[i], NULL, thread_probe, &thread_contexts[i]);
        if (thread_error != 0) {
            errno = thread_error;
            perror("pthread_create");
            return 1;
        }
    }
    for (i = 0; i < thread_count; i++) {
        thread_error = pthread_join(threads[i], NULL);
        if (thread_error != 0) {
            errno = thread_error;
            perror("pthread_join");
            return 1;
        }
        if (thread_results[i].error_number != 0) {
            errno = thread_results[i].error_number;
            perror("thread clock probe");
            return 1;
        }
        thread_negative_count += thread_results[i].negative_count;
        thread_zero_count += thread_results[i].zero_count;
    }
    free(threads);
    free(thread_contexts);
    free(thread_results);

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
    printf("mixed_zero_count=%ld\n", zero_count);
    printf("thread_count=%ld\n", thread_count);
    printf("thread_iterations=%ld\n", thread_iterations);
    printf("thread_negative_count=%ld\n", thread_negative_count);
    printf("thread_zero_count=%ld\n", thread_zero_count);
    printf("monotonic_raw_ns=%lld\n", (long long)timespec_to_ns(&monotonic));
    printf("gettimeofday_seconds=%lld\n", (long long)wall.tv_sec);
    printf("direct_gettimeofday_seconds=%lld\n", (long long)direct_wall.tv_sec);
    printf("instruction_gettimeofday_seconds=%lld\n",
        (long long)instruction_wall.tv_sec);
    printf("time_seconds=%lld\n", (long long)seconds);
    printf("direct_time_seconds=%lld\n", (long long)direct_seconds);
    printf("instruction_time_seconds=%lld\n", (long long)instruction_seconds);

    if (strict && (negative_count != 0 || zero_count != 0
            || thread_negative_count != 0 || thread_zero_count != 0)) {
        fprintf(stderr,
            "clock was non-increasing: mixed_negative=%ld mixed_zero=%ld "
            "thread_negative=%ld thread_zero=%ld\n",
            negative_count, zero_count, thread_negative_count, thread_zero_count);
        return 3;
    }
    return 0;
}
