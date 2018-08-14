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
         printf("%d|", n->keys[i].key_size);
      }
      printf("<n> ");
      printf("\",group=leafs];\n");
   } else {
      for (int i = 0; i < n->count; i++) {
         printf("<f%i> |%d|", i, n->keys[i].key_size);
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

void test_find_first_less_than()
{
   int key_count = 10;
   bptree_key_t* keys = (bptree_key_t*)malloc(sizeof(bptree_key_t)*key_count);

   for (int i = 0; i < key_count; i++) {
      keys[i] = int_key(i);
   }

   int r = bptree_find_first_less_than(keys, key_count, int_key(3), size_compare);
   assert(r == 2);

   r = bptree_find_first_less_than(keys, key_count, int_key(5), size_compare);
   assert(r == 4);

   r = bptree_find_first_less_than(keys, key_count, int_key(10), size_compare);
   assert(r == 9);

   r = bptree_find_first_less_than(keys, key_count, int_key(0), size_compare);
   // TODO: should this return -1?
   assert(r == 0);
}

void test_scan()
{
   bptree t = {3, 0, size_compare};

   int keys = 10;
   for (int i = 0; i <= keys; i++)
   {
      bptree_key_t k = int_key(i);
      bptree_insert(&t, k, (void*)(uintptr_t)i);
   }

   int k = 0;
   bptree_iterator it;
   bptree_begin(&t, &it);

   while (!bptree_iterator_is_end(&it)) {
      bptree_key_t ik = bptree_key(&it);

      printf("%i\n", ik.key_size);
      assert(ik.key_size == int_key(k).key_size);
      k++;

      bptree_iterator_next(&it);
   }

}

int main(int argc, char** argv)
{

//   test_find_first_less_than();
   test_scan();

#if 0
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
#endif

   return 0;
}
