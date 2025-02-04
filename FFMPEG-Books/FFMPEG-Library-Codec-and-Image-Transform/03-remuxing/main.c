#include <libavformat/avformat.h>
#include <libavcodec/codec.h>
#include <libavcodec/avcodec.h>
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

int create_output(const char *filename, const VideoContext *pVideoContext, VideoContext *outputContext);

void Release(VideoContext *pContext, VideoContext *pWriteContext);

int main(int argc, char **argv) {
    int ret;
    char resourcePath[BUFFER_MAX] = {0};
    char savePath[BUFFER_MAX] = {0};

    int outputStreamIdx;

    /** Stream과 Context에 대한 정보를 저장을 하이 위한 변수 */
    VideoContext video_cxt;
    VideoContext copy_cxt;

    /** Codec으로 압축된 스트림 데이터를 저장을 하는데 사용이 되는 자료형이다. */
    AVPacket *pkt = av_packet_alloc();

    if (!GetResourcePath("out.mp4", resourcePath)) {
        printf("Failed Get Resource Path...\r\n");
        return -1;
    }
    if (!GetResourcePath("saveOutput.mp4", savePath)) {
        printf("Failed Get Resource Path...\r\n");
        return -1;
    }

    if (open_input(resourcePath, &video_cxt) < 0) {
        printf("Failed FFMPEG Open..\r\n");
        goto RELEASE;
    }
    /** 복제할 파일 열기 */
    if (create_output(savePath, &video_cxt, &copy_cxt) < 0) {
        printf("Get Save File Path Failed...\r\n");
        goto RELEASE;
    }

    /** ouput에 대한 dump 출력 */
    av_dump_format(copy_cxt.fmt_ctx, 0, copy_cxt.fmt_ctx->url, 1);

    while (true) {

        /** context에서 데이터를 읽어서 packet 형태로 만들어 주기 */
        ret = av_read_frame(video_cxt.fmt_ctx, pkt);
        /** Frame이 끝일 경우 */
        if (ret == AVERROR_EOF) {
            printf("End of Frame\r\n");
            break;
        }

        /** video channel 이거나 audio channel 이 아닌 경우 건너 뛴다. */
        if (pkt->stream_index != video_cxt.video_idx && pkt->stream_index != video_cxt.audio_idx) {
            /** packet은 데이터를 한 번 읽어주면 다음에 초기화를 해줘야 한다. */
            av_packet_unref(pkt);
            continue;
        }
        /** 해당 Packet에 대한 Stream을 가져온다. */
        AVStream *pInStream = video_cxt.fmt_ctx->streams[pkt->stream_index];
        /** 출력할 스트림의 종류 가져오기 */
        outputStreamIdx = (pkt->stream_index == video_cxt.video_idx) ? copy_cxt.video_idx : copy_cxt.audio_idx;
        /** output stream 가져오기 */
        AVStream *pOutStream = copy_cxt.fmt_ctx->streams[outputStreamIdx];
        /** 시간 정보를 변경을 해준다. */
        av_packet_rescale_ts(pkt, pInStream->time_base, pOutStream->time_base);
        /** packet에 있는 StreamIdx에 대한 값을 변경을 해준다. -> 변경을 해주지 않아도 동작한다. */
        pkt->stream_index = outputStreamIdx;
        /** file에 데이터 쓰기 */
        if ((ret = av_interleaved_write_frame(copy_cxt.fmt_ctx, pkt)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG](%d)Failed to write packet into file...\r\n", ret);
            break;
        }

        /** packet은 데이터를 한 번 읽어주면 다음에 초기화를 해줘야 한다. */
        av_packet_unref(pkt);
    }
    /** 파일에 쓰는 시점에 마무리 못한 정보를 처리 */
    av_write_trailer(copy_cxt.fmt_ctx);

    /** 사용한 packet에 대한 메모리에서 해제 */
    av_packet_free(&pkt);

    RELEASE:
    Release(&video_cxt, &copy_cxt);
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

int create_output(const char *filename, const VideoContext *pVideoContext, VideoContext *outputContext) {
    /** stream에 대한 Index 가져오기 위한 변수 */
    int streamIdx;
    int out_idx = 0;
    int errCode = 0;


    outputContext->fmt_ctx = NULL;
    outputContext->video_idx = outputContext->audio_idx = -1;

    /** 동영상을 만들어주기 위한 파일 열기 */
    if ((errCode = avformat_alloc_output_context2(&outputContext->fmt_ctx, NULL, NULL, filename)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG](%d) Output File Create Failed... [%s]\r\n", errCode, filename);
        return -1;
    }
    av_dump_format(outputContext->fmt_ctx, 0, filename, 1);

    for (streamIdx = 0; streamIdx < pVideoContext->fmt_ctx->nb_streams; ++streamIdx) {
        /** 입력 AVForatmContext의 비디오 채널과 오디오 채널이 아닌 경우 */
        if (streamIdx != pVideoContext->video_idx && streamIdx != pVideoContext->audio_idx) {
            continue;
        }

        AVStream *pInStream = pVideoContext->fmt_ctx->streams[streamIdx];
        AVCodecParameters *pInCodecContext = pInStream->codecpar;
        /** Codec에 대한 정보를 codec_id를 통해서 가져온다. */
        const AVCodec *pCodec = avcodec_find_decoder(pInCodecContext->codec_id);
        if (pCodec == NULL) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG]Failed to get decode...\r\n");
            return -2;
        }
        /** 새로 만들어주는 Stream 동영상 파일을 만들기 위해서 사용이 되는 스트림이다. */
        AVStream *pOutStream = avformat_new_stream(outputContext->fmt_ctx, pCodec);
        if (pOutStream == NULL) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG]Failed to Create Stream...\r\n");
            return -2;
        }

        /** Straem에 Codec에 대한 파라미터 정보를 복사해준다. */
        if ((errCode = avcodec_parameters_copy(pOutStream->codecpar, pInStream->codecpar)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG](%d)Failed to copy codec parameter\r\n", errCode);
            return -2;
        }

        /** Codec에 대한 Context를 만들어 준다. */
        AVCodecContext *pOutputCodecContext = avcodec_alloc_context3(pCodec);
        if (pOutputCodecContext == NULL) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG]Failed to Create Codec Context...\r\n");
            return -2;
        }

        /** 시간 정보를 복사를 해준다. */
        pOutStream->time_base = pInStream->time_base;

        /** Codec에 대한 Meta 정보를 삭제 -> FFMPEG에서 지원하는 코덱과 호환성을 맞추기 위해서 삭제를 한다. */
        pOutputCodecContext->codec_tag = 0;
        /** Codec Context에 대한 Header 설정 */
        if (outputContext->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            pOutputCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (streamIdx == pVideoContext->video_idx) {
            outputContext->video_idx = out_idx++;
        } else {
            outputContext->audio_idx = out_idx++;
        }
        avcodec_free_context(&pOutputCodecContext);
    }

    /** 파일에 동영상 복제 */
    if (!(outputContext->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        /** 파일에 데이터를 쓰기 위한 파일 열기 */
        if ((errCode = avio_open(&outputContext->fmt_ctx->pb, filename, AVIO_FLAG_WRITE)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG](%d)Failed to create output file ... [%s]\r\n", errCode, filename);
            return -4;
        }
    }


    /** Container 헤더 파일 작성 */
    if ((errCode = avformat_write_header(outputContext->fmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG](%d)Failed writing header into output file...\r\n", errCode);
        return -5;
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