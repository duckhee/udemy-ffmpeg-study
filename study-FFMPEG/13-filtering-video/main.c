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
#include <libavutil/opt.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libswscale/swscale.h>

#define BUFFER_MAX                  1024
/** 절대 경로 해석용 버퍼 크기 (Linux PATH_MAX=4096 대응) */
#define RESOURCE_PATH_MAX           4096
/** PPM으로 저장할 필터링 결과 프레임 수 */
#define SAVE_FRAME_MAX              5
/**
 * 필터 그래프 기술 문자열 (ffmpeg -vf 와 같은 문법).
 * hflip   : 좌우 반전
 * drawbox : 좌상단에 빨간 사각형 그리기
 */
#define FILTER_DESCRIPTION          "hflip,drawbox=x=10:y=10:w=100:h=100:color=red:t=8"

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
 * study-FFMPEG 13 — libavfilter: 필터 그래프
 *
 * ffmpeg -vf "hflip,drawbox=..." 가 내부에서 하는 일을 직접 만든다.
 *
 * 필터 그래프의 구조:
 *   [buffer 소스] → hflip → drawbox → [buffersink 싱크]
 *
 *   buffer     : 우리가 디코딩한 프레임을 그래프에 "밀어 넣는" 입구
 *   buffersink : 필터링이 끝난 프레임을 "꺼내는" 출구
 *
 * 디코딩한 프레임 5장을 그래프에 통과시켜 PPM으로 저장한다.
 */
