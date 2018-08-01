
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

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

