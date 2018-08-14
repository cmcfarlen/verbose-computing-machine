#ifndef _HAMT_H_
#define _HAMT_H_

#include <stdint.h>
#include <assert.h>

typedef uint32_t (*hash_fn_t)(void*,int);
typedef int (*compare_fn_t)(void*,void*);

typedef struct hamt hamt;
typedef struct hamt_iterator hamt_iterator;

uint32_t hamt_hash_key(const char* key, uint32_t len, int level);

hamt* hamt_init(hamt*, hash_fn_t f, compare_fn_t c);
void hamt_compact(hamt* t);

void hamt_insert(hamt*, void* key, void* value);
void* hamt_find(hamt* t, void* key);
void* hamt_remove(hamt* t, void* key);


int hamt_iterator_is_end(hamt_iterator* it);
void hamt_iterator_next(hamt_iterator* it);
hamt_iterator* hamt_iterator_begin(hamt_iterator* it, hamt* t);
void* hamt_key(hamt_iterator* it);
void* hamt_value(hamt_iterator* it);

#endif

// Implementation

#ifdef HAMT_IMPLEMENATION

#define HAMT_T 32
#define HAMT_T_BITS 5
#define HAMT_T_ENTRIES (1 << HAMT_T_BITS)
#define HAMT_T_MASK (HAMT_T_ENTRIES - 1)
#define HAMT_ENTRY_POOL_SIZE 4096
#define TOIDX(h) (h >> shift_bits) & HAMT_T_MASK
#define HAMT_ITERATOR_STACK_DEPTH 8

typedef struct hamt_entry
{
   uintptr_t korm;
   uintptr_t p;
} hamt_entry;

typedef union hamt_freelist_node
{
   union hamt_freelist_node* next;
   hamt_entry entry;
} hamt_freelist_node;

typedef struct hamt_entry_pool
{
   struct hamt_entry_pool* next;
   char* b;
   char* p;
   char* e;
} hamt_entry_pool;

struct hamt
{
   hamt_entry entries[HAMT_T_ENTRIES];
   hamt_freelist_node* freelists[HAMT_T_ENTRIES];
   hash_fn_t hash_fn;
   compare_fn_t compare_fn;
   hamt_entry_pool* pool;
};

#ifdef _MSC_VER
#include <nmmintrin.h>
int _mm_popcnt_u32(unsigned int);

static inline int ctpop(uintptr_t v)
{
   return _mm_popcnt_u32(v & 0xffffffff);
}

#else
#include <x86intrin.h>

static inline int ctpop(uintptr_t v)
{
   return __builtin_popcount(v & 0xffffffff);
}

inline
uint64_t clocks()
{
   unsigned int aux;
   return __rdtscp(&aux);
}

#endif

char* hamt_advance_to_alignment(char* p, uintptr_t align)
{
   uintptr_t v = (uintptr_t)p;
   return p + (align - (v & (align-1)));
}

hamt_entry_pool* hamt_alloc_pool(size_t size)
{
   char* p = (char*)malloc(size);
   hamt_entry_pool* result = (hamt_entry_pool*)p;
   result->next = 0;
   result->b = hamt_advance_to_alignment(p + sizeof(hamt_entry_pool), 16);
   result->e = p + size;
   result->p = result->b;

   return result;
}

uint32_t hamt_hash_key(uint32_t h, const char* key, uint32_t len, int level)
{
   uint32_t a = 31415;
   uint32_t b = 27183;
   uint32_t l = level + 1;

   while (len--) {
      h = a * h *l + *key++;
      a *= b;
   }
   return h;
}

uint32_t hamt_hash_key(const char* key, uint32_t len, int level)
{
   return hamt_hash_key(0, key, len, level);
}

int compare_string_key(void* a, void* b)
{
   return strcmp((char*)a, (char*)b);
}

// level 0 is most significant 5 bits
uint32_t level_index(uint32_t k, int level)
{
   return HAMT_T_MASK & (k >> (HAMT_T - (HAMT_T_BITS * (level + 1))));
}

// remove the ptr tags that identify the type of p (value or base pointer)
void* ptoptr(uintptr_t p)
{
   return (void*)(p & ~0x3);
}

