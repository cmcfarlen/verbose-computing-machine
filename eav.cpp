
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#define HAMT_IMPLEMENATION
#include "hamt.h"
#include "bptree.h"

struct memory_arena_page
{
   memory_arena_page* next;
   char* data;
   char* end;
   char* p;
};

struct memory_arena
{
   memory_arena_page* page;
   size_t page_size;
   size_t size;
};

memory_arena_page* allocate_page(size_t page_size)
{
   char* p = (char*)malloc(page_size);

   memory_arena_page* new_page = (memory_arena_page*)p;
   new_page->next = 0;
   new_page->data = p + sizeof(memory_arena_page);
   new_page->end = p + page_size;
   new_page->p = new_page->data;

   return new_page;
}

memory_arena* create_arena(size_t page_size)
{
   memory_arena_page* page = allocate_page(page_size);

   memory_arena* arena = (memory_arena*)page->p;
   page->p += sizeof(memory_arena);

   arena->page = page;
   arena->size = sizeof(memory_arena);
   arena->page_size = page_size;

   return arena;
}

void* arena_allocate(memory_arena* arena, size_t size)
{
   size_t remaining = arena->page->end - arena->page->p;

   if (remaining < size) {
      // make another page!
      memory_arena_page* page = allocate_page(arena->page_size < size ? size : arena->page_size);
      page->next = arena->page;
      arena->page = page;
   }

   void* p = arena->page->p;
   arena->page->p += size;
   arena->size += size;

   return p;
}

#define push_struct(arena, type) (type *)arena_allocate(arena, sizeof(type))

struct keyword
{
   const char* ns;
   const char* n;
};

void print_keyword(keyword* kw)
{
   printf(":%s/%s", kw->ns, kw->n);
}

uint32_t hash_keyword(void* k, int lvl)
{
   keyword* kw = (keyword*)k;
   uint32_t h = hamt_hash_key(0, kw->ns, strlen(kw->ns), lvl);

   h = hamt_hash_key(h, kw->n, strlen(kw->n), lvl);

   return h;
}

int compare_keyword(void* ain, void* bin)
{
   keyword* a = (keyword*)ain;
   keyword* b = (keyword*)bin;
   int sc = strcmp(a->ns, b->ns);
   if (sc == 0) {
      sc = strcmp(a->n, b->n);
   }

   return sc;
}

struct interned_keywords
{
   memory_arena* arena;
   hamt h;
};

interned_keywords *global_keywords = 0;

keyword* kw(const char* ns, const char* n)
{
   if (global_keywords == 0) {
      memory_arena* a = create_arena(4096);
      interned_keywords* ikw = push_struct(a, interned_keywords);
      ikw->arena = a;
      hamt_init(&ikw->h, hash_keyword, compare_keyword);

      global_keywords = ikw;
   }

   keyword k = {ns, n};

   keyword* found = (keyword*)hamt_find(&global_keywords->h, &k);

   if (!found) {
      found = push_struct(global_keywords->arena, keyword);
      found->ns = ns;
      found->n = n;

      hamt_insert(&global_keywords->h, found, found);
   }

   return found;
}

struct ref_t {
   int64_t r;
};

inline
ref_t ref(int64_t r)
{
   ref_t result = {r};
   return result;
}

// TODO: blob, vector types
enum value_type
{
   int_value,
   boolean_value,
   float_value,
   string_value,
   ref_value
};

struct datom
{
   int32_t f; // flags (add/retract, type, etc)
   int64_t e;
   int64_t a;
   int64_t t;
   union v {
      double f;
      int64_t i;
      struct {
         int32_t s;
         char c[4];
      } s;
   } v;
};

void print_datom(datom* d)
{
   value_type vtype = (value_type)(d->f & 0xf);
   switch (vtype) {
   case int_value:
      printf("[%lli %lli %lli %lli]", d->e, d->a, d->v.i, d->t);
      break;
   case boolean_value:
      printf("[%lli %lli %s %lli]", d->e, d->a, d->v.i ? "true" : "false", d->t);
      break;
   case float_value:
      printf("[%lli %lli %lf %lli]", d->e, d->a, d->v.f, d->t);
      break;
   case string_value:
      printf("[%lli %lli \"%s\" %lli]", d->e, d->a, d->v.s.c, d->t);
      break;
   case ref_value:
      printf("[%lli %lli %lli %lli]", d->e, d->a, d->v.i, d->t);
      break;
   }
}

