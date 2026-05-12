#ifndef KSTRING_H
#define KSTRING_H

unsigned long kstrlen(const char *s);
int kstrcmp(const char *a, const char *b);
int kstrncmp(const char *a, const char *b, unsigned long n);

int str_eq(const char *a, const char *b);
int starts_with(const char *s, const char *prefix);
unsigned long hextoi(const char *s, int n);
const char *skip_spaces(const char *s);

#endif