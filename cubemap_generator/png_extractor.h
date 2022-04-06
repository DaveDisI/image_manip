#pragma once

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
#define IHDR 'RDHI'
#define IEND 'DNEI'
#define PLTE 'ETLP'
#define IDAT 'TADI'

#define SWAP16(x) ((x <<  8) | (x >>  8))
#define SWAP32(x) ((x << 24) | (x >> 24) | ((x << 8) & 0x00ff0000) | ((x >> 8) & 0x0000ff00))
#define SWAP64(x) ((x << 56) | (x >> 56) | \
                  ((x << 40) & 0x00ff000000000000) | ((x >> 40) & 0x000000000000ff00) | \
                  ((x << 16) & 0x0000ff0000000000) | ((x >> 16) & 0x0000000000ff0000) | \
                  ((x <<  8) & 0x000000ff00000000) | ((x >>  8) & 0x00000000ff000000))

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

void clearPNGHuffman(PNGHuffman* pngh){
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

PNGHuffman generatePNGHuffmanFromCodeLengths(u32 totalCodes, u32* lengths, u32 maxBits){
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

u32 parseHuffmanCodeFromData(u8* data, u32* offset, PNGHuffman* pngh){
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

void convertPNG(s8* fileName, u32* imgWidht, u32* imgHeight, u8** data){
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
    u32 width = 0;
    u32 height = 0;
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
                width = SWAP32(*(u32*)filePtr);
                filePtr += sizeof(u32);
                height = SWAP32(*(u32*)filePtr);
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

    uncompressedDataSize = width * height * bytesPerPixel;
    unfilteredDataSize = uncompressedDataSize + height;
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
    u32 uncompressedRowSize = width * bytesPerPixel;
    u32 rc = 0;
    while(rc < height){
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
        u32 dataWidthAlphaSize = width * height * 4;
        u8* ucDataWithAlpha = (u8*)malloc(dataWidthAlphaSize);
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
        uncompressedDataSize = dataWidthAlphaSize;
    }
    

    free(fileData);
    free(compressedData);

    *data = uncompressedData;
    *imgWidht = width;
    *imgHeight = height;
}
