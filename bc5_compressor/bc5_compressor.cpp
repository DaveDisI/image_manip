#include <stdio.h>
#include <stdlib.h>

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

static u64 absoluteValue(u64 v){
    return v > 0 ? v : -v;
}

static void compressDataBlock(u8* dataBlock, u8* buffer){
    u8 retOrder[] = {1, 7, 6, 5, 4, 3, 2, 0};
    u8 minRed = 255;
    u8 maxRed = 0;
    u8 minGreen = 255;
    u8 maxGreen = 0;

    for(u32 i = 0; i < 32; i += 2){
        if(dataBlock[i] < minRed) minRed = dataBlock[i];
        if(dataBlock[i] > maxRed) maxRed = dataBlock[i];
        if(dataBlock[i + 1] < minGreen) minGreen = dataBlock[i + 1];
        if(dataBlock[i + 1] > maxGreen) maxGreen = dataBlock[i + 1];
    }

    u64* rBuf64 = (u64*)&buffer[0];
    u64* gBuf64 = (u64*)&buffer[8];
    *rBuf64 = maxRed;
    *rBuf64 |= ((u32)minRed << 8);
    *gBuf64 = maxGreen;
    *gBuf64 |= ((u32)minGreen << 8);
     
    u32 shiftCtr = 16;
    u8 redRange = maxRed - minRed;
    u8 greenRange = maxGreen - minGreen;
    for(u32 i = 0; i < 32; i += 2){
        u8 r = dataBlock[i + 0];
        u8 g = dataBlock[i + 1];
        f32 redRat = (f32)(r - minRed) / (f32)redRange;
        f32 redRet = redRat * 7.0f;
        u32 redRetI = redRet;
        f32 redDec = redRet - redRetI;
        u64 redBits = redDec > 0.5 ? retOrder[redRetI + 1] : retOrder[redRetI];
        f32 greenRat = (f32)(g - minGreen) / (f32)greenRange;
        f32 greenRet = greenRat * 7;
        u32 greenRetI = greenRet;
        f32 greenDec = greenRet - greenRetI;
        u64 greenBits = greenDec > 0.5 ? retOrder[greenRetI + 1] : retOrder[greenRetI];  
        *rBuf64 |= (redBits << shiftCtr);
        *gBuf64 |= (greenBits << shiftCtr);
        shiftCtr += 3;
    }
}

int main(int argc, char** argv){
    s8* fileName = "brick_normal.texpix";
    s8* outFileName = "C:/Users/Dave/Desktop/digital_cyber/brick_normal.bc5";
    
    u32 width = 0;
    u32 height = 0;
    FILE* file = fopen(fileName, "rb");
    fread(&width, 4, 1, file);
    fread(&height, 4, 1, file);
    u32 dataSize = width * height * 4;
    u8* uncompressedData = (u8*)malloc(dataSize);
    fread(uncompressedData, 1, dataSize, file);
    fclose(file);

    u32 compressedDataSize = width * height;
    u32 compressedDataCtr = 0;
    u8* compressedData = (u8*)malloc(compressedDataSize);

    for(u32 y = 0; y < height; y += 4){
        for(u32 x = 0; x < width; x += 4){
            u8 dataBlock[32] = {};
            u8 compressedBlock[16] = {};
            u32 dbCtr = 0;
            for(u32 j = y; j < y + 4; j++){
                for(u32 i = x; i < x + 4; i++){
                    u32 r = (j * width * 4) + (i * 4);
                    u32 g = r + 1;
                    dataBlock[dbCtr++] = uncompressedData[r];
                    dataBlock[dbCtr++] = uncompressedData[g];
                }
            }
            compressDataBlock(dataBlock, compressedBlock);
            for(u32 i = 0; i < 16; i++){
                compressedData[compressedDataCtr++] = compressedBlock[i];
            }
        }
    }

    file = fopen(outFileName, "wb");
    fwrite(&width, 4, 1, file);
    fwrite(&height, 4, 1, file);
    fwrite(compressedData, 1, compressedDataSize, file);
    fclose(file); 

    return 0;
}