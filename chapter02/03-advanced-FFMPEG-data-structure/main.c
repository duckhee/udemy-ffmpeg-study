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
    char imagePath[BUFFER_MAX] = {0,};
    AVFormatContext *pAvFormatContext = NULL;
    /** 원본 데ㅐ이터에 대해서 가지고 있는 구조체이다. 원본 데이터는 바이트 단위로 정의가 되어 있다. */
    AVPacket *pAvPacket = NULL;
    /** 원본 오디오 및 비디오 데이터를 풀어 놓은 데이터를 담는 구조체이다. */
    AVFrame *pAvFrame = NULL;
    /** 인코딩된 스트림에 대한 정보를 담고 있는 구조체이다. */
    AVCodecParameters *pVideoCodecParameters = NULL;
    /** codec에 대한 정보를 담고 있는 구조체이다. */
    AVCodec *pVideoCodec = NULL;
    AVCodecContext *pVideoCodecContext = NULL;

    int videoStreamIdx = -1;

    int FFMPEG_ErrorCode = 0;

    if (!GetResourcePath("out.mp4", imagePath)) {
        printf("Failed Get Resource path...\r\n");
        return -1;
    }

    FFMPEG_ErrorCode = avformat_open_input(&pAvFormatContext, imagePath, NULL, NULL);
    if (FFMPEG_ErrorCode != 0) {
        /** FFMPEG 로그 함수를 이용한 실패 로그 출력 */
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] open resource out.mp4\r\n", FFMPEG_ErrorCode);
    }


    avformat_close_input(&pAvFormatContext);

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
