#include <d2d1.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

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

#define SWAP16(x) ((x << 8) | (x >> 8))
#define SWAP32(x) ((x << 24) | (x >> 24) | ((x << 8) & 0x00ff0000) | ((x >> 8) & 0x0000ff00))
#define SWAP64(x) ((x << 56) | (x >> 56) |                                               \
                   ((x << 40) & 0x00ff000000000000) | ((x >> 40) & 0x000000000000ff00) | \
                   ((x << 16) & 0x0000ff0000000000) | ((x >> 16) & 0x0000000000ff0000) | \
                   ((x << 8) & 0x000000ff00000000) | ((x >> 8) & 0x00000000ff000000))

RECT rc;
ID2D1HwndRenderTarget* pRT = 0;
ID2D1Bitmap* d2dBitmap = 0;
u32 bmw, bmh;

struct V2 {
    s16 x;
    s16 y;
};

struct Line {
    V2 p1;
    V2 p2;
};

struct Curve {
    V2 p1;
    V2 p2;
    V2 cp;
};

struct GlyfShape {
    Line lines[256];
    Curve curves[256];
    s16 xMin;
    s16 xMax;
    s16 yMin;
    s16 yMax;
    u32 totalLines;
    u32 totalCurves;
};

struct TrueTypeFont {
    u8* fileData;
    u8* headPtr;
    u8* cmapPtr;
    u8* locaPtr;
    u8* glyfPtr;
    u32 locaEntrySize;
};

static f32 maxVal(f32 v1, f32 v2) {
    return v1 > v2 ? v1 : v2;
}

static f32 distance(V2 p1, V2 p2) {
    s32 d1 = p2.x - p1.x;
    s32 d2 = p2.y - p1.y;
    return sqrt(d1 * d1 + d2 * d2);
}

static f32 absoluteValue(f32 v) {
    return v > 0 ? v : -v;
}

static void printU32AsString(u32 s) {
    s8 str[] = {
        (s8)(s & 0x000000ff), (s8)((s >> 8) & 0x000000ff), (s8)((s >> 16) & 0x000000ff), (s8)(s >> 24), '\0'};
    printf("%s\n", str);
}

static u32 readBytesFromArray(u8* array, u32 numBytes, u32* offset, bool increaseOffset = true) {
    u32 result = 0;
    u32 byteIndex = *offset;
    for (int i = 0; i < numBytes; i++) {
        u8 byte = array[byteIndex];
        result |= byte << (i * 8);
        byteIndex++;
    }

    if (increaseOffset) *offset += numBytes;
    return result;
}

