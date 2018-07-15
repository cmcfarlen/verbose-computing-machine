// https://infoscience.epfl.ch/record/64398/files/idealhashtrees.pdf

#define HAMT_IMPLEMENATION
#include "hamt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <assert.h>

void visit_entry(hamt_entry* e, void(*f)(void*, int, hamt_entry*), void* arg, int level)
{
   f(arg, level, e);
   if (e->p & 0x2) {
      hamt_entry* se = (hamt_entry*)ptoptr(e->p);
      for (int i = 0; i < HAMT_T; i++) {
         if (e->korm & (uintptr_t)(1 << i)) {
            visit_entry(se++, f, arg, level+1);
         }
      }
   }
}

void visit(hamt* t, void(*f)(void*, int, hamt_entry*), void* arg)
{
   for (int i = 0; i < HAMT_T; i++) {
      if (t->entries[i].p) {
         visit_entry(t->entries + i, f, arg, 0);
      }
   }
}

int count_list(hamt_freelist_node* n)
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

void stat_visit(void* p, int level, hamt_entry* e)
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

void print_stats(hamt* t)
{
   amt_stats stats = {0};
   size_t freelist_mem = 0;
   size_t page_cnt = 0;
   printf("Freelists:\n");
   for (int i = 0; i < HAMT_T; i++) {
      printf("%3i ", i+1);
   }
   printf("\n");
   for (int i = 0; i < HAMT_T; i++) {
      int c = count_list(t->freelists[i]);
      freelist_mem += (i+1) * c * sizeof(hamt_freelist_node);
      printf("%3i ", c);
   }
   printf("\n");

   hamt_entry_pool* p = t->pool;
   while (p) {
      page_cnt++;
      p = p->next;
   }

   visit(t, stat_visit, &stats);

   if (stats.entry_count) {
      printf("max level: %i\n", stats.max_level);
      printf("entry count: %i\n", stats.entry_count);
      printf("key count: %i\n", stats.key_count);
      printf("subtree count: %i\n", stats.subtree_count);
      printf("tree ratio: %f\n", (float)stats.subtree_count / (float)stats.key_count);
      printf("freelist memory: %lld\n", freelist_mem);
      printf("allocd pages: %zd(%zd bytes)\n", page_cnt, page_cnt*HAMT_ENTRY_POOL_SIZE);
   } else {
      printf("empty\n");
   }
}

void insert_cstr(hamt* t, const char* s)
{
   hamt_insert(t, (void*)s, (void*)s);
}

char* random_string(char* buff, int len)
{
   static char* chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
   char* b = buff;
   for (int i = 0; i < len; i++) {
      *b++ = chars[rand() % 52];
   }
   *b = 0;
   return buff;
}

char** make_random_keys(int count, int length)
{
   uintptr_t align = 8;
   size_t size = sizeof(char*) * count + sizeof(char) * (length+1+align+align) * count;
   char* memory = (char*)malloc(size);

   char** keys = (char**)memory;
   char* k = memory + sizeof(char*) * count;
   for (int i = 0; i < count; i++) {
      keys[i] = k;
      random_string(k, length);
      k += sizeof(char) * (length + 1);
      k = hamt_advance_to_alignment(k, align);
      assert((k + length + 1) < (memory + size));
   }
   return keys;
}

void test_random_keys(hamt* h, int cnt)
{
   char** keys = make_random_keys(cnt, 32);

   printf("\n\nTesting with %i keys\n", cnt);

   for (int i = 0; i < cnt; i++) {
      insert_cstr(h, keys[i]);
      for (int j = 0; j < i; j++) {
         char* t = (char*)hamt_find(h, keys[j]);
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

   hamt_compact(h);

   printf("After compaction\n");
   print_stats(h);

   for (int i = 0; i < cnt; i++) {
      void* t = hamt_find(h, keys[i]);
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
               void* t = hamt_find(h, keys[j]);
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

   print_stats(h);

   for (int i = 0; i < cnt; i++) {
      void* t = hamt_find(h, keys[i]);
      if (t) {
         printf("still found key %i: %s\n", i, keys[i]);
      }
   }

   printf("Done!\n");

   free(keys);
}

uint32_t hash_string_key(void* k, int level)
{
   return hamt_hash_key((const char*)k, (uint32_t)strlen((const char*)k), level);
}

void test_iterator(hamt* h, int cnt)
{
   char** keys = make_random_keys(cnt, 32);

   hamt_iterator it = {0};

   hamt_iterator_begin(&it, h);
   assert(hamt_iterator_is_end(&it));

   for (int i = 0; i < cnt; i++) {
      insert_cstr(h, keys[i]);
   }

   int c = 0;
   hamt_iterator_begin(&it, h);
   while (!hamt_iterator_is_end(&it)) {
      assert(hamt_key(&it));
      assert(hamt_value(&it));
      c++;
      hamt_iterator_next(&it);
   }

   printf("iterated %i times\n", c);
   assert(c == cnt);

   for (int i = 0; i < cnt; i++) {
      hamt_remove(h, keys[i]);
   }
}

int main(int argc, char** argv)
{
   printf("T: %i\n", HAMT_T);
   printf("T_BITS: %i\n", HAMT_T_BITS);
   printf("T_ENTRIES: %i\n", HAMT_T_ENTRIES);
   printf("T_MASK: 0x%x\n", HAMT_T_MASK);
   printf("hamt_entry size: %lld\n", sizeof(hamt_entry));

   hamt ht = {0};
   hamt* h = hamt_init(&ht, hash_string_key, compare_string_key);

   test_iterator(h, 5);
   test_iterator(h, 100);
   test_iterator(h, 1000);
   test_iterator(h, 5000);

   test_random_keys(h, 10);
   test_random_keys(h, 100);
   test_random_keys(h, 1000);
   test_random_keys(h, 2000);
   test_random_keys(h, 5000);
   test_random_keys(h, 2000);
   test_random_keys(h, 1000);
   test_random_keys(h, 100);
   test_random_keys(h, 10);

   hamt_compact(h);
   print_stats(h);

   return 0;
}
