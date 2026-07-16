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
#include <libswscale/swscale.h>

#define BUFFER_MAX                  1024
/** 절대 경로 해석용 버퍼 크기 (Linux PATH_MAX=4096 대응) */
#define RESOURCE_PATH_MAX           4096
/** PPM으로 저장할 프레임 개수 */
#define SAVE_FRAME_MAX              5
/** 출력 이미지 크기 (원본에서 다운스케일) */
#define OUTPUT_WIDTH                640
#define OUTPUT_HEIGHT               360

#define FFMPEG_ERROR(errorCode, msg) \
{                                    \
if((errorCode) < (0)) {              \
av_log(NULL, AV_LOG_ERROR, (msg));   \
return -1;                           \
}else{                               \
}                                     \
}                                    \


bool GetResourcePath(const char *name, char *const pathBuffer);

/** resources/GeneratedStudy 디렉터리를 만든다 (이미 있으면 무시) */
bool EnsureGeneratedStudyDirectory(void);

/** RGB24 버퍼를 PPM(P6) 파일로 저장 */
bool SavePPMImage(const char *fileName, const uint8_t *rgbData, int lineSize, int width, int height);

/**
 * study-FFMPEG 06 — swscale: 픽셀 포맷 변환과 스케일링
 *
 * 디코딩된 프레임은 보통 YUV420P다. 화면 표시/이미지 저장에는 RGB가 필요하다.
 * libswscale의 SwsContext 하나로 두 가지를 동시에 처리한다:
 *   1) 픽셀 포맷 변환 : YUV420P → RGB24
 *   2) 크기 변경      : 원본 → 640x360 (다운스케일)
 * 변환된 프레임을 PPM(P6) 이미지로 저장해 눈으로 확인한다.
 */
