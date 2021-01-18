#pragma once

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

#define PNG_HEADER 727905341920923785

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

struct PNGHuffman {
    u32 totalCodes;
    u32 minBitLength;
    u32 maxBitLength;
    u32* codes;
    u32* values;
    u32* lengths;
};

u32 readBitsFromArray(u8* arr, u64 offset, u8 numBits){
    u32 res = 0;

    u64 byteOffset = offset / 8;
    u8 bitOffset = offset % 8;

    for(u32 i = 0; i < numBits; i++){
        u8 cByte = arr[byteOffset];
        u32 r = (cByte & (1 << bitOffset)) >> bitOffset;
        res |= r << i;
        bitOffset++;
        if(bitOffset % 8 == 0){
            bitOffset = 0;
            byteOffset++;
        }
    }

    return res;
}

u32 readBitsFromArrayReversed(u8* arr, u64 offset, u8 numBits){
    u32 res = 0;

    u64 byteOffset = offset / 8;
    u8 bitOffset = offset % 8;

    for(u32 i = 0; i < numBits; i++){
        u8 cByte = arr[byteOffset];
        u32 r = (cByte & (1 << bitOffset)) >> bitOffset;
        res |= r << (numBits - i - 1);
        bitOffset++;
        if(bitOffset % 8 == 0){
            bitOffset = 0;
            byteOffset++;
        }
    }

    return res;
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
    u32* b1Count = ALLOCATE_MEMORY(u32, maxBits + 1);
    u32* nextCode = ALLOCATE_MEMORY(u32, maxBits + 1);
    setMemory(b1Count, 0, (maxBits + 1) * sizeof(u32));
    setMemory(nextCode, 0, (maxBits + 1) * sizeof(u32));
    for(u32 i = 0; i < totalCodes; i++){
        b1Count[lengths[i]]++;
    }

    u32 code = 0;
    b1Count[0] = 0;
    for(u32 i = 1; i <= maxBits; i++){
        code = (code + b1Count[i - 1]) << 1;
        nextCode[i] = code;
    }  

    u32* codes = ALLOCATE_MEMORY(u32, totalCodes);
    u32* values = ALLOCATE_MEMORY(u32, totalCodes);
    u32* lens = ALLOCATE_MEMORY(u32, totalCodes);
    
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

u32 parseHuffmanCodeFromData(u8* data, u64* offset, PNGHuffman* pngh){
    u32 lastIndex = 0;
    for(u32 i = pngh->minBitLength; i <= pngh->maxBitLength; i++){
        u32 hufCode = readBitsFromArrayReversed(data, *offset, i);
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

u8* getPixelDataFromPNGImage(u8* fileData, u32* width, u32* height, u32* bitsPerPixel, bool addAlpha = false){
    u64 pngHeader = *(u64*)fileData;
    fileData += 8;
    if(pngHeader != PNG_HEADER){
        printf("%llu\n", pngHeader);
        return 0;
    }

    u8* compressedData = 0;
    u64 totalCompressedDataSize = 0;

    u64 unfilteredDataSize = 0;
    u64 totalImageSize = 0;

    u32 bytesPerPixel;

    bool hasAlpha = true;

    bool continueThrougFile = true;
    while(continueThrougFile){
        u32 chunkLength = SWAP32(*(u32*)fileData);
        fileData += 4;
        u32 chunkType = *(u32*)fileData;
        fileData += 4;
        if(STR_TO_INT("RDHI") == chunkType){
            *width = SWAP32(*(u32*)fileData);
            fileData += 4;
            *height = SWAP32(*(u32*)fileData);
            fileData += 4;
            u8 bitDepth = *fileData++;
            u8 colorType = *fileData++;
            u8 compressionMethod = *fileData++;
            u8 filterMethod = *fileData++;
            u8 interlaceMethod = *fileData++;

            switch(colorType){
                case 0:
                case 3:{
                    *bitsPerPixel = bitDepth;
                    break;
                }
                case 2:{
                    hasAlpha = false;
                    *bitsPerPixel = bitDepth * 3;
                    break;
                }
                case 4:{
                    *bitsPerPixel = bitDepth * 2;
                    break;
                }
                case 6:{
                    *bitsPerPixel = bitDepth * 4;
                    break;
                }
            }
            bytesPerPixel = ((*bitsPerPixel / 8) > 0 ? (*bitsPerPixel / 8) : 1);
            totalImageSize = *width * *height * bytesPerPixel;
            unfilteredDataSize = totalImageSize + *height;
        }else if(STR_TO_INT("TADI") == chunkType){
            totalCompressedDataSize += chunkLength; 
            compressedData = (u8*)realloc(compressedData, totalCompressedDataSize);
            for(u32 i = totalCompressedDataSize - chunkLength; i < totalCompressedDataSize; i++){
                compressedData[i] = *fileData++;    
            }
        }else if(STR_TO_INT("DNEI") == chunkType){ 
            continueThrougFile = false;
        }else{
            fileData += chunkLength;
        }
        u32 crc = SWAP32(*(u32*)fileData);
        fileData += 4;
    }

    u8 cmf = *compressedData++;
    u8 flg = *compressedData++;
    u8 cm = readBitsFromArray(&cmf, 0, 4);
    u8 cinfo = readBitsFromArray(&cmf, 4, 4);
    u8 fcheck = readBitsFromArray(&flg, 0, 5);
    u8 fdict = readBitsFromArray(&flg, 5, 1);
    u8 flevel = readBitsFromArray(&flg, 6, 2);
    if(fdict) { compressedData += 4; }

    u8* unfilteredData = ALLOCATE_MEMORY(u8, unfilteredDataSize);
    u8* unfilteredDataPtr = unfilteredData;
    u64 offset = 0;

    u8 bfinal = readBitsFromArray(compressedData, offset++, 1);
    u8 btype = readBitsFromArray(compressedData, offset, 2);
    offset += 2; 

    bool notEOF = true;
    while(notEOF) {
        printf("bfinal: %u\tbtype: %u\n", bfinal, btype);
        u32 lenExtraBits[29] = { 
            0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 
        };
        u32 lenAddAmt[29] = { 
            3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
        };
        u32 distExtraBits[30] = {
            0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 
        };
        u32 distAddAmt[30] = {
            1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
        };
        switch(btype){
            case 0:{
                u8 overbits = offset % 8;
                if(overbits > 0){
                    offset += 8 - overbits;
                }
    
                u8* compDatPtr = compressedData + (offset / 8);
                u16 len = SWAP16(*(u16*)compDatPtr);
                compDatPtr += 2;
                u16 nlen = SWAP16(*(u16*)compDatPtr);
                compDatPtr += 2;

                for(u32 i = 0; i < len; i++){
                    *unfilteredDataPtr++ = *compDatPtr++;
                }
                offset += len * 8;

                if(!bfinal){
                    bfinal = readBitsFromArray(compressedData, offset++, 1);
                    btype = readBitsFromArray(compressedData, offset, 2);
                    offset += 2; 
                }else{
                    notEOF = false;
                }
                break;
            }
            case 1:{
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
                    code = parseHuffmanCodeFromData(compressedData, &offset, &litLenHuff);
                    if(code < 256){
                        *unfilteredDataPtr++ = (u8)code;
                    }else if(code > 256){
                        code -= 257;

                        u32 length = lenAddAmt[code] + readBitsFromArray(compressedData, offset, lenExtraBits[code]);
                        offset += lenExtraBits[code];
                        u32 distCode = readBitsFromArray(compressedData, offset, 5);
                        offset += 5;
                        u32 distance = distAddAmt[distCode] + readBitsFromArray(compressedData, offset, distExtraBits[distCode]);
                        offset += distExtraBits[distCode];

                        u8* tempDataPtr = unfilteredDataPtr - distance;
                        for(u32 i = 0; i < length; i++){
                            *unfilteredDataPtr++ = *tempDataPtr++;
                        }
                    }else{
                        if(!bfinal){
                            bfinal = readBitsFromArray(compressedData, offset++, 1);
                            btype = readBitsFromArray(compressedData, offset, 2);
                            offset += 2; 
                        }else{
                            notEOF = false;
                        }
                    }
                }

                break;
            }
            case 2:{
                u32 hlit = readBitsFromArray(compressedData, offset, 5) + 257;
                offset += 5;
                u32 hdist = readBitsFromArray(compressedData, offset, 5) + 1;
                offset += 5;
                u32 hclen = readBitsFromArray(compressedData, offset, 4) + 4;
                offset += 4;

                u8 codeLengthAlphabet[19] = { 
                    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 
                };

                u32 litLenCodeLengths[19] = {};

                for(u32 i = 0; i < hclen; i++){
                    litLenCodeLengths[codeLengthAlphabet[i]] = readBitsFromArray(compressedData, offset, 3);
                    offset += 3;
                }

                PNGHuffman litLenHuff = generatePNGHuffmanFromCodeLengths(19, litLenCodeLengths, 7);

                u32 lenDistLengths[318] = {};
                u32 totalLenDistLenghtsFound = 0;
                u32 totalLenDistLenghts = hlit + hdist; 

                while(totalLenDistLenghtsFound < totalLenDistLenghts){
                    u32 code = parseHuffmanCodeFromData(compressedData, &offset, &litLenHuff); 
                    
                    if(code < 16){
                        lenDistLengths[totalLenDistLenghtsFound++] = code;
                    }else if(code == 16){
                        u32 copyLen = readBitsFromArray(compressedData, offset, 2) + 3;
                        offset += 2;
                        for(u32 i = 0; i < copyLen; i++){
                            lenDistLengths[totalLenDistLenghtsFound] = lenDistLengths[totalLenDistLenghtsFound - 1];
                            totalLenDistLenghtsFound++;
                        }
                    }else if(code == 17){
                        u32 copyLen = readBitsFromArray(compressedData, offset, 3) + 3;
                        offset += 3;
                        for(u32 i = 0; i < copyLen; i++){
                            lenDistLengths[totalLenDistLenghtsFound++] = 0;
                        }
                    }else if(code == 18){
                        u32 copyLen = readBitsFromArray(compressedData, offset, 7) + 11;
                        offset += 7;
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
                    code = parseHuffmanCodeFromData(compressedData, &offset, &literalHuff);
                    
                    if(code < 256){
                        *unfilteredDataPtr++ = (u8)code;
                    }else if(code > 256){
                        code -= 257;

                        u32 length = lenAddAmt[code] + readBitsFromArray(compressedData, offset, lenExtraBits[code]);
                        offset += lenExtraBits[code];

                        u32 distCode = parseHuffmanCodeFromData(compressedData, &offset, &distHuff);
                        u32 distance = distAddAmt[distCode] + readBitsFromArray(compressedData, offset, distExtraBits[distCode]);
                        offset += distExtraBits[distCode];

                        u8* tempDataPtr = unfilteredDataPtr - distance;
                        for(u32 i = 0; i < length; i++){
                            *unfilteredDataPtr++ = *tempDataPtr++;
                        }
                    }else{
                        if(!bfinal){
                            bfinal = readBitsFromArray(compressedData, offset++, 1);
                            btype = readBitsFromArray(compressedData, offset, 2);
                            offset += 2; 
                        }else{
                            notEOF = false;
                        }
                    }
                }
                
                clearPNGHuffman(&literalHuff);
                clearPNGHuffman(&distHuff);
                break;
            }
            default:{
                return 0;
            }
        }
    }

    //filter data here
    unfilteredDataPtr = unfilteredData;
    u8* filteredData = ALLOCATE_MEMORY(u8, totalImageSize);
    u8* filteredDataPtr = filteredData;
    u32 filteredRowSize = *width * bytesPerPixel;
    u32 rowCtr = 0;
    while(rowCtr < *height){
        u8 filterType = *unfilteredDataPtr++;
        switch(filterType){
            case 0:{
                for(u32 i = 0; i < filteredRowSize; i++){
                    *filteredDataPtr++ = *unfilteredDataPtr++;
                }
                break;
            }
            case 1:{
                for(u32 i = 0; i < filteredRowSize; i++){
                    if(i > bytesPerPixel){
                        *filteredDataPtr = *unfilteredDataPtr++ + *(filteredDataPtr - bytesPerPixel);
                        filteredDataPtr++;
                    }else{
                        *filteredDataPtr++ = *unfilteredDataPtr++;
                    }
                }
                break;
            }
            case 2:{
                for(u32 i = 0; i < filteredRowSize; i++){
                    if(rowCtr > 0){
                        *filteredDataPtr = *unfilteredDataPtr++ + *(filteredDataPtr - filteredRowSize);
                        filteredDataPtr++;
                    }else{
                        *filteredDataPtr++ = *unfilteredDataPtr++;
                    }
                }
                break;
            }
            case 3:{
                for(u32 i = 0; i < filteredRowSize; i++){
                    u8 up = 0;
                    u8 left = 0;
                    if(i > bytesPerPixel){
                        left = *(filteredDataPtr - bytesPerPixel);
                    }
                    if(rowCtr > 0){
                        up = *(filteredDataPtr - filteredRowSize);
                    }
                    u32 avg = ((u32)up + (u32)left) / 2;
                    *filteredDataPtr++ = *unfilteredDataPtr++ + (u8)avg;
                }
                break;
            }
            case 4:{
                for(u32 i = 0; i < filteredRowSize; i++){
                    u8 a = 0;
                    u8 b = 0;
                    u8 c = 0;
                    u8 addr = 0;
                    if(i > bytesPerPixel){
                        a = *(filteredDataPtr - bytesPerPixel);
                    }
                    if(rowCtr > 0){
                        b = *(filteredDataPtr - filteredRowSize);
                    }
                    if(i > bytesPerPixel && rowCtr > 0){
                        c = *(filteredDataPtr - filteredRowSize - bytesPerPixel);
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
                    *filteredDataPtr++ = *unfilteredDataPtr++ + addr;
                }
                break;
            }
            default: return 0;
        }
        
        rowCtr++;
    }

    if(unfilteredData){
        free(unfilteredData);
    }

    if(!hasAlpha && addAlpha){
        u8* dataWithAlpha = ALLOCATE_MEMORY(u8, totalImageSize + (*width * *height));
        u32 ctr = 0 ;
        for(int i = 0; i < totalImageSize; i += 3){
            dataWithAlpha[ctr++] = filteredData[i + 0];
            dataWithAlpha[ctr++] = filteredData[i + 1];
            dataWithAlpha[ctr++] = filteredData[i + 2];
            dataWithAlpha[ctr++] = 255;
        }

        free(filteredData);
        filteredData = dataWithAlpha;
    }

    return filteredData;
}

void freeImageData(u8** imageData){
    if(*imageData){
        delete[] *imageData;
        *imageData = 0;
    }
}