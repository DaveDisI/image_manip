#include <stdlib.h>
#include <stdio.h>
#include <math.h>

typedef char s8;
typedef int s32;
typedef unsigned char u8;
typedef unsigned int u32;
typedef float f32;

static bool compareStrings(u8* s1, u8* s2){
    while(*s1 != '\0'){
        if(*s1 != *s2){
            return false;
        }
        s1++;
        s2++;
    }
    return *s2 == '\0';
}

static void copyMemory(void* src, void* dest, u32 amt){
    u8* s = (u8*)src;
    u8* d = (u8*)dest;
    while(amt){
        *d++ = *s++;
        amt--;
    }
}

static u32 getLineFromFile(u8* fileData, u8* buffer){
    u8* c = fileData;
    u8* b = buffer;
    u32 ctr = 0;
    while(*c != '\n'){
        *b++ = *c++;
        ctr++;
    }
    *b = '\0';
    return ++ctr;
}

static void rgbeToF32RGB(u8* rgbe, f32 *red, f32 *green, f32 *blue){
    if (rgbe[3] > 0) { 
        f32 f = ldexp(1.0, (s32)rgbe[3] - 136);
        *red = rgbe[0] * f;
        *green = rgbe[1] * f;
        *blue = rgbe[2] * f;
    } else {
        *red = 0;
        *green = 0;
        *blue = 0;
    }
}

static void parseDimensions(u8* line, u32* width, u32* height){
    u8* c = line;
    u8 hstr[32] = {};
    u8 wstr[32] = {};
    u32 hlen = 0;
    u32 wlen = 0;
    while(*c != ' '){
        c++;
    }
    c++;
    while(*c != ' '){
        hstr[hlen++] = *c++;
    }
    c++;
    while(*c != ' '){
        c++;
    }
    c++;
    while(*c != '\0'){
        wstr[wlen++] = *c++;
    }

    *height = 0;
    for(u32 i = 0; i < hlen; i++){
        u32 n = hstr[hlen - i - 1] - '0';
        for(u32 j = 0; j < i; j++){
            n *= 10;
        }
        *height = *height + n;
    }
    *width = 0;
    for(u32 i = 0; i < wlen; i++){
        u32 n = wstr[wlen - i - 1] - '0';
        for(u32 j = 0; j < i; j++){
            n *= 10;
        }
        *width = *width + n;
    }
}

static void decompressRLE(u8* compressedData, u8* uncompressedData, u32 width, u32 height){
    u8* cdPtr = compressedData;
    u8* ucPtr = uncompressedData;
    for(u32 i = 0; i < height; i++){
        cdPtr += 4;
        for(u32 j = 0; j < 4; j++){
            u32 wctr = 0;
            while(wctr < width){
                u8 c = *cdPtr++;
                u8 v  = *cdPtr++;
                if(c > 128){
                    c -= 128;
                    for(u32 k = 0; k < c; k++){
                        *ucPtr++ = v;
                        wctr++;
                    }
                }else{
                    *ucPtr++ = v;
                    wctr++;
                    c--;
                    for(u32 k = 0; k < c; k++){
                        *ucPtr++ = *cdPtr++;
                        wctr++;
                    }
                }
            }
        }
    }

    u8* orderedRow = (u8*)malloc(width * 4);
    for(u32 y = 0; y < height; y++){
        u8* rp = uncompressedData + y * width * 4;
        for(u32 x = 0; x < width; x++){
            u32 idx = x * 4;
            orderedRow[idx + 0] = *(rp + x + width * 0);
            orderedRow[idx + 1] = *(rp + x + width * 1);
            orderedRow[idx + 2] = *(rp + x + width * 2);
            orderedRow[idx + 3] = *(rp + x + width * 3);
        }   
        for(u32 i = 0; i < width * 4; i++){ 
            rp[i] = orderedRow[i];
        }
    }

    free(orderedRow);
}

static f32* extractHDR(const s8* filename, u32* width, u32* height){
    FILE* file = fopen(filename, "rb");
    fseek(file, 0L, SEEK_END);
    u32 fileSize = ftell(file);
    rewind(file);
    u8* fileContents = (u8*)malloc(fileSize);
    fread(fileContents, sizeof(u8), fileSize, file);
    fclose(file);

    u8 line[128];
    u8* fptr = fileContents;
    u32 lineLen = getLineFromFile(fptr, line);
    if(!compareStrings(line, (u8*)"#?RADIANCE")){
        printf("ERROR! Does not appear to be a proper .HDR file.");
        return 0;
    }
    fptr += lineLen;
    while(!compareStrings(line, (u8*)"")){
        lineLen = getLineFromFile(fptr, line);
        fptr += lineLen;
    }
    lineLen = getLineFromFile(fptr, line);
    fptr += lineLen;
    parseDimensions(line, width, height);
    
    u32 dif = fptr - fileContents;
    u32 dataSize = fileSize - dif;
    u8* imageData = fptr;

    u32 rgbeDataSize = *width * *height * 4;
    u8* rgbeData = (u8*)malloc(rgbeDataSize);

    if(imageData[0] == 2 && imageData[1] == 2){
        u32 scanlineSize = (((u32)imageData[2]) << 8) | imageData[3];
        if(scanlineSize == *width){
            decompressRLE(imageData, rgbeData, *width, *height);
        }
    }else{
        copyMemory(imageData, rgbeData, dataSize);
    }

    // for(u32 i = 0; i < rgbeDataSize; i++){
    //     printf("%i\n", rgbeData[i]);
    // }

    u32 uncompressedDataSize = *width * *height * 3 * sizeof(f32);
    f32* uncompressedData = (f32*)malloc(uncompressedDataSize);

    for(u32 y = 0; y < *height; y++){
        for(u32 x = 0; x < *width; x++){
            u32 rgbeIdx = y * *width * 4 + x * 4;
            u32 fIdx = y * *width * 3 + x * 3;
            f32 r;
            f32 g;
            f32 b;
            rgbeToF32RGB(&rgbeData[rgbeIdx], &r, &g, &b);
            uncompressedData[fIdx + 0] = r;
            uncompressedData[fIdx + 1] = g;
            uncompressedData[fIdx + 2] = b;
        }
    }
    free(rgbeData);

    return uncompressedData;
}

int main(int argc, char** argv){
    u32 w, h;
    f32* dats = extractHDR("C:/Users/Dave/Desktop/posX.hdr", &w, &h);
    u32 size = w * h * 3;

    FILE* file = fopen("C:/Users/Dave/Desktop/sky.skybox_hdr", "wb");
    fwrite(&w, sizeof(u32), 1, file);
    fwrite(&h, sizeof(u32), 1, file);
    fwrite(dats, sizeof(f32), size, file);
    free(dats);
    dats = extractHDR("C:/Users/Dave/Desktop/negX.hdr", &w, &h);
    fwrite(dats, sizeof(f32), size, file);
    free(dats);
    dats = extractHDR("C:/Users/Dave/Desktop/posY.hdr", &w, &h);
    fwrite(dats, sizeof(f32), size, file);
    free(dats);
    dats = extractHDR("C:/Users/Dave/Desktop/negY.hdr", &w, &h);
    fwrite(dats, sizeof(f32), size, file);
    free(dats);
    dats = extractHDR("C:/Users/Dave/Desktop/posZ.hdr", &w, &h);
    fwrite(dats, sizeof(f32), size, file);
    free(dats);
    dats = extractHDR("C:/Users/Dave/Desktop/negZ.hdr", &w, &h);
    fwrite(dats, sizeof(f32), size, file);
    free(dats);
    fclose(file);

    return 0;
}