int main(int argc, char **argv) {
    char resourcePath[BUFFER_MAX] = {0};
    char outputPath[BUFFER_MAX] = {0};
    char outputName[BUFFER_MAX] = {0};
    int errorCode = -1;
    /** 프로그램 종료 코드: 성공 경로 끝에서만 0으로 바뀐다 */
    int exitStatus = -1;

    AVFormatContext *pFormatContext = NULL;
    AVPacket *pPacket = NULL;
    AVFrame *pFrame = NULL;

    const AVCodec *pVideoCodec = NULL;
    AVCodecContext *pVideoCodecContext = NULL;
    int videoStreamIdx = -1;

    struct SwsContext *pSwsContext = NULL;
    /** 변환 결과(RGB24)를 담을 버퍼: 평면 포인터 4개 + 각 평면의 stride */
    uint8_t *rgbData[4] = {NULL};
    int rgbLineSize[4] = {0};

    int savedFrameCount = 0;

    if (!GetResourcePath("murage.mp4", resourcePath)) {
        printf("Failed Get murage.mp4 resource path...\r\n");
        return -1;
    }
    if (!EnsureGeneratedStudyDirectory()) {
        printf("Failed Create GeneratedStudy directory...\r\n");
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

    videoStreamIdx = av_find_best_stream(pFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &pVideoCodec, 0);
    if (videoStreamIdx < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Video Stream Found Failed...\r\n", videoStreamIdx);
        goto ffmpeg_release;
    }

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

    printf("source : %dx%d %s → output : %dx%d rgb24\r\n",
           pVideoCodecContext->width, pVideoCodecContext->height,
           av_get_pix_fmt_name(pVideoCodecContext->pix_fmt),
           OUTPUT_WIDTH, OUTPUT_HEIGHT);

    /**
     * SwsContext 생성.
     * (입력 크기/포맷, 출력 크기/포맷, 보간 알고리즘)을 지정한다.
     * SWS_BILINEAR: 속도/품질 균형이 좋은 기본 선택지.
     */
    pSwsContext = sws_getContext(
            pVideoCodecContext->width, pVideoCodecContext->height, pVideoCodecContext->pix_fmt,
            OUTPUT_WIDTH, OUTPUT_HEIGHT, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, NULL, NULL, NULL);
    if (pSwsContext == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Create SwsContext...\r\n");
        goto ffmpeg_release;
    }

    /**
     * 출력 RGB 버퍼 할당.
     * av_image_alloc이 포맷에 맞는 평면 포인터와 stride(lineSize)를 채워준다.
     * RGB24는 단일 평면이므로 rgbData[0]만 사용된다.
     */
    errorCode = av_image_alloc(rgbData, rgbLineSize, OUTPUT_WIDTH, OUTPUT_HEIGHT, AV_PIX_FMT_RGB24, 1);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Allocate RGB Buffer...\r\n", errorCode);
        goto ffmpeg_release;
    }

    pPacket = av_packet_alloc();
    pFrame = av_frame_alloc();
    if (pPacket == NULL || pFrame == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load packet/frame structure...\r\n");
        goto ffmpeg_release;
    }

    /** 디코딩하며 앞에서부터 SAVE_FRAME_MAX 장을 PPM으로 저장 */
    while (savedFrameCount < SAVE_FRAME_MAX && av_read_frame(pFormatContext, pPacket) >= 0) {
        if (pPacket->stream_index == videoStreamIdx) {
            errorCode = avcodec_send_packet(pVideoCodecContext, pPacket);
            if (errorCode < 0) {
                av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Sending packet to decoder\r\n", errorCode);
                av_packet_unref(pPacket);
                break;
            }

            while (savedFrameCount < SAVE_FRAME_MAX) {
                errorCode = avcodec_receive_frame(pVideoCodecContext, pFrame);
                if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
                    break;
                } else if (errorCode < 0) {
                    av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Receive frame\r\n", errorCode);
                    break;
                }

                /**
                 * SwsContext는 코덱 컨텍스트의 초기 사양으로 만들었다.
                 * 스트림 중간에 해상도/포맷이 바뀔 수 있으므로(H.264에서 합법)
                 * 실제 프레임 사양이 다르면 잘못된 메모리 접근을 피하기 위해 건너뛴다.
                 */
                if (pFrame->width != pVideoCodecContext->width ||
                    pFrame->height != pVideoCodecContext->height ||
                    pFrame->format != pVideoCodecContext->pix_fmt) {
                    printf("frame spec changed (%dx%d) — skip\r\n", pFrame->width, pFrame->height);
                    av_frame_unref(pFrame);
                    continue;
                }

                /**
                 * 실제 변환 수행.
                 * 입력 프레임의 평면(data)과 stride(linesize)를 주면
                 * 출력 버퍼에 변환/스케일된 픽셀이 채워진다.
                 * 세 번째/네 번째 인자(0, height)는 처리할 입력의 세로 범위다.
                 */
                sws_scale(pSwsContext,
                          (const uint8_t *const *) pFrame->data, pFrame->linesize,
                          0, pFrame->height,
                          rgbData, rgbLineSize);

                snprintf(outputName, sizeof(outputName), "GeneratedStudy/study-scaled-%03d.ppm", savedFrameCount);
                if (!GetResourcePath(outputName, outputPath)) {
                    av_frame_unref(pFrame);
                    goto ffmpeg_release;
                }

                if (SavePPMImage(outputPath, rgbData[0], rgbLineSize[0], OUTPUT_WIDTH, OUTPUT_HEIGHT)) {
                    printf("saved : %s (pts=%lld)\r\n", outputPath, pFrame->pts);
                    savedFrameCount++;
                }
                av_frame_unref(pFrame);
                memset(outputPath, 0, sizeof(outputPath));
            }
        }
        av_packet_unref(pPacket);
    }

    printf("saved frame count : %d\r\n", savedFrameCount);

    exitStatus = 0;

    ffmpeg_release:
    av_frame_free(&pFrame);
    av_packet_free(&pPacket);
    /** av_image_alloc으로 받은 버퍼는 av_freep(첫 평면 포인터)로 해제 */
    av_freep(&rgbData[0]);
    sws_freeContext(pSwsContext);
    avcodec_free_context(&pVideoCodecContext);
    avformat_close_input(&pFormatContext);
    if (exitStatus == 0) {
        printf("Scaling Video Done!\r\n");
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

    /** PPM(P6) 헤더: 매직넘버, 가로 세로, 최대 색상값 */
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    /**
     * stride(lineSize)는 정렬 때문에 width*3보다 클 수 있으므로
     * 한 줄씩 width*3 바이트만 잘라서 쓴다.
     */
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