static u8* createBitmapFromShape(GlyfShape* shape) {
    u32 bmw = shape->xMax - shape->xMin;
    u32 bmh = shape->yMax - shape->yMin;

    u8* bitmap = (u8*)malloc(bmw * bmh * 4);
    u32 ctr = 0;
    for (s16 y = shape->yMin; y < shape->yMax; y++) {
        for (s16 x = shape->xMin; x < shape->xMax; x++) {
            u8 color[] = {255, 255, 255, 255};
            s32 windCount = 0;
            for (u32 i = 0; i < shape->totalLines; i++) {
                Line l = shape->lines[i];
                if ((l.p1.y < y && l.p2.y < y) ||
                    (l.p1.y > y && l.p2.y > y) ||
                    (l.p1.x > x && l.p2.x > x)) {
                    continue;
                } else if (l.p1.x == l.p2.x) {
                    if (l.p1.y <= y && l.p2.y > y) {
                        windCount++;
                    } else if (l.p2.y <= y && l.p1.y > y) {
                        windCount--;
                    } else {
                        continue;
                    }
                } else {
                    f32 m = (f32)(l.p2.y - l.p1.y) / (f32)(l.p2.x - l.p1.x);
                    f32 b = (f32)l.p2.y - (m * (f32)l.p2.x);
                    f32 xCrs = ((f32)y / m) - (b / m);
                    if (xCrs <= x) {
                        if (l.p1.y <= y && l.p2.y > y) {
                            windCount++;
                        } else if (l.p2.y <= y && l.p1.y > y) {
                            windCount--;
                        }
                    }
                }
            }
            for (u32 i = 0; i < shape->totalCurves; i++) {
                Curve cv = shape->curves[i];
                if ((cv.p1.y < y && cv.p2.y < y && cv.cp.y < y) ||
                    (cv.p1.y > y && cv.p2.y > y && cv.cp.y > y) ||
                    (cv.p1.x > x && cv.p2.x > x && cv.cp.x > x)) {
                    continue;
                } else {
                    f32 t0 = -1;
                    f32 t1 = -1;
                    f32 a = cv.p1.y;
                    f32 b = cv.p2.y;
                    f32 c = cv.cp.y;
                    if (a + b != 2 * c) {
                        f32 rt = sqrt(-a * b + a * y + b * y + c * c - 2 * c * y);
                        f32 denom = a + b - 2 * c;
                        t0 = -(rt - a + c) / (a + b - 2 * c);
                        t1 = (rt + a - c) / (a + b - 2 * c);
                        if ((t0 == 1 && t1 == 1) || (t0 == 0 && t1 == 0)) {
                            if (y > cv.p1.y || y > cv.p2.y) continue;
                        }
                        if (t0 > 0 && t0 <= 1) {
                            f32 xp0 = (((1 - t0) * (1 - t0)) * cv.p1.x) + ((2 * t0) * (1 - t0) * cv.cp.x) + (t0 * t0 * cv.p2.x);
                            if (xp0 <= x) {
                                windCount--;
                            }
                        }
                        if (t1 >= 0 && t1 < 1) {
                            f32 xp0 = (((1 - t1) * (1 - t1)) * cv.p1.x) + ((2 * t1) * (1 - t1) * cv.cp.x) + (t1 * t1 * cv.p2.x);
                            if (xp0 <= x) {
                                windCount++;
                            }
                        }
                    } else if (b != c && a == 2 * c - b) {
                        t0 = (b - 2 * c + y) / (2 * b - 2 * c);
                        if (t0 >= 0 && t0 < 1) {
                            f32 xp = (((1 - t0) * (1 - t0)) * cv.p1.x) + ((2 * t0) * (1 - t0) * cv.cp.x) + (t0 * t0 * cv.p2.x);
                            if (xp <= x) {
                                windCount++;
                            }
                        }
                    }
                }
            }

            if (windCount != 0) {
                color[0] = 0;
                color[1] = 0;
                color[2] = 0;
            }
            bitmap[ctr++] = color[0];
            bitmap[ctr++] = color[1];
            bitmap[ctr++] = color[2];
            bitmap[ctr++] = color[3];
        }
    }
    return bitmap;
}

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
            pRT->SetTransform(D2D1::Matrix3x2F::Scale(D2D1::Size(1.0f, -1.0f), D2D1::Point2F(0.0f, (bmh / 2) / 2)));
            pRT->DrawBitmap(d2dBitmap, D2D1::RectF(0, 0, bmw / 2, bmh / 2), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1::RectF(0, 0, bmw, bmh));
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
            printf("%i %i\n", xPos, (bmh / 2) - yPos);
            break;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static u16 getGlyfIndex(TrueTypeFont* font, u16 characterCode) {
    u32 dataOffset = 2;  //skip version
    u16 numCMAPTables = readBytesFromArray(font->cmapPtr, 2, &dataOffset);
    numCMAPTables = SWAP16(numCMAPTables);
    for (u32 i = 0; i < numCMAPTables; i++) {
        u16 platformID = readBytesFromArray(font->cmapPtr, 2, &dataOffset);
        u16 encodingID = readBytesFromArray(font->cmapPtr, 2, &dataOffset);
        u32 subtableOffset = readBytesFromArray(font->cmapPtr, 4, &dataOffset);
        platformID = SWAP16(platformID);
        encodingID = SWAP16(encodingID);
        subtableOffset = SWAP32(subtableOffset);
        subtableOffset *= 1;
        u16 format = readBytesFromArray(font->cmapPtr, 2, &subtableOffset);
        format = SWAP16(format);
        switch (format) {
            case 0: {
                if (characterCode > 255) break;
                subtableOffset += 4;  //skip length and language
                subtableOffset += characterCode;
                return readBytesFromArray(font->cmapPtr, 1, &subtableOffset);
            }
            case 2: {
                printf("2\n");
                break;
            }
            case 4: {
                subtableOffset += 4;  //skip length and language
                u16 segCountx2 = readBytesFromArray(font->cmapPtr, 2, &subtableOffset);
                subtableOffset += 6;  //skip search range, entry selector, and range shift
                segCountx2 = SWAP16(segCountx2);
                u16 segCount = segCountx2 / 2;

                u32 sz = segCount * sizeof(u16);
                u16* endCodes = (u16*)font->cmapPtr + (subtableOffset / 2);
                subtableOffset += sz;
                u16 reservedPad = readBytesFromArray(font->cmapPtr, 2, &subtableOffset);
                u16* startCodes = (u16*)font->cmapPtr + (subtableOffset / 2);
                subtableOffset += sz;
                u16* idDeltas = (u16*)font->cmapPtr + (subtableOffset / 2);
                subtableOffset += sz;
                u16* idRangeOffsets = (u16*)font->cmapPtr + (subtableOffset / 2);
                subtableOffset += sz;

                for (u32 j = 0; j < segCount; j++) {
                    endCodes[j] = SWAP16(endCodes[j]);
                    if (endCodes[j] >= characterCode) {
                        startCodes[j] = SWAP16(startCodes[j]);
                        if (startCodes[j] <= characterCode) {
                            idRangeOffsets[j] = SWAP16(idRangeOffsets[j]);
                            if (idRangeOffsets[j] > 0) {
                                u16 idx = *(&idRangeOffsets[j] + idRangeOffsets[j] / 2 + (characterCode - startCodes[j]));
                                idx = SWAP16(idx);
                                if (idx > 0) {
                                    idDeltas[j] = SWAP16(idDeltas[j]);
                                    return (idx + idDeltas[j]) % 65536;
                                }
                            } else {
                                idDeltas[j] = SWAP16(idDeltas[j]);
                                return (characterCode + idDeltas[j]) % 65536;
                            }
                        } else {
                            return 0;
                        }
                        break;
                    }
                }

                break;
            }
            case 6: {
                printf("6\n");
                break;
            }
            case 8: {
                printf("8\n");
                break;
            }
            case 10: {
                printf("10\n");
                break;
            }
            case 12: {
                printf("12\n");
                break;
            }
            case 13: {
                printf("13\n");
                break;
            }
            case 14: {
                printf("14\n");
                break;
            }
        }
    }
    return 0;
}

