// OSTEP Ch. 29 HW Q4: hand-over-hand (lock coupling) list vs. single-lock list.
// Build: gcc -O2 -pthread -o hoh hoh.c
// Usage: ./hoh <list_len> <ops_per_thread> <max_threads> <work_per_node> <insert_pct>
//   work_per_node: extra spin iterations per node visited (simulates an expensive compare)
//   insert_pct:    percent of ops that are inserts (rest are lookups)
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

static int WORK = 0;
static inline void node_work(void) {
    for (volatile int i = 0; i < WORK; i++) ;
}

/* ---------- Standard list: one lock for the whole list (chapter Fig. 29.8) ---------- */
typedef struct snode { int key; struct snode *next; } snode_t;
typedef struct { snode_t *head; pthread_mutex_t lock; } slist_t;

static void s_init(slist_t *L) { L->head = NULL; pthread_mutex_init(&L->lock, NULL); }
static void s_insert(slist_t *L, int key) {
    snode_t *n = malloc(sizeof *n);          // malloc outside the lock
    n->key = key;
    pthread_mutex_lock(&L->lock);
    n->next = L->head;
    L->head = n;
    pthread_mutex_unlock(&L->lock);
}
static int s_lookup(slist_t *L, int key) {
    int found = 0;
    pthread_mutex_lock(&L->lock);
    for (snode_t *c = L->head; c; c = c->next) {
        node_work();
        if (c->key == key) { found = 1; break; }
    }
    pthread_mutex_unlock(&L->lock);
    return found;
}

/* ---------- Hand-over-hand list: one lock per node ---------- */
// A dummy head node holds the lock that protects the first real pointer.
typedef struct hnode { int key; struct hnode *next; pthread_mutex_t lock; } hnode_t;
typedef struct { hnode_t dummy; } hlist_t;

static void h_init(hlist_t *L) { L->dummy.next = NULL; pthread_mutex_init(&L->dummy.lock, NULL); }
static void h_insert(hlist_t *L, int key) {
    hnode_t *n = malloc(sizeof *n);
    n->key = key;
    pthread_mutex_init(&n->lock, NULL);
    pthread_mutex_lock(&L->dummy.lock);
    n->next = L->dummy.next;
    L->dummy.next = n;
    pthread_mutex_unlock(&L->dummy.lock);
}
// Lock coupling: grab the next node's lock BEFORE releasing the current one,
// so no one can unlink/change the node we're about to step onto.
static int h_lookup(hlist_t *L, int key) {
    hnode_t *prev = &L->dummy;
    pthread_mutex_lock(&prev->lock);
    hnode_t *cur = prev->next;
    while (cur) {
        pthread_mutex_lock(&cur->lock);
        pthread_mutex_unlock(&prev->lock);
        node_work();
        if (cur->key == key) { pthread_mutex_unlock(&cur->lock); return 1; }
        prev = cur;
        cur = cur->next;
    }
    pthread_mutex_unlock(&prev->lock);
    return 0;
}

/* ---------- Benchmark ---------- */
static slist_t SL;
static hlist_t HL;
static int LEN, OPS, INS_PCT, USE_HOH;

static void *worker(void *arg) {
    unsigned seed = (unsigned)(long)arg * 7919 + 1;
    for (int i = 0; i < OPS; i++) {
        int key = rand_r(&seed) % LEN;
        int ins = (rand_r(&seed) % 100) < INS_PCT;
        if (USE_HOH) { if (ins) h_insert(&HL, key); else h_lookup(&HL, key); }
        else         { if (ins) s_insert(&SL, key); else s_lookup(&SL, key); }
    }
    return NULL;
}

static double now_sec(void) {
    struct timeval t; gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec / 1e6;
}

static double run(int hoh, int nthreads) {
    USE_HOH = hoh;
    // Fresh list of LEN keys each run (leaks old nodes; fine for a benchmark).
    s_init(&SL); h_init(&HL);
    for (int k = LEN - 1; k >= 0; k--) { if (hoh) h_insert(&HL, k); else s_insert(&SL, k); }
    pthread_t th[nthreads];
    double start = now_sec();
    for (long i = 0; i < nthreads; i++) pthread_create(&th[i], NULL, worker, (void *)i);
    for (int i = 0; i < nthreads; i++) pthread_join(th[i], NULL);
    return now_sec() - start;
}

int main(int argc, char *argv[]) {
    LEN     = argc > 1 ? atoi(argv[1]) : 1000;
    OPS     = argc > 2 ? atoi(argv[2]) : 10000;
    int max = argc > 3 ? atoi(argv[3]) : 4;
    WORK    = argc > 4 ? atoi(argv[4]) : 0;
    INS_PCT = argc > 5 ? atoi(argv[5]) : 0;
    printf("CPUs=%ld len=%d ops/thread=%d work/node=%d inserts=%d%%\n",
           sysconf(_SC_NPROCESSORS_ONLN), LEN, OPS, WORK, INS_PCT);
    printf("%-8s %-12s %-12s %s\n", "threads", "standard(s)", "hoh(s)", "hoh/standard");
    for (int n = 1; n <= max; n *= 2) {
        double s = run(0, n), h = run(1, n);
        printf("%-8d %-12.3f %-12.3f %.2fx\n", n, s, h, h / s);
    }
    return 0;
}
