// OSTEP Ch. 29 HW Q1: How accurate is gettimeofday()? Smallest interval?
// Build: gcc -O2 -o timer timer.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#define HAVE_RDTSC 1
#endif

#define N 1000000

static double tv_us(struct timeval t) { return t.tv_sec * 1e6 + t.tv_usec; }
static double ts_ns(struct timespec t) { return t.tv_sec * 1e9 + t.tv_nsec; }

int main(void) {
    // 1. Smallest nonzero step of gettimeofday(): spin until the value changes.
    long min_step = 1L << 30, zero_diffs = 0;
    for (int i = 0; i < N; i++) {
        struct timeval a, b;
        gettimeofday(&a, NULL);
        do { gettimeofday(&b, NULL); } while (tv_us(b) == tv_us(a));
        long d = (long)(tv_us(b) - tv_us(a));
        if (d < min_step) min_step = d;
    }
    // How many back-to-back calls return the *same* value?
    for (int i = 0; i < N; i++) {
        struct timeval a, b;
        gettimeofday(&a, NULL);
        gettimeofday(&b, NULL);
        if (tv_us(a) == tv_us(b)) zero_diffs++;
    }
    printf("gettimeofday: smallest nonzero step = %ld us\n", min_step);
    printf("gettimeofday: %.1f%% of back-to-back calls returned identical time\n",
           100.0 * zero_diffs / N);

    // 2. Cost of one gettimeofday() call (average over N calls).
    struct timeval s, e;
    gettimeofday(&s, NULL);
    for (int i = 0; i < N; i++) { struct timeval t; gettimeofday(&t, NULL); }
    gettimeofday(&e, NULL);
    printf("gettimeofday: avg cost per call = %.1f ns\n",
           (tv_us(e) - tv_us(s)) * 1000.0 / N);

    // 3. clock_gettime(CLOCK_MONOTONIC): nanosecond-resolution alternative.
    struct timespec res;
    clock_getres(CLOCK_MONOTONIC, &res);
    double min_ns = 1e18;
    for (int i = 0; i < N; i++) {
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        do { clock_gettime(CLOCK_MONOTONIC, &b); } while (ts_ns(b) == ts_ns(a));
        double d = ts_ns(b) - ts_ns(a);
        if (d < min_ns) min_ns = d;
    }
    printf("clock_gettime(MONOTONIC): reported res = %ld ns, smallest observed step = %.0f ns\n",
           res.tv_nsec, min_ns);

    // 4. Sanity check accuracy: sleep a known amount, measure with gettimeofday.
    int sleeps_us[] = {1, 10, 100, 1000, 10000, 100000};
    for (int k = 0; k < 6; k++) {
        gettimeofday(&s, NULL);
        usleep(sleeps_us[k]);
        gettimeofday(&e, NULL);
        printf("usleep(%6d us) measured as %8.0f us\n", sleeps_us[k], tv_us(e) - tv_us(s));
    }

#ifdef HAVE_RDTSC
    // 5. rdtsc: cycle counter. Calibrate cycles/sec against gettimeofday over 100 ms.
    uint64_t c0 = __rdtsc();
    gettimeofday(&s, NULL);
    usleep(100000);
    uint64_t c1 = __rdtsc();
    gettimeofday(&e, NULL);
    double ghz = (double)(c1 - c0) / ((tv_us(e) - tv_us(s)) * 1000.0);
    uint64_t min_cyc = UINT64_MAX;
    for (int i = 0; i < N; i++) {
        uint64_t a = __rdtsc(), b = __rdtsc();
        if (b - a < min_cyc) min_cyc = b - a;
    }
    printf("rdtsc: ~%.2f GHz TSC, back-to-back min = %lu cycles (~%.1f ns)\n",
           ghz, (unsigned long)min_cyc, min_cyc / ghz);
#endif
    return 0;
}
