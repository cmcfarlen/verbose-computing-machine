
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
typedef int bool;

#define false 0
#define true 1

#define READER_STRING_LENGTH_MAX 1000

// core internal

typedef struct value
{
    u32 tag;
    union {
        int number;
        char* string;
    } d;
} value;


typedef struct interpreter
{
} interpreter;

value* alloc_value()
{
    value* v = (value*)malloc(sizeof(v));
    return v;
}

value* number_value(int n)
{
    value* v = alloc_value();
    v->d.number = n;
    return v;
}

value* string_value(char* s, int n)
{
    value* v = alloc_value();
    v->d.string = strndup(s, n);
    return v;
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

void skip_whitespace(FILE* f)
{
    int c = fgetc(f);
    while (is_space(c) && c != EOF)
    {
        c = fgetc(f);
    }
    ungetc(c, f);
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

        c = fgetc(in);
    }

    return 0;
}

value* readstr(interpreter* l, const char* s)
{
    FILE* f = string_open(s);
    value* v = read(l, f);
    fclose(f);
    return v;
}

void print(value* v, FILE* out)
{
    fprintf(out, "%d\n", v->d.number);
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

void tests(interpreter* i)
{
    test_read_number(i);
    test_read_string(i);
}

int main(int argc, char** argv)
{
    tests(0);

    return 0;
}