static u8* getGlyfPtrFromIndex(TrueTypeFont* ttf, u16 index) {
    u32 dataOffset = 0;
    u8* locaPtr = ttf->locaPtr + (index * ttf->locaEntrySize);
    u32 glyphOffset = readBytesFromArray(locaPtr, ttf->locaEntrySize, &dataOffset, false);
    if (ttf->locaEntrySize == 2) {
        glyphOffset = SWAP16(glyphOffset);
    } else {
        glyphOffset = SWAP32(glyphOffset);
    }
    return ttf->glyfPtr + glyphOffset;
}

static GlyfShape getGlyfShape(TrueTypeFont* ttf, u16 index) {
    GlyfShape shape = {};

    u8* glyfPtr = getGlyfPtrFromIndex(ttf, index);
    u32 dataOffset = 0;
    s16 numberOfContours = readBytesFromArray(glyfPtr, 2, &dataOffset);
    u16 gXMinU = readBytesFromArray(glyfPtr, 2, &dataOffset);
    u16 gYMinU = readBytesFromArray(glyfPtr, 2, &dataOffset);
    u16 gXMaxU = readBytesFromArray(glyfPtr, 2, &dataOffset);
    u16 gYMaxU = readBytesFromArray(glyfPtr, 2, &dataOffset);
    numberOfContours = SWAP16(numberOfContours);
    shape.xMin = SWAP16(gXMinU);
    shape.yMin = SWAP16(gYMinU);
    shape.xMax = SWAP16(gXMaxU);
    shape.yMax = SWAP16(gYMaxU);

    if (numberOfContours < 0) {
        GlyfShape s = {};
        MessageBox(0, "Error!", "Cannot get shape from complex glyfs", 0);
        return s;
    }

    u8 gflags[256];
    u16 endPtsOfContours[256];
    V2 points[256];

    for (u32 i = 0; i < numberOfContours; i++) {
        endPtsOfContours[i] = readBytesFromArray(glyfPtr, 2, &dataOffset);
        endPtsOfContours[i] = SWAP16(endPtsOfContours[i]);
    }
    u16 instructionLength = readBytesFromArray(glyfPtr, 2, &dataOffset);
    instructionLength = SWAP16(instructionLength);
    dataOffset += instructionLength;  // skip instructions

    u32 totalPoints = endPtsOfContours[numberOfContours - 1] + 1;
    for (u32 i = 0; i < totalPoints; i++) {
        gflags[i] = readBytesFromArray(glyfPtr, 1, &dataOffset);
        if (gflags[i] & 8) {
            u8 amt = readBytesFromArray(glyfPtr, 1, &dataOffset);
            u8 b = gflags[i];
            for (u32 j = 0; j < amt; j++) {
                i++;
                gflags[i] = b;
            }
        }
    }

    s16 prev = 0;
    for (u32 i = 0; i < totalPoints; i++) {
        u8 flg = gflags[i];
        if (flg & 2) {
            u8 x = readBytesFromArray(glyfPtr, 1, &dataOffset);
            if (flg & 16) {
                points[i].x = prev + x;
            } else {
                points[i].x = prev - x;
            }

        } else {
            if (flg & 16) {
                points[i].x = prev;
            } else {
                u16 x = readBytesFromArray(glyfPtr, 2, &dataOffset);
                x = SWAP16(x);
                points[i].x = prev + (s16)x;
            }
        }
        prev = points[i].x;
    }

    prev = 0;
    for (u32 i = 0; i < totalPoints; i++) {
        u8 flg = gflags[i];
        if (flg & 4) {
            u8 y = readBytesFromArray(glyfPtr, 1, &dataOffset);
            if (flg & 32) {
                points[i].y = prev + y;
            } else {
                points[i].y = prev - y;
            }
        } else {
            if (flg & 32) {
                points[i].y = prev;
            } else {
                u16 y = readBytesFromArray(glyfPtr, 2, &dataOffset);
                y = SWAP16(y);
                points[i].y = prev + (s16)y;
            }
        }
        prev = points[i].y;
    }

    for (u32 i = 0; i < numberOfContours; i++) {
        u32 start = i == 0 ? 0 : endPtsOfContours[i - 1] + 1;
        u32 end = endPtsOfContours[i] + 1;

        V2 gp = points[start];
        for (u32 j = start; j < end; j++) {
            u32 idx = j == end - 1 ? start : j + 1;
            V2 np = points[idx];
            if (gflags[idx] & 1) {
                if (gp.x == np.x && gp.y == np.y) continue;
                Line l = {gp.x, gp.y, np.x, np.y};
                shape.lines[shape.totalLines++] = l;
                gp = np;
            } else {
                u32 idx2 = j == end - 2 ? start : j + 2;
                V2 p3 = points[idx2];
                if (gflags[idx2] & 1) {
                    Curve c = {
                        gp, p3, np};
                    shape.curves[shape.totalCurves++] = c;
                    gp = p3;
                } else {
                    V2 bnp;
                    bnp.x = np.x + ((p3.x - np.x) / 2);
                    bnp.y = np.y + ((p3.y - np.y) / 2);
                    Curve c = {
                        gp, bnp, np};
                    shape.curves[shape.totalCurves++] = c;
                    gp = bnp;
                }
            }
        }
    }

    return shape;
}

