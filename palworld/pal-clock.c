#define _GNU_SOURCE

#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define NS_PER_SECOND 1000000000LL

static _Atomic int initialized = 0;
static _Atomic unsigned int trace_mask = 0;
static int64_t anchor_epoch_ns = 0;
static int64_t anchor_monotonic_ns = 0;

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

static long raw_syscall3(long number, long first, long second, long third)
{
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"(number), "D"(first), "S"(second), "d"(third)
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

static void raw_write(const char *message, size_t length)
{
    (void)raw_syscall3(SYS_write, STDERR_FILENO, (long)message, (long)length);
}

#define PAL_CLOCK_LOG_LITERAL(message) raw_write(message, sizeof(message) - 1)

static int64_t timespec_to_ns(const struct timespec *value)
{
    return (int64_t)value->tv_sec * NS_PER_SECOND + value->tv_nsec;
}

static void ns_to_timespec(int64_t value, struct timespec *result)
{
    result->tv_sec = (time_t)(value / NS_PER_SECOND);
    result->tv_nsec = (long)(value % NS_PER_SECOND);
    if (result->tv_nsec < 0) {
        result->tv_sec -= 1;
        result->tv_nsec += NS_PER_SECOND;
    }
}

static int parse_ns(const char *name, int64_t *result)
{
    const char *value = getenv(name);
    char *end = NULL;
    long long parsed;

    if (value == NULL || *value == '\0') {
        return -1;
    }

    errno = 0;
    parsed = strtoll(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed <= 0) {
        return -1;
    }

    *result = (int64_t)parsed;
    return 0;
}

static void initialize_clock(void)
{
    struct timespec realtime;
    struct timespec monotonic;

    if (atomic_load_explicit(&initialized, memory_order_acquire) != 0) {
        return;
    }

    if (parse_ns("PAL_CLOCK_ANCHOR_EPOCH_NS", &anchor_epoch_ns) == 0
        && parse_ns("PAL_CLOCK_ANCHOR_MONOTONIC_NS", &anchor_monotonic_ns) == 0) {
        PAL_CLOCK_LOG_LITERAL("[pal-clock] active: external epoch anchor + CLOCK_MONOTONIC_RAW\n");
    } else {
        if (raw_clock_gettime(CLOCK_REALTIME, &realtime) != 0
            || raw_clock_gettime(CLOCK_MONOTONIC_RAW, &monotonic) != 0) {
            PAL_CLOCK_LOG_LITERAL("[pal-clock] fatal: unable to initialize clock anchor\n");
            _exit(125);
        }
        anchor_epoch_ns = timespec_to_ns(&realtime);
        anchor_monotonic_ns = timespec_to_ns(&monotonic);
        PAL_CLOCK_LOG_LITERAL("[pal-clock] warning: external anchor missing; process-start system time used\n");
    }

    atomic_store_explicit(&initialized, 1, memory_order_release);
}

static int virtual_realtime_ns(int64_t *result)
{
    struct timespec monotonic;
    int64_t current_monotonic_ns;
    int64_t elapsed_ns;

    initialize_clock();
    if (raw_clock_gettime(CLOCK_MONOTONIC_RAW, &monotonic) != 0) {
        return -1;
    }

    current_monotonic_ns = timespec_to_ns(&monotonic);
    if (current_monotonic_ns < anchor_monotonic_ns) {
        errno = ERANGE;
        return -1;
    }

    elapsed_ns = current_monotonic_ns - anchor_monotonic_ns;
    if (anchor_epoch_ns > INT64_MAX - elapsed_ns) {
        errno = EOVERFLOW;
        return -1;
    }

    *result = anchor_epoch_ns + elapsed_ns;
    return 0;
}

static void trace_once(unsigned int bit, const char *message, size_t length)
{
    unsigned int previous = atomic_fetch_or_explicit(&trace_mask, bit, memory_order_relaxed);

    if ((previous & bit) == 0) {
        raw_write(message, length);
    }
}

int clock_gettime(clockid_t clock_id, struct timespec *value)
{
    static const char realtime_message[] =
        "[pal-clock] intercepted clock_gettime(CLOCK_REALTIME)\n";
    static const char monotonic_message[] =
        "[pal-clock] observed clock_gettime(monotonic family)\n";
    int64_t virtual_ns;

    if (clock_id == CLOCK_REALTIME
#ifdef CLOCK_REALTIME_COARSE
        || clock_id == CLOCK_REALTIME_COARSE
#endif
    ) {
        trace_once(1U, realtime_message, sizeof(realtime_message) - 1);
        if (virtual_realtime_ns(&virtual_ns) != 0) {
            return -1;
        }
        ns_to_timespec(virtual_ns, value);
        return 0;
    }

    if (clock_id == CLOCK_MONOTONIC || clock_id == CLOCK_MONOTONIC_RAW
#ifdef CLOCK_MONOTONIC_COARSE
        || clock_id == CLOCK_MONOTONIC_COARSE
#endif
#ifdef CLOCK_BOOTTIME
        || clock_id == CLOCK_BOOTTIME
#endif
    ) {
        trace_once(2U, monotonic_message, sizeof(monotonic_message) - 1);
    }

    return raw_clock_gettime(clock_id, value);
}

int __clock_gettime(clockid_t clock_id, struct timespec *value)
{
    return clock_gettime(clock_id, value);
}

int gettimeofday(struct timeval *value, void *timezone_value)
{
    static const char message[] = "[pal-clock] intercepted gettimeofday()\n";
    int64_t virtual_ns;

    if (timezone_value != NULL) {
        long result = raw_syscall2(SYS_gettimeofday, 0, (long)timezone_value);
        if (result < 0) {
            errno = (int)-result;
            return -1;
        }
    }

    trace_once(4U, message, sizeof(message) - 1);
    if (virtual_realtime_ns(&virtual_ns) != 0) {
        return -1;
    }
    value->tv_sec = (time_t)(virtual_ns / NS_PER_SECOND);
    value->tv_usec = (suseconds_t)((virtual_ns % NS_PER_SECOND) / 1000);
    return 0;
}

int __gettimeofday(struct timeval *value, void *timezone_value)
{
    return gettimeofday(value, timezone_value);
}

time_t time(time_t *result)
{
    static const char message[] = "[pal-clock] intercepted time()\n";
    int64_t virtual_ns;
    time_t value;

    trace_once(8U, message, sizeof(message) - 1);
    if (virtual_realtime_ns(&virtual_ns) != 0) {
        return (time_t)-1;
    }
    value = (time_t)(virtual_ns / NS_PER_SECOND);
    if (result != NULL) {
        *result = value;
    }
    return value;
}

time_t __time(time_t *result)
{
    return time(result);
}

__attribute__((constructor)) static void pal_clock_constructor(void)
{
    initialize_clock();
}
