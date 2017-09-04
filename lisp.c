
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

// Read a string as a file
typedef struct string_cookie
{
   const char* s; // the string
   size_t len; // the length of the string
   const char* p; // the read position
} string_cookie;

typedef struct string_write_cookie
{
   char* buffer; // write buffer
   int len;
   char* p; // current write position
} string_write_cookie;

int string_read(void* ck, char* buf, int nbyte)
{
   string_cookie* c = (string_cookie*) ck;
   const char* p = c->p;
   size_t left = c->s + c->len - p;
   int cnt = 0;
   if (left > 0) {
      cnt = (left < nbyte) ? left : nbyte;
      memcpy(buf, p, cnt);

      c->p += cnt;
   }

   return cnt;
}

int string_write(void* ck, const char* buf, int bc)
{
   string_write_cookie* c = (string_write_cookie*)ck;
   size_t left = c->buffer + c->len - c->p - 1;
   int cnt = 0;
   if (left > 0) {
      cnt = (left < bc) ? left : bc;
      memcpy(c->p, buf, cnt);

      c->p += cnt;
      *c->p = 0;
   }

   return cnt;
}

int string_close(void* c)
{
   free(c);
   return 0;
}

FILE* string_open(const char* s)
{
   size_t len = strlen(s);
   string_cookie* c = malloc(sizeof(string_cookie));
   c->s = s;
   c->p = s;
   c-> len = len;

   FILE* f = funopen(c, string_read, 0, 0, string_close);

   return f;
}

FILE* string_open_write(char* buff, int len)
{
   string_write_cookie* c = malloc(sizeof(string_write_cookie));
   c->buffer = buff;
   c->p = buff;
   c->len = len;

   return funopen(c, 0, string_write, 0, string_close);
}

// definitions

typedef unsigned long u32;
typedef unsigned char u8;
typedef int bool;

#define false 0
#define true 1

#define READER_STRING_LENGTH_MAX 1000

// core internal

enum {
   GARBAGE_VALUE,
   NUMBER_VALUE,
   STRING_VALUE,
   SYMBOL_VALUE,
   KEYWORD_VALUE,
   CONS_VALUE,
   BUILTIN_VALUE,
   PROC_VALUE,
   MAP_VALUE,
   ENTRY_VALUE
};

typedef struct value value;
typedef struct environment environment;
typedef value* (*builtin_fn)();

struct value
{
   u8 tag;
   u32 mark;
   value* next; // temp for playing with GC
   union {
      int number;
      char* string;
      struct symbol {
         char* namespace;
         char* name;
      } symbol;
      struct cons {
         value* car;
         value* cdr;
      } cons;
      struct entry {
         value* key;
         value* val;
         value* next;
      } entry;
      struct builtin {
         builtin_fn proc;
      } builtin;
      struct proc {
         value* arglist;
         value* body;
         value* env;
      } proc;
      struct map {
         value* entries;
      } map;
   } d;
};

static value nil_value;
value* nil = &nil_value;

static value true_value;
value* T = &true_value;

const char* tag_name(value* v)
{
   u8 t = v->tag;
   switch (t) {
   case GARBAGE_VALUE: return "garbage";
   case NUMBER_VALUE: return "number";
   case STRING_VALUE: return "string";
   case SYMBOL_VALUE: return "symbol";
   case KEYWORD_VALUE: return "keyword";
   case CONS_VALUE: return "list";
   case BUILTIN_VALUE: return "builtin";
   case PROC_VALUE: return "proc";
   case MAP_VALUE: return "map";
   case ENTRY_VALUE: return "mapentry";
   default: return "unknown";
   }
}


struct environment
{
   value object_root; // root object to keep track of allocated values
   int alloc_count;
   int object_count;
   u32 generation;
   value* symbols;
   value* free_list;
};

value* car(value* v)
{
   return v->d.cons.car;
}

value* cdr(value* v)
{
   return v->d.cons.cdr;
}

value* cadr(value* v)
{
   return car(cdr(v));
}

int number(value* v)
{
   return v->d.number;
}

char* string(value* v)
{
   return v->d.string;
}

