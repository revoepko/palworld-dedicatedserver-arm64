#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define NTP_PACKET_SIZE 48
#define NTP_UNIX_EPOCH_DELTA 2208988800ULL
#define NS_PER_SECOND 1000000000LL

struct sample {
    int64_t epoch_ns;
    int64_t monotonic_ns;
};

static int64_t timespec_to_ns(const struct timespec *value)
{
    return (int64_t)value->tv_sec * NS_PER_SECOND + value->tv_nsec;
}

static uint32_t read_be32(const unsigned char *value)
{
    uint32_t result;
    memcpy(&result, value, sizeof(result));
    return ntohl(result);
}

static int query_server(const char *hostname, struct sample *result)
{
    struct addrinfo hints = {0};
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    unsigned char packet[NTP_PACKET_SIZE] = {0};
    struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};
    struct timespec started;
    struct timespec finished;
    int status;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    status = getaddrinfo(hostname, "123", &hints, &addresses);
    if (status != 0) {
        fprintf(stderr, "[pal-clock-anchor] %s DNS failed: %s\n", hostname, gai_strerror(status));
        return -1;
    }

    packet[0] = 0x23;
    for (address = addresses; address != NULL; address = address->ai_next) {
        int socket_fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        ssize_t received;
        uint32_t seconds;
        uint32_t fraction;
        int64_t round_trip_ns;
        uint64_t unix_seconds;

        if (socket_fd < 0) {
            continue;
        }
        (void)setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (connect(socket_fd, address->ai_addr, address->ai_addrlen) != 0) {
            close(socket_fd);
            continue;
        }

        if (clock_gettime(CLOCK_MONOTONIC_RAW, &started) != 0
            || send(socket_fd, packet, sizeof(packet), 0) != (ssize_t)sizeof(packet)) {
            close(socket_fd);
            continue;
        }

        received = recv(socket_fd, packet, sizeof(packet), 0);
        status = clock_gettime(CLOCK_MONOTONIC_RAW, &finished);
        close(socket_fd);
        if (received < NTP_PACKET_SIZE || status != 0) {
            continue;
        }

        if ((packet[0] & 0x07) != 4 || packet[1] == 0 || packet[1] >= 16) {
            continue;
        }

        seconds = read_be32(packet + 40);
        fraction = read_be32(packet + 44);
        if ((uint64_t)seconds <= NTP_UNIX_EPOCH_DELTA) {
            continue;
        }

        unix_seconds = (uint64_t)seconds - NTP_UNIX_EPOCH_DELTA;
        round_trip_ns = timespec_to_ns(&finished) - timespec_to_ns(&started);
        if (round_trip_ns < 0 || round_trip_ns > 5LL * NS_PER_SECOND) {
            continue;
        }

        result->epoch_ns = (int64_t)(unix_seconds * NS_PER_SECOND)
            + (int64_t)(((uint64_t)fraction * NS_PER_SECOND) >> 32)
            + round_trip_ns / 2;
        result->monotonic_ns = timespec_to_ns(&finished);
        freeaddrinfo(addresses);
        return 0;
    }

    fprintf(stderr, "[pal-clock-anchor] %s NTP query failed: %s\n", hostname, strerror(errno));
    freeaddrinfo(addresses);
    return -1;
}

static int compare_int64(const void *left, const void *right)
{
    int64_t a = *(const int64_t *)left;
    int64_t b = *(const int64_t *)right;
    return (a > b) - (a < b);
}

static int print_system_anchor(void)
{
    struct timespec realtime;
    struct timespec monotonic;

    if (clock_gettime(CLOCK_REALTIME, &realtime) != 0
        || clock_gettime(CLOCK_MONOTONIC_RAW, &monotonic) != 0) {
        return 1;
    }

    printf("%lld %lld\n",
        (long long)timespec_to_ns(&realtime),
        (long long)timespec_to_ns(&monotonic));
    return 0;
}

int main(int argc, char **argv)
{
    struct sample samples[16];
    int64_t normalized[16];
    struct timespec reference;
    size_t sample_count = 0;
    int first_server = 1;

    if (argc == 2 && strcmp(argv[1], "--system") == 0) {
        return print_system_anchor();
    }

    if (argc < 2) {
        fprintf(stderr, "usage: %s NTP_SERVER...\n", argv[0]);
        return 2;
    }

    for (int index = 1; index < argc && sample_count < 16; ++index) {
        if (query_server(argv[index], &samples[sample_count]) == 0) {
            if (!first_server) {
                fputc(',', stderr);
            }
            fprintf(stderr, "%s", argv[index]);
            first_server = 0;
            ++sample_count;
        }
    }

    if (sample_count == 0 || clock_gettime(CLOCK_MONOTONIC_RAW, &reference) != 0) {
        fputc('\n', stderr);
        return 1;
    }

    int64_t reference_ns = timespec_to_ns(&reference);
    for (size_t index = 0; index < sample_count; ++index) {
        normalized[index] = samples[index].epoch_ns
            + reference_ns - samples[index].monotonic_ns;
    }
    qsort(normalized, sample_count, sizeof(normalized[0]), compare_int64);

    fprintf(stderr, " [pal-clock-anchor] samples=%zu\n", sample_count);
    printf("%lld %lld\n",
        (long long)normalized[sample_count / 2],
        (long long)reference_ns);
    return 0;
}
