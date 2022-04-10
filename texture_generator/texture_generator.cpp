#include <stdlib.h>
#include <stdio.h>

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

#define PNG_SIGNATURE 727905341920923785
#define IHDR 1380206665 //'RDHI'
#define IEND 1145980233 //'DNEI'
#define PLTE 1163152464 //'ETLP'
#define IDAT 1413563465 //'TADI'

#define SWAP16(x) ((x <<  8) | (x >>  8))
#define SWAP32(x) ((x << 24) | (x >> 24) | ((x << 8) & 0x00ff0000) | ((x >> 8) & 0x0000ff00))
#define SWAP64(x) ((x << 56) | (x >> 56) | \
                  ((x << 40) & 0x00ff000000000000) | ((x >> 40) & 0x000000000000ff00) | \
                  ((x << 16) & 0x0000ff0000000000) | ((x >> 16) & 0x0000000000ff0000) | \
                  ((x <<  8) & 0x000000ff00000000) | ((x >>  8) & 0x00000000ff000000))

#define NO_COMPRESSION 0
#define BC1_COMPRESSION 1
#define BC4_COMPRESSION 2
#define BC5_COMPRESSION 3

struct PNGHuffman {
    u32 totalCodes;
    u32 minBitLength;
    u32 maxBitLength;
    u32* codes;
    u32* values;
    u32* lengths;
};

