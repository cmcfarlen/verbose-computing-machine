
#include <stdint.h>

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

