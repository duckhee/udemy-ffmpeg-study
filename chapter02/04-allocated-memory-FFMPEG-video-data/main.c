#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <stdbool.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <stb_image.h>
#include <stb_image_write.h>

#if defined(WIN32) || defined(WIN64)

#include <Windows.h>

#endif

#define BUFFER_MAX                          1024

#define FFMPEG_ERROR(errorCode, msg)    \
{                                       \
if((errorCode) < (0)){                  \
av_log(NULL, AV_LOG_ERROR, (msg));      \
return -1;                              \
}else{                                  \
}                                       \
}

bool GetResourcePath(const char *name, char *const pathBuffer);


int main(int argc, char **argv) {
    char resourcePath[BUFFER_MAX] = {0};

    AVFormatContext *pAvFormatContext = NULL;
    AVPacket *pAvPacket = NULL;
    AVFrame *pAvFrame = NULL;
    AVCodecParameters *pVideoCodecParameters = NULL;
    AVCodec *pVideoCode = NULL;
    AVCodecContext *pVideoCodecContext = NULL;
    int errorCode = -1;
    int streamChannelIdx = 0;

    if (!GetResourcePath("out.mp4", resourcePath)) {
        printf("Failed Get out.mp4 resource Path...\r\n");
        return -1;
    }

    errorCode = avformat_open_input(&pAvFormatContext, resourcePath, NULL, NULL);
    if (errorCode != 0) {
        printf("Load FFMPEG Open...\r\n");
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] open resource out.mp4\r\n", errorCode);
        return -1;
    }

    /** Get FFMPEG Stream Data Get */
    errorCode = avformat_find_stream_info(pAvFormatContext, NULL);
    if (errorCode != 0) {
        printf("Failed find [%s] Stream Information ...\r\n", resourcePath);
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] find stream [%s]\r\n", errorCode, resourcePath);

        avformat_close_input(&pAvFormatContext);
        return -1;
    }

    /** Allocated packet data in memory */
    /** av_packet_alloc() 함수는 기본 필드 값을 가져와서 메모리에 올려주기 때문에 추후에 packet 의 필드에 접근 시 기본 값을 확인을 할 수 있다. */
    pAvPacket = av_packet_alloc();
    if (pAvPacket == NULL) {
        printf("Failed Load packet data...\r\n");
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] allocating packet [%s]\r\n", errorCode, resourcePath);
        avformat_close_input(&pAvFormatContext);
        return -1;
    }

    /** allocated frame data in memory */
    pAvFrame = av_frame_alloc();
    if (pAvFrame == NULL) {
        printf("Failed Load frame data...\r\n");
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] allocating frame [%s]\r\n", errorCode, resourcePath);
        avformat_close_input(&pAvFormatContext);
        return -1;
    }

    /** stream 갯수를 출력 */
    printf("number of stream : %d\r\n", pAvFormatContext->nb_streams);


    avformat_close_input(&pAvFormatContext);
    av_packet_free(&pAvPacket);
//    av_frame_free(&pAvFrame);
    av_free(pAvFrame);
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

    removeEndIdx = (pRemoveStart - executeBuffer);

    memcpy(pathBuffer, executeBuffer, sizeof(char) * removeEndIdx);


#if defined(WIN32) || defined(WIN64)
    strcat(pathBuffer, "\\resources\\");
#else
    strcat(pathBuffer, "/resources/");
#endif
    strcat(pathBuffer, name);
    return true;
}
