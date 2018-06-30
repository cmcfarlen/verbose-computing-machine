// https://infoscience.epfl.ch/record/64398/files/idealhashtrees.pdf

#include <stdio.h>
#include <nmmintrin.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <memory.h>
#include <assert.h>

#define T 32
#define T_BITS 5
#define T_ENTRIES (1 << T_BITS)
#define T_MASK (T_ENTRIES - 1)
#define T_REHASH_LEVELS 6

typedef uint32_t (*hash_fn_t)(void*,int);
typedef int (*compare_fn_t)(void*,void*);

typedef struct amt_entry
{
   uintptr_t korm;
   uintptr_t p;
} amt_entry;

typedef union freelist_node
{
   union freelist_node* next;
   amt_entry entry;
} freelist_node;

typedef struct amt
{
   amt_entry entries[T_ENTRIES];
   freelist_node* freelists[T_ENTRIES];
   hash_fn_t hash_fn;
   compare_fn_t compare_fn;
} amt;

#ifdef _MSC_VER
int _mm_popcnt_u32(unsigned int);

int ctpop(uintptr_t v)
{
   return _mm_popcnt_u32(v & 0xffffffff);
}

#else

static inline int ctpop(uintptr_t v)
{
   return __builtin_popcount(v & 0xffffffff);
}

#endif

void* aligned_malloc(size_t len)
{
   void* r = malloc(len);
   assert(0 == ((uintptr_t)r & 0xf));
   return r;
}

uint32_t hash_key(const char* key, uint32_t len, int level)
{
   uint32_t a = 31415;
   uint32_t b = 27183;
   uint32_t h = 0;
   uint32_t l = level + 1;

   while (len--) {
      h = a * h *l + *key++;
      a *= b;
   }
   return h;
}

uint32_t hash_string_key(void* k, int level)
{
   return hash_key((const char*)k, (uint32_t)strlen((const char*)k), level);
}


int compare_string_key(void* a, void* b)
{
   return strcmp((char*)a, (char*)b) == 0;
}

// level 0 is most significant 5 bits
uint32_t level_index(uint32_t k, int level)
{
   return T_MASK & (k >> (T - (T_BITS * (level + 1))));
}

// remove the ptr tags that identify the type of p (value or base pointer)
void* ptoptr(uintptr_t p)
{
   return (void*)(p & ~0x3);
}

amt* create_amt(hash_fn_t f, compare_fn_t c)
{
   amt* result = aligned_malloc(sizeof(amt));
   memset(result, 0, sizeof(amt));
   result->hash_fn = f;
   result->compare_fn = c;
   return result;
}

// alloc a subtree node of length len
amt_entry* amt_alloc_node(amt* t, int len)
{
   amt_entry* result = 0;
   freelist_node* next = t->freelists[len-1];

   if (!next) {
      result = (amt_entry*)aligned_malloc(sizeof(amt_entry)*len);
   } else {
      freelist_node* nnext = next->next;
      t->freelists[len-1] = nnext;
      result = &next->entry;
   }
   memset(result, 0, sizeof(amt_entry)*len);
   return result;
}

void amt_free_node(amt* t, void* e, int len)
{
   freelist_node* n = (freelist_node*)e;

   n->next = t->freelists[len-1];
   t->freelists[len-1] = n;
}

#define TOIDX(h) (h >> shift_bits) & T_MASK

