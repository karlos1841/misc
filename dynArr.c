#include "custom/dynArr.h"

uint8_t DynArrInit(DynArr *d, size_t capacity)
{
    d->DynPtr = calloc(capacity + 1, sizeof(char));
    if(d->DynPtr == NULL) return 1;

    // set capacity
    d->DynCapacity = (capacity + 1) * sizeof(char);

    // set number of elements to 0
    d->DynENumber = 0;

    // set size of each element, for now it's NULL
    d->DynESize = NULL;

    return 0;
}

void DynArrDestroy(DynArr *d)
{
    // free array containing user data
    free(d->DynPtr);

    // free array containing size of each element
    free(d->DynESize);
}

size_t DynArrGetSize(DynArr *d)
{
    return strlen(d->DynPtr);
}

void DynArrStrAdd(DynArr *d, const char *e)
{
    size_t DynArrSize = DynArrGetSize(d);
    size_t e_size = strlen(e);

    // update number of elements and size of each element
    size_t *DynETmp = realloc(d->DynESize, (d->DynENumber + 1) * sizeof(size_t));
    if(DynETmp == NULL) return;

    d->DynESize = DynETmp;
    d->DynESize[d->DynENumber] = e_size;
    d->DynENumber += 1;


    // update DynArr content
    if(DynArrSize + e_size < d->DynCapacity)
    {
        memcpy(d->DynPtr + DynArrSize, e, e_size);
    }

    // reallocation needed
    else
    {
        char *DynTmp = calloc(DynArrSize + e_size + 1, sizeof(char));
        if(DynTmp == NULL) return;
        memcpy(DynTmp, d->DynPtr, DynArrSize);
        memcpy(DynTmp + DynArrSize, e, e_size);

        free(d->DynPtr);
        d->DynPtr = DynTmp;

        // update capacity
        d->DynCapacity = (DynArrSize + e_size + 1) * sizeof(char);
    }
}

char *DynArrStrAt(const DynArr *d, size_t index)
{
    if(index < d->DynENumber)
    {
        size_t total = 0;
        for(size_t i = 0; i < index; i++)
        {
            total += d->DynESize[i];
        }

        char *element = calloc(d->DynESize[index] + 1, sizeof(char));
        if(element == NULL) return NULL;

        memcpy(element, d->DynPtr + total, d->DynESize[index]);
        return element;
    }

    return NULL;
}

void DynArrIntAdd(DynArr *d, int64_t e)
{
    char int64_str[DynBuffer];
    snprintf(int64_str, DynBuffer, "%ld", e);

    DynArrStrAdd(d, int64_str);
}

int64_t DynArrIntAt(const DynArr *d, size_t index)
{
    char *int64_str = DynArrStrAt(d, index);
    if(int64_str == NULL) return 0;

    int64_t value;
    if((value = strtol(int64_str, NULL, 0)) == 0) return 0;

    free(int64_str);
    return value;
}