// alloc a subtree node of length len
hamt_entry* hamt_alloc_node(hamt* t, int len)
{
   hamt_entry* result = 0;
   hamt_freelist_node* next = t->freelists[len-1];

   if (!next) {
      size_t size = sizeof(hamt_entry)*len;
      size_t rem = t->pool->e - t->pool->p;
      if (rem < size) {
         hamt_entry_pool* newpool = hamt_alloc_pool(HAMT_ENTRY_POOL_SIZE);
         newpool->next = t->pool;
         t->pool = newpool;
      }

      result = (hamt_entry*)t->pool->p;
      t->pool->p += size;
   } else {
      hamt_freelist_node* nnext = next->next;
      t->freelists[len-1] = nnext;
      result = &next->entry;
   }
   memset(result, 0, sizeof(hamt_entry)*len);
   return result;
}

void hamt_free_node(hamt* t, void* e, int len)
{
   hamt_freelist_node* n = (hamt_freelist_node*)e;

   n->next = t->freelists[len-1];
   t->freelists[len-1] = n;
}

void hamt_compact_entry(hamt* t, hamt_entry* e)
{
   uint32_t table_size = ctpop(e->korm);
   hamt_entry* otable = (hamt_entry*)ptoptr(e->p);
   hamt_entry* ntable = hamt_alloc_node(t, table_size);

   for (uint32_t i = 0; i < table_size; i++) {
      hamt_entry* c = otable + i;
      hamt_entry* d = ntable + i;

      d->korm = c->korm;
      d->p = c->p;

      if (d->p & 0x2) {
         hamt_compact_entry(t, d);
      }
   }

   e->p = (uintptr_t)ntable | 0x2;
}

hamt* hamt_init(hamt* result, hash_fn_t f, compare_fn_t c)
{
   result->hash_fn = f;
   result->compare_fn = c;
   result->pool = hamt_alloc_pool(HAMT_ENTRY_POOL_SIZE);
   return result;
}

void hamt_compact(hamt* t)
{
   hamt_entry_pool* p = t->pool;

   t->pool = hamt_alloc_pool(HAMT_ENTRY_POOL_SIZE);

   // clear the free list so new tables are allocated from new pools
   for (int i = 0; i < HAMT_T_ENTRIES; i++) {
      t->freelists[i] = 0;
   }

   for (int i = 0; i < HAMT_T_ENTRIES; i++) {
      hamt_entry* e = t->entries + i;
      if (e->p & 0x2) {
         hamt_compact_entry(t, e);
      }
   }

   while (p) {
      hamt_entry_pool* tmp = p->next;
      free(p);
      p = tmp;
   }
}


void hamt_insert_recur(hamt* t, hamt_entry* e, uint32_t shift_bits, uint32_t hash, void* key, void* value)
{
   if (e->p & 0x1) {
      uint32_t ehash = t->hash_fn((void*)e->korm, 0);
      if (ehash == hash) {
         if (t->compare_fn((void*)e->korm, key) == 0) {
            e->p = ((uintptr_t)value & 0x1);
            return;
         } else {
            // TODO(cmcfarlen): handle hash collision
            assert(0);
         }
      }

      hamt_entry* ntable = hamt_alloc_node(t, 1);
      uint32_t eidx = TOIDX(ehash);
      ntable->korm = e->korm;
      ntable->p = e->p;
      e->korm = (uintptr_t)(1 << eidx);
      e->p = ((uintptr_t)ntable) | 0x2;

      hamt_insert_recur(t, e, shift_bits, hash, key, value);
   } else {
      uint32_t idx = TOIDX(hash);
      uint32_t collides = (uintptr_t)(1 << idx) & e->korm;

      if (collides) {
         hamt_entry* se = (hamt_entry*)ptoptr(e->p);
         e = se + ctpop(e->korm & (collides-1));
         hamt_insert_recur(t, e, shift_bits + HAMT_T_BITS, hash, key, value);
      } else {
         // add bit
         uint32_t table_size = ctpop(e->korm);
         hamt_entry* ntable = hamt_alloc_node(t, table_size+1);
         hamt_entry* te = ntable;
         hamt_entry* oe = (hamt_entry*)ptoptr(e->p);
         for (uint32_t i = 0; i < HAMT_T; i++) {
            if (i == idx) {
               te->korm = (uintptr_t)key;
               te->p = ((uintptr_t)value | 0x1);
               te++;
            } else if (e->korm & (uintptr_t)(1 << i)) {
               te->korm = oe->korm;
               te->p = oe->p;
               te++;
               oe++;
            }
         }

         e->korm |= (uintptr_t)(1 << idx);
         hamt_free_node(t, ptoptr(e->p), table_size);
         e->p = ((uintptr_t)ntable | 0x2);
      }
   }
}