int compare(environment* env, value* a, value* b)
{
   if (a == b) {
      return 0;
   }

   if (a->tag != b->tag) {
      fprintf(stdout, "Can't compare different %s and %s!", tag_name(a), tag_name(b));
      return -1;
   }

   return 0;
}

value* equals(environment* env, value* a, value* b)
{
   if (a == b) {
      return T;
   }

   if (a->tag == b->tag) {
      switch (a->tag) {
      case NUMBER_VALUE:
         if (number(a) == number(b)) {
            return T;
         }
         break;
      case STRING_VALUE:
         if (strcmp(string(a), string(b)) == 0) {
            return T;
         }
         break;
      case CONS_VALUE:
         while (a != nil && b != nil) {
            if (equals(env, car(a), car(b)) == nil) {
               return nil;
            }
            a = cdr(a);
            b = cdr(b);
         }
         if (a == nil && b == nil) {
            return T;
         }
         break;
      }
   }
   return nil;
}

int mark(value* v, u32 gen)
{
   if (v == nil) {
      return 0;
   }

   if (v->mark == gen) {
       return 0;
   }

   u8 t = v->tag;
   if (t == GARBAGE_VALUE)
   {
       fprintf(stdout, "Fatal error! Trying to mark garbage!\n");
       return 0;
   }
   v->mark = gen;
   if (t == CONS_VALUE)
   {
       return 1 + mark(car(v), gen) + mark(cdr(v), gen);
   }

   if (t == PROC_VALUE)
   {
       return 1 + mark(v->d.proc.arglist, gen) + mark(v->d.proc.body, gen) + mark(v->d.proc.env, gen);
   }

   if (t == MAP_VALUE)
   {
      return 1 + mark(v->d.map.entries, gen);
   }

   if (t == ENTRY_VALUE)
   {
      return 1 + mark(v->d.entry.key, gen) + mark(v->d.entry.val, gen) + mark(v->d.entry.next, gen);
   }

   return 1;
}

void gc(environment* env, value* roots)
{
   int g = ++env->generation;
   int mark_cnt = 0;
   int garbage_cnt = 0;

   if (env->symbols == 0) {
      env->symbols = nil;
   }

   mark_cnt += mark(env->symbols, g);
   mark_cnt += mark(roots, g);

   value* o = env->object_root.next;
   value* p = &env->object_root;
   while (o) {
      value* c = o;
      o = o->next;

      if (c->mark != g) {
         if (p) {
            p->next = c->next;
         }

         if (c->tag == STRING_VALUE && c->d.string) {
            free(c->d.string);
         }
         c->tag = GARBAGE_VALUE;

         c->next = env->free_list;
         env->free_list = c;
         ++garbage_cnt;
         --env->object_count;
      } else {
         p = c;
      }
   }

   printf("\ngc object cnt: %i\n", env->object_count);
   printf("gc alloc cnt: %i\n", env->alloc_count);
   printf("gc mark cnt: %i\n", mark_cnt);
   printf("gc garbage cnt: %i\n", garbage_cnt);
}

value* alloc_value(environment* env)
{
   value* v = 0;

   if (env->free_list) {
      v = env->free_list;
      env->free_list = v->next;
   } else {
      v = (value*)malloc(sizeof(value));
      env->alloc_count++;
   }

   v->next = env->object_root.next;
   v->mark = -1;
   env->object_root.next = v;
   env->object_count++;

   return v;
}

value* number_value(environment* env, int n)
{
   value* v = alloc_value(env);
   v->tag = NUMBER_VALUE;
   v->d.number = n;
   return v;
}

value* string_value(environment* env, char* s, int n)
{
   value* v = alloc_value(env);
   v->tag = STRING_VALUE;
   // TODO: alloc strings in env
   v->d.string = strndup(s, n);
   return v;
}

value* cons(environment* env, value* a, value* b)
{
   value* cell = alloc_value(env);
   cell->tag = CONS_VALUE;
   cell->d.cons.car = a;
   cell->d.cons.cdr = b;
   return cell;
}

