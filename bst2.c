// OSTEP Ch. 29 HW Q6: BST with three locking strategies.
//   0 = single mutex (Q5 baseline)
//   1 = single reader-writer lock (lookups share, inserts exclusive)
//   2 = hand-over-hand per-node mutexes (lock coupling down the tree)
// Build: gcc -O2 -pthread -o bst2 bst2.c
// Usage: ./bst2 <prefill> <ops_per_thread> <max_threads> <insert_pct>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#define KEYSPACE (1 << 22)

typedef struct node {
    int key;
    struct node *left, *right;
    pthread_mutex_t lock;          // used only by strategy 2
} node_t;

typedef struct {
    node_t *root;
    pthread_mutex_t mutex;         // strategy 0
    pthread_rwlock_t rw;           // strategy 1
    pthread_mutex_t rootlock;      // strategy 2: protects the root pointer
} bst_t;

static node_t *new_node(int key) {
    node_t *n = malloc(sizeof *n);
    n->key = key; n->left = n->right = NULL;
    pthread_mutex_init(&n->lock, NULL);
    return n;
}

static void bst_init(bst_t *t) {
    t->root = NULL;
    pthread_mutex_init(&t->mutex, NULL);
    pthread_rwlock_init(&t->rw, NULL);
    pthread_mutex_init(&t->rootlock, NULL);
}

/* ----- plain (unlocked) helpers used by strategies 0 and 1 ----- */
static int raw_insert(bst_t *t, node_t *n) {
    node_t **p = &t->root;
    while (*p) {
        if (n->key == (*p)->key) return 0;
        p = n->key < (*p)->key ? &(*p)->left : &(*p)->right;
    }
    *p = n;
    return 1;
}
static int raw_lookup(bst_t *t, int key) {
    node_t *c = t->root;
    while (c && c->key != key) c = key < c->key ? c->left : c->right;
    return c != NULL;
}

/* ----- strategy 0: one mutex ----- */
static void ins0(bst_t *t, int key) {
    node_t *n = new_node(key);
    pthread_mutex_lock(&t->mutex);
    int ok = raw_insert(t, n);
    pthread_mutex_unlock(&t->mutex);
    if (!ok) free(n);
}
static void look0(bst_t *t, int key) {
    pthread_mutex_lock(&t->mutex);
    raw_lookup(t, key);
    pthread_mutex_unlock(&t->mutex);
}

/* ----- strategy 1: reader-writer lock ----- */
static void ins1(bst_t *t, int key) {
    node_t *n = new_node(key);
    pthread_rwlock_wrlock(&t->rw);
    int ok = raw_insert(t, n);
    pthread_rwlock_unlock(&t->rw);
    if (!ok) free(n);
}
static void look1(bst_t *t, int key) {
    pthread_rwlock_rdlock(&t->rw);
    raw_lookup(t, key);
    pthread_rwlock_unlock(&t->rw);
}

/* ----- strategy 2: hand-over-hand down the tree -----
 * Hold the parent's lock while grabbing the child's, then drop the parent.
 * Two operations only block each other while their paths overlap, so after
 * the first few levels they usually proceed in parallel. */
static void ins2(bst_t *t, int key) {
    node_t *n = new_node(key);
    pthread_mutex_lock(&t->rootlock);
    if (!t->root) { t->root = n; pthread_mutex_unlock(&t->rootlock); return; }
    node_t *cur = t->root;
    pthread_mutex_lock(&cur->lock);
    pthread_mutex_unlock(&t->rootlock);
    for (;;) {
        if (key == cur->key) { pthread_mutex_unlock(&cur->lock); free(n); return; }
        node_t **child = key < cur->key ? &cur->left : &cur->right;
        if (!*child) {                       // cur is locked, so linking is safe
            *child = n;
            pthread_mutex_unlock(&cur->lock);
            return;
        }
        node_t *next = *child;
        pthread_mutex_lock(&next->lock);
        pthread_mutex_unlock(&cur->lock);
        cur = next;
    }
}
static void look2(bst_t *t, int key) {
    pthread_mutex_lock(&t->rootlock);
    node_t *cur = t->root;
    if (!cur) { pthread_mutex_unlock(&t->rootlock); return; }
    pthread_mutex_lock(&cur->lock);
    pthread_mutex_unlock(&t->rootlock);
    while (cur->key != key) {
        node_t *next = key < cur->key ? cur->left : cur->right;
        if (!next) break;
        pthread_mutex_lock(&next->lock);
        pthread_mutex_unlock(&cur->lock);
        cur = next;
    }
    pthread_mutex_unlock(&cur->lock);
}

/* ----- benchmark ----- */
static bst_t T;
static int OPS, INS_PCT, MODE;

static void *worker(void *arg) {
    unsigned seed = (unsigned)(long)arg * 104729 + 17;
    for (int i = 0; i < OPS; i++) {
        int key = rand_r(&seed) % KEYSPACE;
        int ins = (int)(rand_r(&seed) % 100) < INS_PCT;
        switch (MODE) {
            case 0: ins ? ins0(&T, key) : look0(&T, key); break;
            case 1: ins ? ins1(&T, key) : look1(&T, key); break;
            case 2: ins ? ins2(&T, key) : look2(&T, key); break;
        }
    }
    return NULL;
}

static double now_sec(void) {
    struct timeval t; gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec / 1e6;
}

static double run(int mode, int nthreads, int prefill) {
    MODE = mode;
    bst_init(&T);
    unsigned s = 42;
    for (int i = 0; i < prefill; i++) { node_t *n = new_node(rand_r(&s) % KEYSPACE); if (!raw_insert(&T, n)) free(n); }
    pthread_t th[nthreads];
    double start = now_sec();
    for (long i = 0; i < nthreads; i++) pthread_create(&th[i], NULL, worker, (void *)i);
    for (int i = 0; i < nthreads; i++) pthread_join(th[i], NULL);
    double el = now_sec() - start;
    return (double)nthreads * OPS / el / 1e6;   // Mops/s
}

int main(int argc, char *argv[]) {
    int prefill = argc > 1 ? atoi(argv[1]) : 100000;
    OPS         = argc > 2 ? atoi(argv[2]) : 500000;
    int max     = argc > 3 ? atoi(argv[3]) : 8;
    INS_PCT     = argc > 4 ? atoi(argv[4]) : 10;
    printf("CPUs=%ld prefill=%d ops/thread=%d inserts=%d%%  (Mops/s, higher is better)\n",
           sysconf(_SC_NPROCESSORS_ONLN), prefill, OPS, INS_PCT);
    printf("%-8s %-10s %-10s %s\n", "threads", "mutex", "rwlock", "hand-over-hand");
    for (int n = 1; n <= max; n *= 2)
        printf("%-8d %-10.2f %-10.2f %.2f\n", n, run(0, n, prefill), run(1, n, prefill), run(2, n, prefill));
    return 0;
}
