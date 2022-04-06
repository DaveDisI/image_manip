#include "png_extractor.h"

s32 main(u32 argc, s8** argv){
    u8* data[6] = {0, 0, 0, 0, 0, 0};
    u32 width;
    u32 height;
    convertPNG("C:/Users/Dave/Desktop/skybox_e.png", &width, &height, &data[0]);
    convertPNG("C:/Users/Dave/Desktop/skybox_w.png", &width, &height, &data[1]);
    convertPNG("C:/Users/Dave/Desktop/skybox_u.png", &width, &height, &data[2]);
    convertPNG("C:/Users/Dave/Desktop/skybox_d.png", &width, &height, &data[3]);
    convertPNG("C:/Users/Dave/Desktop/skybox_n.png", &width, &height, &data[4]);
    convertPNG("C:/Users/Dave/Desktop/skybox_s.png", &width, &height, &data[5]);

    u32 dataSize = width * height * 4 * 6; 
    u8* cubemapData = (u8*)malloc(dataSize);

    u32 sz = width * height * 4;
    u32 dataCtr = 0;
    for(u32 i = 0; i < 6; i++){
        for(u32 j = 0; j < sz; j++){
            cubemapData[dataCtr++] = data[i][j];
        }
    }


    FILE* fileHandle = fopen("skybox.cubemap", "wb");
    fwrite(&width, sizeof(u32), 1, fileHandle);
    fwrite(&height, sizeof(u32), 1, fileHandle);
    fwrite(cubemapData, sizeof(u8), dataSize, fileHandle);
    fclose(fileHandle);

    return 0;
}