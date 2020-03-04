#ifndef DYNARR_H
#define DYNARR_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define DynBuffer 100

// internal struct, do not touch
typedef struct DynArr
{
    char *DynPtr;
    size_t DynCapacity;
    size_t DynENumber;
    size_t *DynESize;
} DynArr;

// initialize DynArr, 0 on success
uint8_t DynArrInit(DynArr *d, size_t capacity);

// destroy DynArr
void DynArrDestroy(DynArr *d);

// get size
size_t DynArrGetAllSize(DynArr *d);

// get number of elements
size_t DynArrGetESize(DynArr *d);

// add string to DynArr
void DynArrStrAdd(DynArr *d, const char *e);

// get dynamically allocated value at index, first element is at index 0, on error NULL is returned
char *DynArrStrAt(const DynArr *d, size_t index);

// add int to DynArr
void DynArrIntAdd(DynArr *d, int64_t e);

// get int value at index, first element is at index 0, on error 0 is returned
int64_t DynArrIntAt(const DynArr *d, size_t index);

#endif