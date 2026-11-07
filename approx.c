// OSTEP Ch. 29 HW Q3: approximate (sloppy) counter, timed vs. threads and threshold.
// Build: gcc -O2 -pthread -o approx approx.c
// Usage: ./approx threads <threshold> <max_threads>   -> vary threads, fixed S
//        ./approx thresh  <threads>   <max_S>         -> vary S (powers of 2), fixed threads
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#define LOOPS 1000000          // increments per thread (same as Q2)
#define MAXCPUS 64

// Each local counter + its lock sits on its own 64-byte cache line,
// so different CPUs don't fight over the same line ("false sharing").
typedef struct {
    int value;
    pthread_mutex_t lock;
} __attribute__((aligned(64))) local_t;

typedef struct {
    int global;
    pthread_mutex_t glock;
    local_t local[MAXCPUS];
    int threshold;
    int ncpus;
} counter_t;

static void init(counter_t *c, int threshold, int ncpus) {
    c->global = 0;
    pthread_mutex_init(&c->glock, NULL);
    c->threshold = threshold;
    c->ncpus = ncpus;
    for (int i = 0; i < ncpus; i++) {
        c->local[i].value = 0;
        pthread_mutex_init(&c->local[i].lock, NULL);
    }
}

// Grab the local lock, bump the local count; once it reaches the
// threshold, move it into the global counter under the global lock.
static void update(counter_t *c, int tid, int amt) {
    local_t *l = &c->local[tid % c->ncpus];
    pthread_mutex_lock(&l->lock);
    l->value += amt;
    if (l->value >= c->threshold) {
        pthread_mutex_lock(&c->glock);
        c->global += l->value;
        pthread_mutex_unlock(&c->glock);
        l->value = 0;
    }
    pthread_mutex_unlock(&l->lock);
}

// Returns the global count only, so it can be off by up to ncpus * (S-1).
static int get(counter_t *c) {
    pthread_mutex_lock(&c->glock);
    int v = c->global;
    pthread_mutex_unlock(&c->glock);
    return v;
}

static counter_t counter;
typedef struct { int tid; } arg_t;

static void *worker(void *p) {
    int tid = ((arg_t *)p)->tid;
    for (int i = 0; i < LOOPS; i++) update(&counter, tid, 1);
    return NULL;
}

static double now_sec(void) {
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec / 1e6;
}

static double run(int nthreads, int threshold, int ncpus, int *final) {
    init(&counter, threshold, ncpus);
    pthread_t th[nthreads];
    arg_t args[nthreads];
    double start = now_sec();
    for (int i = 0; i < nthreads; i++) {
        args[i].tid = i;
        pthread_create(&th[i], NULL, worker, &args[i]);
    }
    for (int i = 0; i < nthreads; i++) pthread_join(th[i], NULL);
    double elapsed = now_sec() - start;
    *final = get(&counter);
    return elapsed;
}

int main(int argc, char *argv[]) {
    int ncpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    const char *mode = argc > 1 ? argv[1] : "threads";
    int a = argc > 2 ? atoi(argv[2]) : 1024;
    int b = argc > 3 ? atoi(argv[3]) : 8;
    printf("CPUs online: %d, increments per thread: %d\n", ncpus, LOOPS);

    if (strcmp(mode, "threads") == 0) {
        printf("threshold S = %d\n%-8s %-10s %-10s %s\n", a, "threads", "time(s)", "ns/inc", "get() / expected");
        for (int n = 1; n <= b; n++) {
            int final;
            double t = run(n, a, ncpus, &final);
            printf("%-8d %-10.3f %-10.1f %d / %d\n", n, t, t * 1e9 / ((double)n * LOOPS), final, n * LOOPS);
        }
    } else {
        printf("threads = %d\n%-8s %-10s %-10s %s\n", a, "S", "time(s)", "ns/inc", "get() / expected");
        for (int s = 1; s <= b; s *= 2) {
            int final;
            double t = run(a, s, ncpus, &final);
            printf("%-8d %-10.3f %-10.1f %d / %d\n", s, t, t * 1e9 / ((double)a * LOOPS), final, a * LOOPS);
        }
    }
    return 0;
}
