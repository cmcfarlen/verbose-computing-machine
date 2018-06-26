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

int _mm_popcnt_u32(unsigned int);

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

int ctpop(uintptr_t v)
{
   return _mm_popcnt_u32(v & 0xffffffff);
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
   amt* result = malloc(sizeof(amt));
   memset(result, 0, sizeof(amt));
   result->hash_fn = f;
   result->compare_fn = c;
   return result;
}

// alloc a subtree node of length len
amt_entry* amt_alloc_node(amt* t, int len)
{
   amt_entry* result = 0;
   freelist_node* next = t->freelists[len];

   if (!next) {
      result = (amt_entry*)malloc(sizeof(amt_entry)*len);
   } else {
      freelist_node* nnext = next->next;
      t->freelists[len] = nnext;
      result = &next->entry;
   }
   memset(result, 0, sizeof(amt_entry)*len);
   return result;
}

void amt_free_node(amt* t, void* e, int len)
{
   freelist_node* n = (freelist_node*)e;

   n->next = t->freelists[len];
   t->freelists[len] = n;
}

void insert_node(amt* t, amt_entry* e, int level, uint32_t hash, void* key, void* value)
{
   if (e->p & 0x1) {
      // key/value entry
      // collision, convert to table
      amt_entry* ntable = amt_alloc_node(t, 1);

      uint32_t ehash = t->hash_fn((void*)e->korm, level / T_REHASH_LEVELS);
      if (ehash == hash) {
         if (t->compare_fn((void*)e->korm, key)) {
            // already inserted
            return;
         } else {
            // hash collision
            assert(0);
         }
      }
      uint32_t eidx = level_index(ehash, level+1);
      ntable->korm = e->korm;
      ntable->p = e->p;
      e->korm = (uintptr_t)(1 << eidx);
      e->p = ((uintptr_t)ntable) | 0x2;
      // fall through to subtable case
   }

   uint32_t nidx = level_index(hash, level+1);
   uint32_t collides = (uintptr_t)(1 << nidx) & e->korm;
   if (collides) {
      amt_entry* se = (amt_entry*)ptoptr(e->p);
      if (nidx == 0) {
         insert_node(t, se, level+1, hash, key, value);
      } else {
         insert_node(t, se + ctpop(e->korm & (collides-1)), level+1, hash, key, value);
      }
   } else {
      // add bit
      uint32_t table_size = ctpop(e->korm);
      amt_entry* ntable = amt_alloc_node(t, table_size+1);
      amt_entry* te = ntable;
      amt_entry* oe = (amt_entry*)ptoptr(e->p);
      for (uint32_t i = 0; i < T; i++) {
         if (i == nidx) {
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

      e->korm |= (uintptr_t)(1 << nidx);
      amt_free_node(t, ptoptr(e->p), table_size);
      e->p = ((uintptr_t)ntable | 0x2);
   }
}

void insert(amt* t, void* key, void* value)
{
   uint32_t hash = t->hash_fn(key, 0);

   uint32_t idx = level_index(hash, 0);
   amt_entry* e = t->entries + idx;

   if (0 == e->p) {
      e->korm = (uintptr_t)key;
      e->p = ((uintptr_t)value) | 0x1;
   } else {
      insert_node(t, e, 0, hash, key, value);
   }
}

void* find_entry(amt* t, amt_entry* e, int level, uint32_t hash, void* key)
{
   void * result = 0;
   if (e->p & 0x1) {
      if (t->compare_fn(ptoptr(e->korm), key)) {
         result = (void*)ptoptr(e->p);
      }
   } else {
      uint32_t idx = level_index(hash, level+1);
      uint32_t collides = (uintptr_t)(1 << idx) & e->korm;
      if (collides) {
         amt_entry* se = (amt_entry*)ptoptr(e->p);
         if (idx == 0) {
            result = find_entry(t, se, level+1, hash, key);
         } else {
            result = find_entry(t, se + ctpop(e->korm & (collides-1)), level+1, hash, key);
         }
      }
   }
   return result;
}

void* find(amt* t, void* key)
{
   uint32_t hash = t->hash_fn(key, 0);

   uint32_t idx = level_index(hash, 0);
   amt_entry* e = t->entries + idx;
   void* result = 0;

   if (e->p) {
      result = find_entry(t, e, 0, hash, key);
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

void stat_visit(void* p, int level, amt_entry* e)
{
   amt_stats* s = (amt_stats*)p;
   s->entry_count++;
   if (level > s->max_level) {
      s->max_level = level;
   }
   if (e->p & 0x1) {
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

char* random_string(char* buff, int len)
{
   static char* chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
   for (int i = 0; i < len; i++) {
      buff[i] = chars[rand() % 52];
   }
   buff[len-1] = 0;
   return _strdup(buff);
}

int main(int argc, char** argv)
{
   printf("hello world\n");
   printf("popcnt: %i\n", _mm_popcnt_u32(0x01010101));

   void* memory = malloc(1024);

   printf("memory pointer %p\n", memory);
   printf("T: %i\n", T);
   printf("T_BITS: %i\n", T_BITS);
   printf("T_ENTRIES: %i\n", T_ENTRIES);
   printf("T_MASK: 0x%x\n", T_MASK);
   printf("amt_entry size: %lld\n", sizeof(amt_entry));

   free(memory);

   amt* h = create_amt(hash_string_key, compare_string_key);

   const char* v = "HEllow World\n";
   insert_cstr(h, v);

   int cnt = 1000;
   char buff[32];
   srand(0);
   while (cnt--) {
      insert_cstr(h, random_string(buff, sizeof(buff)));
   }

   print_stats(h);

   assert(find(h, (void*)v));

   return 0;
}
