#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>

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

#if defined(__APPLE__)

/** 디코더가 지원 픽셀 포맷 목록에서 하나를 고르라고 부르는 콜백 */
static enum AVPixelFormat GetHwPixelFormat(AVCodecContext *pCodecContext, const enum AVPixelFormat *pPixelFormats);

/** GPU 프레임을 CPU 메모리로 내려 raw 파일로 저장 */
static bool SaveHwFrameToFile(AVFrame *pHwFrame, const char *outputPath);

#endif

/**
 * study-FFMPEG HW-02 — VideoToolbox 하드웨어 디코딩
 *
 * SW 디코딩(04)과의 차이는 딱 세 가지다:
 *   1) codecContext->hw_device_ctx 에 VideoToolbox 디바이스를 걸어준다
 *   2) codecContext->get_format 콜백에서 AV_PIX_FMT_VIDEOTOOLBOX를 선택한다
 *   3) 디코딩된 프레임은 GPU 메모리에 있으므로(format=videotoolbox)
 *      CPU에서 쓰려면 av_hwframe_transfer_data로 내려받는다 (보통 NV12)
 *
 * 전체 프레임 수와 소요 시간을 출력하고, 첫 프레임을 raw NV12로 저장한다.
 */
int main(int argc, char **argv) {
#if !defined(__APPLE__)
    printf("This lesson requires macOS (VideoToolbox). Skipped.\r\n");
    return 0;
#else
    char resourcePath[BUFFER_MAX] = {0};
    char outputPath[BUFFER_MAX] = {0};
    int errorCode = -1;
    /** 프로그램 종료 코드: 성공 경로 끝에서만 0으로 바뀐다 */
    int exitStatus = -1;

    AVFormatContext *pFormatContext = NULL;
    const AVCodec *pVideoCodec = NULL;
    AVCodecContext *pVideoCodecContext = NULL;
    int videoStreamIdx = -1;
    AVPacket *pPacket = NULL;
    AVFrame *pFrame = NULL;

    AVBufferRef *pHwDeviceContext = NULL;

    int decodedFrameCount = 0;
    bool firstFrameSaved = false;
    clock_t startClock;
    double elapsedSeconds = 0.0;

    if (!GetResourcePath("murage.mp4", resourcePath)) {
        printf("Failed Get murage.mp4 resource path...\r\n");
        return -1;
    }
    if (!EnsureGeneratedStudyDirectory()) {
        printf("Failed Create GeneratedStudy directory...\r\n");
        return -1;
    }
    if (!GetResourcePath("GeneratedStudy/study-hw-decoded.nv12", outputPath)) {
        return -1;
    }
    printf("resource path - %s\r\n", resourcePath);

    /** ===== 1. VideoToolbox 디바이스 생성 ===== */
    errorCode = av_hwdevice_ctx_create(&pHwDeviceContext, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, NULL, NULL, 0);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Create VideoToolbox Device...\r\n", errorCode);
        printf("HW acceleration not available on this machine.\r\n");
        return 0;
    }

    /** ===== 2. 입력 + 디코더 준비 ===== */
    /** 이미 만든 HW 디바이스 컨텍스트가 누수되지 않도록 매크로 대신 goto 정리 경로를 쓴다 */
    errorCode = avformat_open_input(&pFormatContext, resourcePath, NULL, NULL);
    if (errorCode != 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) FFMPEG Open Failed...\r\n", errorCode);
        goto ffmpeg_release;
    }

    errorCode = avformat_find_stream_info(pFormatContext, NULL);
    if (errorCode < 0) {
        goto ffmpeg_release;
    }

    videoStreamIdx = av_find_best_stream(pFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &pVideoCodec, 0);
    if (videoStreamIdx < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Video Stream Found Failed...\r\n", videoStreamIdx);
        goto ffmpeg_release;
    }

    pVideoCodecContext = avcodec_alloc_context3(pVideoCodec);
    if (pVideoCodecContext == NULL) {
        goto ffmpeg_release;
    }
    errorCode = avcodec_parameters_to_context(pVideoCodecContext,
                                              pFormatContext->streams[videoStreamIdx]->codecpar);
    if (errorCode < 0) {
        goto ffmpeg_release;
    }

    /**
     * ===== 3. HW 디코딩 설정 (SW 디코딩과 다른 유일한 부분) =====
     * hw_device_ctx: 이 디코더가 사용할 HW 디바이스 (참조 카운트 증가시켜 전달)
     * get_format   : 디코더가 SW/HW 포맷 중 무엇을 쓸지 물어볼 때의 답변 콜백
     */
    pVideoCodecContext->hw_device_ctx = av_buffer_ref(pHwDeviceContext);
    pVideoCodecContext->get_format = GetHwPixelFormat;

    errorCode = avcodec_open2(pVideoCodecContext, pVideoCodec, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Open Video Codec...\r\n", errorCode);
        goto ffmpeg_release;
    }

    pPacket = av_packet_alloc();
    pFrame = av_frame_alloc();
    if (pPacket == NULL || pFrame == NULL) {
        goto ffmpeg_release;
    }

    /** ===== 4. 디코딩 루프 ===== */
    startClock = clock();

    while (av_read_frame(pFormatContext, pPacket) >= 0) {
        if (pPacket->stream_index == videoStreamIdx) {
            errorCode = avcodec_send_packet(pVideoCodecContext, pPacket);
            if (errorCode < 0) {
                av_packet_unref(pPacket);
                break;
            }

            while (true) {
                errorCode = avcodec_receive_frame(pVideoCodecContext, pFrame);
                if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
                    break;
                } else if (errorCode < 0) {
                    break;
                }

                decodedFrameCount++;

                /** 첫 프레임: GPU 메모리 여부 확인 후 CPU로 내려 저장 */
                if (!firstFrameSaved) {
                    printf("frame format : %s %s\r\n",
                           av_get_pix_fmt_name((enum AVPixelFormat) pFrame->format),
                           pFrame->format == AV_PIX_FMT_VIDEOTOOLBOX ? "(GPU memory!)" : "(sw fallback)");
                    if (pFrame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
                        firstFrameSaved = SaveHwFrameToFile(pFrame, outputPath);
                    } else {
                        firstFrameSaved = true;
                    }
                }
                av_frame_unref(pFrame);
            }
        }
        av_packet_unref(pPacket);
    }

    /** 디코더 flush */
    errorCode = avcodec_send_packet(pVideoCodecContext, NULL);
    if (errorCode >= 0) {
        while (avcodec_receive_frame(pVideoCodecContext, pFrame) >= 0) {
            decodedFrameCount++;
            av_frame_unref(pFrame);
        }
    }

    elapsedSeconds = (double) (clock() - startClock) / CLOCKS_PER_SEC;
    printf("decoded frames : %d in %.3f sec (CPU time) → %.1f fps\r\n",
           decodedFrameCount, elapsedSeconds,
           elapsedSeconds > 0 ? decodedFrameCount / elapsedSeconds : 0.0);
    printf("(04 레슨의 SW 디코딩과 CPU 시간을 비교해 보자 — HW 디코딩은 GPU가 일하므로 CPU 시간이 훨씬 적다)\r\n");

    exitStatus = 0;

    ffmpeg_release:
    av_frame_free(&pFrame);
    av_packet_free(&pPacket);
    avcodec_free_context(&pVideoCodecContext);
    avformat_close_input(&pFormatContext);
    av_buffer_unref(&pHwDeviceContext);
    if (exitStatus == 0) {
        printf("HW Decode Done!\r\n");
    } else {
        printf("Finished with error(s)...\r\n");
    }
    return exitStatus;
