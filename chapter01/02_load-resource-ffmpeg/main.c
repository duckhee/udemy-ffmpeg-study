#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>

// FFMPEG Lib
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <inttypes.h>
#include <stdbool.h>

#if defined(WIN32) || defined(WIN64)
#include <Windows.h>
#endif

#define BUFFER_MAX                      1024

bool GetFilePath(const char *name, char *pathBuffer);


int main(int argc, char **argv) {
    char filePath[BUFFER_MAX] = {0,};
    FILE *pFile = NULL;

    if (!GetFilePath("murage.mp4", filePath)) {
        printf("Failed File Path ...\r\n");
        return -1;
    }

    pFile = fopen(filePath, "rb");
    if (pFile == NULL) {
        printf("Failed File Open ... %s\r\n", filePath);
        return -1;
    }

    /** FFMPEG Error Code */
    int errorValue = 0;

    /** Format Context */
    AVFormatContext *formatContext = NULL;

    /** get AV File Format -> 가져온 정보를 가지고 AVFormatContext를 만들어준다. */
    errorValue = avformat_open_input(&formatContext, filePath, NULL, NULL);
    if (errorValue < 0) {
        printf("Get Failed to open file\r\n");
        av_log(NULL, AV_LOG_ERROR, "Error Opening File");
        return -1;
    }

    printf("File Open Success\r\n");


    fclose(pFile);

    return 0;
}


bool GetFilePath(const char *name, char *pathBuffer) {
    char tempBuffer[BUFFER_MAX] = {0,};
    char executeBuffer[BUFFER_MAX] = {0,};
    char *pRemoveStart = NULL;
    int removeEndIdx = 0;

#if defined(WIN32) || defined(WIN64)
    GetCurrentDirectory(BUFFER_MAX, executeBuffer);
    pRemoveStart = strstr(executeBuffer, "\\cmake");
#else
    realpath(".", executeBuffer);
    pRemoveStart = strstr(executeBuffer, "/cmake");
#endif

    if (pRemoveStart == NULL) {
        printf("Failed Get Path...\r\n");
        return false;
    }

    removeEndIdx = (int) (pRemoveStart - executeBuffer);

    memcpy(tempBuffer, executeBuffer, sizeof(char) * removeEndIdx);

#if defined(WIN32) || defined(WIN64)
    strcat(tempBuffer, "\\resources\\");
#else
    strcat(tempBuffer, "/resources/");
#endif

    strcat(tempBuffer, name);
    strcpy(pathBuffer, tempBuffer);
    return true;
}