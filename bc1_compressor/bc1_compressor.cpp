#include <stdio.h>
#include <stdlib.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef float f32;

u64 absoluteValue(u64 v){
    return v > 0 ? v : -v;
}

u8 convertColor16To2Bits(u16 color, u16 color_0, u16 color_1, u16 color_2, u16 color_3){
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

u16 convertRGB24to16(u8* rgb){
    u8 r = (u8)(((float)rgb[0] / 255.0f) * 31.0f);
    u8 g = (u8)(((float)rgb[1] / 255.0f) * 63.0f);
    u8 b = (u8)(((float)rgb[2] / 255.0f) * 31.0f);
    u16 v = (r << 11) | (g << 5) | b;
    return v;
}

u64 compressPixelPatch(u8* pixelPatch){
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

void compressPixelData(int width, int height, u8* data, u8* buffer){
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
            *(u64*)buffer = compressPixelPatch(uncompressedPixelPatch);
            buffer += sizeof(u64);
        }
    }
}

int main(int argc, char** argv){
    if(argc < 3){
        printf("bc1_compressor [input file] [output file]\n");
        return 1;
    }
    u32 width = 1024;
    u32 height = 1024;
    FILE* file = fopen(argv[1], "rb");
    if(file == 0){
        printf("could not open file %s\n", argv[1]);
        exit(1);
    }
    fread(&width, sizeof(u32), 1, file);
    fread(&height, sizeof(u32), 1, file);
    u32 dataSize = width * height * 4;
    u8* data = (u8*)malloc(dataSize);
    fread(data, sizeof(u8), dataSize, file);
    fclose(file);

    u32 compressedDataSize = dataSize / 8;
    u8* compressedData = (u8*)malloc(compressedDataSize);
    compressPixelData(width, height, data, compressedData);

    file = fopen(argv[2], "wb");
    if(file == 0){
        printf("could not open file %s\n", argv[1]);
        exit(2);
    }
    fwrite(&width, sizeof(u32), 1, file);
    fwrite(&height, sizeof(u32), 1, file);
    fwrite(compressedData, sizeof(u8), compressedDataSize, file);
    fclose(file);

    free(data);
    free(compressedData);
    return 0;
}