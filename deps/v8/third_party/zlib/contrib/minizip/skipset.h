// skipset.h -- set operations using a skiplist
// Copyright (C) 2024 Mark Adler
// See MiniZip_info.txt for the license.

// This implements a skiplist set, i.e. just keys, no data, with ~O(log n) time
// insert and search operations. The application defines the type of a key, and
// provides a function to compare two keys.

// This header is not definitions of functions found in another source file --
// it creates the set functions, with the application's key type, right where
// the #include is. Before this header is #included, these must be defined:
//
// 1. A macro or typedef for set_key_t, the type of a key.
// 2. A macro or function set_cmp(a, b) to compare two keys. The return values
//    are < 0 for a < b, 0 for a == b, and > 0 for a > b.
// 3. A macro or function set_drop(s, k) to release the key k's resources, if
//    any, when doing a set_end() or set_clear(). s is a pointer to the set
//    that key is in, for use with set_free() if desired.
//
// Example usage:
//
//      typedef int set_key_t;
//      #define set_cmp(a, b) ((a) < (b) ? -1 : (a) == (b) ? 0 : 1)
//      #define set_drop(s, k)
//      #include "skipset.h"
//
//      int test(void) {        // return 0: good, 1: bad, -1: out of memory
//          set_t set;
//          if (setjmp(set.env))
//              return -1;
//          set_start(&set);
//          set_insert(&set, 2);
//          set_insert(&set, 1);
//          set_insert(&set, 7);
//          int bad = !set_found(&set, 2);
//          bad = bad || set_found(&set, 5);
//          set_end(&set);
//          return bad;
//      }
//
// Interface summary (see more details below):
// - set_t is the type of the set being operated on (a set_t pointer is passed)
// - set_start() initializes a new, empty set (initialize set.env first)
// - set_insert() inserts a new key into the set, or not if it's already there
// - set_found() determines whether or not a key is in the set
// - set_end() ends the use of the set, freeing all memory
// - set_clear() empties the set, equivalent to set_end() and then set_start()
// - set_ok() checks if set appears to be usable, i.e. started and not ended
//
// Auxiliary functions available to the application:
// - set_alloc() allocates memory with optional tracking (#define SET_TRACK)
// - set_free() deallocates memory allocated by set_alloc()
// - set_rand() returns 32 random bits (seeded by set_start())

#ifndef SKIPSET_H
#define SKIPSET_H

#include <stdlib.h>     // realloc(), free(), NULL, size_t
#include <stddef.h>     // ptrdiff_t
#include <setjmp.h>     // jmp_buf, longjmp()
#include <errno.h>      // ENOMEM
#include <time.h>       // time(), clock()
#include <assert.h>     // assert.h
#include "ints.h"       // i16_t, ui32_t, ui64_t

// Structures and functions below noted as "--private--" should not be used by
// the application. set_t is partially private and partially public -- see the
// comments there.

// There is no POSIX random() in MSVC, and rand() is awful. For portability, we
// cannot rely on a library function for random numbers. Instead we use the
// fast and effective algorithm below, invented by Melissa O'Neill.

// *Really* minimal PCG32 code / (c) 2014 M.E. O'Neill / www.pcg-random.org
// Licensed under Apache License 2.0 (NO WARRANTY, etc. see website)
// --private-- Random number generator state.
typedef struct {
    ui64_t state;       // 64-bit generator state
    ui64_t inc;         // 63-bit sequence id
} set_rand_t;
// --private-- Initialize the state *gen using seed and seq. seed seeds the
// advancing 64-bit state. seq is a sequence selection constant.
void set_seed(set_rand_t *gen, ui64_t seed, ui64_t seq) {
    gen->inc = (seq << 1) | 1;
    gen->state = (seed + gen->inc) * 6364136223846793005ULL + gen->inc;
}
// Return 32 random bits, advancing the state *gen.
ui32_t set_rand(set_rand_t *gen) {
    ui64_t state = gen->state;
    gen->state = state * 6364136223846793005ULL + gen->inc;
    ui32_t mix = (ui32_t)(((state >> 18) ^ state) >> 27);
    int rot = state >> 59;
    return (mix >> rot) | (mix << ((-rot) & 31));
}
// End of PCG32 code.

// --private-- Linked-list node.
typedef struct set_node_s set_node_t;
struct set_node_s {
    set_key_t key;          // the key (not used for head or path)
    i16_t size;             // number of allocated pointers in right[]
    i16_t fill;             // number of pointers in right[] filled in
    set_node_t **right;     // pointer for each level, each to the right
};

