
// https://en.wikipedia.org/wiki/B%2B_tree

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

typedef struct bptree bptree;
typedef struct bptree_node bptree_node;

struct bptree_key_t {
   size_t key_size;
   void* key_data_p;
};

typedef int (*bptree_key_compare_fn)(bptree_key_t a, bptree_key_t b);

// return keys_count if no keys are greater than keys.
int bptree_find_first_greater_than(bptree_key_t* keys, size_t keys_count, bptree_key_t key, bptree_key_compare_fn compare)
{
   int low = 0;
   int high = keys_count;

   while (low != high) {
      int mid = (low + high) / 2;
      int cmp = compare(keys[mid], key);
      if (cmp <= 0) {
         low = mid + 1; // must be past mid
      } else {
         high = mid; // must be before high
      }
   }

   return low;
}

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
   bptree_key_compare_fn compare;
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
   nn->pointers = (void**)((char*)p + sizeof(bptree_node) + sizeof(bptree_key_t) * order);

   return nn;
}

bptree_node* bptree_search_recur(bptree* t, bptree_node* n, bptree_key_t key)
{
   if (n->is_leaf) {
      return n;
   }

   assert(n->count > 0);

   int cmp = bptree_find_first_greater_than(n->keys, n->count, key, t->compare);

   assert(cmp <= n->count);

   return bptree_search_recur(t, (bptree_node*)n->pointers[cmp], key);
}

void bptree_insert_node(bptree* t, bptree_node* n, bptree_key_t key, void* value)
{

   int pos = bptree_find_first_greater_than(n->keys, n->count, key, t->compare);

   for (int i = n->count; i > pos; i--) {
      n->keys[i] = n->keys[i-1];
   }
   n->keys[pos] = key;

   void** pv = n->is_leaf ? n->pointers : n->pointers+1;
   for (int i = n->count; i > pos; i--) {
      pv[i] = pv[i-1];
   }
   pv[pos] = value;

   n->count++;

   // make this node the newly inserted node's parent
   if (!n->is_leaf) {
      bptree_node* nn = (bptree_node*)value;
      nn->parent = n;
   }

   if (n->count == t->order) {
      bptree_node* newnode = alloc_node(t->order, n->is_leaf);

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

      // skip first pointer for internal nodes
      if (!n->is_leaf) {
         src++;
         psrc++;
         move--;
         newnode->count = move;
      }

      while (move--) {
         *dst++ = *src++;
         *pdst++ = *psrc++;
      }

      if (!n->is_leaf) {
         *pdst++ = *psrc++;

         // reparent children of new node
         bptree_node** cn = (bptree_node**)newnode->pointers;
         for (int i = 0; i < (newnode->count+1); i++) {
            cn[i]->parent = newnode;
         }
      }

      // move a key to the parent
      if (n->parent) {
         // insert key into parent
         bptree_insert_node(t, n->parent, n->keys[keep], newnode);
      } else {
         // split the root
         bptree_node* newroot = alloc_node(t->order, 0);

         newroot->count = 1;
         newroot->keys[0] = n->keys[keep];
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
      n = bptree_search_recur(t, t->root, key);
   } else {
      n = alloc_node(t->order, 1);
      t->root = n;
   }

   bptree_insert_node(t, n, key, value);

   return 0;
}

