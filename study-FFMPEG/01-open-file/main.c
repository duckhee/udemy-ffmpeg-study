#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
// FFMPEG Library
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>

#if defined(WIN32) || defined(WIN64)

#include <Windows.h>

#endif

#define BUFFER_MAX                  1024
/** 절대 경로 해석용 버퍼 크기 (Linux PATH_MAX=4096 대응) */
#define RESOURCE_PATH_MAX           4096

#define FFMPEG_ERROR(errorCode, msg) \
{                                    \
if((errorCode) < (0)) {              \
av_log(NULL, AV_LOG_ERROR, (msg));   \
return -1;                           \
}else{                               \
}                                     \
}                                    \


bool GetResourcePath(const char *name, char *const pathBuffer);

/**
 * study-FFMPEG 01 — 파일 열기와 메타데이터
 *
 * FFmpeg 학습의 출발점.
 * 컨테이너(mp4)를 열어 AVFormatContext를 얻고,
 * 전체 재생 시간/비트레이트/메타데이터를 확인한 뒤 올바르게 닫는다.
 */
int main(int argc, char **argv) {
    char resourcePath[BUFFER_MAX] = {0};
    int errorCode = -1;

    AVFormatContext *pFormatContext = NULL;
    const AVDictionaryEntry *pMetaEntry = NULL;

    if (!GetResourcePath("murage.mp4", resourcePath)) {
        printf("Failed Get murage.mp4 resource path...\r\n");
        return -1;
    }
    printf("resource path - %s\r\n", resourcePath);

    /**
     * 컨테이너 열기.
     * avformat_open_input은 파일 헤더만 읽어 AVFormatContext를 할당한다.
     * 성공 시 0, 실패 시 음수 에러 코드를 반환한다.
     */
    errorCode = avformat_open_input(&pFormatContext, resourcePath, NULL, NULL);
    FFMPEG_ERROR(errorCode, "[FFMPEG ERROR] FFMPEG Open Failed...\r\n")

    /**
     * 스트림 정보 채우기.
     * 일부 포맷은 헤더만으로 스트림 정보를 알 수 없어
     * 실제 패킷 몇 개를 읽어 codec/duration 등을 채운다.
     */
    errorCode = avformat_find_stream_info(pFormatContext, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) find Stream Failed...\r\n", errorCode);
        avformat_close_input(&pFormatContext);
        return -1;
    }

    /** 컨테이너 전역 정보 출력 */
    printf("==== Container Information ====\r\n");
    printf("format name    : %s (%s)\r\n", pFormatContext->iformat->name, pFormatContext->iformat->long_name);
    printf("stream count   : %u\r\n", pFormatContext->nb_streams);
    /**
     * duration은 AV_TIME_BASE(1,000,000) 단위의 마이크로초 값이다.
     * 초 단위로 보려면 AV_TIME_BASE로 나눈다.
     */
    printf("duration       : %.2f sec\r\n", (double) pFormatContext->duration / AV_TIME_BASE);
    printf("bit rate       : %lld bps\r\n", pFormatContext->bit_rate);

    /**
     * 메타데이터(AVDictionary) 전체 순회.
     * AV_DICT_IGNORE_SUFFIX 플래그 + 빈 키("")를 주면
     * 이전 엔트리에서 이어서 모든 엔트리를 차례로 돌 수 있다.
     */
    printf("==== Metadata ====\r\n");
    while ((pMetaEntry = av_dict_get(pFormatContext->metadata, "", pMetaEntry, AV_DICT_IGNORE_SUFFIX)) != NULL) {
        printf("%-20s : %s\r\n", pMetaEntry->key, pMetaEntry->value);
    }

    /**
     * ffmpeg -i 와 동일한 형태의 요약 덤프.
     * 마지막 인자 0 = 입력 파일 기준.
     */
    printf("==== av_dump_format ====\r\n");
    av_dump_format(pFormatContext, 0, resourcePath, 0);

    /**
     * 열었던 컨테이너 닫기.
     * avformat_open_input으로 연 것은 반드시 avformat_close_input으로 닫는다.
     * (내부에서 AVFormatContext까지 해제하고 포인터를 NULL로 만든다)
     */
    avformat_close_input(&pFormatContext);
    printf("Open/Close Done!\r\n");
    return 0;
}

bool GetResourcePath(const char *name, char *const pathBuffer) {
    /** 절대 경로는 BUFFER_MAX보다 길 수 있으므로 넉넉한 버퍼를 쓴다 (POSIX PATH_MAX 대응) */
    char executeBuffer[RESOURCE_PATH_MAX] = {0};
    char *pRemoveStart = NULL;
    int removeEndIdx = 0;
    int writtenLength = 0;

#if defined(WIN32) || defined(WIN64)
    if (GetCurrentDirectory(RESOURCE_PATH_MAX, executeBuffer) == 0) {
        printf("Failed Get Current Directory...\r\n");
        return false;
    }
    pRemoveStart = strstr(executeBuffer, "\\cmake");
#else
    if (realpath(".", executeBuffer) == NULL) {
        printf("Failed Resolve Current Directory...\r\n");
        return false;
    }
    pRemoveStart = strstr(executeBuffer, "/cmake");
#endif

    if (pRemoveStart == NULL) {
        printf("Failed Get Resource Path...\r\n");
        return false;
    }

    removeEndIdx = (int) (pRemoveStart - executeBuffer);

    /** snprintf로 길이를 검사하며 조립한다 (strcat 오버플로 방지) */
#if defined(WIN32) || defined(WIN64)
    writtenLength = snprintf(pathBuffer, BUFFER_MAX, "%.*s\\resources\\%s", removeEndIdx, executeBuffer, name);
#else
    writtenLength = snprintf(pathBuffer, BUFFER_MAX, "%.*s/resources/%s", removeEndIdx, executeBuffer, name);
#endif
    if (writtenLength < 0 || writtenLength >= BUFFER_MAX) {
        printf("Failed Get Resource Path... (path too long)\r\n");
        return false;
    }
    return true;
}
