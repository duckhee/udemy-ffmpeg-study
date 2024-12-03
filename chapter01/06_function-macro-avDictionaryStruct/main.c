#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <memory.h>
#include <string.h>

/** av format header */
#include <libavformat/avformat.h>

/** av codec */
#include <libavcodec/codec.h>

/** this is FFMPEG util header */
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>

/** av scalar header */
#include <libswscale/swscale.h>

/** Custom FFMPEG Error MACRO */
#define FFMPEG_CHECK_ERROR(errorNo, errorMsg)                                               \
({                                                                                          \
    if((errorNo) < 0){                                                                      \
        av_log(NULL, AV_LOG_ERROR, (errorMsg));                                             \
        return -1;                                                                          \
    }else{                                                                                  \
     }                                               \
})

#if defined(WIN32) || defined(WIN64)
#include <Windows.h>
#endif

#define BUFFER_MAX                      1024

bool GetResourcePath(const char *name, char *const pathBuffer);

void PrintFormatDuration(int64_t duration);

void PrintFFMPEGMetaData(AVDictionary *pMetaData);

int main(void) {
    char pathBuffer[BUFFER_MAX] = {0};
    AVFormatContext *pContent;
    int ffmpegErrorCode = 0;

    if (!GetResourcePath("murage.mp4", pathBuffer)) {
        printf("Get Resource Path... murage.mp4\r\n");
        return -1;
    }

    ffmpegErrorCode = avformat_open_input(&pContent, pathBuffer, NULL, NULL);
    if (ffmpegErrorCode < 0) {
        printf("Get FFMPEG Get Resource... %s\r\n", pathBuffer);
        FFMPEG_CHECK_ERROR(ffmpegErrorCode, "Error FFMPEG Open Resource File...\r\n");
    }

    PrintFFMPEGMetaData(pContent->metadata);

    ffmpegErrorCode = avformat_find_stream_info(pContent, NULL);
    if (ffmpegErrorCode < 0) {
        FFMPEG_CHECK_ERROR(ffmpegErrorCode, "Error find stream information ...\r\n");
    }

    PrintFormatDuration(pContent->duration);

    avformat_close_input(&pContent);
    return 0;
}

bool GetResourcePath(const char *name, char *const pathBuffer) {
    char executeBuffer[BUFFER_MAX] = {0};
    char *pRemoveStart;
    int removeEndIdx = 0;

#if defined(WIN32) || defined(WIN64)
    GetCurrentDirectory(BUFFER_MAX, executeBuffer);
    pRemoveStart = strstr(executeBuffer, "\\cmake");
#else
    realpath(".", executeBuffer);
    pRemoveStart = strstr(executeBuffer, "/cmake");
#endif

    if (pRemoveStart == NULL) {
        printf("Failed Get Resource Path...\r\n");
        return false;
    }

    removeEndIdx = (int) (pRemoveStart - executeBuffer);

    memcpy(pathBuffer, executeBuffer, sizeof(char) * removeEndIdx);

#if defined(WIN32) || defined(WIN64)
    strcat(pathBuffer, "\\resources\\");
#else
    strcat(pathBuffer, "/resources/");
#endif
    strcat(pathBuffer, name);
    return true;
}

void PrintFFMPEGMetaData(AVDictionary *pMetaData) {
    /** meta data를 tuple 형태로 값을 가지고 오기 위한 구조체 */
    AVDictionaryEntry *pEntry = NULL;
    while ((pEntry = av_dict_get(pMetaData, "", pEntry, AV_DICT_IGNORE_SUFFIX))) {
        printf("key : %s, value: %s\r\n", pEntry->key, pEntry->value);
    }
}

void PrintFormatDuration(int64_t duration) {
    int64_t hours = 0;
    int64_t minutes = 0;
    int64_t seconds = 0;

    /** get video duration time -> make seconds */
    seconds = duration / AV_TIME_BASE;

    minutes = seconds / 60;
    seconds %= 60;
    hours = minutes / 60;
    minutes %= 60;

    printf("duration %lld:%lld:%lld\r\n", hours, minutes, seconds);
}
