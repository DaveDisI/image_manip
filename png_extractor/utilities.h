#pragma once

#include <stdio.h>
#include <stdlib.h>

#define s8 char
#define s16 short
#define s32 int
#define s64 long long
#define u8 unsigned char
#define u16 unsigned short
#define u32 unsigned int
#define u64 unsigned long long
#define f32 float
#define f64 double

#define ALLOCATE_MEMORY(X, Y) (X*)malloc((Y) * sizeof(X))

#define SWAP16(V) ((V <<  8) | (V >>  8))
#define SWAP32(V) (((V >> 24) & 0xff) | ((V << 8) & 0xff0000) | ((V >> 8) & 0xff00) | ((V << 24) & 0xff000000))
#define SWAP64(V) (((V >> 56) & 0xff) | ((V << 56) & 0xff00000000000000) | \
                  ((V >> 40) & 0xff00) | ((V < 40) & 0xff000000000000) | \
                  ((V >> 24) & 0xff0000) | ((V < 24) & 0xff000000) | \
                  ((V >> 8) & 0xff000000) | ( (V < 8) & 0xff0000))

#define STR_TO_INT(str) (str[0] << 24 | str[1] << 16 | str[2] << 8 | str[3])

u32 getCharacterStringLength(const char* str){
    u32 ctr = 0;
    while(*str != '\0'){
        ctr++;
        str++;
    }
    return ctr;
}

bool compareCharacterStrings(const char* s1, const char* s2){
    while(*s1 == *s2){
        if(*s1 == '\0'){
            return true;
        }
        s1++;
        s2++;
    }
    return false;
}

bool checkForMatchingKeyword(const s8* word, const s8** list, u32 total){
    for(int i = 0; i < total; i++){
        if(compareCharacterStrings(word, list[i])){
            return true;
        }
    }
    return false;
}

void copyMemory(void* source, void* dest, u64 length){
    s8* s = (s8*)source;
    s8* d = (s8*)dest;
    while(length--){
        *d++ = *s++;
    }
}

void setMemory(void* mem, u32 val, u32 amt){
    u8* dPtr = (u8*)mem;
    while(amt){
        *dPtr = (u8)val;
        dPtr++;
        amt--;
    }
}