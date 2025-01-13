#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
// FFMPEG Library
#include <libavformat/avformat.h>
#include <libavcodec/codec.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libswscale/swscale.h>
#include <stb_image.h>
#include <stb_image_write.h>
#include <assert.h>

#if defined(WIN32) || defined(WIN64)

#include <Windows.h>

#endif

#define BUFFER_MAX                  1024

#define FFMPEG_ERROR(errorCode, msg) \
{                                    \
if((errorCode) < (0)) {              \
av_log(NULL, AV_LOG_ERROR, (msg));   \
return -1;                           \
}else{                               \
}                                     \
}                                    \


bool GetResourcePath(const char *name, char *const pathBuffer);

/** Decode video packet get grayScale Image */
int DecodeVideoPacket_GreyFrame(AVPacket *packet, AVCodecContext *codecContext, AVFrame *avFrame);

/** save frame gray scale image */
void SaveGreyFrameToPPM(uint8_t *pixels, int wrap, int imageHeight, int imageWidth, char *filename);

int main(int argc, char **argv) {
    char resourcePath[BUFFER_MAX] = {0};
    int errorCode = -1;

    AVFormatContext *pFormatContext = NULL;
    AVPacket *pPacket = NULL;
    AVFrame *pFrame = NULL;

    AVCodecParameters *pVideoCodecParameters = NULL;
    const AVCodec *pVideoCodec;
    AVCodecContext *pVideoCodecContext = NULL;
    int videoStreamIdx = 0;

    AVCodecParameters *pAudioCodecParameter = NULL;
    const AVCodec *pAudioCodec;
    AVCodecContext *pAudioCodecContext = NULL;
    int audioStreamIdx = 0;

    char saveFilePath[BUFFER_MAX] = {0};

    if (!GetResourcePath("out.mp4", resourcePath)) {
        printf("Failed Get out.mp4 resource path...\r\n");
        return -1;
    }


    printf("resource path - %s\r\n", resourcePath);
    /** ffmpeg file open */
    errorCode = avformat_open_input(&pFormatContext, resourcePath, NULL, NULL);
    if (errorCode != 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) FFMPEG Open Failed...\r\n", errorCode);
        return -1;
    }

    /** get stream information */
    errorCode = avformat_find_stream_info(pFormatContext, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) find Stream Failed...\r\n", errorCode);
        avformat_close_input(&pFormatContext);
        return -1;
    }

    /** packet structure memory load */
    pPacket = av_packet_alloc();
    if (pPacket == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load packet structure...\r\n");
        avformat_close_input(&pFormatContext);
        return -1;
    }

    /** frame structure memory load */
    pFrame = av_frame_alloc();
    if (pFrame == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load frame structure...\r\n");
        av_packet_free(&pPacket);
        avformat_close_input(&pFormatContext);
        return -1;
    }

    printf("number of stream : %d\r\n", pFormatContext->nb_streams);

    for (int idx = 0; idx < pFormatContext->nb_streams; ++idx) {
        AVCodecParameters *pCurCodecParameter = NULL;
        const AVCodec *pCurCodec;
        AVStream *pCurStream = NULL;

        /** found current stream */
        pCurStream = pFormatContext->streams[idx];
        /** get codec parameter in stream structure */
        pCurCodecParameter = pCurStream->codecpar;
        /** get codec using codec_id */
        pCurCodec = avcodec_find_decoder(pCurCodecParameter->codec_id);
        if (pCurCodec == NULL) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG](StreamIdx : %d) find Codec Error...\r\n", idx);
            av_packet_free(&pPacket);
            av_frame_free(&pFrame);
            avformat_close_input(&pFormatContext);
            return -1;
        }

        /** video stream */
        if (pCurCodecParameter->codec_type == AVMEDIA_TYPE_VIDEO) {
            printf("Found Video Stream!\r\n");
            videoStreamIdx = idx;
            /** Frame 에 대한 rate 을 가져오기 -> av_q2d는 AVRational 데이터를 double 형태의 값으로 변환을 하는 함수이다. */
            /**
             * stream 에 있는 r_frame_rate 는 실제 frame 에 대한 rate 값을 가져온다.
             * stream 에 있는 avg_frame_rate 는 frame 에 대한 평균 rate 값을 가져온다.
             * */
            double frameRate = av_q2d(pCurStream[idx].r_frame_rate);

            pVideoCodec = pCurCodec;
            pVideoCodecParameters = pCurCodecParameter;

            /** 가져온 정보를 확인하기 위한 출력 */
            printf("codec ID : %d\r\nCodec : %s, BitRate : %lld\r\nWidth: %d, Height: %d, FrameRate : %lf fps\r\n",
                   pCurCodecParameter->codec_id, pCurCodec->name, pCurCodecParameter->bit_rate,
                   pCurCodecParameter->width,
                   pCurCodecParameter->height,
                   frameRate);
        }
            /** audio stream */
        else if (pCurCodecParameter->codec_type == AVMEDIA_TYPE_AUDIO) {
            printf("Found Audio Stream!\r\n");
            audioStreamIdx = idx;
            pAudioCodec = pCurCodec;
            pAudioCodecParameter = pCurStream->codecpar;
            /** Audio에 대한 정보 출력 */
            printf("ID : %d\r\nCodec : %s, BitRate : %lld\r\nChannel : %d, SampleRate : %d\r\n",
                   pCurCodecParameter->codec_id, pCurCodec->name, pCurCodecParameter->bit_rate,
                   idx,
                   pCurCodecParameter->sample_rate
            );
        }
            /** sub-title stream */
        else if (pCurCodecParameter->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            printf("Found subtitle Stream!\r\n");
        }
    }

    /** check video stream */
    if (videoStreamIdx < 0) {
        av_log(NULL, AV_LOG_ERROR, "Video Stream Found Failed...\r\n");
        goto ffmpeg_release;
    }

    /** video stream decoding codec context memory allocated */
    pVideoCodecContext = avcodec_alloc_context3(pVideoCodec);
    if (pVideoCodecContext == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Get Video Stream Codec Context Load Failed...\r\n");
        goto ffmpeg_release;
    }

    /** CodecParameters information copy to CodecContext */
    errorCode = avcodec_parameters_to_context(pVideoCodecContext, pVideoCodecParameters);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed codec parameter copy to context...\r\n", errorCode);
        goto ffmpeg_release;
    }

    /** Codec Open */
    errorCode = avcodec_open2(pVideoCodecContext, pVideoCodec, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Open Video Codec...\r\n", errorCode);
        goto ffmpeg_release;
    }


    /** check audio stream */
    if (audioStreamIdx < 0) {
        av_log(NULL, AV_LOG_ERROR, "Audio Stream Found Failed...\r\n");
        goto ffmpeg_release;
    }

    /** audio stream decoding codec context memory allocated */
    pAudioCodecContext = avcodec_alloc_context3(pAudioCodec);
    if (pAudioCodecContext == NULL) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR]Failed Load Audio Context...\r\n");
        goto ffmpeg_release;
    }

    /** CodecParameters information copy to CodecContext */
    errorCode = avcodec_parameters_to_context(pAudioCodecContext, pAudioCodecParameter);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Load Audio Codec...\r\n", errorCode);
        goto ffmpeg_release;
    }

    /** Audio Codec Open */
    errorCode = avcodec_open2(pAudioCodecContext, pAudioCodec, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d)Failed Open Audio Codec\r\n", errorCode);
        goto ffmpeg_release;
    }

    int packetCount = 0;

    /** Read Frame */
    while (av_read_frame(pFormatContext, pPacket) >= 0) {
        /** video frame read  */
        if (pPacket->stream_index == videoStreamIdx) {
//            printf("Found Video Frame Packet!\r\n");
            DecodeVideoPacket_GreyFrame(pPacket, pVideoCodecContext, pFrame);
        }
            /** audio frame read */
        else if (pPacket->stream_index == audioStreamIdx) {
//            printf("Found Audio Frame Packet!\r\n");
        }
        /** reference free */
        av_packet_unref(pPacket);
        packetCount++;

        /** 20 Frame Image and audio data */
        if (packetCount == 20) {
            break;
        }
    }

    printf("Read Video Done!\r\n");

    ffmpeg_release:
    /** release resource */
    av_frame_free(&pFrame);
    av_packet_free(&pPacket);
    avformat_close_input(&pFormatContext);
    return 0;
}

