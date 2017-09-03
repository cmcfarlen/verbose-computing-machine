
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
   NUMBER_VALUE,
   STRING_VALUE,
   SYMBOL_VALUE,
   KEYWORD_VALUE,
   CONS_VALUE,
   BUILTIN_VALUE,
   PROC_VALUE
};

typedef struct value value;
typedef struct environment environment;
typedef value* (*builtin_fn)();

struct value
{
   u8 tag;
   u8 mark;
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
      struct builtin {
         builtin_fn proc;
      } builtin;
      struct proc {
         value* arglist;
         value* body;
         value* env;
      } proc;
   } d;
};

static value nil_value;
value* nil = &nil_value;


struct environment
{
   value* object_root; // root object to keep track of allocated values
   int object_count;
   int generation;
   value* symbols;
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

void gc(environment* env)
{
}

value* alloc_value(environment* env)
{
   value* v = (value*)malloc(sizeof(value));
   v->next = env->object_root;
   v->mark = -1;
   env->object_root = v;
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
   if (v->tag == NUMBER_VALUE) {
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
   else if (v->tag == CONS_VALUE) {
      fprintf(out, "(");

      for (value* p = v; p != nil; p = cdr(p))
      {
         if (p != v)
         {
            fprintf(out, " ");
         }
         print(i, car(p), out);
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
      // just check names for now
      if (strcmp(s->d.symbol.name, sym->d.symbol.name) == 0)
      {
         return cdr(car(i));
      }
      i = cdr(i);
   }

   printf("no binding for: %s\n", sym->d.symbol.name);
   return nil;
}

value* bind_symbol(environment* e, value* env, value* sym, value* v)
{
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

value* eval(environment* e, value* expr, value* env)
{
   u8 t = expr->tag;
   if (t == SYMBOL_VALUE) {
      return lookup_symbol(env, expr);
   }
   else if (t != CONS_VALUE) {
      return expr;
   }

   value* f = car(expr);
   if ((f->tag == SYMBOL_VALUE) && strcmp("fn", f->d.symbol.name) == 0) {
      return proc_value(e, car(cdr(expr)), car(cdr(cdr(expr))), env);
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

void tests(environment* i)
{
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
   printf("Allocation cnt: %i\n", env->object_count);
   printf("Symbol table:\n");
   for (value* l = env->symbols; l != nil; l = cdr(l)) {
      println(env, car(l), stdout);
   }

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
         print(i, v, out);
      }
      else
      {
        fprintf(out, "Bye\n");
         break;
      }
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