value* symbol_value(environment* env, char* namespace, char* name)
{
   if (env->symbols) {
      for (value* l = env->symbols; l != nil; l = cdr(l)) {
         value* s = car(l);
         if (((0 == namespace && 0 == s->d.symbol.namespace) ||
              (namespace && s->d.symbol.namespace && (strcmp(s->d.symbol.namespace, namespace) == 0))) &&
             (strcmp(s->d.symbol.name, name) == 0)) {
            return s;
         }
      }
   } else {
      env->symbols = nil;
   }

   value* v = alloc_value(env);
   v->tag = SYMBOL_VALUE;
   if (namespace)
   {
      v->d.symbol.namespace = strdup(namespace);
   }
   else
   {
      v->d.symbol.namespace = 0;
   }
   v->d.symbol.name = strdup(name);
   env->symbols = cons(env, v, env->symbols);
   return v;
}

value* keyword_value(environment* env, char* namespace, char* name)
{
   value* v = alloc_value(env);
   v->tag = KEYWORD_VALUE;
   if (namespace)
   {
      v->d.symbol.namespace = strdup(namespace);
   }
   else
   {
      v->d.symbol.namespace = 0;
   }
   v->d.symbol.name = strdup(name);
   return v;
}

value* builtin_value(environment* env, builtin_fn f)
{
   value* v = alloc_value(env);
   v->tag = BUILTIN_VALUE;
   v->d.builtin.proc = f;
   return v;
}

value* proc_value(environment* e, value* arglist, value* body, value* env)
{
   value* v = alloc_value(e);
   v->tag = PROC_VALUE;
   v->d.proc.arglist = arglist;
   v->d.proc.body = body;
   v->d.proc.env = env;

   return v;
}

value* entry_value(environment* e, value* key, value* val)
{
   value* v = alloc_value(e);
   v->tag = ENTRY_VALUE;
   v->d.entry.key = key;
   v->d.entry.val = val;
   v->d.entry.next = nil;

   return v;
}

value* map_value(environment* e, value* entries)
{
   value* v = alloc_value(e);
   v->tag = MAP_VALUE;
   v->d.map.entries = entries;

   return v;
}

// reader

bool is_digit(int c)
{
   int d = c - '0';
   if (d < 10 && d >= 0)
   {
      return true;
   }
   return false;
}

bool is_space(int c)
{
   if (c == ' ' || c == ',' || c == '\n' || c == '\t') {
      return true;
   }
   return false;
}

bool is_delim(int c)
{
   return is_space(c) || c == ')';
}

void print(environment*, value*, FILE*);
void println(environment*, value*, FILE*);
value* pr(environment*, value*);
value* pr_str(environment*, value*);
value* read(environment*, FILE*);

void skip_whitespace(FILE* f)
{
   int c = fgetc(f);
   while (is_space(c) && c != EOF)
   {
      c = fgetc(f);
   }
   ungetc(c, f);
}

value* read_symbol(environment* i, int c,  FILE* in, value*(*create_value)(environment*, char*, char*))
{
   char buffer[READER_STRING_LENGTH_MAX];
   char* p = buffer;
   char* namespace = 0;
   char* name = buffer;
   while (!is_delim(c) && c != EOF) {
      if (c == '/') {
         namespace = buffer;
         *p++ = 0;
         name = p;
      } else {
         *p++ = c;
      }
      c = fgetc(in);
   }
   *p++ = 0;
   ungetc(c, in);

   if (namespace == 0) {
      if (strcmp(name, "nil") == 0) {
         return nil;
      }
      if (strcmp(name, "true") == 0) {
         return T;
      }
   }

   return create_value(i, namespace, name);
}

value* read_list(environment* env, FILE* in)
{
   skip_whitespace(in);
   int c = fgetc(in);
   if (c == ')') {
      return nil;
   }
   ungetc(c, in);

   value* v = read(env, in);
   return cons(env, v, read_list(env, in));
}

value* read_entry(environment* env, FILE* in)
{
   skip_whitespace(in);
   int c = fgetc(in);
   if (c == '}') {
      return nil;
   }
   ungetc(c, in);

   value* k = read(env, in);

   skip_whitespace(in);

   c = fgetc(in);
   if (c == '}') {
      fprintf(stdout, "Map literal must have an even number of forms.");
      return nil;
   }
   ungetc(c, in);

   value* v = read(env, in);

   return entry_value(env, k, v);
}

