#include <d2d1.h>
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

RECT rc;
ID2D1HwndRenderTarget* pRT = 0;
ID2D1Bitmap* d2dBitmap = 0;

u32 bmw = 512;
u32 bmh = 512;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_QUIT:
        case WM_DESTROY:
        case WM_CLOSE: {
            exit(0);
        }
        case WM_PAINT: {
            pRT->BeginDraw();
            pRT->Clear(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
            pRT->DrawBitmap(d2dBitmap, D2D1::RectF(0, 0, bmw, bmh), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1::RectF(0, 0, bmw, bmh));
            pRT->EndDraw();
            break;
        }
        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                exit(0);
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            s32 xPos = LOWORD(lParam);
            s32 yPos = HIWORD(lParam);
            printf("%i %i\n", xPos, bmh - yPos);
            break;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

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
};

void findDistanceToNearestContour(void* data) {
    NearestContourFinder* ncf = (NearestContourFinder*)data;
    u8* bitmap = ncf->bitmap;
    s32 x = ncf->x;
    s32 y = ncf->y;
    u32 bmw = ncf->w;
    u32 bmh = ncf->h;

    u32 bmi = y * bmw * 4 + x * 4;
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
                u32 ind = yy * bmw * 4 + xx * 4;
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
                u32 ind = yy * bmw * 4 + xx * 4;
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
                u32 ind = yy * bmw * 4 + xx * 4;
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
                u32 ind = yy * bmw * 4 + xx * 4;
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

s32 main(s32 argc, s8** argv) {
    SYSTEM_INFO sysInf;
    GetSystemInfo(&sysInf);

    const u32 threadCount = 7;  //sysInf.dwNumberOfProcessors - 1;
    LPVOID threadData;

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

    u32 bmw = 512;
    u32 bmh = 512;
    u8* bitmap = (u8*)malloc(bmw * bmh * 4);
    for (u32 y = 0; y < bmh; y++) {
        for (u32 x = 0; x < bmw; x++) {
            u32 index = y * bmw * 4 + x * 4;
            u32 dx = bmw / 2 - x;
            u32 dy = bmh / 2 - y;
            if (sqrt(dx * dx + dy * dy) < 100) {
                bitmap[index + 0] = 0;
                bitmap[index + 1] = 0;
                bitmap[index + 2] = 0;
                bitmap[index + 3] = 255;
            } else {
                bitmap[index + 0] = 255;
                bitmap[index + 1] = 255;
                bitmap[index + 2] = 255;
                bitmap[index + 3] = 255;
            }
        }
    }
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

    f32 min = 0xffffffff;
    f32 max = 0;
    for (u32 i = 0; i < bmw * bmh; i++) {
        f32 v = sdf[i];
        if (v < min) min = v;
        if (v > max) max = v;
    }
    for (u32 i = 0; i < bmw * bmh; i++) {
        sdf[i] /= max - min;
    }

    u32 bct = 0;
    for (u32 i = 0; i < bmw * bmh; i++) {
        f32 v = ((sdf[i] + 1) / 2.0) * 255;
        
        bitmap[bct++] = (u8)v;
        bitmap[bct++] = (u8)v;
        bitmap[bct++] = (u8)v;
        bitmap[bct++] = 255;
    }

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(0);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.lpszClassName = "D2D";
    RegisterClass(&wc);

    RECT windowRect = {0, 0, (s64)bmw, (s64)bmh};
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, false);
    HWND hwnd = CreateWindowEx(0, "D2D", "D2D", WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, 0, 0, GetModuleHandle(0), 0);
    if (hwnd == 0) {
        return 1;
    }
    ShowWindow(hwnd, argc);

    ID2D1Factory* pD2DFactory = 0;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pD2DFactory);
    GetClientRect(hwnd, &rc);
    D2D1_HWND_RENDER_TARGET_PROPERTIES hrtp = D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
    hr = pD2DFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), hrtp, &pRT);

    D2D1_BITMAP_PROPERTIES bitmapProperties = {
        {DXGI_FORMAT_R8G8B8A8_UNORM,
         D2D1_ALPHA_MODE_IGNORE},
        96.0f,
        96.0f};

    D2D1_SIZE_U d2dSizeU = {
        bmw, bmh};

    pRT->CreateBitmap(d2dSizeU, bitmap, bmw * 4, bitmapProperties, &d2dBitmap);

    while (1) {
        MSG msg = {};
        while (GetMessage(&msg, 0, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    free(bitmap);
    return 0;
}