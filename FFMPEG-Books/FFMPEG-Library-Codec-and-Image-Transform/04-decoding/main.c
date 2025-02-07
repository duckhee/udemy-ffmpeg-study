#include <libavformat/avformat.h>
#include <libavcodec/codec.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/common.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>

#if defined(WIN32) || defined(WIn64)

#include <Windows.h>

#endif

#define BUFFER_MAX                  1024

typedef struct fmt_ctx {
    AVFormatContext *fmt_ctx;
    int video_idx;
    int audio_idx;
} VideoContext;


bool GetResourcePath(const char *name, char *const pathBuffer);

int open_input(const char *filename, VideoContext *outVideoContext);

void Release(VideoContext *pContext, VideoContext *pWriteContext);

int main(int argc, char **argv) {
    int ret;
    char resourcePath[BUFFER_MAX] = {0};
    char outputPath[BUFFER_MAX] = {0};

    VideoContext input_ctx;
    VideoContext output_ctx;

    AVPacket *pPacket;
    AVFrame *pFrame;

    if (!GetResourcePath("out.mp4", resourcePath)) {
        printf("Failed Get Resource Path...\r\n");
        return -1;
    }
    if (!GetResourcePath("saveOutput.mp4", outputPath)) {
        printf("Failed Get Resource Path...\r\n");
        return -1;
    }

    if (open_input(resourcePath, &input_ctx) < 0) {
        printf("Failed FFMPEG Open..\r\n");
        return -1;
    }

    /** packet에 대한 객체 메모리 할당 */
    pPacket = av_packet_alloc();
    /** frame에 대한 객체 메모리 할당 */
    pFrame = av_frame_alloc();




    /** packet에 대한 메모리 해제 */
    av_packet_free(&pPacket);
    /** frame에 대한 메모리 해제 */
    av_frame_free(&pFrame);
    /** 객체 메모리 해제 */
    Release(&input_ctx, &output_ctx);
    return 0;

}


bool GetResourcePath(const char *name, char *const pathBuffer) {
    char tempBuffer[BUFFER_MAX] = {0};
    char *pRemoveStart = NULL;
    int removeEndIdx = 0;

#if defined(WIN32) || defined(WIN64)
    GetCurrentDirectory(BUFFER_MAX, tempBuffer);
    pRemoveStart = strstr(tempBuffer, "\\cmake");
#else
    realpath(".", tempBuffer);
    pRemoveStart = strstr(tempBuffer, "/cmake");
#endif

    if (pRemoveStart == NULL) {
        return false;
    }
    removeEndIdx = (int) (pRemoveStart - tempBuffer);
    memcpy(pathBuffer, tempBuffer, sizeof(char) * removeEndIdx);

#if defined(WIN32) || defined(WIN64)
    strcat(pathBuffer, "\\resources\\");
#else
    strcat(pathBuffer, "/resources/");
#endif

    strcat(pathBuffer, name);
    return true;
}


int open_input(const char *filename, VideoContext *outVideoContext) {
    int idx = 0;
    int errCode = 0;

    /** 입력 받은 구조체 초기화 */
//    Release(outVideoContext, NULL);
    outVideoContext->fmt_ctx = NULL;
    outVideoContext->audio_idx = outVideoContext->video_idx = -1;


    /** FFMPEG로 정보 가져오기 */
    if ((errCode = avformat_open_input(&outVideoContext->fmt_ctx, filename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG](%d)Failed Open FFMPEG File : [%s]\r\n", errCode, filename);
        return -1;
    }

    /** FFMPEG Context에서 Stream 정보 가져오기 */
    if ((errCode = avformat_find_stream_info(outVideoContext->fmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG](%d)Failed Get Stream Information\r\n", errCode);
        return -1;
    }

    /** Stream에 대한 타입 분류 */
    for (idx = 0; idx < outVideoContext->fmt_ctx->nb_streams; ++idx) {
        /** Codec에 대한 Parameter 정보를 가지고 잇느 구조체 생성 */
        AVCodecParameters *pCodecParameters = outVideoContext->fmt_ctx->streams[idx]->codecpar;

        /** Video Type 인 경우 */
        if (pCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO && outVideoContext->video_idx < 0) {
            outVideoContext->video_idx = idx;
        }
            /** Audio Type 인 경우 */
        else if (pCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO && outVideoContext->audio_idx < 0) {
            outVideoContext->audio_idx = idx;
        }
    }
    /** stream을 찾지 못했을 경우 */
    if (outVideoContext->video_idx < 0 && outVideoContext->audio_idx < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed Get Stream Idx...\r\n");
        return -3;
    }
    return 0;
}

void Release(VideoContext *pReadContext, VideoContext *pWriteContext) {
    if (pReadContext != NULL) {
        avformat_close_input(&pReadContext->fmt_ctx);
        pReadContext->video_idx = -1;
        pReadContext->audio_idx = -1;
        pReadContext->fmt_ctx = NULL;
    }
    if (pWriteContext != NULL && pWriteContext->fmt_ctx != NULL) {
        if (!(pWriteContext->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&pWriteContext->fmt_ctx->pb);
        }
        avformat_free_context(pWriteContext->fmt_ctx);
    }
}