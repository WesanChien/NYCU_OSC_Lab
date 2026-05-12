#ifndef INITRD_H
#define INITRD_H

void initrd_set_range(const void *start, const void *end);
int initrd_available(void);

void initrd_list(void);
void initrd_cat(const char* filename);

#endif