void hamt_insert(hamt* t, void* key, void* value)
{
   uint32_t shift_bits = 0;
   uint32_t hash_level = 0;
   uint32_t hash = t->hash_fn(key, hash_level);

   uint32_t idx = TOIDX(hash);
   hamt_entry* e = t->entries + idx;

   if (e->p == 0) {
      e->korm = (uintptr_t)key;
      e->p = ((uintptr_t)value) | 0x1;
   } else {
      hamt_insert_recur(t, e, shift_bits + HAMT_T_BITS, hash, key, value);
   }
}

void* hamt_find_recur(hamt* t, hamt_entry* e, uint32_t shift_bits, uint32_t hash, void* key)
{
   void* result = 0;
   if (e->p & 0x1) {
      if (t->compare_fn((void*)e->korm, key) == 0) {
         result = (void*)ptoptr(e->p);
      }
   } else {
      uint32_t idx = TOIDX(hash);
      uint32_t collides = (uintptr_t)(1 << idx) & e->korm;
      if (collides) {
         hamt_entry* se = (hamt_entry*)ptoptr(e->p);
         e = se + ctpop(e->korm & (collides-1));
         result = hamt_find_recur(t, e, shift_bits + HAMT_T_BITS, hash, key);
      }
   }

   return result;
}

void* hamt_find(hamt* t, void* key)
{
   uint32_t shift_bits = 0;
   uint32_t hash_level = 0;
   uint32_t hash = t->hash_fn(key, hash_level);

   uint32_t idx = TOIDX(hash);
   hamt_entry* e = t->entries + idx;
   void* result = 0;

   if (e->p) {
      result = hamt_find_recur(t, e, shift_bits + HAMT_T_BITS, hash, key);
   }
   return result;
}

void* hamt_remove_recur(hamt* t, hamt_entry* p, uint32_t idx, hamt_entry* e, uint32_t shift_bits, uint32_t hash, void* key)
{
   void* result = 0;
   if (e->p & 0x1) {
      if (t->compare_fn((void*)e->korm, key) == 0) {
         result = (void*)ptoptr(e->p);
         if (p) {
            int table_size = ctpop(p->korm);
            hamt_entry* table = (hamt_entry*)ptoptr(p->p);

            // unpossible!
            assert(table_size != 1);

            if (table_size > 2) {
               // reduce subtable size
               hamt_entry* ntable = hamt_alloc_node(t, table_size-1);
               hamt_entry* te = ntable;
               hamt_entry* oe = table;
               for (uint32_t i = 0; i < HAMT_T; i++) {
                  if (i == idx) {
                     oe++;
                  } else if (p->korm & (uintptr_t)(1 << i)) {
                     te->korm = oe->korm;
                     te->p = oe->p;
                     te++;
                     oe++;
                  }
               }

               uintptr_t mask = (uintptr_t)(1 << idx);
               p->korm ^= mask;
               p->p = ((uintptr_t)ntable | 0x2);
            } else {
               // replace parent with other entry
               hamt_entry* oe = table;
               if (e == table) {
                  oe = table + 1;
               }

               if (oe->p & 0x1) {
                  p->korm = oe->korm;
                  p->p = oe->p;
               } else {
                  // if the other entry is a mask entry, then the heigh of the tree must be maintained
                  hamt_entry* ntable = hamt_alloc_node(t, 1);
                  ntable->korm = oe->korm;
                  ntable->p = oe->p;

                  uintptr_t mask = (uintptr_t)(1 << idx);
                  p->korm ^= mask;
                  p->p = ((uintptr_t)ntable | 0x2);
               }
            }
            hamt_free_node(t, (void*)table, table_size);
         } else {
            // key is in the root, just mark empty
            e->p = 0;
            e->korm = 0;
         }
      }
   } else {
      uint32_t eidx = TOIDX(hash);
      uint32_t collides = (uintptr_t)(1 << eidx) & e->korm;
      if (collides) {
         hamt_entry* se = (hamt_entry*)ptoptr(e->p);
         result = hamt_remove_recur(t, e, eidx, se + ctpop(e->korm & (collides-1)), shift_bits + HAMT_T_BITS, hash, key);

         if (e->p & 0x1 && p) {
            int table_size = ctpop(p->korm);
            if (table_size == 1) {
               hamt_entry* table = (hamt_entry*)ptoptr(p->p);
               p->korm = table->korm;
               p->p = table->p;
               hamt_free_node(t, (void*)table, table_size);
            }
         }
      }
   }

   return result;
}