static u8* getBitmapFromGlyfIndex(TrueTypeFont* ttf, u16 index, u32* width, u32* height) {
    u8* glyfPtr = getGlyfPtrFromIndex(ttf, index);
    u32 dataOffset = 0;
    s16 numberOfContours = readBytesFromArray(glyfPtr, 2, &dataOffset);
    numberOfContours = SWAP16(numberOfContours);

    dataOffset += 8;  //skip dimensions

    if (numberOfContours < 0) {
        //complex glyphs
        GlyfShape masterShape = {};
        masterShape.xMin = 0x7fff;
        masterShape.xMax = 0x8000;
        masterShape.yMin = 0x7fff;
        masterShape.yMax = 0x8000;

        bool MORE_COMPONENTS = true;
        while (MORE_COMPONENTS) {
            u16 flags = readBytesFromArray(glyfPtr, 2, &dataOffset);
            u16 gi = readBytesFromArray(glyfPtr, 2, &dataOffset);
            flags = SWAP16(flags);
            gi = SWAP16(gi);
            GlyfShape shape = getGlyfShape(ttf, gi);

            bool ARG_1_AND_2_ARE_WORDS = flags & 1;
            bool ARGS_ARE_XY_VALUES = flags & 2;
            bool ROUND_XY_TO_GRID = flags & 4;
            bool WE_HAVE_A_SCALE = flags & 8;
            MORE_COMPONENTS = flags & 32;
            bool WE_HAVE_AN_X_AND_Y_SCALE = flags & 64;
            bool WE_HAVE_A_TWO_BY_TWO = flags & 128;
            bool WE_HAVE_INSTRUCTIONS = flags & 256;
            bool USE_MY_METRICS = flags & 512;
            bool OVERLAP_COMPOUND = flags & 1024;

            s16 a, b, c, d, m, n; 
            s32 e, f;  

            if (WE_HAVE_A_TWO_BY_TWO) {
                a = readBytesFromArray(glyfPtr, 2, &dataOffset);
                b = readBytesFromArray(glyfPtr, 2, &dataOffset);
                c = readBytesFromArray(glyfPtr, 2, &dataOffset);
                d = readBytesFromArray(glyfPtr, 2, &dataOffset);
                a = SWAP16(a);
                b = SWAP16(b);
                c = SWAP16(c);
                d = SWAP16(d);
            } else if (WE_HAVE_AN_X_AND_Y_SCALE) {
                a = readBytesFromArray(glyfPtr, 2, &dataOffset);
                b = 0;
                c = 0;
                d = readBytesFromArray(glyfPtr, 2, &dataOffset);
                a = SWAP16(a);
                d = SWAP16(d);
            } else if (WE_HAVE_A_SCALE) {
                a = readBytesFromArray(glyfPtr, 2, &dataOffset);
                b = 0;
                c = 0;
                d = a;
                a = SWAP16(a);
            } else {
                a = 1.0f;
                b = 0;
                c = 0;
                d = 1.0f;
            }

            if (ARG_1_AND_2_ARE_WORDS && ARGS_ARE_XY_VALUES) {
                e = readBytesFromArray(glyfPtr, 2, &dataOffset);
                f = readBytesFromArray(glyfPtr, 2, &dataOffset);
                e = (s16)SWAP16(e);
                f = (s16)SWAP16(f);
            } else if (!ARG_1_AND_2_ARE_WORDS && ARGS_ARE_XY_VALUES) {
                e = (s8)readBytesFromArray(glyfPtr, 1, &dataOffset);
                f = (s8)readBytesFromArray(glyfPtr, 1, &dataOffset);
            } else if (ARG_1_AND_2_ARE_WORDS && !ARGS_ARE_XY_VALUES) {
                // 1st short contains the index of matching point in compound being constructed
                // 2nd short contains index of matching point in component
            } else if (!ARG_1_AND_2_ARE_WORDS && !ARGS_ARE_XY_VALUES) {
                // 1st byte containing index of matching point in compound being constructed
                // 2nd byte containing index of matching point in component
            }

            f32 m0 = maxVal(absoluteValue(a), absoluteValue(b));
            f32 n0 = maxVal(absoluteValue(c), absoluteValue(d));
            if (absoluteValue(absoluteValue(a) - absoluteValue(c)) < 33.0 / 65536.0) {
                m = 2 * m0;
            } else {
                m = m0;
            }
            if (absoluteValue(absoluteValue(b) - absoluteValue(d)) < 33.0 / 65536.0) {
                n = 2 * n0;
            } else {
                n = n0;
            }

            for (u32 i = 0; i < shape.totalLines; i++) {
                s16 x1 = shape.lines[i].p1.x;
                s16 y1 = shape.lines[i].p1.y;
                s16 x2 = shape.lines[i].p2.x;
                s16 y2 = shape.lines[i].p2.y;

                s16 xp1 = m * ((a / m) * x1 + (c / m) * y1 + e);
                s16 xp2 = m * ((a / m) * x2 + (c / m) * y2 + e);
                s16 yp1 = n * ((b / n) * x1 + (d / n) * y1 + f);
                s16 yp2 = n * ((b / n) * x2 + (d / n) * y2 + f);
                shape.lines[i].p1.x = xp1;
                shape.lines[i].p1.y = yp1;
                shape.lines[i].p2.x = xp2;
                shape.lines[i].p2.y = yp2;
                masterShape.lines[masterShape.totalLines++] = shape.lines[i];

                if(xp1 < masterShape.xMin) masterShape.xMin = xp1;
                if(xp2 < masterShape.xMin) masterShape.xMin = xp2;
                if(xp1 > masterShape.xMax) masterShape.xMax = xp1;
                if(xp2 > masterShape.xMax) masterShape.xMax = xp2;
                if(yp1 < masterShape.yMin) masterShape.yMin = yp1;
                if(yp2 < masterShape.yMin) masterShape.yMin = yp2;
                if(yp1 > masterShape.yMax) masterShape.yMax = yp1;
                if(yp2 > masterShape.yMax) masterShape.yMax = yp2;
            }

            for (u32 i = 0; i < shape.totalCurves; i++) {
                s16 x1 = shape.curves[i].p1.x;
                s16 y1 = shape.curves[i].p1.y;
                s16 x2 = shape.curves[i].p2.x;
                s16 y2 = shape.curves[i].p2.y;
                s16 cx = shape.curves[i].cp.x;
                s16 cy = shape.curves[i].cp.y;

                s16 xp1 = m * ((a / m) * x1 + (c / m) * y1 + e);
                s16 xp2 = m * ((a / m) * x2 + (c / m) * y2 + e);
                s16 cxp = m * ((a / m) * cx + (c / m) * cy + e);
                s16 yp1 = n * ((b / n) * x1 + (d / n) * y1 + f);
                s16 yp2 = n * ((b / n) * x2 + (d / n) * y2 + f);
                s16 cyp = n * ((b / n) * cx + (d / n) * cy + f);
                shape.curves[i].p1.x = xp1;
                shape.curves[i].p1.y = yp1;
                shape.curves[i].p2.x = xp2;
                shape.curves[i].p2.y = yp2;
                shape.curves[i].cp.x = cxp;
                shape.curves[i].cp.y = cyp;
                masterShape.curves[masterShape.totalCurves++] = shape.curves[i];

                if(xp1 < masterShape.xMin) masterShape.xMin = xp1;
                if(xp2 < masterShape.xMin) masterShape.xMin = xp2;
                if(cxp < masterShape.xMin) masterShape.xMin = cxp;
                if(xp1 > masterShape.xMax) masterShape.xMax = xp1;
                if(xp2 > masterShape.xMax) masterShape.xMax = xp2;
                if(cxp > masterShape.xMax) masterShape.xMax = cxp;
                if(yp1 < masterShape.yMin) masterShape.yMin = yp1;
                if(yp2 < masterShape.yMin) masterShape.yMin = yp2;
                if(cyp < masterShape.yMin) masterShape.yMin = cyp;
                if(yp1 > masterShape.yMax) masterShape.yMax = yp1;
                if(yp2 > masterShape.yMax) masterShape.yMax = yp2;
                if(cyp > masterShape.yMax) masterShape.yMax = cyp;
                
            }
        }

        *width = masterShape.xMax - masterShape.xMin;
        *height = masterShape.yMax - masterShape.yMin;

        return createBitmapFromShape(&masterShape);
    } else {
        GlyfShape shape = getGlyfShape(ttf, index);

        *width = shape.xMax - shape.xMin;
        *height = shape.yMax - shape.yMin;

        return createBitmapFromShape(&shape);
    }
}