static const u32 LENGTH_EXTRA_BITS[29] = { 
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 
};
static const u32 LENGTH_ADD_AMOUNT[29] = { 
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const u32 DISTANCE_EXTRA_BITS[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 
};
static const u32 DISTANCE_ADD_AMOUNT[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};

static void copyMemory(void* dest, void* src, u64 amt){
    u8* d = (u8*)dest;
    u8* s = (u8*)src;
    while(amt){
        *d = *s;
        d++;
        s++;
        amt--;
    }
}

static void setMemory(void* mem, s8 value, u32 size){
    u8* memPtr = (u8*)mem;
    while(size){
        *memPtr = value;
        memPtr++;
        size--;
    }
}

static bool compareStrings(const s8* s1, const s8* s2){
    const s8* c1 = s1;
    const s8* c2 = s2;
    u32 ctr = 0;
    while(*c1 != '\0'){
        if(*c1 != *c2){
            return false;
        }
        c1++;
        c2++;
    }
    return *c1 == *c2;
}

static u8 convertColor16To2Bits(u16 color, u16 color_0, u16 color_1, u16 color_2, u16 color_3){
    u8 retOrder[] = {1, 3, 2, 0};
    f32 dif = color_0 - color_1;
    if(dif > 0){
        f32 ratio = (color - color_1) / dif;
        f32 ret = ratio * 3;
        u32 retI = ret;
        f32 dec = ret - retI;
        return dec > 0.5 ? retOrder[retI + 1] : retOrder[retI];
    }else{
        return 0;
    }
}

static u16 convertRGB24to16(u8* rgb){
    u8 r = (u8)(((float)rgb[0] / 255.0f) * 31.0f);
    u8 g = (u8)(((float)rgb[1] / 255.0f) * 63.0f);
    u8 b = (u8)(((float)rgb[2] / 255.0f) * 31.0f);
    u16 v = (r << 11) | (g << 5) | b;
    return v;
}

static u32 readBitsFromArray(u8* array, u32 numBits, u32* offset, bool increaseOffset = true){
    u32 result = 0;
    u32 byteIndex = *offset / 8;
    u8 bitIndex = *offset % 8;

    for(int i = 0; i < numBits; i++){
        u8 bit = (array[byteIndex] >> bitIndex) & 1;
        result |= bit << i;
        bitIndex++;
        if(bitIndex % 8 == 0){
            bitIndex = 0;
            byteIndex++;
        }
    }

    if(increaseOffset) *offset += numBits;
    return result;
}

static u32 readBitsFromArrayReversed(u8* array, u32 numBits, u32* offset, bool increaseOffset = true){
    u32 result = 0;
    u32 byteIndex = *offset / 8;
    u8 bitIndex = *offset % 8;

    for(int i = 0; i < numBits; i++){
        u8 bit = (array[byteIndex] >> bitIndex) & 1;
        result |= bit << (numBits - i - 1);
        bitIndex++;
        if(bitIndex % 8 == 0){
            bitIndex = 0;
            byteIndex++;
        }
    }

    if(increaseOffset) *offset += numBits;
    return result;
}

static void printU32AsString(u32 s){
    s8 str[] = {
        (s8)(s & 0x000000ff), (s8)((s >> 8) & 0x000000ff), (s8)((s >> 16) & 0x000000ff), (s8)(s >> 24), '\0' 
    };
    printf("%s\n", str);
}

static u64 compressPixelPatchBC1(u8* pixelPatch){
    u16 colors16[16];
    int ctr = 0;
    for(int i = 0; i < 48; i += 3){
        u8* c = pixelPatch + i;
        colors16[ctr++] = convertRGB24to16(c);
    }

    u16 color_0 = 0;
    u16 color_1 = -1;
    for(int i = 0; i < 16; i++){
        if(colors16[i] < color_1){
            color_1 = colors16[i];
        }
        if(colors16[i] > color_0){
            color_0 = colors16[i];
        }
    }

    u16 color_2 = (u16)((2.0f / 3.0f) * (float)color_0 + (1.0f / 3.0f) * (float)color_1);
    u16 color_3 = (u16)((1.0f / 3.0f) * (float)color_0 + (2.0f / 3.0f) * (float)color_1);

    u64 result = 0;
    u8* rptr = (u8*)&result;
    *(u16*)rptr = color_0;
    rptr += sizeof(u16);
    *(u16*)rptr = color_1;
    rptr += sizeof(u16);
    for(int i = 0; i < 16; i += 4){
        u8 c0 = convertColor16To2Bits(colors16[i + 0], color_0, color_1, color_2, color_3);
        u8 c1 = convertColor16To2Bits(colors16[i + 1], color_0, color_1, color_2, color_3);
        u8 c2 = convertColor16To2Bits(colors16[i + 2], color_0, color_1, color_2, color_3);
        u8 c3 = convertColor16To2Bits(colors16[i + 3], color_0, color_1, color_2, color_3);
        u8 row = (c3 << 6) | (c2 << 4) | (c1 << 2) | c0;
        
        *rptr = row;
        rptr++;
    }

    return result;
}

static void compressPixelDataBC1(int width, int height, u8* data, u8* buffer){
    for(int y = 0; y < height; y += 4){
        for(int x = 0; x < width; x += 4){
            u8 uncompressedPixelPatch[48];
            int ctr = 0;
            for(int j = y; j < y + 4; j++){
                for(int i = x; i < x + 4; i++){
                    u32 r = (j * width * 4) + (i * 4);
                    u32 g = r + 1;
                    u32 b = r + 2;
                    uncompressedPixelPatch[ctr++] = data[r];
                    uncompressedPixelPatch[ctr++] = data[g];
                    uncompressedPixelPatch[ctr++] = data[b];
                }
            }
            *(u64*)buffer = compressPixelPatchBC1(uncompressedPixelPatch);
            buffer += sizeof(u64);
        }
    }
}

static void compressDataBlockBC5(u8* dataBlock, u8* buffer){
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

static void compressPixelDataBC5(u32 width, u32 height, u8* data, u8* buffer){
    u32 compressedDataCtr = 0;
    for(u32 y = 0; y < height; y += 4){
        for(u32 x = 0; x < width; x += 4){
            u8 dataBlock[32] = {};
            u8 compressedBlock[16] = {};
            u32 dbCtr = 0;
            for(u32 j = y; j < y + 4; j++){
                for(u32 i = x; i < x + 4; i++){
                    u32 r = (j * width * 4) + (i * 4);
                    u32 g = r + 1;
                    dataBlock[dbCtr++] = data[r];
                    dataBlock[dbCtr++] = data[g];
                }
            }
            compressDataBlockBC5(dataBlock, compressedBlock);
            for(u32 i = 0; i < 16; i++){
                buffer[compressedDataCtr++] = compressedBlock[i];
            }
        }
    }
}

static void compressDataBlockBC4(u8* dataBlock, u8* buffer){
    u8 retOrder[] = {1, 7, 6, 5, 4, 3, 2, 0};
    u8 minRed = 255;
    u8 maxRed = 0;

    for(u32 i = 0; i < 16; i += 2){
        if(dataBlock[i] < minRed) minRed = dataBlock[i];
        if(dataBlock[i] > maxRed) maxRed = dataBlock[i];
    }

    u64* rBuf64 = (u64*)&buffer[0];
    *rBuf64 = maxRed;
    *rBuf64 |= ((u32)minRed << 8);
     
    u32 shiftCtr = 8;
    u8 redRange = maxRed - minRed;
    for(u32 i = 0; i < 16; i += 2){
        u8 r = dataBlock[i];
        f32 redRat = (f32)(r - minRed) / (f32)redRange;
        f32 redRet = redRat * 7.0f;
        u32 redRetI = redRet;
        f32 redDec = redRet - redRetI;
        u64 redBits = redDec > 0.5 ? retOrder[redRetI + 1] : retOrder[redRetI];
        *rBuf64 |= (redBits << shiftCtr);
        shiftCtr += 3;
    }
}

static void compressBC4(u32 width, u32 height, u8* data, u8* buffer){
    u32 compressedDataCtr = 0;

    for(u32 y = 0; y < height; y += 4){
        for(u32 x = 0; x < width; x += 4){
            u8 dataBlock[16] = {};
            u8 compressedBlock[8] = {};
            u32 dbCtr = 0;
            for(u32 j = y; j < y + 4; j++){
                for(u32 i = x; i < x + 4; i++){
                    u32 r = (j * width * 4) + (i * 4);
                    dataBlock[dbCtr++] = data[r];
                }
            }
            compressDataBlockBC4(dataBlock, compressedBlock);
            for(u32 i = 0; i < 8; i++){
                buffer[compressedDataCtr++] = compressedBlock[i];
            }
        }
    }
}

static void clearPNGHuffman(PNGHuffman* pngh){
    if(pngh->codes){ 
        free(pngh->codes);
        pngh->codes = 0;
    }
    if(pngh->values){ 
        free(pngh->values);
        pngh->values = 0;
    }
    if(pngh->lengths){ 
        free(pngh->lengths);
        pngh->lengths = 0;
    }
}

static PNGHuffman generatePNGHuffmanFromCodeLengths(u32 totalCodes, u32* lengths, u32 maxBits){
    u32 mbp1 = maxBits + 1;
    u32* b1Count = (u32*)malloc(mbp1 * sizeof(u32));
    u32* nextCode = (u32*)malloc(mbp1 * sizeof(u32));
    setMemory(b1Count, 0, mbp1 * sizeof(u32));
    setMemory(nextCode, 0, mbp1 * sizeof(u32));
    for(u32 i = 0; i < totalCodes; i++){
        b1Count[lengths[i]]++;
    }

    u32 code = 0;
    b1Count[0] = 0;
    for(u32 i = 1; i < mbp1; i++){
        code = (code + b1Count[i - 1]) << 1;
        nextCode[i] = code;
    }  

    u32* codes = (u32*)malloc(totalCodes * sizeof(u32));
    u32* values = (u32*)malloc(totalCodes * sizeof(u32));
    u32* lens = (u32*)malloc(totalCodes * sizeof(u32));
    
    u32 minBitLength = -1;
    u32 maxBitLength = 0;
    u32 totalCodesUsed = 0;
    setMemory(codes, 0, totalCodes * sizeof(u32));
    setMemory(values, 0, totalCodes * sizeof(u32));
    setMemory(lens, 0, totalCodes * sizeof(u32));

    for(u32 i = 0; i < totalCodes; i++){
        u32 len = lengths[i];
        if(len != 0){
            if(len < minBitLength) minBitLength = len;
            if(len > maxBitLength) maxBitLength = len;
            codes[totalCodesUsed] = nextCode[len];
            values[totalCodesUsed] = i;
            lens[totalCodesUsed] = len;
            totalCodesUsed++;
            nextCode[len]++;
        }
    } 

    free(b1Count);
    free(nextCode);

    for(u32 i = 0; i < totalCodesUsed - 1; i++){
        for(u32 j =  i + 1; j < totalCodesUsed; j++){
            if(codes[i] > codes[j]){
                u32 t = codes[i];
                codes[i] = codes[j];
                codes[j] = t;
                t = values[i];
                values[i] = values[j];
                values[j] = t;
                t = lens[i];
                lens[i] = lens[j];
                lens[j] = t;
            }
        }
    }

    PNGHuffman pngh = {};
    pngh.totalCodes = totalCodesUsed;
    pngh.codes = codes;
    pngh.values = values;
    pngh.lengths = lens;
    pngh.minBitLength = minBitLength;
    pngh.maxBitLength = maxBitLength;
    return pngh;
}

static u32 parseHuffmanCodeFromData(u8* data, u32* offset, PNGHuffman* pngh){
    u32 lastIndex = 0;
    for(u32 i = pngh->minBitLength; i <= pngh->maxBitLength; i++){
        u32 hufCode = readBitsFromArrayReversed(data, i, offset, false);
        for(u32 j = lastIndex; j < pngh->totalCodes; j++){
            if(hufCode == pngh->codes[j] && pngh->lengths[j] == i){
                *offset += i;
                return pngh->values[j];
            }else if(pngh->codes[j] > hufCode){
                lastIndex = j;
                break;
            }
        }
    }
    return -1;
}

static u8* uncompressPNG(const s8* fileName, u32* width, u32* height){
    FILE* fileHandle = fopen(fileName, "rb");
    fseek(fileHandle, 0L, SEEK_END);
    u32 fileSize = ftell(fileHandle);
    rewind(fileHandle);
    u8* fileData = (u8*)malloc(fileSize);
    fread(fileData, 1, fileSize, fileHandle);
    fclose(fileHandle);
    u8* filePtr = fileData;
    u64 pngSignature = *(u64*)filePtr;
    if(pngSignature != PNG_SIGNATURE){
        printf("Invalid Signature. Check to make sure this is a PNG image.");
        exit(1);
    }
    filePtr += sizeof(u64);

    u32 type = 0;
    u32 uncompressedDataSize = 0;
    u32 compressedDataSize = 0;
    u32 unfilteredDataSize = 0;
    u32 bitsPerPixel = 0;
    u32 bytesPerPixel = 0;
    u8 bitDepth = 0;
    u8 colorType = 0;
    u8 compressionMethod = 0;
    u8 filterMethod = 0;
    u8 interlaceMethod = 0;
    u8* uncompressedData;
    u8* unfilteredData;
    u8* compressedData = (u8*)malloc(0);

    while(type != IEND){
        u32 length = SWAP32(*(u32*)filePtr);
        filePtr += sizeof(u32);
        type = *(u32*)filePtr;
        filePtr += sizeof(u32);
        // printU32AsString(type);
        switch(type){
            case IHDR:{
                *width = SWAP32(*(u32*)filePtr);
                filePtr += sizeof(u32);
                *height = SWAP32(*(u32*)filePtr);
                filePtr += sizeof(u32);
                bitDepth = *filePtr++;
                colorType = *filePtr++;
                compressionMethod = *filePtr++;
                filterMethod = *filePtr++;
                interlaceMethod = *filePtr++;

                switch(colorType){
                    case 0:
                    case 3:{
                        bitsPerPixel = bitDepth;
                        break;
                    }
                    case 2:{
                        bitsPerPixel = bitDepth * 3;
                        break;
                    }
                    case 4:{
                        bitsPerPixel = bitDepth * 2;
                        break;
                    }
                    case 6:{
                        bitsPerPixel = bitDepth * 4;
                        break;
                    }
                }
                bytesPerPixel = ((bitsPerPixel / 8) > 0 ? (bitsPerPixel / 8) : 1);
                break;
            }
            case IDAT:{
                compressedData = (u8*)realloc(compressedData, compressedDataSize + length);
                copyMemory(compressedData + compressedDataSize, filePtr, length);
                compressedDataSize += length;
                filePtr += length;
                break;
            }
            case IEND:{
                break;
            }
            default:{
                filePtr += length;
                break;
            }
        }
        filePtr += sizeof(u32);
    }

    uncompressedDataSize = *width * *height * bytesPerPixel;
    unfilteredDataSize = uncompressedDataSize + *height;
    unfilteredData = (u8*)malloc(unfilteredDataSize);
    uncompressedData = (u8*)malloc(uncompressedDataSize);
    u8* unfilteredDataPtr = unfilteredData;
    u8* uncompressedDataPtr = uncompressedData;

    u32 bitOffset = 0;
    u8* cdPtr = compressedData;
    u8 comMethFlag = *cdPtr++;
    u8 flgCheckBts = *cdPtr++;
    u32 bfinal = readBitsFromArray(cdPtr, 1, &bitOffset);
    u32 btype = readBitsFromArray(cdPtr, 2, &bitOffset);

    bool eof = false;
    while(!eof){
        switch(btype){
            case 0b00:{
                printf("processing uncompressed block\n");
                u8 overbits = bitOffset % 8;
                if(overbits > 0){
                    bitOffset += 8 - overbits;
                }
    
                u8* compDatPtr = compressedData + (bitOffset / 8);
                u16 len = SWAP16(*(u16*)compDatPtr);
                compDatPtr += 2;
                u16 nlen = SWAP16(*(u16*)compDatPtr);
                compDatPtr += 2;

                for(u32 i = 0; i < len; i++){
                    *unfilteredDataPtr++ = *compDatPtr++;
                }
                bitOffset += len * 8;

                if(!bfinal){
                    bfinal = readBitsFromArray(compressedData, 1, &bitOffset);
                    btype = readBitsFromArray(compressedData, 2, &bitOffset);
                }else{
                    eof = true;
                }
                break;
            }
            case 0b01:{
                printf("decompressing fixed huffman codes\n");
                u32 litCodeLengths[288] = {};
                for(u32 i = 0; i < 144; i++){
                    litCodeLengths[i] = 8;
                }
                for(u32 i = 144; i < 256; i++){
                    litCodeLengths[i] = 9;
                }
                for(u32 i = 256; i < 280; i++){
                    litCodeLengths[i] = 7;
                }
                for(u32 i = 280; i < 288; i++){
                    litCodeLengths[i] = 8;
                }
                PNGHuffman litLenHuff = generatePNGHuffmanFromCodeLengths(288, litCodeLengths, 9);

                u32 code = -1;
                while(code != 256){
                    code = parseHuffmanCodeFromData(cdPtr, &bitOffset, &litLenHuff);
                    if(code < 256){
                        *unfilteredDataPtr++ = (u8)code;
                    }else if(code > 256){
                        code -= 257;

                        u32 length = LENGTH_ADD_AMOUNT[code] + readBitsFromArray(cdPtr, LENGTH_EXTRA_BITS[code], &bitOffset);
                        u32 distCode = readBitsFromArray(cdPtr, 5, &bitOffset);
                        u32 distance = DISTANCE_ADD_AMOUNT[distCode] + readBitsFromArray(cdPtr, DISTANCE_EXTRA_BITS[distCode], &bitOffset);

                        u8* tempDataPtr = unfilteredDataPtr - distance;
                        for(u32 i = 0; i < length; i++){
                            *unfilteredDataPtr++ = *tempDataPtr++;
                        }
                    }else{
                        if(!bfinal){
                            bfinal = readBitsFromArray(cdPtr, 1, &bitOffset);
                            btype = readBitsFromArray(cdPtr, 2, &bitOffset);
                        }else{
                            eof = true;
                        }
                    }
                }

                break;
                break;
            }
            case 0b10:{
                u32 hlit = readBitsFromArray(cdPtr, 5, &bitOffset) + 257;
                u32 hdist = readBitsFromArray(cdPtr, 5, &bitOffset) + 1;
                u32 hclen = readBitsFromArray(cdPtr, 4, &bitOffset) + 4;
                u32 codeLengthAlphabet[] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
                u32 codeLengths[19] = {};

                for(int i = 0; i < hclen; i++){
                    codeLengths[codeLengthAlphabet[i]] = readBitsFromArray(cdPtr, 3, &bitOffset);
                }

                PNGHuffman litLenHuff = generatePNGHuffmanFromCodeLengths(19, codeLengths, 7);

                u32 lenDistLengths[318] = {};
                u32 totalLenDistLenghtsFound = 0;
                u32 totalLenDistLenghts = hlit + hdist; 

                while(totalLenDistLenghtsFound < totalLenDistLenghts){
                    u32 code = parseHuffmanCodeFromData(cdPtr, &bitOffset, &litLenHuff); 
                    
                    if(code < 16){
                        lenDistLengths[totalLenDistLenghtsFound++] = code;
                    }else if(code == 16){
                        u32 copyLen = readBitsFromArray(cdPtr, 2, &bitOffset) + 3;
                        for(u32 i = 0; i < copyLen; i++){
                            lenDistLengths[totalLenDistLenghtsFound] = lenDistLengths[totalLenDistLenghtsFound - 1];
                            totalLenDistLenghtsFound++;
                        }
                    }else if(code == 17){
                        u32 copyLen = readBitsFromArray(cdPtr, 3, &bitOffset) + 3;
                        for(u32 i = 0; i < copyLen; i++){
                            lenDistLengths[totalLenDistLenghtsFound++] = 0;
                        }
                    }else if(code == 18){
                        u32 copyLen = readBitsFromArray(cdPtr, 7, &bitOffset) + 11;
                        for(u32 i = 0; i < copyLen; i++){
                            lenDistLengths[totalLenDistLenghtsFound++] = 0;
                        }
                    }
                }

                clearPNGHuffman(&litLenHuff);
                PNGHuffman literalHuff = generatePNGHuffmanFromCodeLengths(hlit, lenDistLengths, 15);
                PNGHuffman distHuff = generatePNGHuffmanFromCodeLengths(hdist, lenDistLengths + hlit, 15);

                u32 code = -1;
                while(code != 256){
                    code = parseHuffmanCodeFromData(cdPtr, &bitOffset, &literalHuff);
                    
                    if(code < 256){
                        *unfilteredDataPtr++ = (u8)code;
                    }else if(code > 256){
                        code -= 257;

                        u32 length = LENGTH_ADD_AMOUNT[code] + readBitsFromArray(cdPtr, LENGTH_EXTRA_BITS[code], &bitOffset);

                        u32 distCode = parseHuffmanCodeFromData(cdPtr, &bitOffset, &distHuff);
                        u32 distance = DISTANCE_ADD_AMOUNT[distCode] + readBitsFromArray(cdPtr, DISTANCE_EXTRA_BITS[distCode], &bitOffset);

                        u8* tempDataPtr = unfilteredDataPtr - distance;
                        for(u32 i = 0; i < length; i++){
                            *unfilteredDataPtr++ = *tempDataPtr++;
                        }
                    }else{
                        if(!bfinal){
                            bfinal = readBitsFromArray(cdPtr, 1, &bitOffset);
                            btype = readBitsFromArray(cdPtr, 2, &bitOffset);
                        }else{
                            eof = true;
                        }
                    }
                }
                
                clearPNGHuffman(&literalHuff);
                clearPNGHuffman(&distHuff);

                break;
            }
            default:{
                printf("An error has occured. Unsupported btype: %i\n.", btype);
                exit(3);
            }
        }
    }

    unfilteredDataPtr = unfilteredData;
    u32 uncompressedRowSize = *width * bytesPerPixel;
    u32 rc = 0;
    while(rc < *height){
        u8 filterType = *unfilteredDataPtr++;
        switch(filterType){
            case 0:{
                for(u32 i = 0; i < uncompressedRowSize; i++){
                    *uncompressedDataPtr++ = *unfilteredDataPtr++;
                }
                break;
            }
            case 1:{
                for(u32 i = 0; i < uncompressedRowSize; i++){
                    if(i >= bytesPerPixel){
                        *uncompressedDataPtr = *unfilteredDataPtr++ + *(uncompressedDataPtr - bytesPerPixel);
                        uncompressedDataPtr++;
                    }else{
                        *uncompressedDataPtr++ = *unfilteredDataPtr++;
                    }
                }
                break;
            }
            case 2:{
                for(u32 i = 0; i < uncompressedRowSize; i++){
                    if(rc > 0){
                        *uncompressedDataPtr = *unfilteredDataPtr++ + *(uncompressedDataPtr - uncompressedRowSize);
                        uncompressedDataPtr++;
                    }else{
                        *uncompressedDataPtr++ = *unfilteredDataPtr++;
                    }
                }
                break;
            }
            case 3:{
                for(u32 i = 0; i < uncompressedRowSize; i++){
                    u8 up = 0;
                    u8 left = 0;
                    if(i >= bytesPerPixel){
                        left = *(uncompressedDataPtr - bytesPerPixel);
                    }
                    if(rc > 0){
                        up = *(uncompressedDataPtr - uncompressedRowSize);
                    }
                    u32 avg = ((u32)up + (u32)left) / 2;
                    *uncompressedDataPtr++ = *unfilteredDataPtr++ + (u8)avg;
                }
                break;
            }
            case 4:{
                for(u32 i = 0; i < uncompressedRowSize; i++){
                    u8 a = 0;
                    u8 b = 0;
                    u8 c = 0;
                    u8 addr = 0;
                    if(i >= bytesPerPixel){
                        a = *(uncompressedDataPtr - bytesPerPixel);
                    }
                    if(rc > 0){
                        b = *(uncompressedDataPtr - uncompressedRowSize);
                    }
                    if(i >= bytesPerPixel && rc > 0){
                        c = *(uncompressedDataPtr - uncompressedRowSize - bytesPerPixel);
                    }
                    s32 p = a + b - c;
                    s32 pa = p - a;
                    s32 pb = p - b;
                    s32 pc = p - c;
                    pa = pa > 0 ? pa : -pa;
                    pb = pb > 0 ? pb : -pb;
                    pc = pc > 0 ? pc : -pc;
                    if(pa <= pb && pa <= pc){
                        addr = a;
                    }else if(pb <= pc){
                        addr = b;
                    }else{
                        addr = c;
                    }
                    *uncompressedDataPtr++ = *unfilteredDataPtr++ + addr;
                }
                break;
            }
        }
        rc++;
    }

    if(colorType == 2){
        u32 dataWithAlphaSize = *width * *height * 4;
        u8* ucDataWithAlpha = (u8*)malloc(dataWithAlphaSize);
        u8* ucdaPtr = ucDataWithAlpha;
        uncompressedDataPtr = uncompressedData;
        for(u32 i = 0; i < uncompressedDataSize; i += 3){
            *ucdaPtr++ = *uncompressedDataPtr++;
            *ucdaPtr++ = *uncompressedDataPtr++;
            *ucdaPtr++ = *uncompressedDataPtr++;
            *ucdaPtr++ = 255;
        }
        free(uncompressedData);
        uncompressedData = ucDataWithAlpha;
        uncompressedDataSize = dataWithAlphaSize;
    }
    
    free(fileData);
    free(compressedData);
    return uncompressedData;
}

s32 main(s32 argc, s8** argv){
    // texture_generator C:/Users/Dave/Desktop/art/test_normal.png C:/Users/Dave/Desktop/output.bc5 bc5 true
    if(argc < 4){
        printf("Arguments required:\n");
        printf("texture_generator <input file> <output file> <compression type [none, bc1, bc4, bc5]>\n");
        return 0;
    }

    s8* inputFile = argv[1];
    s8* outputFile = argv[2];
    s8* compressionType = argv[3];
    u32 compressionIndex = -1;

    if(compareStrings(compressionType, "none")){
        compressionIndex = NO_COMPRESSION;
    }else if(compareStrings(compressionType, "bc1")){
        compressionIndex = BC1_COMPRESSION;
    }else if(compareStrings(compressionType, "bc4")){
        compressionIndex = BC4_COMPRESSION;
    }else if(compareStrings(compressionType, "bc5")){
        compressionIndex = BC5_COMPRESSION;
    }else{
        printf("Invalid Compression Type. Use \'none\', \'bc1\', \'bc4\', or \'bc5\'.");
        return 0;
    }

    u32 width;
    u32 height;
    u8* uncompressedData = uncompressPNG(inputFile, &width, &height);
    u32 uncompressedDataSize = width * height * 4;

    u32 mipLevels = 1;
    u32 mmSize = 0;
    if(argc > 4 && compareStrings(argv[4], "true")){
        if(width != height){
            printf("Must be square texture for mipmaps.\n");
            return 0;
        }
        f32 w = width / 2;
        u32 mmd = w;
        
        while(w > 3){
            mmSize +=  w * w * 4;
            mipLevels++;
            w *= 0.5;
        }
    }

    if(mipLevels > 1){
        uncompressedData = (u8*)realloc(uncompressedData, uncompressedDataSize + mmSize);
        u8* rPtr = uncompressedData;
        u8* wPtr = uncompressedData + uncompressedDataSize;
        u32 wCtr = 0;

        u32 mmW = width;
        u32 mmH = height;
        for(u32 i = 0; i < mipLevels; i++){
            for(u32 y = 0; y < mmH; y += 2){
                for(u32 x = 0; x < mmW; x += 2){
                    u32 idx = y * mmW * 4 + x * 4;
                    u32 idx2 = (y + 1) * mmW * 4 + x * 4;
                    
                    u32 r1 = rPtr[idx + 0];
                    u32 g1 = rPtr[idx + 1];
                    u32 b1 = rPtr[idx + 2];
                    u32 a1 = rPtr[idx + 3];

                    u32 r2 = rPtr[idx + 4];
                    u32 g2 = rPtr[idx + 5];
                    u32 b2 = rPtr[idx + 6];
                    u32 a2 = rPtr[idx + 7];

                    u32 r3 = rPtr[idx2 + 0];
                    u32 g3 = rPtr[idx2 + 1];
                    u32 b3 = rPtr[idx2 + 2];
                    u32 a3 = rPtr[idx2 + 3];

                    u32 r4 = rPtr[idx2 + 4];
                    u32 g4 = rPtr[idx2 + 5];
                    u32 b4 = rPtr[idx2 + 6];
                    u32 a4 = rPtr[idx2 + 7];

                    u32 ra = r1 + r2 + r3 + r4;
                    u32 ga = g1 + g2 + g3 + g4;
                    u32 ba = b1 + b2 + b3 + b4;
                    u32 aa = a1 + a2 + a3 + a4;
                    ra /= 4;
                    ga /= 4;
                    ba /= 4;
                    aa /= 4;
                    wPtr[wCtr++] = ra;
                    wPtr[wCtr++] = ga;
                    wPtr[wCtr++] = ba;
                    wPtr[wCtr++] = aa;
                }
            }

            rPtr += mmW * mmH * 4;
            mmW /= 2;
            mmH /= 2;
        }
        
    }   

    switch(compressionIndex){
        case NO_COMPRESSION:{
            FILE* fileHandle = fopen(outputFile, "wb");
            fwrite(&width, sizeof(u32), 1, fileHandle);
            fwrite(&height, sizeof(u32), 1, fileHandle);
            if(mipLevels > 1){
                fwrite(&mipLevels, sizeof(u32), 1, fileHandle);
            }

            fwrite(uncompressedData, sizeof(u8), uncompressedDataSize + mmSize, fileHandle);
            fclose(fileHandle);
            break;
        }
        case BC1_COMPRESSION:{
            u32 compressedDataSize = uncompressedDataSize / 8;
            u8* compressedData = (u8*)malloc(compressedDataSize);
            compressPixelDataBC1(width, height, uncompressedData, compressedData);
            FILE* fileHandle = fopen(outputFile, "wb");
            fwrite(&width, sizeof(u32), 1, fileHandle);
            fwrite(&height, sizeof(u32), 1, fileHandle);
            if(mipLevels > 1){
                fwrite(&mipLevels, sizeof(u32), 1, fileHandle);
            }

            fwrite(compressedData, sizeof(u8), compressedDataSize, fileHandle);

            u8* rPtr = uncompressedData + uncompressedDataSize;
            u32 d = width / 2;
            for(u32 i = 1; i < mipLevels; i++){
                u32 sz = d * d * 4;
                compressedDataSize = sz / 8;
                compressPixelDataBC1(d, d, rPtr, compressedData);
                fwrite(compressedData, sizeof(u8), compressedDataSize, fileHandle);
                rPtr += sz;
                d /= 2;
            }

            fclose(fileHandle);
            free(compressedData);
            break;
        }
        case BC4_COMPRESSION:{
            u32 compressedDataSize = uncompressedDataSize / 4;
            u8* compressedData = (u8*)malloc(compressedDataSize);
            compressBC4(width, height, uncompressedData, compressedData);
            FILE* fileHandle = fopen(outputFile, "wb");
            fwrite(&width, sizeof(u32), 1, fileHandle);
            fwrite(&height, sizeof(u32), 1, fileHandle);
            fwrite(compressedData, sizeof(u8), compressedDataSize, fileHandle);
            fclose(fileHandle);
            free(compressedData);
            break;
        }
        case BC5_COMPRESSION:{
            u32 compressedDataSize = uncompressedDataSize / 4;
            u8* compressedData = (u8*)malloc(compressedDataSize);
            compressPixelDataBC5(width, height, uncompressedData, compressedData);
            FILE* fileHandle = fopen(outputFile, "wb");
            fwrite(&width, sizeof(u32), 1, fileHandle);
            fwrite(&height, sizeof(u32), 1, fileHandle);
            if(mipLevels > 1){
                fwrite(&mipLevels, sizeof(u32), 1, fileHandle);
            }

            fwrite(compressedData, sizeof(u8), compressedDataSize, fileHandle);

            u8* rPtr = uncompressedData + uncompressedDataSize;
            u32 d = width / 2;
            for(u32 i = 1; i < mipLevels; i++){
                u32 sz = d * d * 4;
                compressedDataSize = sz / 4;
                compressPixelDataBC5(d, d, rPtr, compressedData);
                fwrite(compressedData, sizeof(u8), compressedDataSize, fileHandle);
                rPtr += sz;
                d /= 2;
            }

            fclose(fileHandle);
            free(compressedData);
            break;
        }
    }
    

    
    free(uncompressedData);
    
    return 0;
}
