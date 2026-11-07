// OSTEP Ch. 29 HW Q5: binary search tree protected by a single lock.
// Build: gcc -O2 -pthread -o bst bst.c
// Usage: ./bst <prefill> <ops_per_thread> <max_threads> <insert_pct>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#define KEYSPACE (1 << 22)   // keys drawn from [0, 4M)

typedef struct node { int key; struct node *left, *right; } node_t;
typedef struct { node_t *root; pthread_mutex_t lock; } bst_t;

static void bst_init(bst_t *t) { t->root = NULL; pthread_mutex_init(&t->lock, NULL); }

// Returns 1 if inserted, 0 if key was already present.
static int bst_insert(bst_t *t, int key) {
    node_t *n = malloc(sizeof *n);          // allocate outside the critical section
    n->key = key; n->left = n->right = NULL;
    pthread_mutex_lock(&t->lock);
    node_t **p = &t->root;
    while (*p) {
        if (key == (*p)->key) { pthread_mutex_unlock(&t->lock); free(n); return 0; }
        p = key < (*p)->key ? &(*p)->left : &(*p)->right;
    }
    *p = n;
    pthread_mutex_unlock(&t->lock);
    return 1;
}

static int bst_lookup(bst_t *t, int key) {
    pthread_mutex_lock(&t->lock);
    node_t *c = t->root;
    while (c && c->key != key) c = key < c->key ? c->left : c->right;
    int found = c != NULL;
    pthread_mutex_unlock(&t->lock);
    return found;
}

static bst_t T;
static int OPS, INS_PCT;

static void *worker(void *arg) {
    unsigned seed = (unsigned)(long)arg * 104729 + 17;
    for (int i = 0; i < OPS; i++) {
        int key = rand_r(&seed) % KEYSPACE;
        if ((int)(rand_r(&seed) % 100) < INS_PCT) bst_insert(&T, key);
        else bst_lookup(&T, key);
    }
    return NULL;
}

static double now_sec(void) {
    struct timeval t; gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec / 1e6;
}

int main(int argc, char *argv[]) {
    int prefill = argc > 1 ? atoi(argv[1]) : 100000;
    OPS         = argc > 2 ? atoi(argv[2]) : 500000;
    int max     = argc > 3 ? atoi(argv[3]) : 8;
    INS_PCT     = argc > 4 ? atoi(argv[4]) : 10;
    printf("CPUs=%ld prefill=%d ops/thread=%d inserts=%d%%\n",
           sysconf(_SC_NPROCESSORS_ONLN), prefill, OPS, INS_PCT);
    printf("%-8s %-10s %-10s %s\n", "threads", "time(s)", "ns/op", "Mops/s");
    for (int n = 1; n <= max; n *= 2) {
        // Fresh tree each run; random keys keep it roughly balanced (depth ~ 2 ln N).
        bst_init(&T);
        unsigned s = 42;
        for (int i = 0; i < prefill; i++) bst_insert(&T, rand_r(&s) % KEYSPACE);
        pthread_t th[n];
        double start = now_sec();
        for (long i = 0; i < n; i++) pthread_create(&th[i], NULL, worker, (void *)i);
        for (int i = 0; i < n; i++) pthread_join(th[i], NULL);
        double el = now_sec() - start;
        double total = (double)n * OPS;
        printf("%-8d %-10.3f %-10.1f %.2f\n", n, el, el * 1e9 / total, total / el / 1e6);
    }
    return 0;
}
