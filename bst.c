// bst.c - concurrent binary search tree with four locking strategies
//   mode 0: single mutex for the whole tree           (straightforward)
//   mode 1: single reader-writer lock                 (lookups share the lock)
//   mode 2: hand-over-hand locking (one lock per node, lock coupling)
//   mode 3: partitioned tree (key range split into NPART subtrees,
//           each subtree has its own mutex)
//
// Build:  gcc -O2 -pthread -o bst bst.c
// Run:    ./bst <mode> <threads> <ops_per_thread> <percent_inserts>
//   e.g.  ./bst 3 4 500000 10

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#define KEY_RANGE    1000000
#define INITIAL_SIZE 100000
#define NPART        16

typedef struct node_t {
    int key;
    struct node_t *left, *right;
    pthread_mutex_t lock;      // used only by hand-over-hand (mode 2)
} node_t;

typedef struct {
    node_t *root;
    pthread_mutex_t  mutex;    // whole-tree lock (modes 0, 3); root-pointer lock (mode 2)
    pthread_rwlock_t rwlock;   // mode 1
} tree_t;

static tree_t T;               // used by modes 0, 1, 2
static tree_t parts[NPART];    // used by mode 3
static int mode, ops, pct_insert;

void tree_init(tree_t *t) {
    t->root = NULL;
    pthread_mutex_init(&t->mutex, NULL);
    pthread_rwlock_init(&t->rwlock, NULL);
}

static node_t *new_node(int key) {
    node_t *n = malloc(sizeof(node_t));
    if (!n) { perror("malloc"); exit(1); }
    n->key = key;
    n->left = n->right = NULL;
    pthread_mutex_init(&n->lock, NULL);
    return n;
}

// Which subtree a key belongs to (by range, so the tree stays ordered).
static int part_of(int key) {
    return (int)((long)key * NPART / KEY_RANGE);
}

// ---------- unlocked helpers (caller holds the right lock) ----------
static int bst_insert(tree_t *t, int key) {
    node_t **p = &t->root;
    while (*p) {
        if (key == (*p)->key) return 0;          // already present
        p = (key < (*p)->key) ? &(*p)->left : &(*p)->right;
    }
    *p = new_node(key);
    return 1;
}

static int bst_lookup(tree_t *t, int key) {
    node_t *c = t->root;
    while (c) {
        if (key == c->key) return 1;
        c = (key < c->key) ? c->left : c->right;
    }
    return 0;
}

// ---------- hand-over-hand (mode 2) ----------
// Lock the child before releasing the parent while walking down.
static int hoh_insert(tree_t *t, int key) {
    pthread_mutex_lock(&t->mutex);               // protects the root pointer
    if (!t->root) {
        t->root = new_node(key);
        pthread_mutex_unlock(&t->mutex);
        return 1;
    }
    node_t *c = t->root;
    pthread_mutex_lock(&c->lock);
    pthread_mutex_unlock(&t->mutex);

    while (1) {
        if (key == c->key) { pthread_mutex_unlock(&c->lock); return 0; }
        node_t **p = (key < c->key) ? &c->left : &c->right;
        if (*p == NULL) {                         // found the spot: attach while holding parent
            *p = new_node(key);
            pthread_mutex_unlock(&c->lock);
            return 1;
        }
        node_t *next = *p;
        pthread_mutex_lock(&next->lock);          // grab child...
        pthread_mutex_unlock(&c->lock);           // ...then release parent
        c = next;
    }
}

static int hoh_lookup(tree_t *t, int key) {
    pthread_mutex_lock(&t->mutex);
    node_t *c = t->root;
    if (!c) { pthread_mutex_unlock(&t->mutex); return 0; }
    pthread_mutex_lock(&c->lock);
    pthread_mutex_unlock(&t->mutex);

    while (1) {
        if (key == c->key) { pthread_mutex_unlock(&c->lock); return 1; }
        node_t *next = (key < c->key) ? c->left : c->right;
        if (!next) { pthread_mutex_unlock(&c->lock); return 0; }
        pthread_mutex_lock(&next->lock);
        pthread_mutex_unlock(&c->lock);
        c = next;
    }
}

// ---------- public operations ----------
int tree_insert(int key) {
    int rv;
    switch (mode) {
    case 0:
        pthread_mutex_lock(&T.mutex);
        rv = bst_insert(&T, key);
        pthread_mutex_unlock(&T.mutex);
        break;
    case 1:
        pthread_rwlock_wrlock(&T.rwlock);
        rv = bst_insert(&T, key);
        pthread_rwlock_unlock(&T.rwlock);
        break;
    case 2:
        rv = hoh_insert(&T, key);
        break;
    default: {
        tree_t *s = &parts[part_of(key)];
        pthread_mutex_lock(&s->mutex);
        rv = bst_insert(s, key);
        pthread_mutex_unlock(&s->mutex);
    }
    }
    return rv;
}

int tree_lookup(int key) {
    int rv;
    switch (mode) {
    case 0:
        pthread_mutex_lock(&T.mutex);
        rv = bst_lookup(&T, key);
        pthread_mutex_unlock(&T.mutex);
        break;
    case 1:
        pthread_rwlock_rdlock(&T.rwlock);
        rv = bst_lookup(&T, key);
        pthread_rwlock_unlock(&T.rwlock);
        break;
    case 2:
        rv = hoh_lookup(&T, key);
        break;
    default: {
        tree_t *s = &parts[part_of(key)];
        pthread_mutex_lock(&s->mutex);
        rv = bst_lookup(s, key);
        pthread_mutex_unlock(&s->mutex);
    }
    }
    return rv;
}

void *worker(void *arg) {
    unsigned int seed = (unsigned int)(long)arg;
    for (int i = 0; i < ops; i++) {
        int key = rand_r(&seed) % KEY_RANGE;
        if (rand_r(&seed) % 100 < pct_insert) tree_insert(key);
        else                                  tree_lookup(key);
    }
    return NULL;
}

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s <mode 0=mutex 1=rwlock 2=hoh 3=partitioned> <threads> <ops_per_thread> <percent_inserts>\n", argv[0]);
        return 1;
    }
    mode       = atoi(argv[1]);
    int nthr   = atoi(argv[2]);
    ops        = atoi(argv[3]);
    pct_insert = atoi(argv[4]);

    // Prefill with the same random keys in every mode.
    tree_init(&T);
    for (int i = 0; i < NPART; i++) tree_init(&parts[i]);
    unsigned int seed = 12345;
    for (int i = 0; i < INITIAL_SIZE; i++) {
        int key = rand_r(&seed) % KEY_RANGE;
        if (mode == 3) bst_insert(&parts[part_of(key)], key);
        else           bst_insert(&T, key);
    }

    pthread_t *t = malloc(sizeof(pthread_t) * nthr);
    double start = now();
    for (int i = 0; i < nthr; i++)
        pthread_create(&t[i], NULL, worker, (void *)(long)(i + 1));
    for (int i = 0; i < nthr; i++)
        pthread_join(t[i], NULL);
    double elapsed = now() - start;

    const char *names[] = {"mutex", "rwlock", "hoh", "part"};
    double total = (double)nthr * ops;
    printf("%-6s threads=%d ops/thread=%d inserts=%d%%  time=%.3f s  throughput=%.2f Mops/s\n",
           names[mode], nthr, ops, pct_insert, elapsed, total / elapsed / 1e6);
    return 0;
}
