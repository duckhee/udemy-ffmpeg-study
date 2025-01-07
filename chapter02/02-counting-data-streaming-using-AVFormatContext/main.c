#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <stdbool.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <stb_image.h>
#include <stb_image_write.h>

#if defined(WIN32) || defined(WIN64)

#include <Windows.h>

#endif

#define BUFFER_MAX                          1024

bool GetResourcePath(const char *name, char *const pathBuffer);

int main(int argc, char **argv) {
    char resourcePath[BUFFER_MAX] = {0};
    /** FFMPEG에서 데이터를 가져올 때 담아주는 AV Format Context이다. */
    AVFormatContext *pAvFormatContext = NULL;
    int ffmpegErrorCode = 0;

    if (!GetResourcePath("out.mp4", resourcePath)) {
        printf("Failed Get out.mp4 resource path...\r\n");
        return -1;
    }
    /** FFMPEG 로 해당 동영상 열기 -> 열기 성공 시 0 반환 */
    ffmpegErrorCode = avformat_open_input(&pAvFormatContext, resourcePath, NULL, NULL);
    if (ffmpegErrorCode != 0) {
        printf("Failed get resource using ffmpeg : %d\r\n", ffmpegErrorCode);
        /** FFMPEG 로그 함수를 이용한 실패 로그 출력 */
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] open resource out.mp4\r\n", ffmpegErrorCode);
        return -1;
    }


    /** 사용을 한 후에는 해당 객체를 메모리에서 해제를 해줘야 메모리 누수가 발생하지 않는다. */
    avformat_close_input(&pAvFormatContext);

    return 0;
}


bool GetResourcePath(const char *name, char *const pathBuffer) {
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