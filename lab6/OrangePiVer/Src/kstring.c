#include "kstring.h"

unsigned long kstrlen(const char *s) {
    unsigned long n = 0;
    while (s[n])
        n++;
    return n;
}

int kstrcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        a++;
        b++;
    }

    return (unsigned char)*a - (unsigned char)*b;
}

int kstrncmp(const char *a, const char *b, unsigned long n) {
    while (n > 0 && *a && *b) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;

        a++;
        b++;
        n--;
    }

    if (n == 0)
        return 0;

    return (unsigned char)*a - (unsigned char)*b;
}

int str_eq(const char *a, const char *b) {
    return kstrcmp(a, b) == 0;
}

int starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix)
            return 0;

        s++;
        prefix++;
    }

    return 1;
}

unsigned long hextoi(const char *s, int n) {
    unsigned long r = 0;

    while (n-- > 0) {
        r <<= 4;

        if (*s >= '0' && *s <= '9')
            r += *s - '0';
        else if (*s >= 'a' && *s <= 'f')
            r += *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F')
            r += *s - 'A' + 10;

        s++;
    }

    return r;
}

const char *skip_spaces(const char *s) {
    while (*s == ' ')
        s++;

    return s;
}