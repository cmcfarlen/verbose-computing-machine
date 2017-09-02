
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

typedef struct value
{
    u8 tag;
    union {
        int number;
        char* string;
        struct symbol {
            char* namespace;
            char* name;
        } symbol;
        struct cons {
            struct value* car;
            struct value* cdr;
        } cons;
        struct builtin {
            struct value* (*proc)(struct value* args);
        } builtin;
        struct proc {
            struct value* arglist;
            struct value* body;
            struct value* env;
        } proc;
    } d;
} value;

static value nil_value;
value* nil = &nil_value;

typedef struct interpreter
{
} interpreter;

value* alloc_value()
{
    value* v = (value*)malloc(sizeof(value));
    return v;
}

value* number_value(int n)
{
    value* v = alloc_value();
    v->tag = NUMBER_VALUE;
    v->d.number = n;
    return v;
}

value* string_value(char* s, int n)
{
    value* v = alloc_value();
    v->tag = STRING_VALUE;
    v->d.string = strndup(s, n);
    return v;
}

value* symbol_value(char* namespace, char* name)
{
    value* v = alloc_value();
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
    return v;
}

value* keyword_value(char* namespace, char* name)
{
    value* v = alloc_value();
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

value* builtin_value(value* (*f)(value* args))
{
    value* v = alloc_value();
    v->tag = BUILTIN_VALUE;
    v->d.builtin.proc = f;
    return v;
}

value* proc_value(value* arglist, value* body, value* env)
{
    value* v = alloc_value();
    v->tag = PROC_VALUE;
    v->d.proc.arglist = arglist;
    v->d.proc.body = body;
    v->d.proc.env = env;

    return v;
}

value* cons(value* a, value* b)
{
    value* cell = alloc_value();
    cell->tag = CONS_VALUE;
    cell->d.cons.car = a;
    cell->d.cons.cdr = b;
    return cell;
}

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
    if (c == ' ' || c == ',' || c == '\n' || c == '\t')
    {
        return true;
    }
    return false;
}

bool is_delim(int c)
{
    return is_space(c) || c == ')';
}

void print(interpreter*, value*, FILE*);
void println(interpreter*, value*, FILE*);
value* pr(value*);
value* read(interpreter*, FILE*);

void skip_whitespace(FILE* f)
{
    int c = fgetc(f);
    while (is_space(c) && c != EOF)
    {
        c = fgetc(f);
    }
    ungetc(c, f);
}

value* read_symbol(interpreter* i, int c,  FILE* in, value*(*create_value)(char*, char*))
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
    return create_value(namespace, name);
}

value* read_list(interpreter* l, FILE* in)
{
    skip_whitespace(in);
    int c = fgetc(in);
    if (c == ')') {
        return nil;
    }
    ungetc(c, in);
    value* v = read(l, in);
    return cons(v, read_list(l, in));
}

value* read(interpreter* l, FILE* in)
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
            return number_value(n);
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
            return string_value(buffer, p - buffer);
        }
        // read list
        else if (c == '(') {
            return read_list(l, in);
        }
        // read keyword
        else if (c == ':') {
            return read_symbol(l, fgetc(in), in, keyword_value);
        }
        // read symbol
        else {
            return read_symbol(l, c, in, symbol_value);
        }

        // just keep reading
        c = fgetc(in);
    }

    return 0;
}

