#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <stdbool.h>

#if defined(WIN32) || defined(WIN64)
#include <Windows.h>
#endif

#define BUFFER_MAX                                      1024

#include <inttypes.h>

#include <libavcodec/codec.h>
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

bool GetResourcePath(const char *name, char *const pathBuffer);

void FormatDuration(int64_t duration);

int main(int argc, char **argv) {
    char videoPath[BUFFER_MAX] = "../../resources/murage.mp4";
    FILE *pFile;
    AVFormatContext *pAvContext;
    AVDictionaryEntry *dictionaryEntry = NULL;
    int ffmpegErrorCode = 0;

    if (!GetResourcePath("murage.mp4", videoPath)) {
        printf("Failed Get Resource Path...\r\n");
        return -1;
    }

    if ((pFile = fopen(videoPath, "rb")) == NULL) {
        printf("Failed Open Video File....\r\n");
        return -1;
    }

    ffmpegErrorCode = avformat_open_input(&pAvContext, videoPath, NULL, NULL);
    if (ffmpegErrorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed Get Video Context\r\n");
        return -1;
    }


    /** get video meta data */
    while ((dictionaryEntry = av_dict_get(pAvContext->metadata, "", dictionaryEntry, AV_DICT_IGNORE_SUFFIX))) {
        printf("%s : %s\r\n", dictionaryEntry->key, dictionaryEntry->value);
    }

    /** duration 과 bit rate 에 대한 정보를 가져오기 위해서는 스트림에 대한 정보를 가져와야 확인을 할 수 있다. */
    ffmpegErrorCode = avformat_find_stream_info(pAvContext, NULL);
    if (ffmpegErrorCode < 0) {
        printf("Failed Load Stream Information\r\n");
        return -1;
    }

    int64_t bitRate = pAvContext->bit_rate;
    int64_t duration = pAvContext->duration;


    printf("bit rate : %"PRId64"\r\n", bitRate);
    printf("duration : %"PRId64"\r\n", duration);

    FormatDuration(duration);

    avformat_close_input(&pAvContext);
    avformat_free_context(pAvContext);
    fclose(pFile);
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
        printf("Failed Load Path...\r\n");
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


void FormatDuration(int64_t duration) {
    int64_t hours = 0;
    int64_t minutes = 0;
    int64_t seconds = 0;

    seconds = duration / AV_TIME_BASE;
    minutes = seconds / 60;
    seconds %= 60;
    hours = minutes / 60;
    minutes %= 60;

    printf("duration %lld:%lld:%lld\r\n", hours, minutes, seconds);
}
