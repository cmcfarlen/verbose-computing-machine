#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

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

void dot_node_node(bptree_node* n)
{

   printf("  node%p [label = \"", n);

   if (n->is_leaf) {
      for (int i = 0; i < n->count; i++) {
         printf("%lld|", *(n->keys + i));
      }
      printf("<n> ");
      printf("\",group=leafs];\n");
   } else {
      for (int i = 0; i < n->count; i++) {
         printf("<f%i> |%lld|", i, *(n->keys + i));
      }
      printf("<f%i> ", n->count);
      printf("\"];\n");
   }


   if (!n->is_leaf) {
      void** p = n->pointers;
      int cnt = n->count;
      while (cnt--) {
         dot_node_node((bptree_node*)*p);
         p++;
      }
      dot_node_node((bptree_node*)*p);
   }
}

void dot_node_edges(bptree_node* n)
{
   if (!n->is_leaf) {
      for (int i = 0; i < n->count; i++) {
         printf("  \"node%p\":f%i -> \"node%p\";\n", n, i, *(n->pointers + i));
      }
      printf("  \"node%p\":f%i -> \"node%p\";\n", n, n->count, *(n->pointers + n->count));

      int cnt = n->count;
      void** p = n->pointers;

      while (cnt--) {
         dot_node_edges((bptree_node*)*p);
         p++;
      }
      dot_node_edges((bptree_node*)*p);
   }
   if (n->parent) {
      printf("  \"node%p\" -> \"node%p\" [color = blue];\n", n, n->parent);
   }
   /*
   if (n->is_leaf && n->next) {
      printf("  \"node%p\":n -> \"node%p\" [color = red];\n", n, n->next);
   }
   */
}

void dot_tree(bptree* t)
{
   printf("digraph g {\n");
   printf(" node [shape=record,height=.1];\n");

   dot_node_node(t->root);
   dot_node_edges(t->root);

   printf("}\n");
}


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
      n = bptree_search_recur(t->root, key);
   } else {
      n = alloc_node(t->order, 1);
      t->root = n;
   }

   bptree_insert_node(t, n, key, value);

   return 0;
}

int main(int argc, char** argv)
{
   bptree t = {7, 0};

   int keys = 10;
   for (int i = 1; i <= keys; i++)
   {
      bptree_insert(&t, i, 0);
   }
   dot_tree(&t);

   return 0;
}