value* read(environment* env, FILE* in)
{
   skip_whitespace(in);
   int c = fgetc(in);
   while (c != EOF)
   {
      // read number
      if (is_digit(c) || c == '-') {
         int sign = (c == '-') ? -1 : 1;
         int n = 0;
         while (is_digit(c) && c != EOF) {
            n = n * 10 + c - '0';
            c = fgetc(in);
         }
         ungetc(c, in);
         return number_value(env, n);
      }
      // read string
      else if (c == '"') {
         char buffer[READER_STRING_LENGTH_MAX];
         char* p = buffer;
         c = fgetc(in);
         while (c != '"' && c != EOF) {
            if (c == '\\') {
               c = fgetc(in);
               if (c == 'n') {
                  c = '\n';
               }
               else if (c == 't') {
                  c = '\t';
               }
            }
            *p++ = c;
            c = fgetc(in);
         }
         *p++ = 0;
         return string_value(env, buffer, p - buffer);
      }
      // read list
      else if (c == '(') {
         return read_list(env, in);
      }
      else if (c == '{') {
         value* e = read_entry(env, in);
         value* entries = e;
         while (e != nil) {
            e->d.entry.next = read_entry(env, in);
            e = e->d.entry.next;
         }
         return map_value(env, entries);
      }
      // read keyword
      else if (c == ':') {
         return read_symbol(env, fgetc(in), in, keyword_value);
      }
      // read symbol
      else {
         return read_symbol(env, c, in, symbol_value);
      }

      // just keep reading
      c = fgetc(in);
   }

   return 0;
}

void print(environment* i, value* v, FILE* out)
{
   if (v == nil) {
      fprintf(out, "nil");
   }
   else if (v == T) {
      fprintf(out, "true");
   }
   else if (v->tag == GARBAGE_VALUE) {
       fprintf(out, "Internal error, trying to print garbage!?!\n");
   }
   else if (v->tag == NUMBER_VALUE) {
      fprintf(out, "%d", v->d.number);
   }
   else if (v->tag == STRING_VALUE) {
      char* p = v->d.string;
      fputc('"', out);
      while (*p) {
         if (*p == '"') {
            fputc('\\', out);
            fputc(*p, out);
         } else if (*p == '\n') {
            fputc('\\', out);
            fputc('n', out);
         } else {
            fputc(*p, out);
         }
         p++;
      }
      fputc('"', out);
   }
   else if (v->tag == SYMBOL_VALUE) {
      if (v->d.symbol.namespace) {
         fprintf(out, "%s/%s", v->d.symbol.namespace, v->d.symbol.name);
      }
      else
      {
         fprintf(out, "%s", v->d.symbol.name);
      }
   }
   else if (v->tag == KEYWORD_VALUE) {
      if (v->d.symbol.namespace) {
         fprintf(out, ":%s/%s", v->d.symbol.namespace, v->d.symbol.name);
      }
      else
      {
         fprintf(out, ":%s", v->d.symbol.name);
      }
   }
   else if (v->tag == MAP_VALUE) {
      fprintf(out, "{");

      value* e = v->d.map.entries;
      for (value* p = e; p != nil; p = p->d.entry.next) {
         if (p != e) {
            fprintf(out, ", ");
         }
         print(i, p->d.entry.key, out);
         fprintf(out, " ");
         print(i, p->d.entry.val, out);
      }

      fprintf(out, "}");
   }
   else if (v->tag == CONS_VALUE) {
      fprintf(out, "(");

      if (cdr(v) == nil || cdr(v)->tag == CONS_VALUE) {
         for (value* p = v; p != nil; p = cdr(p))
         {
            if (p != v)
            {
               fprintf(out, " ");
            }
            print(i, car(p), out);
         }
      } else {
         print(i, car(v), out);
         fprintf(out, " . ");
         print(i, cdr(v), out);
      }

      fprintf(out, ")");
   }
   else if (v->tag == BUILTIN_VALUE) {
      fprintf(out, "#<builtin>");
   }
   else if (v->tag == PROC_VALUE) {
      //fprintf(out, "#<procedure>");
      fprintf(out, "(fn ");
      print(i, v->d.proc.arglist, out);
      fprintf(out, " ");
      print(i, v->d.proc.body, out);
      fprintf(out, ")");
   }
}

