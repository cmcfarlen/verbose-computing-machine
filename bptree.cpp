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
