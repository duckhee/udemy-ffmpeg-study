#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#if defined(WIN32) || defined(WIN64)

#include <Windows.h>
#include <direct.h>

#else

#include <sys/stat.h>

#endif

// FFMPEG Library
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>

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

bool EnsureGeneratedStudyDirectory(void);

bool SavePPMImage(const char *fileName, const uint8_t *rgbData, int lineSize, int width, int height);

/**
 * study-FFMPEG 14 — 시킹 (특정 시점으로 이동)
 *
 * 플레이어의 "탐색 바 드래그"가 내부에서 하는 일:
 *
 *   1) av_seek_frame(..., AVSEEK_FLAG_BACKWARD)
 *      → 목표 시점 "이전의 가장 가까운 키프레임"으로 파일 위치 이동.
 *      (P/B 프레임은 키프레임 없이 디코딩할 수 없으므로 항상 키프레임에서 시작해야 한다)
 *   2) avcodec_flush_buffers
 *      → 디코더 내부에 남아 있던 시킹 이전 데이터를 비운다.
 *   3) 키프레임부터 순서대로 디코딩하며 목표 pts에 도달할 때까지 진행.
 *
 * 영상의 50% 지점으로 시킹해 그 프레임을 PPM 스냅샷으로 저장한다.
 */
int main(int argc, char **argv) {
    char resourcePath[BUFFER_MAX] = {0};
    char outputPath[BUFFER_MAX] = {0};
    int errorCode = -1;
    /** 프로그램 종료 코드: 성공 경로 끝에서만 0으로 바뀐다 */
    int exitStatus = -1;

    AVFormatContext *pFormatContext = NULL;
    const AVCodec *pVideoCodec = NULL;
    AVCodecContext *pVideoCodecContext = NULL;
    int videoStreamIdx = -1;
    AVStream *pVideoStream = NULL;
    AVPacket *pPacket = NULL;
    AVFrame *pFrame = NULL;

    struct SwsContext *pSwsContext = NULL;
    uint8_t *rgbData[4] = {NULL};
    int rgbLineSize[4] = {0};

    /** 목표 시점 (마이크로초 단위, AV_TIME_BASE 기준) */
    int64_t targetTimestampUs = 0;
    /** 목표 시점 (비디오 스트림 time_base 단위) */
    int64_t targetPts = 0;
    bool snapshotSaved = false;
    int decodedAfterSeek = 0;

    if (!GetResourcePath("murage.mp4", resourcePath)) {
        printf("Failed Get murage.mp4 resource path...\r\n");
        return -1;
    }
    if (!EnsureGeneratedStudyDirectory()) {
        printf("Failed Create GeneratedStudy directory...\r\n");
        return -1;
    }
    if (!GetResourcePath("GeneratedStudy/study-seek-snapshot.ppm", outputPath)) {
        return -1;
    }
    printf("resource path - %s\r\n", resourcePath);

    /** ===== 1. 입력 + 디코더 준비 ===== */
    errorCode = avformat_open_input(&pFormatContext, resourcePath, NULL, NULL);
    FFMPEG_ERROR(errorCode, "[FFMPEG ERROR] FFMPEG Open Failed...\r\n")

    errorCode = avformat_find_stream_info(pFormatContext, NULL);
    if (errorCode < 0) {
        goto ffmpeg_release;
    }

    videoStreamIdx = av_find_best_stream(pFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &pVideoCodec, 0);
    if (videoStreamIdx < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Video Stream Found Failed...\r\n", videoStreamIdx);
        goto ffmpeg_release;
    }
    pVideoStream = pFormatContext->streams[videoStreamIdx];

    pVideoCodecContext = avcodec_alloc_context3(pVideoCodec);
    if (pVideoCodecContext == NULL) {
        goto ffmpeg_release;
    }
    errorCode = avcodec_parameters_to_context(pVideoCodecContext, pVideoStream->codecpar);
    if (errorCode < 0) {
        goto ffmpeg_release;
    }
    errorCode = avcodec_open2(pVideoCodecContext, pVideoCodec, NULL);
    if (errorCode < 0) {
        goto ffmpeg_release;
    }

    /** ===== 2. 목표 시점 계산: 전체 길이의 50% ===== */
    targetTimestampUs = pFormatContext->duration / 2;
    /** AV_TIME_BASE(1/1,000,000초) 단위 → 비디오 스트림 time_base 단위로 변환 */
    targetPts = av_rescale_q(targetTimestampUs, AV_TIME_BASE_Q, pVideoStream->time_base);

    printf("duration : %.2f sec → seek target : %.2f sec (pts=%lld)\r\n",
           (double) pFormatContext->duration / AV_TIME_BASE,
           (double) targetTimestampUs / AV_TIME_BASE,
           targetPts);

    /**
     * ===== 3. 시킹 실행 =====
     * AVSEEK_FLAG_BACKWARD: 목표 지점 "이전"의 키프레임으로 이동.
     * (이 플래그가 없으면 이후 키프레임으로 갈 수 있어 목표를 지나칠 수 있다)
     */
    errorCode = av_seek_frame(pFormatContext, videoStreamIdx, targetPts, AVSEEK_FLAG_BACKWARD);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Seek...\r\n", errorCode);
        goto ffmpeg_release;
    }

    /**
     * 디코더 내부 버퍼 비우기 — 시킹의 필수 파트너.
     * 비우지 않으면 시킹 이전 위치의 프레임 데이터가 섞여 화면이 깨진다.
     */
    avcodec_flush_buffers(pVideoCodecContext);

    /** ===== 4. RGB 변환 준비 ===== */
    pSwsContext = sws_getContext(
            pVideoCodecContext->width, pVideoCodecContext->height, pVideoCodecContext->pix_fmt,
            pVideoCodecContext->width, pVideoCodecContext->height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, NULL, NULL, NULL);
    if (pSwsContext == NULL) {
        goto ffmpeg_release;
    }
    errorCode = av_image_alloc(rgbData, rgbLineSize,
                               pVideoCodecContext->width, pVideoCodecContext->height, AV_PIX_FMT_RGB24, 1);
    if (errorCode < 0) {
        goto ffmpeg_release;
    }

    pPacket = av_packet_alloc();
    pFrame = av_frame_alloc();
    if (pPacket == NULL || pFrame == NULL) {
        goto ffmpeg_release;
    }

    /**
     * ===== 5. 키프레임부터 목표 pts까지 전진 디코딩 =====
     * 시킹 직후 첫 프레임은 키프레임(목표보다 이전)이므로
     * frame->pts >= targetPts 가 될 때까지 버리면서 디코딩한다.
     */
    while (!snapshotSaved && av_read_frame(pFormatContext, pPacket) >= 0) {
        if (pPacket->stream_index == videoStreamIdx) {
            errorCode = avcodec_send_packet(pVideoCodecContext, pPacket);
            if (errorCode < 0) {
                av_packet_unref(pPacket);
                break;
            }

            while (!snapshotSaved) {
                errorCode = avcodec_receive_frame(pVideoCodecContext, pFrame);
                if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
                    break;
                } else if (errorCode < 0) {
                    break;
                }

                decodedAfterSeek++;

                if (pFrame->best_effort_timestamp >= targetPts) {
                    /** 목표 도달 — 스냅샷 저장 */
                    printf("target frame : pts=%lld (%.2f sec), decoded %d frames after keyframe\r\n",
                           pFrame->best_effort_timestamp,
                           pFrame->best_effort_timestamp * av_q2d(pVideoStream->time_base),
                           decodedAfterSeek);

                    sws_scale(pSwsContext,
                              (const uint8_t *const *) pFrame->data, pFrame->linesize,
                              0, pFrame->height,
                              rgbData, rgbLineSize);

                    if (SavePPMImage(outputPath, rgbData[0], rgbLineSize[0],
                                     pFrame->width, pFrame->height)) {
                        printf("saved : %s\r\n", outputPath);
                        snapshotSaved = true;
                    }
                }
                av_frame_unref(pFrame);
            }
        }
        av_packet_unref(pPacket);
    }

    /**
     * 디코더 flush.
     * 목표 pts가 영상 끝부분이면 B-프레임 재정렬 지연 때문에
     * 마지막 프레임들이 디코더 내부에 남아 있을 수 있다.
     * NULL 패킷을 보내 남은 프레임까지 마저 확인한다.
     */
    if (!snapshotSaved && avcodec_send_packet(pVideoCodecContext, NULL) >= 0) {
        while (!snapshotSaved && avcodec_receive_frame(pVideoCodecContext, pFrame) >= 0) {
            decodedAfterSeek++;

            if (pFrame->best_effort_timestamp >= targetPts) {
                printf("target frame : pts=%lld (%.2f sec), decoded %d frames after keyframe (flushed)\r\n",
                       pFrame->best_effort_timestamp,
                       pFrame->best_effort_timestamp * av_q2d(pVideoStream->time_base),
                       decodedAfterSeek);

                sws_scale(pSwsContext,
                          (const uint8_t *const *) pFrame->data, pFrame->linesize,
                          0, pFrame->height,
                          rgbData, rgbLineSize);

                if (SavePPMImage(outputPath, rgbData[0], rgbLineSize[0],
                                 pFrame->width, pFrame->height)) {
                    printf("saved : %s\r\n", outputPath);
                    snapshotSaved = true;
                }
            }
            av_frame_unref(pFrame);
        }
    }

    if (!snapshotSaved) {
        printf("Failed to reach target pts...\r\n");
        goto ffmpeg_release;
    }

    exitStatus = 0;

    ffmpeg_release:
    av_frame_free(&pFrame);
    av_packet_free(&pPacket);
    av_freep(&rgbData[0]);
    sws_freeContext(pSwsContext);
    avcodec_free_context(&pVideoCodecContext);
    avformat_close_input(&pFormatContext);
    if (exitStatus == 0) {
        printf("Seeking Done!\r\n");
    } else {
        printf("Finished with error(s)...\r\n");
    }
    return exitStatus;
}

bool SavePPMImage(const char *fileName, const uint8_t *rgbData, int lineSize, int width, int height) {
    FILE *pFile = fopen(fileName, "wb");
    if (pFile == NULL) {
        printf("Failed Open Output File... (%s)\r\n", fileName);
        return false;
    }

    fprintf(pFile, "P6\n%d %d\n255\n", width, height);
    for (int y = 0; y < height; ++y) {
        fwrite(rgbData + (ptrdiff_t) y * lineSize, 1, (size_t) width * 3, pFile);
    }

    fclose(pFile);
    return true;
}

bool EnsureGeneratedStudyDirectory(void) {
    char directoryPath[BUFFER_MAX] = {0};
    if (!GetResourcePath("GeneratedStudy", directoryPath)) {
        return false;
    }
#if defined(WIN32) || defined(WIN64)
    _mkdir(directoryPath);
#else
    mkdir(directoryPath, 0755);
#endif
    return true;
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
