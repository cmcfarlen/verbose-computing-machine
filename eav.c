
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

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
typedef uint64_t bptree_key_t;

struct bptree_node {
   int is_leaf;
   int count;
   bptree_node* parent;
   bptree_node* next;
   bptree_key_t* keys; // order keys
   void** pointers; // leaf: order pointers to values. internal: order + 1 pointers to nodes
};

struct bptree {
   int order;
   bptree_node* root;
};

bptree_node* alloc_node(int order, int is_leaf) {
   size_t sz = sizeof(bptree_node) + sizeof(bptree_key_t) * order;

   if (is_leaf) {
      sz += sizeof(void*) * order;
   } else {
      sz += sizeof(void*) * (order+1);
   }

   void* p = malloc(sz);

   bptree_node* nn = (bptree_node*)p;
   nn->is_leaf = is_leaf;
   nn->count = 0;
   nn->parent = 0;
   nn->next = 0;
   nn->keys = (bptree_key_t*)((char*)p + sizeof(bptree_node));
   nn->pointers = (void*)((char*)p + sizeof(bptree_node) + sizeof(bptree_key_t) * order);

   return nn;
}

bptree_node* bptree_search_recur(bptree_node* n, bptree_key_t key)
{
   if (n->is_leaf) {
      return n;
   }

   assert(n->count > 0);

   for (int i = 0; i < n->count; i++) {
      if (n->keys[i] > key) {
         return bptree_search_recur((bptree_node*)n->pointers[i], key);
      }
   }
   return bptree_search_recur((bptree_node*)n->pointers[n->count], key);
}

bptree_node* bptree_search_for_leaf_with_key(bptree* t, bptree_key_t key)
{
   return bptree_search_recur(t->root, key);
}

void bptree_insert_node(bptree* t, bptree_node* n, bptree_key_t key, void* value)
{
   bptree_key_t tmp = key;
   void* tmpp = value;
   bptree_key_t* pk = n->keys;
   void** pv = n->is_leaf ? n->pointers : n->pointers+1;
   bptree_key_t* ek = n->keys + n->count;
   while (pk < ek && *pk < key) {
      ++pk;
      ++pv;
   }
   while (pk < ek) {
      bptree_key_t t2 = *pk;
      void* p2 = *pv;
      *pk++ = tmp;
      *pv++ = tmpp;
      tmp = t2;
      tmpp = p2;
   }
   *pk = tmp;
   *pv = tmpp;
   n->count++;

   if (n->count == t->order) {
      bptree_node* newnode = alloc_node(t->order, 1);

      newnode->next = n->next;
      n->next = newnode;

      int keep = t->order / 2;
      int move = t->order - keep;

      n->count = keep;

      newnode->count = move;

      bptree_key_t* src = n->keys + keep;
      void** psrc = n->pointers + keep;
      bptree_key_t* dst = newnode->keys;
      void** pdst = newnode->pointers;

      while (move--) {
         *dst++ = *src++;
         *pdst++ = *psrc++;
      }

      if (!n->is_leaf) {
         *pdst++ = *psrc++;
      }

      // move a key to the parent
      if (n->parent) {
         // insert key into parent
         bptree_insert_node(t, n->parent, newnode->keys[0], newnode);
         newnode->parent = n->parent;
      } else {
         // split the root
         bptree_node* newroot = alloc_node(t->order, 0);

         newroot->count = 1;
         newroot->keys[0] = newnode->keys[0];
         newroot->pointers[0] = n;
         newroot->pointers[1] = newnode;

         t->root = newroot;
         n->parent = newroot;
         newnode->parent = newroot;
      }
   }
}

int bptree_insert(bptree *t, bptree_key_t key, void* value)
{
   bptree_node* n = 0;
   if (t->root) {
      n = bptree_search_recur(t->root, key);
   } else {
      n = alloc_node(t->order, 1);
      t->root = n;
   }

   bptree_insert_node(t, n, key, value);

   return 0;
}

void print_keys(bptree* t)
{
   bptree_node* n = t->root;

   printf("[");
   while (n && !n->is_leaf) {
      n = (bptree_node*)n->pointers[0];
   }

   while (n) {
      int cnt = n->count;
      bptree_key_t* k = n->keys;
      while (cnt--) {
         printf("%lld ", *k++);
      }
      n = n->next;
      printf(", ");
   }
   printf("]\n");
}

void print_node(bptree_node* n, int h)
{
   if (n->is_leaf) {
      printf("l: ");
   } else {
      printf("o: ");
   }

   int cnt = n->count;
   bptree_key_t* k = n->keys;
   while (cnt--) {
      printf("%lld ", *k++);
   }

}

void print_tree(bptree* t)
{
   bptree_node* n = t->root;

   print_node(n, 1);

}

int main(int argc, char** argv)
{
   printf("Hello World\n");

   bptree t = {3, 0};

   bptree_insert(&t, 2, 0);
   print_keys(&t);
   bptree_insert(&t, 3, 0);
   print_keys(&t);
   bptree_insert(&t, 1, 0);
   print_keys(&t);
   bptree_insert(&t, 4, 0);
   print_keys(&t);
   bptree_insert(&t, 5, 0);
   print_keys(&t);
   bptree_insert(&t, 6, 0);
   print_keys(&t);
   bptree_insert(&t, 7, 0);
   print_keys(&t);
   bptree_insert(&t, 8, 0);
   print_keys(&t);
   bptree_insert(&t, 9, 0);
   print_keys(&t);
   bptree_insert(&t, 10, 0);
   print_keys(&t);

   return 0;
}
