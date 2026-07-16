#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
// FFMPEG Library
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>

#if defined(WIN32) || defined(WIN64)

#include <Windows.h>

#endif

#define BUFFER_MAX                  1024
/** 절대 경로 해석용 버퍼 크기 (Linux PATH_MAX=4096 대응) */
#define RESOURCE_PATH_MAX           4096
/** 상세 정보를 출력할 패킷 개수 (전체를 출력하면 콘솔이 넘친다) */
#define PRINT_PACKET_MAX            20

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
 * study-FFMPEG 03 — 디먹싱(패킷 추출)
 *
 * av_read_frame으로 컨테이너에서 압축된 패킷(AVPacket)을 하나씩 꺼낸다.
 * 아직 디코딩은 하지 않고, pts/dts/size/키프레임 플래그를 관찰한다.
 */
int main(int argc, char **argv) {
    char resourcePath[BUFFER_MAX] = {0};
    int errorCode = -1;

    AVFormatContext *pFormatContext = NULL;
    AVPacket *pPacket = NULL;

    int printedCount = 0;
    int totalPacketCount = 0;
    /** 스트림별 패킷 개수 집계 (스트림이 8개를 넘는 파일은 이 예제에서 다루지 않는다) */
    int streamPacketCount[8] = {0};

    if (!GetResourcePath("murage.mp4", resourcePath)) {
        printf("Failed Get murage.mp4 resource path...\r\n");
        return -1;
    }
    printf("resource path - %s\r\n", resourcePath);

    errorCode = avformat_open_input(&pFormatContext, resourcePath, NULL, NULL);
    FFMPEG_ERROR(errorCode, "[FFMPEG ERROR] FFMPEG Open Failed...\r\n")

    errorCode = avformat_find_stream_info(pFormatContext, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) find Stream Failed...\r\n", errorCode);
        avformat_close_input(&pFormatContext);
        return -1;
    }

    /**
     * AVPacket 할당.
     * 패킷 구조체 자체는 한 번만 할당하고,
     * 매 루프에서 av_packet_unref로 내부 데이터 참조만 해제하며 재사용한다.
     */
    pPacket = av_packet_alloc();
    if (pPacket == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load packet structure...\r\n");
        avformat_close_input(&pFormatContext);
        return -1;
    }

    printf("==== Packet Dump (first %d packets) ====\r\n", PRINT_PACKET_MAX);
    printf("%-8s %-8s %-12s %-12s %-10s %-10s %s\r\n",
           "index", "stream", "pts", "dts", "size", "key", "pts(sec)");

    /**
     * 디먹싱 루프.
     * av_read_frame은 성공 시 0, 파일 끝(EOF)이나 에러 시 음수를 반환한다.
     * 반환된 패킷은 파일에 저장된 순서(=dts 순서)로 나온다.
     */
    while (av_read_frame(pFormatContext, pPacket) >= 0) {
        if (printedCount < PRINT_PACKET_MAX) {
            AVStream *pStream = pFormatContext->streams[pPacket->stream_index];
            char ptsText[32] = {0};
            char ptsSecondsText[32] = {0};

            /**
             * 일부 컨테이너/코덱은 pts 없는 패킷을 내놓는다(AV_NOPTS_VALUE = INT64_MIN).
             * 이 값을 그대로 av_rescale_q에 넣으면 오버플로된 쓰레기 값이 나오므로
             * 반드시 검사 후 사용한다.
             */
            if (pPacket->pts == AV_NOPTS_VALUE) {
                snprintf(ptsText, sizeof(ptsText), "N/A");
                snprintf(ptsSecondsText, sizeof(ptsSecondsText), "N/A");
            } else {
                /**
                 * pts는 스트림 time_base 단위이므로 초로 보려면 변환이 필요하다.
                 * av_rescale_q(값, 원래 단위, 바꿀 단위) — 여기서는
                 * 스트림 time_base → AV_TIME_BASE_Q(1/1,000,000초) 로 바꾼 뒤 초로 나눈다.
                 */
                double ptsSeconds =
                        (double) av_rescale_q(pPacket->pts, pStream->time_base, AV_TIME_BASE_Q) / AV_TIME_BASE;
                snprintf(ptsText, sizeof(ptsText), "%lld", pPacket->pts);
                snprintf(ptsSecondsText, sizeof(ptsSecondsText), "%.3f", ptsSeconds);
            }

            printf("%-8d %-8d %-12s %-12lld %-10d %-10s %s\r\n",
                   totalPacketCount,
                   pPacket->stream_index,
                   ptsText,
                   pPacket->dts,
                   pPacket->size,
                   /** AV_PKT_FLAG_KEY: 이 패킷이 키프레임(I-프레임)을 담고 있음 */
                   (pPacket->flags & AV_PKT_FLAG_KEY) ? "KEY" : "-",
                   ptsSecondsText);
            printedCount++;
        }

        if (pPacket->stream_index < 8) {
            streamPacketCount[pPacket->stream_index]++;
        }
        totalPacketCount++;

        /**
         * 중요: av_read_frame이 채운 패킷 데이터의 참조를 반드시 해제한다.
         * 해제하지 않으면 매 패킷마다 메모리가 누적된다.
         */
        av_packet_unref(pPacket);
    }

    printf("\r\n==== Packet Summary ====\r\n");
    printf("total packets  : %d\r\n", totalPacketCount);
    for (int idx = 0; idx < (int) pFormatContext->nb_streams && idx < 8; ++idx) {
        printf("stream #%d (%s) : %d packets\r\n",
               idx,
               av_get_media_type_string(pFormatContext->streams[idx]->codecpar->codec_type),
               streamPacketCount[idx]);
    }

    /** 해제는 할당의 역순 */
    av_packet_free(&pPacket);
    avformat_close_input(&pFormatContext);
    printf("Demuxing Done!\r\n");
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