static void initTrueTypeFont(s8* fileName, TrueTypeFont* font) {
    FILE* file = fopen(fileName, "rb");
    fseek(file, 0L, SEEK_END);
    u32 fileSize = ftell(file);
    rewind(file);
    u8* fileData = (u8*)malloc(fileSize);
    fread(fileData, 1, fileSize, file);
    fclose(file);

    u32 dataOffset = 0;
    u32 sfntVersion = readBytesFromArray(fileData, 4, &dataOffset);
    u16 numTables = readBytesFromArray(fileData, 2, &dataOffset);
    u16 searchRange = readBytesFromArray(fileData, 2, &dataOffset);
    u16 entrySelector = readBytesFromArray(fileData, 2, &dataOffset);
    u16 rangeShift = readBytesFromArray(fileData, 2, &dataOffset);
    numTables = SWAP16(numTables);

    u32 headerTableOffset = 0;
    u32 cmapTableOffset = 0;
    u32 locaTableOffset = 0;
    u32 glyfTableOffset = 0;
    for (u32 i = 0; i < numTables; i++) {
        u32 tag = readBytesFromArray(fileData, 4, &dataOffset);
        u32 checksum = readBytesFromArray(fileData, 4, &dataOffset);
        u32 offset = readBytesFromArray(fileData, 4, &dataOffset);
        u32 length = readBytesFromArray(fileData, 4, &dataOffset);
        offset = SWAP32(offset);

        switch (tag) {
            case 'daeh': {
                headerTableOffset = offset;
                break;
            }
            case 'pamc': {
                cmapTableOffset = offset;
                break;
            }
            case 'acol': {
                locaTableOffset = offset;
                break;
            }
            case 'fylg': {
                glyfTableOffset = offset;
                break;
            }
        }
    }

    u8* headPtr = fileData + headerTableOffset;
    dataOffset = 0;
    u16 majorVersion = readBytesFromArray(headPtr, 2, &dataOffset);
    u16 minorVersion = readBytesFromArray(headPtr, 2, &dataOffset);
    f32 fontRevision = readBytesFromArray(headPtr, 4, &dataOffset);
    u32 checksumAdjustment = readBytesFromArray(headPtr, 4, &dataOffset);
    u32 majicNumber = readBytesFromArray(headPtr, 4, &dataOffset);
    u16 flags = readBytesFromArray(headPtr, 2, &dataOffset);
    u16 unitsPerEm = readBytesFromArray(headPtr, 2, &dataOffset);
    dataOffset += 16;  //skip created and modified dates
    s16 xmin = readBytesFromArray(headPtr, 2, &dataOffset);
    s16 ymin = readBytesFromArray(headPtr, 2, &dataOffset);
    s16 xmax = readBytesFromArray(headPtr, 2, &dataOffset);
    s16 ymax = readBytesFromArray(headPtr, 2, &dataOffset);
    u16 macStyle = readBytesFromArray(headPtr, 2, &dataOffset);
    u16 lowestRecPPEM = readBytesFromArray(headPtr, 2, &dataOffset);
    s16 fontDirectionHint = readBytesFromArray(headPtr, 2, &dataOffset);
    s16 indexToLocFormat = readBytesFromArray(headPtr, 2, &dataOffset);
    s16 glyphDataFormat = readBytesFromArray(headPtr, 2, &dataOffset);

    flags = SWAP16(flags);
    xmin = SWAP16(xmin);
    ymin = SWAP16(ymin);
    xmax = SWAP16(xmax);
    ymax = SWAP16(ymax);
    lowestRecPPEM = SWAP16(lowestRecPPEM);
    fontDirectionHint = SWAP16(fontDirectionHint);
    indexToLocFormat = SWAP16(indexToLocFormat);
    glyphDataFormat = SWAP16(glyphDataFormat);

    font->fileData = fileData;
    font->cmapPtr = fileData + cmapTableOffset;
    font->headPtr = headPtr;
    font->locaPtr = fileData + locaTableOffset;
    font->glyfPtr = fileData + glyfTableOffset;
    font->locaEntrySize = (2 + (2 * indexToLocFormat));
}

s32 main(u32 argc, s8** argv) {
    TrueTypeFont ttf = {};
    initTrueTypeFont("ARIAL.TTF", &ttf);
    u16 idx = getGlyfIndex(&ttf, 0x012f);
    u8* bitmap = getBitmapFromGlyfIndex(&ttf, idx, &bmw, &bmh);

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(0);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.lpszClassName = "D2D";
    RegisterClass(&wc);

    RECT windowRect = {0, 0, (s64)bmw / 2, (s64)bmh / 2};
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