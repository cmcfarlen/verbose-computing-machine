#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include "bptree.h"

void dot_node_node(bptree_node* n)
{

   printf("  node%p [label = \"", n);

   if (n->is_leaf) {
      for (int i = 0; i < n->count; i++) {
         printf("%lu|", n->keys[i].key_size);
      }
      printf("<n> ");
      printf("\",group=leafs];\n");
   } else {
      for (int i = 0; i < n->count; i++) {
         printf("<f%i> |%lu|", i, n->keys[i].key_size);
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

int size_compare(bptree_key_t a, bptree_key_t b)
{
   return a.key_size - b.key_size;
}

inline bptree_key_t int_key(int i)
{
   bptree_key_t k = {i, 0};
   return k;
}

int main(int argc, char** argv)
{
   bptree t = {7, 0, size_compare};

   int keys = 50;
   for (int i = 0; i <= keys; i++)
   {
      bptree_key_t k = int_key(i);
      bptree_insert(&t, k, (void*)(uintptr_t)i);
   }
   dot_tree(&t);

   void* v = bptree_find(&t, int_key(5));
   assert(v);
   assert(5 == (int)(uintptr_t)v);

   return 0;
}
