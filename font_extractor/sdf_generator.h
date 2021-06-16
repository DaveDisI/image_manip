#pragma once

#include <math.h>
#include <stdio.h>
#include <windows.h>
#include <assert.h>

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

struct WorkEntry {
    void (*function)(void*);
    void* data;
};

struct WorkQueue {
    static const u32 MAX_ENTRIES = 256;
    WorkEntry entries[MAX_ENTRIES];
    volatile u32 entryAddPos;
    volatile u32 entryStartPos;
    volatile u32 entriesAdded;
    volatile u32 entriesCompleted;
    HANDLE semaphore;
};

struct ThreadInfo {
    WorkQueue* queue;
    s32 index;
};

static void addWorkQueueEntry(WorkQueue* wq, void (*function)(void*), void* data) {
    u32 nextAddPos = (wq->entryAddPos + 1) % wq->MAX_ENTRIES;
    assert(nextAddPos != wq->entryStartPos);
    wq->entries[wq->entryAddPos].function = function;
    wq->entries[wq->entryAddPos].data = data;
    wq->entriesAdded++;
    _mm_sfence();
    wq->entryAddPos = nextAddPos;
    ReleaseSemaphore(wq->semaphore, 1, 0);
}

static inline boolean performWorkQueueEntry(WorkQueue* wq) {
    u32 started = wq->entryStartPos;
    u32 nextStartPos = (started + 1) % wq->MAX_ENTRIES;
    if (started != wq->entryAddPos) {
        u32 wes = InterlockedCompareExchange((volatile LONG*)&wq->entryStartPos, nextStartPos, started);
        if(wes == started){
            WorkEntry* entry = wq->entries + wes;
            entry->function(entry->data);
 
            InterlockedIncrement((volatile LONG*)&wq->entriesCompleted);
            return true;
        }
    }
    return false;
}

static void completeWorkQueueEntries(WorkQueue* wq) {
    while (wq->entriesCompleted < wq->entriesAdded) {
        performWorkQueueEntry(wq);
    }
    wq->entriesAdded = 0;
    wq->entriesCompleted = 0;
}

DWORD threadProc(LPVOID threadData) {
    ThreadInfo* threadInfo = (ThreadInfo*)threadData;
    while (true) {
        if (!performWorkQueueEntry(threadInfo->queue)) {
            WaitForSingleObjectEx(threadInfo->queue->semaphore, INFINITE, false);
        }
    }
    return 0;
}

f32 distance(f32 x1, f32 y1, f32 x2, f32 y2) {
    return sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
}

struct NearestContourFinder {
    u8* bitmap;
    f32* distance;
    s32 x;
    s32 y;
    u32 w;
    u32 h;
    u32 bytesPerPixel;
};

void findDistanceToNearestContour(void* data) {
    NearestContourFinder* ncf = (NearestContourFinder*)data;
    u8* bitmap = ncf->bitmap;
    s32 x = ncf->x;
    s32 y = ncf->y;
    u32 bmw = ncf->w;
    u32 bmh = ncf->h;
    u32 bbp = ncf->bytesPerPixel;

    u32 bmi = y * bmw * bbp + x * bbp;
    u32 sdi = y * bmw + x;
    s32 range = 1;
    f32 cDist = 0xffffffff;
    u8 fndValue = 0;

    if (bitmap[bmi] == 0) {
        fndValue = 255;
    }

    bool found = false;
    while (!found) {
        //check upper limit
        s32 yy = y - range;
        s32 xx;
        if (yy > -1) {
            for (xx = x - range; xx < x + range; xx++) {
                if (xx < 0 || xx >= bmw) continue;
                u32 ind = yy * bmw * bbp + xx * bbp;
                f32 d = 0;
                if (bitmap[ind] == fndValue) {
                    d = distance(x, y, xx, yy);
                    if (d < cDist) cDist = d;
                    found = true;
                }
            }
        }
        //check lower limit
        yy = y + range;
        if (yy < bmh) {
            for (xx = x - range; xx < x + range; xx++) {
                if (xx < 0 || xx >= bmw) continue;
                u32 ind = yy * bmw * bbp + xx * bbp;
                f32 d = 0;
                if (bitmap[ind] == fndValue) {
                    d = distance(x, y, xx, yy);
                    if (d < cDist) cDist = d;
                    found = true;
                }
            }
        }
        //check left limit
        xx = x - range;
        if (xx > -1) {
            for (yy = y - range; yy < y + range; yy++) {
                if (yy < 0 || yy >= bmh) continue;
                u32 ind = yy * bmw * bbp + xx * bbp;
                f32 d = 0;
                if (bitmap[ind] == fndValue) {
                    d = distance(x, y, xx, yy);
                    if (d < cDist) cDist = d;
                    found = true;
                }
            }
        }
        //check right limit
        xx = x + range;
        if (xx < bmw) {
            for (yy = y - range; yy < y + range; yy++) {
                if (yy < 0 || yy >= bmh) continue;
                u32 ind = yy * bmw * bbp + xx * bbp;
                f32 d = 0;
                if (bitmap[ind] == fndValue) {
                    d = distance(x, y, xx, yy);
                    if (d < cDist) cDist = d;
                    found = true;
                }
            }
        }
        range++;
    }
    if (fndValue == 255) {
        *ncf->distance = -cDist;
    } else {
        *ncf->distance = cDist;
    }
}

static f32* convertBitmapToSDF(u8* bitmap, u32 bmw, u32 bmh, u32 bytesPerPixel){
    const u32 threadCount = 7;  //sysInf.dwNumberOfProcessors - 1;
    WorkQueue workQueue = {};
    ThreadInfo threadInfo[threadCount + 1] = {};
    workQueue.semaphore = CreateSemaphoreEx(0, 0, threadCount, 0, 0, SEMAPHORE_ALL_ACCESS);
    for (u32 i = 0; i < threadCount; i++) {
        threadInfo[i].index = i;
        threadInfo[i].queue = &workQueue;
        DWORD threadID;
        HANDLE thread = CreateThread(0, 0, threadProc, &threadInfo[i], 0, &threadID);
        CloseHandle(thread);
    }
    threadInfo[threadCount].index = 7;
    threadInfo[threadCount].queue = &workQueue;

    f32* sdf = (f32*)malloc(bmw * bmh * sizeof(f32));
    NearestContourFinder* ncf = (NearestContourFinder*)malloc(bmw * bmh * sizeof(NearestContourFinder));
    int ctr = 0;
    for (s32 y = 0; y < bmw; y++) {
        for (s32 x = 0; x < bmh; x++) {
            u32 i = y * bmw + x;
            ncf[i].bitmap = bitmap;
            ncf[i].x = x;
            ncf[i].y = y;
            ncf[i].w = bmw;
            ncf[i].h = bmh;
            ncf[i].distance = &sdf[i];
            ncf[i].bytesPerPixel = bytesPerPixel;
            // findDistanceToNearestContour(&ncf[x][y]);
            addWorkQueueEntry(&workQueue, findDistanceToNearestContour, &ncf[i]);
            ctr++;
            if(ctr == workQueue.MAX_ENTRIES){
                completeWorkQueueEntries(&workQueue);
                ctr = 0;
            }
        }
    }
    
    completeWorkQueueEntries(&workQueue);

    f32 min = INFINITY;
    f32 max = 0;
    u32 sz = bmw * bmh;
    for (u32 i = 0; i < sz; i++) {
        f32 v = sdf[i];
        if (v < min) min = v;
        if (v > max) max = v;
    }
    for (u32 i = 0; i < sz; i++) {
        sdf[i] /= max - min;
    }
    return sdf;
}