
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#if 0
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
      uint32_t e;
      uint32_t a;
      uint32_t v;
      uint32_t t;
   };
   uint32_t d[4];
};

struct datom_index
{

};

struct segment;

struct partition
{

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
   uint64_t ts;
   uint32_t txn_id;
   int datom_count;
   union datom* datoms;
};

struct log
{
   struct log* next;
   struct transaction txn;
};

#endif

// https://en.wikipedia.org/wiki/B%2B_tree

typedef struct bptree bptree;
typedef struct bptree_node bptree_node;
typedef struct bptree_node_entry bptree_node_entry;
typedef struct bptree_leaf_entry bptree_leaf_entry;
typedef uint64_t bptree_key_t;

struct bptree_node_entry {
   bptree_node_entry* next;
   bptree_key_t key;
   void* p;
};

struct bptree_node {
   int is_leaf;
   int count;
   bptree_node* parent;
   bptree_node_entry* free_entries;
   bptree_node_entry* first;
};

struct bptree {
   int order;
   bptree_node* root;
};

bptree_node* alloc_node(int order, int is_leaf) {
   size_t sz = sizeof(bptree_node) + sizeof(bptree_node_entry) * order;

   bptree_node* nn = (bptree_node*)malloc(sz);
   nn->is_leaf = is_leaf;
   nn->count = 0;
   nn->parent = 0;
   nn->first = 0;

   bptree_node_entry* pe = (bptree_node_entry*)((void*)(nn + 1));

   nn->free_entries = 0;
   while (order--) {
      pe->next = nn->free_entries;
      nn->free_entries = pe;
      pe++;
   }

   return nn;
}

bptree_node* bptree_search_recur(bptree_node* n, bptree_key_t key)
{
   if (n->is_leaf) {
      return n;
   }
   for (int i = 0; i < n->count; i++) {
      bptree_node_entry* e = n->first;
      while (e) {
         if (e->key > key) {
            return bptree_search_recur(e->p, key);
         }
         e = e->next;
      }
   }
   return 0;
}

bptree_node* bptree_search_for_leaf_with_key(bptree* t, bptree_key_t key)
{
   return bptree_search_recur(t->root, key);
}

int bptree_insert(bptree *t, bptree_key_t key)
{
   bptree_node* n = 0;
   if (t->root) {
      n = bptree_search_recur(t->root, key);
   } else {
      n = alloc_node(t->order, 1);
      t->root = n;
   }

   if (n->count < t->order) {
      if (n->first) {
         bptree_node_entry* p = 0;
         bptree_node_entry* e = n->first;
         while (e && e->key < key) {
            p = e;
            e = e->next;
         }

         bptree_node_entry* a = n->free_entries;
         n->free_entries = n->free_entries->next;
         a->key = key;
         a->next = e;
         if (p) {
            p->next = a;
         } else {
            n->first = a;
         }
      } else {
         n->first = n->free_entries;
         n->free_entries = n->free_entries->next;

         n->first->next = 0;
         n->first->key = key;
      }
      n->count++;
   } else {
      bptree_node* newnode = alloc_node(t->order, 1);

      int keep = t->order / 2;
      int move = t->order - keep;

      n->count = keep;
      newnode->count = move;

      bptree_node_entry* e = n->first;
      while (--keep) {
         e = e->next;
      }

      bptree_node_entry* src = e->next;
      bptree_node_entry* dst = 0;

      e->next = 0;

      while (src) {
         if (dst) {
            e = newnode->free_entries;
            dst->next = e;
            dst = e;
         } else {
            newnode->first = dst = newnode->free_entries;
         }
         newnode->free_entries = newnode->free_entries->next;

         dst->key = src->key;

         e = src->next;

         src->next = n->free_entries;
         src->next = 0;
         n->free_entries = src;
         src = e;
      }

      // move a key to the parent
      if (n->parent) {
         // insert key into parent
      } else {
         // split the root
      }
   }

   return 0;
}

int main(int argc, char** argv)
{
   printf("Hello World\n");

   bptree t = {3, 0};

   bptree_insert(&t, 1);
   bptree_insert(&t, 2);
   bptree_insert(&t, 3);
   bptree_insert(&t, 4);
   bptree_insert(&t, 5);
   bptree_insert(&t, 6);
   bptree_insert(&t, 7);
   bptree_insert(&t, 8);
   bptree_insert(&t, 9);
   bptree_insert(&t, 10);

   return 0;
}