int main(int argc, char **argv) {
    char resourcePath[BUFFER_MAX] = {0};
    char outputPath[BUFFER_MAX] = {0};
    char outputName[BUFFER_MAX] = {0};
    char filterSourceArgs[BUFFER_MAX] = {0};
    int errorCode = -1;
    /** 프로그램 종료 코드: 성공 경로 끝에서만 0으로 바뀐다 */
    int exitStatus = -1;

    AVFormatContext *pFormatContext = NULL;
    const AVCodec *pVideoCodec = NULL;
    AVCodecContext *pVideoCodecContext = NULL;
    int videoStreamIdx = -1;
    AVPacket *pPacket = NULL;
    AVFrame *pFrame = NULL;
    AVFrame *pFilteredFrame = NULL;

    /** 필터 그래프 구성 요소 */
    AVFilterGraph *pFilterGraph = NULL;
    AVFilterContext *pBufferSourceContext = NULL;
    AVFilterContext *pBufferSinkContext = NULL;
    AVFilterInOut *pInputs = NULL;
    AVFilterInOut *pOutputs = NULL;

    /** RGB 변환용 (PPM 저장) */
    struct SwsContext *pSwsContext = NULL;
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
    printf("filter : %s\r\n", FILTER_DESCRIPTION);

    /** ===== 1. 입력 + 디코더 준비 (04와 동일) ===== */
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

    pVideoCodecContext = avcodec_alloc_context3(pVideoCodec);
    if (pVideoCodecContext == NULL) {
        goto ffmpeg_release;
    }
    errorCode = avcodec_parameters_to_context(pVideoCodecContext,
                                              pFormatContext->streams[videoStreamIdx]->codecpar);
    if (errorCode < 0) {
        goto ffmpeg_release;
    }
    errorCode = avcodec_open2(pVideoCodecContext, pVideoCodec, NULL);
    if (errorCode < 0) {
        goto ffmpeg_release;
    }

    /** ===== 2. 필터 그래프 구성 ===== */
    pFilterGraph = avfilter_graph_alloc();
    if (pFilterGraph == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Allocate Filter Graph...\r\n");
        goto ffmpeg_release;
    }

    /**
     * buffer 소스 생성.
     * 그래프에 들어올 프레임의 사양(크기/포맷/time_base/화면비)을
     * 문자열 인자로 알려줘야 한다.
     */
    snprintf(filterSourceArgs, sizeof(filterSourceArgs),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             pVideoCodecContext->width, pVideoCodecContext->height,
             pVideoCodecContext->pix_fmt,
             pFormatContext->streams[videoStreamIdx]->time_base.num,
             pFormatContext->streams[videoStreamIdx]->time_base.den,
             pVideoCodecContext->sample_aspect_ratio.num,
             pVideoCodecContext->sample_aspect_ratio.den > 0 ? pVideoCodecContext->sample_aspect_ratio.den : 1);

    errorCode = avfilter_graph_create_filter(&pBufferSourceContext,
                                             avfilter_get_by_name("buffer"), "in",
                                             filterSourceArgs, NULL, pFilterGraph);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Create Buffer Source...\r\n", errorCode);
        goto ffmpeg_release;
    }

    /** buffersink 싱크 생성 */
    errorCode = avfilter_graph_create_filter(&pBufferSinkContext,
                                             avfilter_get_by_name("buffersink"), "out",
                                             NULL, NULL, pFilterGraph);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Create Buffer Sink...\r\n", errorCode);
        goto ffmpeg_release;
    }

    /**
     * 싱크가 내놓을 픽셀 포맷을 입력과 같게 고정한다.
     * 고정하지 않으면 필터 체인에 따라 다른 포맷이 나올 수 있고,
     * 그러면 아래에서 만든 RGB 변환용 SwsContext와 어긋난다.
     * (필요하면 libavfilter가 변환 필터를 자동 삽입해 포맷을 맞춰준다)
     */
    {
        enum AVPixelFormat sinkPixelFormats[] = {pVideoCodecContext->pix_fmt, AV_PIX_FMT_NONE};
        errorCode = av_opt_set_int_list(pBufferSinkContext, "pix_fmts", sinkPixelFormats,
                                        AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
        if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Set Sink Pixel Format...\r\n", errorCode);
            goto ffmpeg_release;
        }
    }

    /**
     * 필터 문자열 파싱 + 소스/싱크 연결.
     * AVFilterInOut은 문자열 그래프의 "열린 끝"을 우리 소스/싱크에 잇는 연결 정보다.
     *   outputs: 소스(in)의 출력 → 문자열 그래프의 입력에 연결
     *   inputs : 문자열 그래프의 출력 → 싱크(out)의 입력에 연결
     */
    pOutputs = avfilter_inout_alloc();
    pInputs = avfilter_inout_alloc();
    if (pOutputs == NULL || pInputs == NULL) {
        goto ffmpeg_release;
    }

    pOutputs->name = av_strdup("in");
    pOutputs->filter_ctx = pBufferSourceContext;
    pOutputs->pad_idx = 0;
    pOutputs->next = NULL;

    pInputs->name = av_strdup("out");
    pInputs->filter_ctx = pBufferSinkContext;
    pInputs->pad_idx = 0;
    pInputs->next = NULL;

    errorCode = avfilter_graph_parse_ptr(pFilterGraph, FILTER_DESCRIPTION, &pInputs, &pOutputs, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Parse Filter Graph...\r\n", errorCode);
        goto ffmpeg_release;
    }

    /** 그래프 유효성 검사 + 내부 링크 설정 완료 */
    errorCode = avfilter_graph_config(pFilterGraph, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Config Filter Graph...\r\n", errorCode);
        goto ffmpeg_release;
    }

    /** ===== 3. RGB 변환 준비 (필터 결과는 여전히 YUV420P) ===== */
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
    pFilteredFrame = av_frame_alloc();
    if (pPacket == NULL || pFrame == NULL || pFilteredFrame == NULL) {
        goto ffmpeg_release;
    }

    /** ===== 4. 디코딩 → 필터 → 저장 루프 ===== */
    while (savedFrameCount < SAVE_FRAME_MAX && av_read_frame(pFormatContext, pPacket) >= 0) {
        if (pPacket->stream_index == videoStreamIdx) {
            errorCode = avcodec_send_packet(pVideoCodecContext, pPacket);
            if (errorCode < 0) {
                av_packet_unref(pPacket);
                break;
            }

            while (savedFrameCount < SAVE_FRAME_MAX) {
                errorCode = avcodec_receive_frame(pVideoCodecContext, pFrame);
                if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
                    break;
                } else if (errorCode < 0) {
                    break;
                }

                /** 디코딩된 프레임을 그래프에 밀어 넣기 */
                errorCode = av_buffersrc_add_frame_flags(pBufferSourceContext, pFrame,
                                                         AV_BUFFERSRC_FLAG_KEEP_REF);
                if (errorCode < 0) {
                    av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Add Frame To Filter...\r\n", errorCode);
                    av_frame_unref(pFrame);
                    break;
                }

                /** 필터링 완료된 프레임 꺼내기 (디코더처럼 EAGAIN 처리) */
                while (savedFrameCount < SAVE_FRAME_MAX) {
                    errorCode = av_buffersink_get_frame(pBufferSinkContext, pFilteredFrame);
                    if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
                        break;
                    } else if (errorCode < 0) {
                        break;
                    }

                    /** YUV → RGB 변환 후 PPM 저장 */
                    sws_scale(pSwsContext,
                              (const uint8_t *const *) pFilteredFrame->data, pFilteredFrame->linesize,
                              0, pFilteredFrame->height,
                              rgbData, rgbLineSize);

                    snprintf(outputName, sizeof(outputName),
                             "GeneratedStudy/study-filtered-%03d.ppm", savedFrameCount);
                    memset(outputPath, 0, sizeof(outputPath));
                    if (GetResourcePath(outputName, outputPath) &&
                        SavePPMImage(outputPath, rgbData[0], rgbLineSize[0],
                                     pFilteredFrame->width, pFilteredFrame->height)) {
                        printf("saved : %s\r\n", outputPath);
                        savedFrameCount++;
                    }
                    av_frame_unref(pFilteredFrame);
                }
                av_frame_unref(pFrame);
            }
        }
        av_packet_unref(pPacket);
    }

    printf("saved frame count : %d\r\n", savedFrameCount);

    exitStatus = 0;

    ffmpeg_release:
    avfilter_inout_free(&pInputs);
    avfilter_inout_free(&pOutputs);
    av_frame_free(&pFilteredFrame);
    av_frame_free(&pFrame);
    av_packet_free(&pPacket);
    av_freep(&rgbData[0]);
    sws_freeContext(pSwsContext);
    /** 그래프 해제 시 그래프에 속한 필터 컨텍스트(소스/싱크)도 함께 해제된다 */
    avfilter_graph_free(&pFilterGraph);
    avcodec_free_context(&pVideoCodecContext);
    avformat_close_input(&pFormatContext);
    if (exitStatus == 0) {
        printf("Filtering Video Done!\r\n");
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
