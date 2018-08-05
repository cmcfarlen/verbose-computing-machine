
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "bptree.h"

enum value_type
{
   int_value,
   boolean_value,
   float_value,
   string_value,
   blob_value,
   ref_value
};

union datom
{
   struct {
      int32_t e;
      int32_t a;
      int32_t v;
      int32_t t;
   };
   int32_t d[4];
};

struct datom_index
{

};

struct segment;

struct partition
{
   uint32_t id;
   const char name[33];
   int32_t sequence;

};

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

struct cons_cell
{
   void* car;
   void* cdr;
};

struct database
{
   struct datom_index eavt;
   struct datom_index aevt;
   struct datom_index vaet;
   struct datom_index avet;
   struct partition* partitions;
   int partition_count;
};

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

transaction* create_transaction()
{
   memory_arena* arena = create_arena(4096);

   transaction* t = push_struct(arena, transaction);

   t->arena = arena;

   return t;
}

bool add_fact(transaction* txn, int32_t e, int32_t a, int v)
{
   cons_cell* c = push_struct(txn->arena, cons_cell);
   datom* d = push_struct(txn->arena, datom);

   d->e = e;
   d->a = a;
   d->v = v;
   d->t = txn->txn_id;

   c->car = d;
   c->cdr = txn->items;

   txn->items = c;

   return true;
}

transaction_result* transact(database* db, transaction* txn)
{
   return 0;
}

int main(int argc, char** argv)
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

   return 0;
}