void println(environment* i, value* v, FILE* out)
{
   print(i, v, out);
   fprintf(out, "\n");
}

value* pr(environment* e, value* v)
{
   println(e, v, stdout);
   return v;
}

value* pr_str(environment* env, value* v)
{
   char buffer[READER_STRING_LENGTH_MAX];
   FILE* f = string_open_write(buffer, READER_STRING_LENGTH_MAX);
   print(0, v, f);
   fclose(f);

   return string_value(env, buffer, strlen(buffer));
}

value* readstr(environment* l, const char* s)
{
   FILE* f = string_open(s);
   value* v = read(l, f);
   fclose(f);
   return v;
}

value* lookup_symbol(value* env, value* sym)
{
   value* i = env;
   while (i != nil) {
      value* s = car(car(i));
      // TODO: just check names for now
      if (strcmp(s->d.symbol.name, sym->d.symbol.name) == 0)
      {
         return cdr(car(i));
      }
      i = cdr(i);
   }

   return nil;
}

value* bind_symbol(environment* e, value* env, value* sym, value* v)
{
   value* existing = lookup_symbol(env, sym);
   if (existing != nil) {
      printf("replacing binding for symbol: ");
      print(e, sym, stdout);
      printf(" -> ");
      println(e, existing, stdout);
   }

   printf("Binding symbol: ");
   print(e, sym, stdout);
   printf(" to ");
   print(e, v, stdout);
   printf("\n");

   return cons(e, cons(e, sym, v), env);
}

value* bind_fn(environment* e, value* env, char* s, builtin_fn f)
{
   return bind_symbol(e, env, symbol_value(e, 0, s), builtin_value(e, f));
}

value* bind_var(environment* e, value* sym, value* v)
{
   return nil;
}

value* eval(environment* e, value*, value*);

value* eval_args(environment* e, value* args, value* env)
{
   if (args == nil) {
      return nil;
   }
   return cons(e, eval(e, car(args), env), eval_args(e, cdr(args), env));
}

value* apply(environment* e, value* f, value* args)
{
   if (f->tag == BUILTIN_VALUE) {
      builtin_fn fn = f->d.builtin.proc;
      value* argv[10];
      int cnt = 0;

      for (value* a = args; a != nil; a = cdr(a)) {
         argv[cnt++] = car(a);
      }

      switch (cnt) {
      case 0: return fn(e);
      case 1: return fn(e, argv[0]);
      case 2: return fn(e, argv[0], argv[1]);
      case 3: return fn(e, argv[0], argv[1], argv[2], argv[3]);
      case 4: return fn(e, argv[0], argv[1], argv[2], argv[3], argv[4]);
      case 5: return fn(e, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]);
      case 6: return fn(e, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);
      case 7: return fn(e, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);
      case 8: return fn(e, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8]);
      case 9: return fn(e, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8], argv[9]);
      }
   }

   if (f->tag == PROC_VALUE) {
      value* arglist = f->d.proc.arglist;
      value* env = f->d.proc.env;
      value* body = f->d.proc.body;

      while (arglist != nil) {
         if (args == nil) {
            printf("Wrong number of args to fn");
            return nil;
         }
         env = bind_symbol(e, env, car(arglist), car(args));
         arglist = cdr(arglist);
         args = cdr(args);
      }

      return eval(e, body, env);
   }

   return nil;
}

int is_special_form(value* f, const char* s)
{
   return (f->tag == SYMBOL_VALUE) && strcmp(s, f->d.symbol.name) == 0;
}

value* eval(environment* e, value* expr, value* env)
{
   u8 t = expr->tag;
   if (t == SYMBOL_VALUE) {
      value* s = lookup_symbol(env, expr);
      if (s == nil) {
         printf("no binding for: %s\n", expr->d.symbol.name);
      }
      return s;
   }
   else if (t != CONS_VALUE) {
      return expr;
   }

   value* f = car(expr);
   if (is_special_form(f, "fn")) {
      return proc_value(e, car(cdr(expr)), car(cdr(cdr(expr))), env);
   }
   if (is_special_form(f, "def")) {
      return bind_var(e, car(cdr(expr)), eval(e, cdr(cdr(expr)), env));
   }
   if (is_special_form(f, "quote")) {
      return car(cdr(expr));
   }

   return apply(e, eval(e, car(expr), env), eval_args(e, cdr(expr), env));
}

