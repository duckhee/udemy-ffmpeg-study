#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <stdbool.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <stb_image.h>
#include <stb_image_write.h>

#if defined(WIN32) || defined(WIN64)

#include <Windows.h>

#endif

#define BUFFER_MAX                          1024

#define FFMPEG_ERROR(errorCode, msg)    \
{                                       \
if((errorCode) < (0)){                  \
av_log(NULL, AV_LOG_ERROR, (msg));      \
return -1;                              \
}else{                                  \
}                                       \
}

bool GetResourcePath(const char *name, char *const pathBuffer);


int main(int argc, char **argv) {
    char resourcePath[BUFFER_MAX] = {0};

    AVFormatContext *pAvFormatContext = NULL;
    AVPacket *pAvPacket = NULL;
    AVFrame *pAvFrame = NULL;
    AVCodecParameters *pVideoCodecParameters = NULL;
    AVCodec *pVideoCode = NULL;
    AVCodecContext *pVideoCodecContext = NULL;
    int errorCode = -1;
    int videoStreamChannelIdx = 0;

    if (!GetResourcePath("out.mp4", resourcePath)) {
        printf("Failed Get out.mp4 resource Path...\r\n");
        return -1;
    }

    errorCode = avformat_open_input(&pAvFormatContext, resourcePath, NULL, NULL);
    if (errorCode != 0) {
        printf("Load FFMPEG Open...\r\n");
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] open resource out.mp4\r\n", errorCode);
        return -1;
    }

    /** Get FFMPEG Stream Data Get */
    errorCode = avformat_find_stream_info(pAvFormatContext, NULL);
    if (errorCode < 0) {
        printf("Failed find [%s] Stream Information ...\r\n", resourcePath);
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] find stream [%s]\r\n", errorCode, resourcePath);

        avformat_close_input(&pAvFormatContext);
        return -1;
    }

    /** Allocated packet data in memory */
    /** av_packet_alloc() 함수는 기본 필드 값을 가져와서 메모리에 올려주기 때문에 추후에 packet 의 필드에 접근 시 기본 값을 확인을 할 수 있다. */
    pAvPacket = av_packet_alloc();
    if (pAvPacket == NULL) {
        printf("Failed Load packet data...\r\n");
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] allocating packet [%s]\r\n", errorCode, resourcePath);
        avformat_close_input(&pAvFormatContext);
        return -1;
    }

    /** allocated frame data in memory */
    pAvFrame = av_frame_alloc();
    if (pAvFrame == NULL) {
        printf("Failed Load frame data...\r\n");
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] allocating frame [%s]\r\n", errorCode, resourcePath);
        avformat_close_input(&pAvFormatContext);
        return -1;
    }

    /** stream 갯수를 출력 */
    printf("number of stream : %d\r\n", pAvFormatContext->nb_streams);

    for (int streamIdx = 0; streamIdx < pAvFormatContext->nb_streams; streamIdx++) {
        AVCodecParameters *pCurrentCodecParameter = NULL;
        AVCodec *pCurrentCodec = NULL;
        AVStream *pCurrentStream = NULL;
        /** stream 에 대한 정보 가져오기 */
        pCurrentStream = pAvFormatContext->streams[streamIdx];
        /** stream 에서 codecParameter 가져오기 */
        pCurrentCodecParameter = pCurrentStream->codecpar;
        /** codecParameter 저장되어 있는 codec_id 를 가지고 codec 에 대한 값 가져오기 */
        pCurrentCodec = avcodec_find_decoder(pCurrentCodecParameter->codec_id);
        if (pCurrentCodec == NULL) {
            printf("Failed Get Current Codec ...\r\n");
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] find decoder codec [%s]\r\n", errorCode, resourcePath);
            goto release;
        }

        /** codec 에 대한 정보가 비디오 일 경우 */
        if (pCurrentCodecParameter->codec_type == AVMEDIA_TYPE_VIDEO) {
            printf("Found Video Stream!\r\n");
            videoStreamChannelIdx = streamIdx;
            /** Frame 에 대한 rate 을 가져오기 -> av_q2d는 AVRational 데이터를 double 형태의 값으로 변환을 하는 함수이다. */
            /**
             * stream 에 있는 r_frame_rate 는 실제 frame 에 대한 rate 값을 가져온다.
             * stream 에 있는 avg_frame_rate 는 frame 에 대한 평균 rate 값을 가져온다.
             * */
            double frameRate = av_q2d(pCurrentStream[streamIdx].r_frame_rate);

            pVideoCode = pCurrentCodec;
            pVideoCodecParameters = pCurrentCodecParameter;

            /** 가져온 정보를 확인하기 위한 출력 */
            printf("codec ID : %d\r\nCodec : %s, BitRate : %lld\r\nWidth: %d, Height: %d, FrameRate : %lf fps\r\n",
                   pCurrentCodecParameter->codec_id, pCurrentCodec->name, pCurrentCodecParameter->bit_rate,
                   pCurrentCodecParameter->width,
                   pCurrentCodecParameter->height,
                   frameRate);
        }
            /** codec 에 대한 정보가 오디오 일 경우 */
        else if (pCurrentCodecParameter->codec_type == AVMEDIA_TYPE_AUDIO) {
            printf("Found Audio Stream!\r\n");
        }
            /** codec 에 대한 정보가 서브 타이틀 일 경우 */
        else if (pCurrentCodecParameter->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            printf("Found subtitle Stream!\r\n");
        }
    }

    /** not found video stream error */
    if (videoStreamChannelIdx < 0) {
        av_log(NULL, AV_LOG_ERROR, "Video Stream Found Failed...\r\n");
    }

    release:
    avformat_close_input(&pAvFormatContext);
    av_packet_free(&pAvPacket);
//    av_frame_free(&pAvFrame);
    av_free(pAvFrame);
    return 0;
}


bool GetResourcePath(const char *name, char *const pathBuffer) {
    char executeBuffer[BUFFER_MAX] = {0};
    char *pRemoveStart;
    int removeEndIdx = 0;

#if defined(WIN32) || defined(WIN64)
    GetCurrentDirectory(BUFFER_MAX, executeBuffer);
    pRemoveStart = strstr(executeBuffer, "\\cmake");
#else
    realpath(".", executeBuffer);
    pRemoveStart = strstr(executeBuffer, "/cmake");
#endif

    if (pRemoveStart == NULL) {
        printf("Failed Get Resource Path...\r\n");
        return false;
    }

    removeEndIdx = (pRemoveStart - executeBuffer);

    memcpy(pathBuffer, executeBuffer, sizeof(char) * removeEndIdx);


#if defined(WIN32) || defined(WIN64)
    strcat(pathBuffer, "\\resources\\");
#else
    strcat(pathBuffer, "/resources/");
#endif
    strcat(pathBuffer, name);
    return true;
}