#endif
}

#if defined(__APPLE__)

static enum AVPixelFormat GetHwPixelFormat(AVCodecContext *pCodecContext, const enum AVPixelFormat *pPixelFormats) {
    /**
     * 디코더가 제안하는 포맷 목록(AV_PIX_FMT_NONE으로 끝남)에서
     * VideoToolbox GPU 포맷이 있으면 그것을 고른다.
     * 없으면(HW 초기화 실패 등) 첫 번째 SW 포맷으로 폴백한다.
     */
    for (const enum AVPixelFormat *pFormat = pPixelFormats; *pFormat != AV_PIX_FMT_NONE; pFormat++) {
        if (*pFormat == AV_PIX_FMT_VIDEOTOOLBOX) {
            return *pFormat;
        }
    }
    printf("VideoToolbox pixel format not offered → SW decoding fallback\r\n");
    return pPixelFormats[0];
}

static bool SaveHwFrameToFile(AVFrame *pHwFrame, const char *outputPath) {
    AVFrame *pSwFrame = av_frame_alloc();
    FILE *pOutputFile = NULL;
    int errorCode = 0;
    bool result = false;

    if (pSwFrame == NULL) {
        return false;
    }

    /**
     * GPU 메모리 → CPU 메모리 전송.
     * 출력 포맷을 지정하지 않으면(0) FFmpeg이 적절한 SW 포맷(보통 NV12)을 고른다.
     */
    errorCode = av_hwframe_transfer_data(pSwFrame, pHwFrame, 0);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Transfer HW Frame...\r\n", errorCode);
        av_frame_free(&pSwFrame);
        return false;
    }

    printf("transferred to CPU : %s %dx%d\r\n",
           av_get_pix_fmt_name((enum AVPixelFormat) pSwFrame->format),
           pSwFrame->width, pSwFrame->height);

    /**
     * 아래 저장 코드는 NV12(2평면: Y + 인터리브 CbCr) 레이아웃 전용이다.
     * 10bit 소스(p010le) 등 다른 포맷으로 전송되면 레이아웃이 달라 파일이 깨지므로
     * NV12가 아닐 때는 저장을 건너뛴다.
     */
    if (pSwFrame->format != AV_PIX_FMT_NV12) {
        printf("unexpected sw format (%s) — skip raw dump (NV12 전용 저장 코드)\r\n",
               av_get_pix_fmt_name((enum AVPixelFormat) pSwFrame->format));
        av_frame_free(&pSwFrame);
        return true;
    }

    pOutputFile = fopen(outputPath, "wb");
    if (pOutputFile != NULL) {
        /** NV12: Y 평면(전체 크기) + CbCr 인터리브 평면(세로 절반) */
        for (int y = 0; y < pSwFrame->height; ++y) {
            fwrite(pSwFrame->data[0] + (ptrdiff_t) y * pSwFrame->linesize[0], 1, pSwFrame->width, pOutputFile);
        }
        for (int y = 0; y < pSwFrame->height / 2; ++y) {
            fwrite(pSwFrame->data[1] + (ptrdiff_t) y * pSwFrame->linesize[1], 1, pSwFrame->width, pOutputFile);
        }
        fclose(pOutputFile);
        printf("saved : %s\r\n", outputPath);
        printf("확인 명령: ffplay -f rawvideo -pixel_format nv12 -video_size %dx%d \"%s\"\r\n",
               pSwFrame->width, pSwFrame->height, outputPath);
        result = true;
    }

    av_frame_free(&pSwFrame);
    return result;
}

#endif

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
