#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
// FFMPEG Library
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>

#if defined(WIN32) || defined(WIN64)

#include <Windows.h>

#endif

#define BUFFER_MAX                  1024
/** 절대 경로 해석용 버퍼 크기 (Linux PATH_MAX=4096 대응) */
#define RESOURCE_PATH_MAX           4096
/** 상세 정보를 출력할 프레임 개수 */
#define PRINT_FRAME_MAX             10

#define FFMPEG_ERROR(errorCode, msg) \
{                                    \
if((errorCode) < (0)) {              \
av_log(NULL, AV_LOG_ERROR, (msg));   \
return -1;                           \
}else{                               \
}                                     \
}                                    \


bool GetResourcePath(const char *name, char *const pathBuffer);

/** 디코더에서 프레임을 모두 꺼내 정보 출력. 꺼낸 프레임 수를 반환한다. */
int DrainDecodedFrames(AVCodecContext *pCodecContext, AVFrame *pFrame, int *pPrintedCount);

/**
 * study-FFMPEG 04 — 비디오 디코딩 파이프라인
 *
 * FFmpeg의 디코더는 큐처럼 동작한다:
 *   avcodec_send_packet()   : 압축 패킷을 디코더에 넣는다
 *   avcodec_receive_frame() : 디코딩된 비압축 프레임을 꺼낸다
 * 1패킷=1프레임이 보장되지 않으므로(B-프레임 재정렬 등) receive는 루프로 돈다.
 * 마지막에는 NULL 패킷을 보내 디코더 내부에 남은 프레임까지 flush한다.
 */
int main(int argc, char **argv) {
    char resourcePath[BUFFER_MAX] = {0};
    int errorCode = -1;
    /** 프로그램 종료 코드: 성공 경로 끝에서만 0으로 바뀐다 */
    int exitStatus = -1;

    AVFormatContext *pFormatContext = NULL;
    AVPacket *pPacket = NULL;
    AVFrame *pFrame = NULL;

    const AVCodec *pVideoCodec = NULL;
    AVCodecContext *pVideoCodecContext = NULL;
    int videoStreamIdx = -1;

    int decodedFrameCount = 0;
    int printedCount = 0;

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
        goto ffmpeg_release;
    }

    /**
     * av_find_best_stream:
     * 원하는 타입의 "가장 적합한" 스트림을 찾아주고
     * 마지막에서 두 번째 인자로 디코더까지 함께 찾아준다.
     * (스트림을 직접 for문으로 순회하는 것보다 간결하다 — 02 레슨과 비교)
     */
    videoStreamIdx = av_find_best_stream(pFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &pVideoCodec, 0);
    if (videoStreamIdx < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Video Stream Found Failed...\r\n", videoStreamIdx);
        goto ffmpeg_release;
    }
    printf("video stream index : %d, codec : %s\r\n", videoStreamIdx, pVideoCodec->name);

    /** 디코더 컨텍스트 생성 → 스트림 파라미터 복사 → 오픈 (3단계 필수 절차) */
    pVideoCodecContext = avcodec_alloc_context3(pVideoCodec);
    if (pVideoCodecContext == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load Video Codec Context...\r\n");
        goto ffmpeg_release;
    }

    errorCode = avcodec_parameters_to_context(pVideoCodecContext,
                                              pFormatContext->streams[videoStreamIdx]->codecpar);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed codec parameter copy to context...\r\n", errorCode);
        goto ffmpeg_release;
    }

    errorCode = avcodec_open2(pVideoCodecContext, pVideoCodec, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Open Video Codec...\r\n", errorCode);
        goto ffmpeg_release;
    }

    pPacket = av_packet_alloc();
    pFrame = av_frame_alloc();
    if (pPacket == NULL || pFrame == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load packet/frame structure...\r\n");
        goto ffmpeg_release;
    }

    printf("==== Decoded Frames (first %d) ====\r\n", PRINT_FRAME_MAX);

    /** 디먹싱 + 디코딩 루프 */
    while (av_read_frame(pFormatContext, pPacket) >= 0) {
        if (pPacket->stream_index == videoStreamIdx) {
            /** 압축 패킷을 디코더에 공급 */
            errorCode = avcodec_send_packet(pVideoCodecContext, pPacket);
            if (errorCode < 0) {
                av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Sending packet to decoder\r\n", errorCode);
                av_packet_unref(pPacket);
                break;
            }
            /** 이번 패킷으로 나올 수 있는 프레임을 모두 꺼낸다 */
            decodedFrameCount += DrainDecodedFrames(pVideoCodecContext, pFrame, &printedCount);
        }
        av_packet_unref(pPacket);
    }

    /**
     * 디코더 flush.
     * NULL 패킷을 보내면 "입력 끝"이라는 신호가 되어
     * 디코더 내부 버퍼(B-프레임 재정렬 대기 등)에 남아 있던
     * 마지막 프레임들까지 receive로 꺼낼 수 있게 된다.
     */
    errorCode = avcodec_send_packet(pVideoCodecContext, NULL);
    if (errorCode >= 0) {
        int flushedCount = DrainDecodedFrames(pVideoCodecContext, pFrame, &printedCount);
        printf("flushed frames : %d\r\n", flushedCount);
        decodedFrameCount += flushedCount;
    }

    printf("total decoded frames : %d\r\n", decodedFrameCount);

    exitStatus = 0;

    ffmpeg_release:
    /** 해제는 할당의 역순 */
    av_frame_free(&pFrame);
    av_packet_free(&pPacket);
    /** avcodec_free_context가 컨텍스트 close까지 처리한다 (FFmpeg 7.x에서 avcodec_close는 사용 금지) */
    avcodec_free_context(&pVideoCodecContext);
    avformat_close_input(&pFormatContext);
    if (exitStatus == 0) {
        printf("Decode Video Done!\r\n");
    } else {
        printf("Finished with error(s)...\r\n");
    }
    return exitStatus;
}

int DrainDecodedFrames(AVCodecContext *pCodecContext, AVFrame *pFrame, int *pPrintedCount) {
    int receivedCount = 0;
    int errorCode = 0;

    while (errorCode >= 0) {
        errorCode = avcodec_receive_frame(pCodecContext, pFrame);
        /**
         * AVERROR(EAGAIN): 프레임을 내놓으려면 입력이 더 필요함 (에러 아님)
         * AVERROR_EOF   : flush 완료, 더 나올 프레임 없음
         */
        if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
            break;
        } else if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Receive frame\r\n", errorCode);
            break;
        }

        if (*pPrintedCount < PRINT_FRAME_MAX) {
            printf("Frame %-4lld type=%c pts=%-8lld %dx%d key=%s\r\n",
                   pCodecContext->frame_num,
                   /** I/P/B 프레임 타입을 문자로 변환 */
                   av_get_picture_type_char(pFrame->pict_type),
                   pFrame->pts,
                   pFrame->width, pFrame->height,
                   /** FFmpeg 7.x: key_frame 필드 대신 flags의 AV_FRAME_FLAG_KEY를 본다 */
                   (pFrame->flags & AV_FRAME_FLAG_KEY) ? "KEY" : "-");
            (*pPrintedCount)++;
        }

        receivedCount++;
        /** 프레임이 참조하는 픽셀 버퍼 참조 해제 (구조체 자체는 재사용) */
        av_frame_unref(pFrame);
    }

    return receivedCount;
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