void print(interpreter* i, value* v, FILE* out)
{
    if (v->tag == NUMBER_VALUE)
    {
        fprintf(out, "%d", v->d.number);
    }
    else if (v->tag == STRING_VALUE)
    {
        fprintf(out, "\"%s\"", v->d.string);
    }
    else if (v->tag == SYMBOL_VALUE)
    {
        if (v->d.symbol.namespace) {
            fprintf(out, "%s/%s", v->d.symbol.namespace, v->d.symbol.name);
        }
        else
        {
            fprintf(out, "%s", v->d.symbol.name);
        }
    }
    else if (v->tag == KEYWORD_VALUE)
    {
        if (v->d.symbol.namespace) {
            fprintf(out, ":%s/%s", v->d.symbol.namespace, v->d.symbol.name);
        }
        else
        {
            fprintf(out, ":%s", v->d.symbol.name);
        }
    }
    else if (v->tag == CONS_VALUE)
    {
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
    else if (v->tag == BUILTIN_VALUE)
    {
        fprintf(out, "#<builtin>");
    }
    else if (v->tag == PROC_VALUE)
    {
        //fprintf(out, "#<procedure>");
        fprintf(out, "(fn ");
        print(i, v->d.proc.arglist, out);
        fprintf(out, " ");
        print(i, v->d.proc.body, out);
        fprintf(out, ")");
    }
}

void println(interpreter* i, value* v, FILE* out)
{
    print(i, v, out);
    fprintf(out, "\n");
}

value* pr(value* v)
{
    println(0, v, stdout);
    return v;
}

value* readstr(interpreter* l, const char* s)
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

value* bind_symbol(value* env, value* sym, value* v)
{
    printf("Binding symbol: ");
    print(0, sym, stdout);
    printf(" to ");
    print(0, v, stdout);
    printf("\n");

    return cons(cons(sym, v), env);
}

value* bind_fn(value* env, char* s, value*(*f)(value*))
{
    return bind_symbol(env, symbol_value(0, s), builtin_value(f));
}

value* eval(value*, value*);

value* eval_args(value* args, value* env)
{
    if (args == nil)
    {
        return nil;
    }
    return cons(eval(car(args), env), eval_args(cdr(args), env));
}

value* apply(value* f, value* args)
{
    if (f->tag == BUILTIN_VALUE) {
        return f->d.builtin.proc(args);
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
            env = bind_symbol(env, car(arglist), car(args));
            arglist = cdr(arglist);
            args = cdr(args);
        }

        return eval(body, env);
    }

    return nil;
}

value* eval(value* e, value* env)
{
    u8 t = e->tag;
    if (t == SYMBOL_VALUE)
    {
        return lookup_symbol(env, e);
    }
    if (t != CONS_VALUE)
    {
        return e;
    }

    value* f = car(e);
    if ((f->tag == SYMBOL_VALUE) && strcmp("fn", f->d.symbol.name) == 0)
    {
        return proc_value(car(cdr(e)), car(cdr(cdr(e))), env);
    }

    return apply(eval(car(e), env), eval_args(cdr(e), env));
}

void assert_round_trip(interpreter* i, const char* expr)
{

}

void test_read_number(interpreter* i)
{
    assert(12345 == number(readstr(i, "12345")));
    assert(0 == number(readstr(i, "0")));
    assert(0 == number(readstr(i, "0000")));
}

void test_read_string(interpreter* i)
{
    value* s = readstr(i, "\"foo\"");
    assert(strcmp("foo", string(s)) == 0);
    s = readstr(i, "\"fo\\\"ooo\\\"oobar\"");
    assert(strcmp("fo\"ooo\"oobar", string(s)) == 0);
}

void test_read_symbol(interpreter *i)
{
    value* s = readstr(i, "foo/bar");
    assert(strcmp(s->d.symbol.namespace, "foo") == 0);
    assert(strcmp(s->d.symbol.name, "bar") == 0);
}

void test_read_list(interpreter* i)
{
    value* l = readstr(i, "(1 2 3 4 5)");

    assert(1 == number(car(l)));
}

void test_cons(interpreter* i)
{
    value* v = number_value(42);
    value* v2 = string_value("fooo", 5);
    value* l = cons(v2, cons(v, nil));

    assert(42 == number(v));
    assert(v2 == car(l));
    assert(nil == cdr(cdr(l)));

    print(i, l, stdout);
}

void tests(interpreter* i)
{

    print(i, eval(readstr(i, "((fn (x) x) 42)"), nil), stdout);

    print(i, eval(readstr(i, "(fn (x) (inc x))"), nil), stdout);


    test_read_list(i);
    test_read_number(i);
    test_read_string(i);
    test_read_symbol(i);
    test_cons(i);
}

value* identity(value* args)
{
    printf("here at identity\n");
    return car(args);
}

value* inc(value* args)
{
    printf("here at inc\n");
    int n = number(car(args));
    return number_value(n + 1);
}

void repl(interpreter* i, FILE* in, FILE* out)
{
    value* v = 0;
    value* env = nil;

#define bind(f) env = bind_fn(env, #f, f)

    bind(identity);
    bind(inc);

    while (true) {
        fprintf(out, "\n> ");
        fflush(out);
        v = read(i, in);
        if (v) {
           fprintf(out, "read: ");
           print(i, v, out);
           fprintf(out, "\n");
        }
        v = eval(v, env);
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

    tests(0);
    repl(0, stdin, stdout);

    return 0;
}