#define key_to_datom(K) ((datom*)((K).key_data_p))

bptree_key_t datom_to_key(datom* d)
{
   bptree_key_t k = {sizeof(datom), d};
   return k;
}

int compare_value(datom* a, datom* b)
{
   value_type a_type = (value_type)(a->f & 0xf);
   value_type b_type = (value_type)(b->f & 0xf);

   if (a_type == b_type) {
      switch (a_type) {
         case int_value:
         case boolean_value:
         case ref_value:
            return a->v.i - a->v.i;
            break;
         case float_value:
            // TODO: Not dumb float compare
            return (int)(a->v.f - a->v.f);
            break;
         case string_value:
            // TODO: don't use strcmp
            return strcmp(a->v.s.c, b->v.s.c);
            break;
      }
   } else {
      // TODO ???
      return 0;
   }
}

int compare_eavt(bptree_key_t ak, bptree_key_t bk)
{
   datom* a = key_to_datom(ak);
   datom* b = key_to_datom(bk);

   if (a->e == b->e) {
      if (a->a == b->a) {
         int vcmp = compare_value(a, b);
         if (vcmp == 0) {
            return a->t - b->t;
         } else {
            return vcmp;
         }
      } else {
         return a->a - b->a;
      }
   } else {
      return a->e - b->e;
   }
}

int compare_aevt(bptree_key_t ak, bptree_key_t bk)
{
   datom* a = key_to_datom(ak);
   datom* b = key_to_datom(bk);

   if (a->a == b->a) {
      if (a->e == b->e) {
         int vcmp = compare_value(a, b);
         if (vcmp == 0) {
            return a->t - b->t;
         } else {
            return vcmp;
         }
      } else {
         return a->e - b->e;
      }
   } else {
      return a->a - b->a;
   }
}

int compare_vaet(bptree_key_t ak, bptree_key_t bk)
{
   datom* a = key_to_datom(ak);
   datom* b = key_to_datom(bk);

   // NOTE: vaet is only for ref types, so just use v.i
   if (a->v.i == b->v.i) {
      if (a->a == b->a) {
         if (a->e == b->e) {
            return a->t - b->t;
         } else {
            return a->e - b->e;
         }
      } else {
         return a->a - b->a;
      }
   } else {
      return a->v.i - b->v.i;
   }
}

struct datom_index
{
   bptree t;
};


void init_index(datom_index* idx, int order, bptree_key_compare_fn cmp)
{
   idx->t.order = order;
   idx->t.root = 0;
   idx->t.compare = cmp;
}

struct segment;

struct partition
{
   uint32_t id;
   const char name[33];
   int32_t sequence;

};

struct cons_cell
{
   void* car;
   void* cdr;
};

inline
void* car(cons_cell* c)
{
   if (c) {
      return c->car;
   }
   return 0;
}

inline
void* cdr(cons_cell* c)
{
   if (c) {
     return c->cdr;
   }
   return 0;
}

struct database
{
   struct datom_index eavt;
   struct datom_index aevt;
   struct datom_index vaet;
   struct datom_index avet;
   struct partition* partitions;
   int partition_count;
};


void init_database(database* db)
{
   init_index(&db->eavt, 7, compare_eavt);
   init_index(&db->aevt, 7, compare_aevt);
   init_index(&db->vaet, 7, compare_vaet);
}

struct transaction
{
   memory_arena* arena;
   uint64_t ts;
   uint32_t txn_id;
   int datom_count;
   cons_cell* items;
};

struct transaction_result
{
   database* before;
   database* after;
   transaction* txn;
};

struct log
{
   struct log* next;
   struct transaction txn;
};

transaction* create_transaction()
{
   memory_arena* arena = create_arena(4096);

   transaction* t = push_struct(arena, transaction);

   t->arena = arena;

   return t;
}

bool add_datom(transaction* txn, datom* d)
{
   cons_cell* c = push_struct(txn->arena, cons_cell);

   c->car = d;
   c->cdr = txn->items;

   txn->items = c;

   return true;
}


bool add_fact(transaction* txn, int64_t e, int64_t a, int64_t v)
{
   datom* d = push_struct(txn->arena, datom);

   d->f = int_value;
   d->e = e;
   d->a = a;
   d->t = txn->txn_id;
   d->v.i = v;

   return add_datom(txn, d);
}

