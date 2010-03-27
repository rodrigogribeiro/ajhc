

#ifndef JGC_H
#define JGC_H

// #if __GNUC_PREREQ__(2, 96)
#define __predict_true(exp)     __builtin_expect(!!(exp), 1)
#define __predict_false(exp)    __builtin_expect(!!(exp), 0)
// #else
// #define __predict_true(exp)     (exp)
// #define __predict_false(exp)    (exp)
// #endif

#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>

#define JGC_STATUS 0

#define ALIGN(a,n) ((n) - 1 + ((a) - ((n) - 1) % (a)))

typedef struct {
        uint8_t count;
        uint8_t nptrs;
        uint16_t tag;
} entry_header_t;

struct frame {
        struct frame *prev;
        unsigned nptrs;
        void *ptrs[0];
};


// round all allocations up to this many blocks.
// the underlying malloc implementation has some
// minimum size and this allows memory blocks to
// be reused more often.
#define GC_MINIMUM_SIZE 3
#define GC_BASE sizeof(void *)

#define TO_BLOCKS(x) ((x) <= GC_MINIMUM_SIZE*GC_BASE ? GC_MINIMUM_SIZE : (((x) - 1)/GC_BASE) + 1)

#define INITIAL_GC NULL
//typedef struct frame *gc_t;

#define gc_frame0(gc,n,...) struct { struct frame *prev; unsigned nptrs;void *ptrs[n]; } l \
          = { gc, n, { __VA_ARGS__ } }; gc_t gc = (gc_t)(void *)&l;

#define gc_count(ty)  (TO_BLOCKS(sizeof(ty)))
#define gc_mk_alloc_tag(ty, np, tag) static inline ty *gc_alloc_ ## ty(gc_t gc) { ty *x = gc_alloc(gc,gc_count(ty),np); gc_tag(x) = tag; return x; }
#define gc_mk_alloc(ty, np) gc_mk_alloc_tag(ty,np,0)
#define gc_mk_alloc_tag_s(ty, np, tag) static inline ty *gc_alloc_ ## ty ## _s(gc_t gc, ty v) { ty *x = gc_alloc(gc,gc_count(ty),np); gc_tag(x) = tag; *x = v; return x; }
#define gc_tag(p) (((entry_header_t *)((void *)p - sizeof(void *)))->tag)

#define FOOF 0xF00DF00FACEBAFFUL

void gc_print_stats(gc_t gc);
void gc_perform_gc(gc_t gc);
void *gc_alloc_tag(gc_t gc,unsigned count, unsigned nptrs, int tag);

static inline void *
gc_alloc_bytes(gc_t gc,size_t count) {
        return gc_alloc_tag(gc, TO_BLOCKS(count), 0, 0);
}

bool gc_add_root(gc_t gc, void *root);
bool gc_del_root(gc_t gc, void *root);

#endif


#include <Judy.h>
#include <assert.h>
#include <stdio.h>


#if JGC_STATUS > 1
#define debugf(...) fprintf(stderr,__VA_ARGS__)
#else
#define debugf(...)
#endif


static Pvoid_t gc_roots = NULL;       // extra roots in addition to the stack
static Pvoid_t gc_allocated = NULL;   // black set of currently allocated memory
static size_t heap_threshold = 2048;  // threshold at which we want to run a gc rather than malloc more memory
static size_t mem_inuse;              // amount of memory in use by gc'ed memory
static unsigned number_gcs;           // number of garbage collections
static unsigned number_allocs;        // number of allocations since last garbage collection

#define SHOULD_FOLLOW(w)  IS_PTR(w)

typedef struct {
        union {
                entry_header_t v;
                void * _dummy;
        } u;
        void * ptrs[0];
} entry_t;


static bool
gc_add_root(gc_t gc, void *root)
{
        if(SHOULD_FOLLOW(root)) {
                int r;
                J1S(r,gc_roots,((Word_t)root / GC_BASE) - 1 );
                return (bool)r;
        } else 
                return false;
}

static bool
gc_del_root(gc_t gc, void *root)
{
        if(SHOULD_FOLLOW(root)) {
                int r;
                J1U(r,gc_roots,((Word_t)root / GC_BASE) - 1);
                return (bool)r;
        } else 
                return false;
}

static void
gc_print_stats(gc_t gc)
{
        Word_t n_allocated,n_roots;
        J1C(n_allocated,gc_allocated,0,-1);
        J1C(n_roots,gc_roots,0,-1);
        fprintf(stderr,"allocated: %5lu roots: %3lu mem_inuse: %5lu heap_threshold: %5lu gcs: %3u\n",n_allocated,n_roots,(long unsigned)mem_inuse,(long unsigned)heap_threshold,number_gcs);
}

