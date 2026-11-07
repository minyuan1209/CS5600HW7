// list.c - linked list with two locking strategies:
//   mode 0: one big lock for the whole list (the chapter's version)
//   mode 1: hand-over-hand locking (one lock per node)
//
// Build:  gcc -O2 -pthread -o list list.c
// Run:    ./list <mode> <threads> <list_size> <lookups_per_thread> <work_per_node>
//   e.g.  ./list 1 4 1000 10000 0

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

typedef struct node_t {
    int key;
    struct node_t *next;
    pthread_mutex_t lock;          // used only by hand-over-hand
} node_t;

typedef struct {
    node_t *head;
    pthread_mutex_t lock;          // big lock (mode 0) / head lock (mode 1)
} list_t;

static list_t L;
static int mode, list_size, lookups, work;

// Simulates extra work done at each node (e.g. an expensive key comparison).
static void do_work(void) {
    for (volatile int i = 0; i < work; i++)
        ;
}

void list_init(list_t *l) {
    l->head = NULL;
    pthread_mutex_init(&l->lock, NULL);
}

// Insert at head. Both modes protect the head pointer with the list lock.
int list_insert(list_t *l, int key) {
    node_t *n = malloc(sizeof(node_t));
    if (!n) return -1;
    n->key = key;
    pthread_mutex_init(&n->lock, NULL);
    pthread_mutex_lock(&l->lock);
    n->next = l->head;
    l->head = n;
    pthread_mutex_unlock(&l->lock);
    return 0;
}

// Standard lookup: hold the single lock for the whole traversal.
int lookup_biglock(list_t *l, int key) {
    int rv = -1;
    pthread_mutex_lock(&l->lock);
    for (node_t *curr = l->head; curr; curr = curr->next) {
        do_work();
        if (curr->key == key) { rv = 0; break; }
    }
    pthread_mutex_unlock(&l->lock);
    return rv;
}

// Hand-over-hand lookup: lock the next node BEFORE releasing the current one,
// so a thread always holds at least one lock while moving through the list.
int lookup_hoh(list_t *l, int key) {
    pthread_mutex_lock(&l->lock);
    node_t *curr = l->head;
    if (!curr) { pthread_mutex_unlock(&l->lock); return -1; }
    pthread_mutex_lock(&curr->lock);
    pthread_mutex_unlock(&l->lock);

    while (curr) {
        do_work();
        if (curr->key == key) {
            pthread_mutex_unlock(&curr->lock);
            return 0;
        }
        node_t *next = curr->next;
        if (next) pthread_mutex_lock(&next->lock);   // grab next...
        pthread_mutex_unlock(&curr->lock);           // ...then release current
        curr = next;
    }
    return -1;
}

void *worker(void *arg) {
    unsigned int seed = (unsigned int)(long)arg;
    for (int i = 0; i < lookups; i++) {
        int key = rand_r(&seed) % list_size;
        if (mode == 0) lookup_biglock(&L, key);
        else           lookup_hoh(&L, key);
    }
    return NULL;
}

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr, "usage: %s <mode 0=big 1=hoh> <threads> <list_size> <lookups_per_thread> <work_per_node>\n", argv[0]);
        return 1;
    }
    mode      = atoi(argv[1]);
    int nthr  = atoi(argv[2]);
    list_size = atoi(argv[3]);
    lookups   = atoi(argv[4]);
    work      = atoi(argv[5]);

    list_init(&L);
    for (int i = 0; i < list_size; i++)
        list_insert(&L, i);

    pthread_t *t = malloc(sizeof(pthread_t) * nthr);
    double start = now();
    for (int i = 0; i < nthr; i++)
        pthread_create(&t[i], NULL, worker, (void *)(long)(i + 1));
    for (int i = 0; i < nthr; i++)
        pthread_join(t[i], NULL);
    double elapsed = now() - start;

    printf("%-4s threads=%d size=%d lookups/thread=%d work=%d  time=%.3f s\n",
           mode == 0 ? "big" : "hoh", nthr, list_size, lookups, work, elapsed);
    return 0;
}