// A set. The application sets env, may use gen with set_rand(), and may read
// allocs and memory. The remaining variables are --private-- .
typedef struct set_s {
    set_node_t *head;       // skiplist head -- no key, just links
    set_node_t *path;       // right[] is path to key from set_found()
    set_node_t *node;       // node under construction, in case of longjmp()
    i16_t depth;            // maximum depth of the skiplist
    ui64_t ran;             // a precious trove of random bits
    set_rand_t gen;         // random number generator state
    jmp_buf env;            // setjmp() environment for allocation errors
#ifdef SET_TRACK
    size_t allocs;          // number of allocations
    size_t memory;          // total amount of allocated memory (>= requests)
#endif
} set_t;

// Memory allocation and deallocation. set_alloc(set, ptr, size) returns a
// pointer to an allocation of size bytes if ptr is NULL, or the previous
// allocation ptr resized to size bytes. set_alloc() will never return NULL.
// set_free(set, ptr) frees an allocation created by set_alloc(). These may be
// used by the application. e.g. if allocation tracking is desired.
#ifdef SET_TRACK
// Track the number of allocations and the total backing memory size.
#  if defined(_WIN32)
#    include <malloc.h>
#    define SET_ALLOC_SIZE(ptr) _msize(ptr)
#  elif defined(__MACH__)
#    include <malloc/malloc.h>
#    define SET_ALLOC_SIZE(ptr) malloc_size(ptr)
#  elif defined(__linux__)
#    include <malloc.h>
#    define SET_ALLOC_SIZE(ptr) malloc_usable_size(ptr)
#  elif defined(__FreeBSD__)
#    include <malloc_np.h>
#    define SET_ALLOC_SIZE(ptr) malloc_usable_size(ptr)
#  elif defined(__NetBSD__)
#    include <jemalloc/jemalloc.h>
#    define SET_ALLOC_SIZE(ptr) malloc_usable_size(ptr)
#  else     // e.g. OpenBSD
#    define SET_ALLOC_SIZE(ptr) 0
#  endif
// With tracking.
void *set_alloc(set_t *set, void *ptr, size_t size) {
    size_t had = ptr == NULL ? 0 : SET_ALLOC_SIZE(ptr);
    void *mem = realloc(ptr, size);
    if (mem == NULL)
        longjmp(set->env, ENOMEM);
    set->allocs += ptr == NULL;
    set->memory += SET_ALLOC_SIZE(mem) - had;
    return mem;
}
void set_free(set_t *set, void *ptr) {
    if (ptr != NULL) {
        set->allocs--;
        set->memory -= SET_ALLOC_SIZE(ptr);
        free(ptr);
    }
}
#else
// Without tracking.
void *set_alloc(set_t *set, void *ptr, size_t size) {
    void *mem = realloc(ptr, size);
    if (mem == NULL)
        longjmp(set->env, ENOMEM);
    return mem;
}
void set_free(set_t *set, void *ptr) {
    (void)set;
    free(ptr);
}
#endif

// --private-- Grow node's array right[] as needed to be able to hold at least
// want links. If fill is true, assure that the first want links are filled in,
// setting them to set->head if not previously filled in. Otherwise it is
// assumed that the first want links are about to be filled in.
void set_grow(set_t *set, set_node_t *node, int want, int fill) {
    if (node->size < want) {
        int more = node->size ? node->size : 1;
        while (more < want)
            more <<= 1;
        node->right = set_alloc(set, node->right, more * sizeof(set_node_t *));
        node->size = (i16_t)more;
    }
    int i;
    if (fill)
        for (i = node->fill; i < want; i++)
            node->right[i] = set->head;
    node->fill = (i16_t)want;
}

// --private-- Return a new node. key is left uninitialized.
set_node_t *set_node(set_t *set) {
    set_node_t *node = set_alloc(set, NULL, sizeof(set_node_t));
    node->size = 0;
    node->fill = 0;
    node->right = NULL;
    return node;
}

// --private-- Free the list linked from head, along with the keys.
void set_sweep(set_t *set) {
    set_node_t *step = set->head->right[0];
    while (step != set->head) {
        set_node_t *next = step->right[0];      // save link to next node
        set_drop(set, step->key);
        set_free(set, step->right);
        set_free(set, step);
        step = next;
    }
}