bool add_fact(transaction* txn, int64_t e, int64_t a, ref_t r)
{
   datom* d = push_struct(txn->arena, datom);

   d->f = ref_value;
   d->e = e;
   d->a = a;
   d->t = txn->txn_id;
   d->v.i = r.r;

   return add_datom(txn, d);
}

bool add_fact(transaction* txn, int64_t e, int64_t a, const char* v)
{
   int len = strlen(v)+1;

   datom* d = (datom*)arena_allocate(txn->arena, sizeof(datom) + len - 4);

   d->f = string_value;
   d->e = e;
   d->a = a;
   d->t = txn->txn_id;
   d->v.s.s = len;
   strcpy(d->v.s.c, v);

   return add_datom(txn, d);
}

transaction_result* transact(database* db, transaction* txn)
{
   cons_cell* seq = txn->items;
   int cnt = 0;

   while (seq) {
      datom* d = (datom*)car(seq);

      bptree_key_t k = datom_to_key(d);

      bptree_insert(&db->eavt.t, k);
      bptree_insert(&db->aevt.t, k);

      seq = (cons_cell*)cdr(seq);

      cnt++;
   }

   printf("Transacted %d datoms\n", cnt);

   return 0;
}

void test_simple_transaction()
{
   database db = {};
   init_database(&db);

   transaction* txn = create_transaction();

   add_fact(txn, 0, 1, 5);
   add_fact(txn, 0, 2, 6);
   add_fact(txn, 0, 3, 7);
   add_fact(txn, 0, 4, "foo");
   add_fact(txn, 1, 1, 5);
   add_fact(txn, 1, 2, 6);
   add_fact(txn, 1, 3, 7);
   add_fact(txn, 1, 4, "zzzz");

   transact(&db, txn);

   printf("scan eavt\n");
   datom q = {0};
   bptree_iterator it;
   bptree_scan(&db.eavt.t, datom_to_key(&q), &it);

   while (!bptree_iterator_is_end(&it)) {
      datom* d = key_to_datom(bptree_key(&it));

      print_datom(d);
      printf("\n");

      bptree_iterator_next(&it);
   }

   printf("scan aevt\n");

   //q.a = 1;
   bptree_scan(&db.aevt.t, datom_to_key(&q), &it);

   while (!bptree_iterator_is_end(&it)) {
      datom* d = key_to_datom(bptree_key(&it));

      print_datom(d);
      printf("\n");

      bptree_iterator_next(&it);
   }

   txn = create_transaction();

   add_fact(txn, 2, 1, 5);
   add_fact(txn, 2, 2, 6);
   add_fact(txn, 2, 3, 7);
   add_fact(txn, 2, 4, "aaaa");
   add_fact(txn, 3, 1, 5);
   add_fact(txn, 3, 2, 6);
   add_fact(txn, 3, 3, 7);
   add_fact(txn, 3, 4, "yyyy");

   transact(&db, txn);

   printf("scan eavt 2\n");
   bptree_scan(&db.eavt.t, datom_to_key(&q), &it);

   while (!bptree_iterator_is_end(&it)) {
      datom* d = key_to_datom(bptree_key(&it));

      print_datom(d);
      printf("\n");

      bptree_iterator_next(&it);
   }

   printf("scan aevt 2\n");

   bptree_scan(&db.aevt.t, datom_to_key(&q), &it);

   while (!bptree_iterator_is_end(&it)) {
      datom* d = key_to_datom(bptree_key(&it));

      print_datom(d);
      printf("\n");

      bptree_iterator_next(&it);
   }
}

void test_memory_arena()
{
  memory_arena* arena = create_arena(4096);

  char* t = arena->page->p;
  char* p = (char*)arena_allocate(arena, 512);

  assert(t == p);

  p = (char*)arena_allocate(arena, 512);
  assert(t + 512 == p);

  p = (char*)arena_allocate(arena, 4096);
  assert(t != arena->page->p);

  t = arena->page->p;
  datom* d = push_struct(arena, datom);
  assert(d);

  size_t sz = arena->page->p - t;
  assert(sz == sizeof(datom));
}

void test_interning_keywords()
{
   keyword* a = kw("foo", "a");
   keyword* b = kw("foo", "b");
   keyword* a2 = kw("foo", "a");

   assert(a != b);
   assert(a == a2);

}

int main(int argc, char** argv)
{
   test_interning_keywords();
   test_memory_arena();
   test_simple_transaction();

   return 0;
}

