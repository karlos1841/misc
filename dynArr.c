#include "dynArr.h"

void dynArrCharInit(DynArr *d, const char *e)
{
    d->NUMBER_OF_ELEMENTS = strlen(e);
    d->ELEMENT_SIZE = sizeof(char);
    d->ptr = calloc(d->NUMBER_OF_ELEMENTS + 1, d->ELEMENT_SIZE);
    memcpy(d->ptr, e, (d->NUMBER_OF_ELEMENTS) * (d->ELEMENT_SIZE));
}

void dynArrIntInit(DynArr *d, int e)
{
    d->NUMBER_OF_ELEMENTS = 1;
    d->ELEMENT_SIZE = sizeof(int);
    d->ptr = calloc(d->NUMBER_OF_ELEMENTS, d->ELEMENT_SIZE);
    memcpy(d->ptr, &e, (d->NUMBER_OF_ELEMENTS) * (d->ELEMENT_SIZE));
}

void dynArrCharAdd(DynArr *d, const char *e)
{
    if(d->ELEMENT_SIZE != sizeof(char))
        // dynArrCharInit was not called
        return;

    size_t num_of_elem = strlen(e);
    char *ptr = calloc(d->NUMBER_OF_ELEMENTS + num_of_elem + 1, d->ELEMENT_SIZE);
    memcpy(ptr, d->ptr, (d->NUMBER_OF_ELEMENTS) * (d->ELEMENT_SIZE));
    memcpy(ptr + (d->NUMBER_OF_ELEMENTS) * (d->ELEMENT_SIZE), e, num_of_elem * (d->ELEMENT_SIZE));
    free(d->ptr);

    d->NUMBER_OF_ELEMENTS += num_of_elem;
    d->ptr = ptr;
}

void dynArrIntAdd(DynArr *d, int e)
{
    if(d->ELEMENT_SIZE != sizeof(int))
        // dynArrIntInit was not called
        return;

    char *ptr = calloc(d->NUMBER_OF_ELEMENTS + 1, d->ELEMENT_SIZE);
    memcpy(ptr, d->ptr, (d->NUMBER_OF_ELEMENTS) * (d->ELEMENT_SIZE));
    memcpy(ptr + (d->NUMBER_OF_ELEMENTS) * (d->ELEMENT_SIZE), &e, d->ELEMENT_SIZE);
    free(d->ptr);

    d->NUMBER_OF_ELEMENTS += 1;
    d->ptr = ptr;
}

void *dynArrAt(const DynArr *d, size_t index)
{
    if(index < d->NUMBER_OF_ELEMENTS)
        return d->ptr + index * d->ELEMENT_SIZE;

    return NULL;
}

void dynArrClear(DynArr *d)
{
    // clear all bytes
    for(size_t i = 0; i < (d->NUMBER_OF_ELEMENTS) * (d->ELEMENT_SIZE); i++)
        *(d->ptr + i) = 0;
}

void dynArrDestroy(DynArr *d)
{
    d->NUMBER_OF_ELEMENTS = 0;
    d->ELEMENT_SIZE = 0;
    free(d->ptr);
    d->ptr = NULL;
}