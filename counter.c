// OSTEP Ch. 29 HW Q2: simple concurrent counter (one mutex), timed vs. thread count.
// Build: gcc -O2 -pthread -o counter counter.c
// Usage: ./counter [max_threads] [increments_per_thread]
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct {
    int value;
    pthread_mutex_t lock;
} counter_t;

static void init(counter_t *c) { c->value = 0; pthread_mutex_init(&c->lock, NULL); }
static void increment(counter_t *c) {
    pthread_mutex_lock(&c->lock);
    c->value++;
    pthread_mutex_unlock(&c->lock);
}
static int get(counter_t *c) {
    pthread_mutex_lock(&c->lock);
    int v = c->value;
    pthread_mutex_unlock(&c->lock);
    return v;
}

static counter_t counter;
static int loops;

static void *worker(void *arg) {
    (void)arg;
    for (int i = 0; i < loops; i++) increment(&counter);
    return NULL;
}

static double now_sec(void) {
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec / 1e6;
}

int main(int argc, char *argv[]) {
    int max_threads = argc > 1 ? atoi(argv[1]) : 8;
    loops = argc > 2 ? atoi(argv[2]) : 1000000;

    printf("CPUs online: %ld\n", sysconf(_SC_NPROCESSORS_ONLN));
    printf("%-8s %-12s %-10s %s\n", "threads", "total_incs", "time(s)", "ns/inc");

    for (int n = 1; n <= max_threads; n++) {
        init(&counter);
        pthread_t th[n];
        double start = now_sec();
        for (int i = 0; i < n; i++) pthread_create(&th[i], NULL, worker, NULL);
        for (int i = 0; i < n; i++) pthread_join(th[i], NULL);
        double elapsed = now_sec() - start;

        int expected = n * loops;
        if (get(&counter) != expected)
            fprintf(stderr, "ERROR: counter=%d expected=%d\n", get(&counter), expected);
        printf("%-8d %-12d %-10.3f %.1f\n", n, expected, elapsed, elapsed * 1e9 / expected);
    }
    return 0;
}