static void
gc_perform_gc(gc_t gc)
{
        profile_push(&gc_gc_time);
        unsigned number_redirects = 0;
        unsigned number_stack = 0;
        unsigned number_ptr = 0;
        unsigned number_whnf = 0;
        number_gcs++;
        Pvoid_t gc_grey = NULL;
        Pvoid_t gc_black = NULL;
        Word_t ix;
        int r;
        // initialize the grey set with the roots
        debugf("Setting Roots:");
        for(ix = 0,(J1F(r,gc_roots,ix)); r; (J1N(r,gc_roots,ix))) {
                debugf(" %p",(void *)(ix * GC_BASE));
                int d; J1S(d,gc_grey,ix);
        }
        debugf("\n");
        debugf("Trace:");
        for(;gc;gc = gc->prev) {
                debugf(" |");
                for(unsigned i = 0;i < gc->nptrs; i++) {
                        number_stack++;
                        //if(IS_LAZY(gc->ptrs[i])) {
                        //        if(!IS_LAZY(GETHEAD(FROM_SPTR(gc->ptrs[i])))) {
                        //                number_redirects++;
                        //                debugf(" *");
                        //                gc->ptrs[i] = GETHEAD(FROM_SPTR(gc->ptrs[i]));
                        //                number_whnf++;
                        //        }
                        //} else {
                        //        number_whnf++;
                        //}
                        if(__predict_false(!SHOULD_FOLLOW(gc->ptrs[i]))) {
                                debugf(" -");
                                continue;
                        }
                        number_ptr++;
                        entry_t *e = (entry_t *)FROM_SPTR(gc->ptrs[i]) - 1;
                        debugf(" %p",(void *)e);
                        int d; J1S(d,gc_grey,(Word_t)e / GC_BASE);
                }
        }
        debugf("\n");
        // trace the grey
        while(ix = 0,(J1F(r,gc_grey,ix)),r) {
                debugf("Processing Grey: %p ",(void *)(ix * GC_BASE));
                J1U(r,gc_grey,ix);
                J1U(r,gc_allocated,ix);
                if(__predict_false(r == 0)) {
                        debugf("Skipping.\n");
                        continue;
                }
                debugf("Blackening\n");
                J1S(r,gc_black,ix);

                entry_t *e = (entry_t *)(ix * GC_BASE);
                int offset = e->u.v.tag ? 1 : 0;
                for(int i = 0 + offset;i < e->u.v.nptrs + offset; i++) {
                        if(P_LAZY == GET_PTYPE(e->ptrs[i])) {
                                if(!IS_LAZY(GETHEAD(FROM_SPTR(e->ptrs[i])))) {
                                        number_redirects++;
                                        debugf(" *");
                                        e->ptrs[i] = GETHEAD(FROM_SPTR(e->ptrs[i]));
                                }
                        }
                        entry_t * ptr = e->ptrs[i];
                        if(SHOULD_FOLLOW(ptr)) {
                                ptr = FROM_SPTR(ptr);
                                debugf("Following: %p %p\n",e->ptrs[i], (void *)(ptr - 1));
                                Word_t p = (Word_t)(ptr - 1) / GC_BASE;
                                int r;
                                J1T(r,gc_black,p);
                                if(__predict_true(!r))
                                        J1S(r,gc_grey,p);
                        }

                }
        }
        assert(gc_grey == NULL);
        for(ix = 0, (J1F(r,gc_allocated,ix)); r; (J1N(r,gc_allocated,ix))) {
                entry_t *e = (entry_t *)(ix * GC_BASE);
                mem_inuse -= (e->u.v.count + 1)*GC_BASE;
                free(e);
        }
        J1FA(r,gc_allocated);
        gc_allocated = gc_black;
#if JGC_STATUS
        fprintf(stderr, "Ss: %5u Ws: %5u Ps: %5u Rs: %5u As: %5u ", number_stack, number_whnf, number_ptr, number_redirects, number_allocs);
        number_allocs = 0;
        gc_print_stats(gc);
#endif
        profile_pop(&gc_gc_time);
}

static void *
gc_alloc_tag(gc_t gc,unsigned count, unsigned nptrs, int tag)
{
        profile_push(&gc_alloc_time);
        number_allocs++;
        assert(nptrs <= count);
        if(__predict_false(mem_inuse > heap_threshold)) {
                gc_perform_gc(gc);
                if(__predict_false(mem_inuse > ((heap_threshold * 6) / 10))) {
                        heap_threshold *= 2;
#if JGC_STATUS
                        fprintf(stderr, "Increasing heap threshold to %u bytes because mem usage is %u.\n", (unsigned) heap_threshold, (unsigned)mem_inuse);
#endif
                }
        }
        entry_t *e = malloc((count + 1)*GC_BASE);
        mem_inuse += (count + 1)*GC_BASE;
        e->u.v.count = count;
        e->u.v.nptrs = nptrs;
        e->u.v.tag = tag;
        debugf("allocated: %p %i %i %i\n",(void *)e, count, nptrs, tag);
        int r; J1S(r,gc_allocated,(Word_t)e / GC_BASE);
        profile_pop(&gc_alloc_time);
        return (void *)(e + 1);
}