void assert_round_trip(environment* i, const char* expr)
{

}

void test_read_number(environment* i)
{
   assert(12345 == number(readstr(i, "12345")));
   assert(0 == number(readstr(i, "0")));
   assert(0 == number(readstr(i, "0000")));
}

void test_read_string(environment* i)
{
   value* s = readstr(i, "\"foo\"");
   assert(strcmp("foo", string(s)) == 0);
   s = readstr(i, "\"fo\\\"ooo\\\"oobar\"");
   assert(strcmp("fo\"ooo\"oobar", string(s)) == 0);
}

void test_read_symbol(environment *i)
{
   value* s = readstr(i, "foo/bar");
   assert(strcmp(s->d.symbol.namespace, "foo") == 0);
   assert(strcmp(s->d.symbol.name, "bar") == 0);
}

void test_read_list(environment* i)
{
   value* l = readstr(i, "(1 2 3 4 5)");

   assert(1 == number(car(l)));
}

void test_read_map(environment* i)
{
   value* m = readstr(i, "{1 2 3 4 5 6}");

   gc(i, nil);

}

void test_cons(environment* i)
{
   value* v = number_value(i, 42);
   value* v2 = string_value(i, "fooo", 5);
   value* l = cons(i, v2, cons(i, v, nil));

   assert(42 == number(v));
   assert(v2 == car(l));
   assert(nil == cdr(cdr(l)));

   print(i, l, stdout);
}

void test_string_write(environment* e)
{
   char buffer[8];
   int len = sizeof(buffer);
   FILE* f = string_open_write(buffer, len);
   assert(f);

   fprintf(f, "123456789");
   fclose(f);


   assert(buffer[0] == '1');
   assert(buffer[7] == 0);

   assert(strcmp("42", string(pr_str(e, number_value(e, 42)))) == 0);

}

void test_gc()
{
   environment env = {0};

   value* sym = symbol_value(&env, 0, "test");

   gc(&env, nil);

   alloc_value(&env);
   alloc_value(&env);
   alloc_value(&env);

   gc(&env, nil);
}

void tests(environment* i)
{
   test_read_map(i);
   test_gc();
   test_string_write(i);

   print(i, eval(i, readstr(i, "((fn (x) x) 42)"), nil), stdout);

   print(i, eval(i, readstr(i, "(fn (x) (inc x))"), nil), stdout);


   test_read_list(i);
   test_read_number(i);
   test_read_string(i);
   test_read_symbol(i);
   test_cons(i);
}


value* identity(environment* e, value* x)
{
   return x;
}

value* inc(environment* e, value* v)
{
   int n = number(v);
   return number_value(e, n + 1);
}

value* plus(environment* e, value* a, value* b)
{
   int va = number(a);
   int vb = number(b);
   return number_value(e, va + vb);
}

value* stats(environment* env)
{
   printf("Allocation cnt: %i\n", env->alloc_count);
   printf("Object cnt: %i\n", env->object_count);
   printf("Symbol table:\n");
   println(env, env->symbols, stdout);

   return nil;
}

void repl(environment* i, FILE* in, FILE* out)
{
   value* v = 0;
   value* env = nil;

#define bind(f) env = bind_fn(i, env, #f, (builtin_fn) f)

   bind(identity);
   bind(inc);
   bind(plus);
   bind(stats);

#undef bind

   env = bind_fn(i, env, "=", equals);

   while (true) {
      fprintf(out, "\n> "); fflush(out);
      v = read(i, in);
      if (v) {
         fprintf(out, "read: ");
         print(i, v, out);
         fprintf(out, "\n");
      }
      v = eval(i, v, env);
      if (v) {
         fprintf(out, "eval: ");
         println(i, v, out);
      }
      else
      {
         fprintf(out, "Bye\n");
         break;
      }
      println(i, env, out);
      gc(i, env);
      stats(i);
   }
}

int main(int argc, char** argv)
{
   size_t value_size = sizeof(value);
   environment env = {0};

   tests(&env);
   repl(&env, stdin, stdout);

   return 0;
}
