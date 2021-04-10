#pragma once

typedef char s8;
typedef short s16;
typedef int s32;
typedef long long s64;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef float f32;
typedef double f64;

struct Bitmap{
    u8* data;
    u32 w;
    u32 h;
};

static void sortBitmaps(Bitmap* bitmaps, u32 totalBitmaps){
    for(u32 i = 0; i < totalBitmaps - 1; i++){
        for(u32 j = i + 1; j < totalBitmaps; j++){
            u64 si = (u64)bitmaps[i].w * (u64)bitmaps[i].h;
            u64 sj = (u64)bitmaps[j].w * (u64)bitmaps[j].h;
            if(si > sj){
                Bitmap t = bitmaps[i];
                bitmaps[i] = bitmaps[j];
                bitmaps[j] = t;
            }
        }
    }
}