bool GetResourcePath(const char *name, char *const pathBuffer) {
    char executeBuffer[BUFFER_MAX] = {0};
    char *pRemoveStart = NULL;
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

    removeEndIdx = (int) (pRemoveStart - executeBuffer);
    memcpy(pathBuffer, executeBuffer, sizeof(char) * removeEndIdx);

#if defined(WIN32) || defined(WIN64)
    strcat(pathBuffer, "\\resources\\");
#else
    strcat(pathBuffer, "/resources/");
#endif
    strcat(pathBuffer, name);
    return true;
}

/** 비디오에서는 패킷당 한 장면만 읽어온다. */
/** Decode video packet get grayScale Image */
int DecodeVideoPacket_GreyFrame(AVPacket *packet, AVCodecContext *codecContext, AVFrame *avFrame) {
    int returnValue = 0;
    /** Send Compressed packet for decompression */
    returnValue = avcodec_send_packet(codecContext, packet);
    if (returnValue < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Sending packet to dcoder\r\n", returnValue);
    }

    while (returnValue >= 0) {
        /** Receive decompressed frame */
        returnValue = avcodec_receive_frame(codecContext, avFrame);
        /** get failed frame status and end */
        if (returnValue == AVERROR(EAGAIN) || returnValue == AVERROR_EOF) {
            av_frame_unref(avFrame);
            break;
        }
            /** Error receive frame */
        else if (returnValue < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Receive frame\r\n", returnValue);
        }
            /** image data get */
        else {

            /** we have a picture */
            printf("Frame number %lld (type = %c frame, size = %dbytes, width=%d, height=%d) pts %lld key_frame %d [DTS %lld]\r\n",
                   codecContext->frame_num,
                   av_get_picture_type_char(avFrame->pict_type),
                   packet->size,
                   avFrame->width,
                   avFrame->height,
                   avFrame->pts,
                   avFrame->key_frame,
//                   avFrame->flags,
//                   avFrame->pict_type, // key frame is I Frame
//                   packet->dts
                   avFrame->pkt_dts
            );
            char savePathBuffer[BUFFER_MAX] = {0};
            char fileNameBuffer[40] = {0,};
#if defined(WIN32) || defined(WIN64)
            sprintf(fileNameBuffer, "\\GeneratedGrayImage\\testPPM.ppm");
#else
            sprintf(fileNameBuffer, "/GeneratedGrayImage/testPPM.ppm");
#endif
            if (!GetResourcePath(fileNameBuffer, savePathBuffer)) {
                printf("Failed Get Resource Path...\r\n");
            }
            /** Turn Frame into ppm */
            SaveGreyFrameToPPM(avFrame->data[0], avFrame->linesize[0], avFrame->height, avFrame->width, savePathBuffer);
        }

    }

    return returnValue;
}


void SaveGreyFrameToPPM(uint8_t *pixels, int wrap, int imageHeight, int imageWidth, char *filename) {

    FILE *pFile = fopen(filename, "w");
//    fopen_s(&pFile, filename, "w");
    assert(pFile != NULL);

    /** write file PPM File Header */
    fprintf(pFile, "P5\n%d %d\n%d\n", imageWidth, imageHeight, 255);

    /** write image data */
    for (int i = 0; i < imageHeight; i++) {
        unsigned char *ch = (pixels + i * wrap);
        /** write Data using PPM File format */
        fwrite(ch, sizeof(char), imageWidth, pFile);
    }

    fclose(pFile);
}