// Initialize a new set. set->env must be initialized using setjmp() before
// set_start() is called. A longjmp(set->env, ENOMEM) will be used to handle a
// memory allocation failure during any of the operations. (See setjmp.h and
// errno.h.) The set can still be used if this happens, assuming that it didn't
// happen during set_start(). Whether set_start() completed or not, set_end()
// can be used to free the set's memory after a longjmp().
void set_start(set_t *set) {
#ifdef SET_TRACK
    set->allocs = 0;
    set->memory = 0;
#endif
    set->head = set->path = set->node = NULL;   // in case set_node() fails
    set->path = set_node(set);
    set->head = set_node(set);
    set_grow(set, set->head, 1, 1); // one link back to head for an empty set
    *(unsigned char *)&set->head->key = 137;    // set id
    set->depth = 0;
    set_seed(&set->gen, ((ui64_t)(ptrdiff_t)set << 32) ^
                        ((ui64_t)time(NULL) << 12) ^ clock(), 0);
    set->ran = 1;
}

// Return true if *set appears to be in a usable state. If *set has been zeroed
// out, then set_ok(set) will be false and set_end(set) will be safe.
int set_ok(set_t *set) {
    return set->head != NULL &&
           set->head->right != NULL &&
           *(unsigned char *)&set->head->key == 137;
}

// Empty the set. This frees the memory used for the previous set contents.
// After set_clear(), *set is ready for use, as if after a set_start().
void set_clear(set_t *set) {
    assert(set_ok(set) && "improper use");

    // Free all the keys and their nodes.
    set_sweep(set);

    // Leave the head and path allocations as is. Clear their contents, with
    // head pointing to itself and setting depth to zero, for an empty set.
    set->head->right[0] = set->head;
    set->head->fill = 1;
    set->path->fill = 0;
    set->depth = 0;
}

// Done using the set -- free all allocations. The only operation on *set
// permitted after this is set_start(). Though another set_end() would do no
// harm. This can be done at any time after a set_start(), or after a longjmp()
// on any allocation failure, including during a set_start().
void set_end(set_t *set) {
    if (set->head != NULL) {
        // Empty the set and free the head node.
        if (set->head->right != NULL) {
            set_sweep(set);
            set_free(set, set->head->right);
        }
        set_free(set, set->head);
        set->head = NULL;
    }
    if (set->path != NULL) {
        // Free the path work area.
        set_free(set, set->path->right);
        set_free(set, set->path);
        set->path = NULL;
    }
    if (set->node != NULL) {
        // Free the node that was under construction when longjmp() hit.
        set_drop(set, set->node->key);
        set_free(set, set->node->right);
        set_free(set, set->node);
        set->node = NULL;
    }
}

// Look for key. Return 1 if found or 0 if not. This also puts the path to get
// there in set->path, for use by set_insert().
int set_found(set_t *set, set_key_t key) {
    assert(set_ok(set) && "improper use");

    // Start at depth and work down and right as determined by key comparisons.
    set_node_t *head = set->head, *here = head;
    int i = set->depth;
    set_grow(set, set->path, i + 1, 0);
    do {
        while (here->right[i] != head &&
               set_cmp(here->right[i]->key, key) < 0)
            here = here->right[i];
        set->path->right[i] = here;
    } while (i--);

    // See if the key matches.
    here = here->right[0];
    return here != head && set_cmp(here->key, key) == 0;
}

// Insert the key key. Return 0 on success, or 1 if key is already in the set.
int set_insert(set_t *set, set_key_t key) {
    assert(set_ok(set) && "improper use");

    if (set_found(set, key))
        // That key is already in the set.
        return 1;

    // Randomly generate a new level-- level 0 with probability 1/2, 1 with
    // probability 1/4, 2 with probability 1/8, etc.
    int level = 0;
    for (;;) {
        if (set->ran == 1)
            // Ran out. Get another 32 random bits.
            set->ran = set_rand(&set->gen) | (1ULL << 32);
        int bit = set->ran & 1;
        set->ran >>= 1;
        if (bit)
            break;
        assert(level < 32767 &&
               "Overhead, without any fuss, the stars were going out.");
        level++;
    }
    if (level > set->depth) {
        // The maximum depth is now deeper. Update the structures.
        set_grow(set, set->path, level + 1, 1);
        set_grow(set, set->head, level + 1, 1);
        set->depth = (i16_t)level;
    }

    // Make a new node for the provided key, and insert it in the lists up to
    // and including level.
    set->node = set_node(set);
    set->node->key = key;
    set_grow(set, set->node, level + 1, 0);
    int i;
    for (i = 0; i <= level; i++) {
        set->node->right[i] = set->path->right[i]->right[i];
        set->path->right[i]->right[i] = set->node;
    }
    set->node = NULL;
    return 0;
}

#else
#error ** another skiplist set already created here
// Would need to implement a prefix in order to support multiple sets.
#endif