void insert_recur(amt* t, amt_entry* e, uint32_t shift_bits, uint32_t hash, void* key, void* value)
{
   if (e->p & 0x1) {
      uint32_t ehash = t->hash_fn((void*)e->korm, 0);
      if (ehash == hash) {
         if (t->compare_fn((void*)e->korm, key)) {
            e->p = ((uintptr_t)value & 0x1);
            return;
         } else {
            // TODO(cmcfarlen): handle hash collision
            assert(0);
         }
      }

      amt_entry* ntable = amt_alloc_node(t, 1);
      uint32_t eidx = TOIDX(ehash);
      ntable->korm = e->korm;
      ntable->p = e->p;
      e->korm = (uintptr_t)(1 << eidx);
      e->p = ((uintptr_t)ntable) | 0x2;

      insert_recur(t, e, shift_bits, hash, key, value);
   } else {
      uint32_t idx = TOIDX(hash);
      uint32_t collides = (uintptr_t)(1 << idx) & e->korm;

      if (collides) {
         amt_entry* se = (amt_entry*)ptoptr(e->p);
         e = se + ctpop(e->korm & (collides-1));
         insert_recur(t, e, shift_bits + T_BITS, hash, key, value);
      } else {
         // add bit
         uint32_t table_size = ctpop(e->korm);
         amt_entry* ntable = amt_alloc_node(t, table_size+1);
         amt_entry* te = ntable;
         amt_entry* oe = (amt_entry*)ptoptr(e->p);
         for (uint32_t i = 0; i < T; i++) {
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
         amt_free_node(t, ptoptr(e->p), table_size);
         e->p = ((uintptr_t)ntable | 0x2);
      }
   }
}


void insert(amt* t, void* key, void* value)
{
   uint32_t shift_bits = 0;
   uint32_t hash_level = 0;
   uint32_t hash = t->hash_fn(key, hash_level);

   uint32_t idx = TOIDX(hash);
   amt_entry* e = t->entries + idx;

   if (e->p == 0) {
      e->korm = (uintptr_t)key;
      e->p = ((uintptr_t)value) | 0x1;
   } else {
      insert_recur(t, e, shift_bits + T_BITS, hash, key, value);
   }
}

void* find_recur(amt* t, amt_entry* e, uint32_t shift_bits, uint32_t hash, void* key)
{
   void* result = 0;
   if (e->p & 0x1) {
      if (t->compare_fn(ptoptr(e->korm), key)) {
         result = (void*)ptoptr(e->p);
      }
   } else {
      uint32_t idx = TOIDX(hash);
      uint32_t collides = (uintptr_t)(1 << idx) & e->korm;
      if (collides) {
         amt_entry* se = (amt_entry*)ptoptr(e->p);
         e = se + ctpop(e->korm & (collides-1));
         result = find_recur(t, e, shift_bits + T_BITS, hash, key);
      }
   }

   return result;
}

void* find(amt* t, void* key)
{
   uint32_t shift_bits = 0;
   uint32_t hash_level = 0;
   uint32_t hash = t->hash_fn(key, hash_level);

   uint32_t idx = TOIDX(hash);
   amt_entry* e = t->entries + idx;
   void* result = 0;

   if (e->p) {
      result = find_recur(t, e, shift_bits + T_BITS, hash, key);
   }
   return result;
}

void* hamt_remove_recur(amt* t, amt_entry* p, uint32_t idx, amt_entry* e, uint32_t shift_bits, uint32_t hash, void* key)
{
   void* result = 0;
   if (e->p & 0x1) {
      if (t->compare_fn(ptoptr(e->korm), key)) {
         result = (void*)ptoptr(e->p);
         if (p) {
            int table_size = ctpop(p->korm);
            amt_entry* table = (amt_entry*)ptoptr(p->p);

            // unpossible!
            assert(table_size != 1);

            if (table_size > 2) {
               // reduce subtable size
               amt_entry* ntable = amt_alloc_node(t, table_size-1);
               amt_entry* te = ntable;
               amt_entry* oe = table;
               for (uint32_t i = 0; i < T; i++) {
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
               p->korm &= ~mask;
               p->p = ((uintptr_t)ntable | 0x2);
            } else {
               // replace parent with other entry
               amt_entry* oe = table;
               if (e == table) {
                  oe = table + 1;
               }

               p->korm = oe->korm;
               p->p = oe->p;
            }
            amt_free_node(t, (void*)table, table_size);
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
         amt_entry* se = (amt_entry*)ptoptr(e->p);
         result = hamt_remove_recur(t, e, eidx, se + ctpop(e->korm & (collides-1)), shift_bits + T_BITS, hash, key);

         if (e->p & 0x1 && p) {
            int table_size = ctpop(p->korm);
            if (table_size == 1) {
               printf("%s: parent table size is one\n", (char*)key);
               amt_entry* table = (amt_entry*)ptoptr(p->p);
               p->korm = table->korm;
               p->p = table->p;
               amt_free_node(t, (void*)table, table_size);
            }
         }
      }
   }

   return result;
}

void* hamt_remove(amt* t, void* key)
{
   uint32_t shift_bits = 0;
   uint32_t hash_level = 0;
   uint32_t hash = t->hash_fn(key, hash_level);

   uint32_t idx = TOIDX(hash);
   amt_entry* e = t->entries + idx;
   void* result = 0;

   if (e->p) {
      result = hamt_remove_recur(t, 0, 0, e, shift_bits + T_BITS, hash, key);
   }
   return result;
}

void visit_entry(amt_entry* e, void(*f)(void*, int, amt_entry*), void* arg, int level)
{
   if (e->p & 0x1) {
      f(arg, level, e);
   } else {
      amt_entry* se = (amt_entry*)ptoptr(e->p);
      for (int i = 0; i < T; i++) {
         if (e->korm & (uintptr_t)(1 << i)) {
            visit_entry(se++, f, arg, level+1);
         }
      }
   }
}

void visit(amt* t, void(*f)(void*, int, amt_entry*), void* arg)
{
   for (int i = 0; i < T; i++) {
      if (t->entries[i].p) {
         visit_entry(t->entries + i, f, arg, 0);
      }
   }
}

int count_list(freelist_node* n)
{
   int cnt = 0;
   while (n) {
      ++cnt;
      n = n->next;
   }
   return cnt;
}

typedef struct amt_stats
{
   int entry_count;
   int key_count;
   int subtree_count;
   int max_level;
} amt_stats;

static int print_keys = 0;

void stat_visit(void* p, int level, amt_entry* e)
{
   amt_stats* s = (amt_stats*)p;
   s->entry_count++;
   if (level > s->max_level) {
      s->max_level = level;
   }
   if (e->p & 0x1) {
      if (print_keys) {
         printf("key: %s\n", (char*)e->korm);
      }
      s->key_count++;
   } else if (e->p & 2) {
      s->subtree_count++;
   }
}

void print_stats(amt* t)
{
   amt_stats stats = {0};
   printf("Freelists:\n");
   for (int i = 0; i < T; i++) {
      printf("%3i ", i);
   }
   printf("\n");
   for (int i = 0; i < T; i++) {
      printf("%3i ", count_list(t->freelists[i]));
   }
   printf("\n");

   visit(t, stat_visit, &stats);

   printf("max level: %i\n", stats.max_level);
   printf("entry count: %i\n", stats.entry_count);
   printf("key count: %i\n", stats.key_count);
   printf("subtree count: %i\n", stats.subtree_count);
}

void insert_cstr(amt* t, const char* s)
{
   insert(t, (void*)s, (void*)s);
}

char* random_string(int len)
{
   static char* chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
   char* buff = (char*)aligned_malloc(len+1);
   for (int i = 0; i < len; i++) {
      buff[i] = chars[rand() % 52];
   }
   buff[len] = 0;
   return buff;
}

int main(int argc, char** argv)
{
   void* memory = aligned_malloc(1024);

   printf("memory pointer %p\n", memory);
   printf("T: %i\n", T);
   printf("T_BITS: %i\n", T_BITS);
   printf("T_ENTRIES: %i\n", T_ENTRIES);
   printf("T_MASK: 0x%x\n", T_MASK);
   printf("amt_entry size: %lld\n", sizeof(amt_entry));

   free(memory);

   amt* h = create_amt(hash_string_key, compare_string_key);

   int cnt = 5000;
   char** keys = (char**)malloc(sizeof(char*)*cnt);
   for (int i = 0; i < cnt; i++) {
      char* buff = (char*)malloc(32);
      snprintf(buff, sizeof(buff), "key%d", i);
      keys[i] = buff;
   }

   for (int i = 0; i < cnt; i++) {
      insert_cstr(h, keys[i]);
      for (int j = 0; j < i; j++) {
         char* t = (char*)find(h, keys[j]);
         if (t) {
            if (strcmp(t, keys[j]) != 0) {
               printf("INSERT(%i, %i): expected %s but got %s\n", i, j, keys[j], t);
            }
         } else {
            printf("INSERT(%i, %i): failed to find %s after inserting %s\n", i, j, keys[j], keys[i]);
         }
      }
   }

   printf("After insert\n");
   print_stats(h);

   for (int i = 0; i < cnt; i++) {
      void* t = find(h, keys[i]);
      if (!t) {
         printf("couldn't find key %i: %s\n", i, keys[i]);
      } else if (strcmp(keys[i], (char*)t)) {
         printf("looked for %s but got %s\n", keys[i], (char*)t);
      }
   }

   for (int i = 0; i < cnt; i++) {
      char* result = (char*)hamt_remove(h, keys[i]);
      if (result) {
         if (strcmp(result, keys[i]) == 0) {
            for (int j = i+1; j < cnt; j++) {
               void* t = find(h, keys[j]);
               if (!t) {
                  printf("REMOVE(%i, %i): couldn't find key %s after removing %s\n", i, j, keys[j], keys[i]);
               } else if (strcmp(keys[j], (char*)t)) {
                  printf("REMOVE(%i, %i): looked for %s but got %s\n", i, j, keys[j], (char*)t);
               }
            }
         } else {
            printf("expected %s removed %s\n", keys[i], result);
         }
      } else {
         printf("Failed to remove %s\n", keys[i]);
      }
   }

   print_keys = 1;
   print_stats(h);

   for (int i = 0; i < cnt; i++) {
      void* t = find(h, keys[i]);
      if (t) {
         printf("still found key %i: %s\n", i, keys[i]);
      }
   }

   printf("Done!\n");

   return 0;
}
