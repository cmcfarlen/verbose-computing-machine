
// https://en.wikipedia.org/wiki/B%2B_tree

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

typedef struct bptree bptree;
typedef struct bptree_node bptree_node;

struct bptree_key_t {
   int key_size;
   void* key_data_p;
};

typedef int (*bptree_key_compare_fn)(bptree_key_t a, bptree_key_t b);

// return keys_count(one past the end) if no keys are greater than keys.
int bptree_find_first_greater_than(bptree_key_t* keys, int keys_count, bptree_key_t key, bptree_key_compare_fn compare)
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

int bptree_find_first_less_than(bptree_key_t* keys, int keys_count, bptree_key_t key, bptree_key_compare_fn compare)
{
   int low = 0;
   int high = keys_count;

   while (low < high) {
      int mid = (low + high) / 2 + ((low + high) % 2); // round up!
      if (mid < keys_count) {
         int cmp = compare(keys[mid], key);
         if (cmp >= 0) {
            high = mid - 1;
         } else {
            low = mid;
         }
      } else {
         low = high = keys_count - 1; // all keys < key, set to last index
      }
   }

   return low;
}

int bptree_find_key(bptree_key_t* keys, int keys_count, bptree_key_t key, bptree_key_compare_fn compare)
{
   int low = 0;
   int high = keys_count;

   while (low != high) {
      int mid = (low + high) / 2;
      int cmp = compare(keys[mid], key);
      if (cmp == 0) {
         return mid;
      } else if (cmp < 0) {
         low = mid + 1;
      } else {
         high = mid;
      }
   }

   return -1;
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

int bptree_insert(bptree *t, bptree_key_t key)
{
   return bptree_insert(t, key, 0);
}

void* bptree_find(bptree* t, bptree_key_t key)
{
   void* v = 0;
   if (t->root) {
      bptree_node* n = bptree_search_recur(t, t->root, key);

      if (n) {
         // TODO: to support duplicate keys, this will need to find the first key less than key and see if the next one matches
         int idx = bptree_find_key(n->keys, n->count, key, t->compare);
         if (idx != -1) {
            v = n->pointers[idx];
         }
      }
   }

   return v;
}

struct bptree_iterator
{
   bptree* t;
   bptree_node* n;
   int key_idx;
};

int bptree_end(bptree*t, bptree_iterator* it)
{
   if (t->root) {
      bptree_node* n = t->root;

      while (!n->is_leaf) {
         n = (bptree_node*)n->pointers[n->count];
      }

      it->t = t;
      it->n = n;
      it->key_idx = n->count;
      return 1;
   }
   return 0;
}

int bptree_begin(bptree*t, bptree_iterator* it)
{
   if (t->root) {
      bptree_node* n = t->root;

      while (!n->is_leaf) {
         n = (bptree_node*)n->pointers[0];
      }

      it->t = t;
      it->n = n;
      it->key_idx = 0;
      return 1;
   }
   return 0;
}

int bptree_iterator_is_end(bptree_iterator* it)
{
   return (it->n->next == 0 &&
           it->n->count == it->key_idx);
}

void bptree_iterator_next(bptree_iterator* it)
{
   if (!bptree_iterator_is_end(it)) {
      it->key_idx++;
      if (it->key_idx == it->n->count && it->n->next) {
         it->n = it->n->next;
         it->key_idx = 0;
      }
   }
}

bptree_key_t bptree_key(bptree_iterator* it)
{
   return it->n->keys[it->key_idx];
}

void* bptree_value(bptree_iterator* it)
{
   return it->n->pointers[it->key_idx];
}

int bptree_scan(bptree* t, bptree_key_t after, bptree_iterator* it)
{
   if (t->root) {
      bptree_node* n = bptree_search_recur(t, t->root, after);

      if (n) {
         int idx = bptree_find_first_greater_than(n->keys, n->count, after, t->compare);

         it->t = t;
         it->n = n;
         it->key_idx = idx;
         return 1;
      }
   }

   return 0;
}