void* hamt_remove(hamt* t, void* key)
{
   uint32_t shift_bits = 0;
   uint32_t hash_level = 0;
   uint32_t hash = t->hash_fn(key, hash_level);

   uint32_t idx = TOIDX(hash);
   hamt_entry* e = t->entries + idx;
   void* result = 0;

   if (e->p) {
      result = hamt_remove_recur(t, 0, 0, e, shift_bits + HAMT_T_BITS, hash, key);
   }
   return result;
}

typedef struct hamt_iterator_entry
{
   hamt_entry* table;
   int table_idx;
   int table_size;
} hamt_iterator_entry;

typedef struct hamt_iterator
{
   hamt* t;
   hamt_iterator_entry stack[HAMT_ITERATOR_STACK_DEPTH];
   int stack_idx;
} hamt_iterator;

int hamt_iterator_is_end(hamt_iterator* it)
{
   return (it->stack_idx == 0) &&
          (it->stack->table_idx == it->stack->table_size);
}

void hamt_iterator_next(hamt_iterator* it)
{
   if (!hamt_iterator_is_end(it)) {
      hamt_iterator_entry* e = it->stack + it->stack_idx;

      do {
         e->table_idx++;
         while (e->table_idx == e->table_size) {
            if (it->stack_idx > 0) {
               it->stack_idx--;
               e--;
               e->table_idx++;
            } else {
               e = 0;
               break;
            }
         }

         while (e && e->table[e->table_idx].p & 0x2) {
            assert(it->stack_idx+1 < HAMT_ITERATOR_STACK_DEPTH);

            int table_size = ctpop(e->table[e->table_idx].korm);
            hamt_entry* newtable = (hamt_entry*)ptoptr(e->table[e->table_idx].p);
            e++;
            e->table = newtable;
            e->table_size = table_size;
            e->table_idx = 0;
            it->stack_idx++;
         }
      } while (e && (e->table[e->table_idx].p & 0x1) == 0);
   }
}

hamt_iterator* hamt_iterator_begin(hamt_iterator* it, hamt* t)
{
   it->t = t;
   it->stack_idx = 0;

   hamt_iterator_entry* e = it->stack;


   e->table = t->entries;
   e->table_size = 32;
   e->table_idx = -1;

   hamt_iterator_next(it);

   return it;
}

void* hamt_key(hamt_iterator* it)
{
   if (!hamt_iterator_is_end(it)) {
      hamt_iterator_entry* e = it->stack + it->stack_idx;
      hamt_entry* p = e->table + e->table_idx;

      assert(p->p & 0x1);

      return (void*)p->korm;
   }
   return 0;
}

void* hamt_value(hamt_iterator* it)
{
   if (!hamt_iterator_is_end(it)) {
      hamt_iterator_entry* e = it->stack + it->stack_idx;
      hamt_entry* p = e->table + e->table_idx;

      assert(p->p & 0x1);

      return (void*)ptoptr(p->p);
   }
   return 0;
}



#endif

