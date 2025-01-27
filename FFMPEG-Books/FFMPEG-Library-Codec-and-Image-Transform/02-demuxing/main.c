#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <memory.h>

/** ffmpeg library */
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

/**
 * 동영상 파일에서 오디오 stream index 와 영상 stream index 에 대한 값
 * 동영상 파일의 context 에 대한 정보를 담고 있는 구조체
 * */
typedef struct _VideoContext {
    AVFormatContext *fmt_ctx;
    int video_idx;
    int audio_idx;
} VideoContent;

#if defined(WIN32) || defined(WIN64)

#include <Windows.h>

#endif

#define BUFFER_MAX                  1024

bool GetResourcePath(const char *name, char *const pathBuffer);

/** 파일에 대해서 여는 것은 한 번만 있어야 하기 때문에 static 선언 함수 */
static int open_input(const char *filename, VideoContent *outVideoContext) {
    int index = 0;
    int errorCode = 0;
    /** 구조체 초기화 */
    outVideoContext->fmt_ctx = NULL;
    outVideoContext->audio_idx = outVideoContext->video_idx = -1;

    /** FFMPEG 로 파일 열기 */
    if ((errorCode = avformat_open_input(&outVideoContext->fmt_ctx, filename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG](%d)Failed Open FFMPEG : [%s]\r\n", errorCode, filename);
        return -1;
    }

    /** FFMPEG Context에서 Stream 정보 찾아오기 */
    if ((errorCode = avformat_find_stream_info(outVideoContext->fmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG](%d) Failed Get FFMPEG Stream...\r\n", errorCode);
        return -1;
    }

    /** Stream Index Found */
    for (index = 0; index < outVideoContext->fmt_ctx->nb_streams; ++index) {
        /** Codec 에 대한 Parameter 값을 담고 있는 구조체 가져 오기 */
        AVCodecParameters *codec_params = outVideoContext->fmt_ctx->streams[index]->codecpar;
        /** Video Type 일 경우 */
        if (codec_params->codec_type == AVMEDIA_TYPE_VIDEO && outVideoContext->video_idx < 0) {
            outVideoContext->video_idx = index;
        }
            /** Audio Type 일 경우 */
        else if (codec_params->codec_type == AVMEDIA_TYPE_AUDIO && outVideoContext->audio_idx < 0) {
            outVideoContext->audio_idx = index;
        }
    }

    /** stream을 찾지 못했을 경우 */
    if (outVideoContext->video_idx < 0 && outVideoContext->audio_idx < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed Get Stream Idx...\r\n");
        return -3;
    }
    return 0;
}

void Release(VideoContent *pContext);

int main(int argc, char **argv) {
    int ret;
    char resourcePath[BUFFER_MAX] = {0};

    /** Stream과 Context에 대한 정보를 저장을 하이 위한 변수 */
    VideoContent video_cxt;

    /** Codec으로 압축된 스트림 데이터를 저장을 하는데 사용이 되는 자료형이다. */
    AVPacket *pkt = av_packet_alloc();

    if (!GetResourcePath("out.mp4", resourcePath)) {
        printf("Failed Get Resource Path...\r\n");
        return -1;
    }

    if (open_input(resourcePath, &video_cxt) < 0) {
        printf("Failed FFMPEG Open..\r\n");
        return -1;
    }

    while (true) {

        /** context에서 데이터를 읽어서 packet 형태로 만들어 주기 */
        ret = av_read_frame(video_cxt.fmt_ctx, pkt);
        /** Frame이 끝일 경우 */
        if (ret == AVERROR_EOF) {
            printf("End of Frame\r\n");
            break;
        }
        /** packet에 있는 데이터가 비디오 스트림일 경우 */
        if (pkt->stream_index == video_cxt.video_idx) {
            printf("Video Packet!\r\n");
        }
            /** packet에 있는 데이터가 오디오 스트림일 경우 */
        else if (pkt->stream_index == video_cxt.audio_idx) {
            printf("Audio Packet!\r\n");
        }
        /** packet은 데이터를 한 번 읽어주면 다음에 초기화를 해줘야 한다. */
        av_packet_unref(pkt);
    }

    /** 사용한 packet에 대한 메모리에서 해제 */
    av_packet_free(&pkt);
    Release(&video_cxt);
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

void Release(VideoContent *pContext) {
    if (pContext != NULL) {
        avformat_close_input(&pContext->fmt_ctx);
        pContext->video_idx = -1;
        pContext->audio_idx = -1;
    }
}