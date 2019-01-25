#ifndef DYNARR_H
#define DYNARR_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// initialize to zero
typedef struct DynArr
{
    char *ptr; // 1 byte wide
    size_t NUMBER_OF_ELEMENTS;
    size_t ELEMENT_SIZE;

} DynArr;

void dynArrCharInit(DynArr *d, const char *e);
void dynArrCharAdd(DynArr *d, const char *e); // can be only used after dynArrCharInit

void dynArrIntInit(DynArr *d, int e);
void dynArrIntAdd(DynArr *d, int e); // can be only used after dynArrIntInit

void dynArrDestroy(DynArr *d);
void *dynArrAt(const DynArr *d, size_t index);
void dynArrClear(DynArr *d); // clear content, size of memblock is untouched

#endif