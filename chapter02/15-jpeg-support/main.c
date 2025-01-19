#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <stdbool.h>
/** FFMPEG */
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <assert.h>

#if defined(WIN32) || defined(WIN64)

#include <Windows.h>

#endif

#define BUFFER_MAX                  1024

/** JPEG 를 사용하기 위한 설정 */
#define STB_IMAGE_IMPLEMENTATION

#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <stb_image_write.h>

#define FFMPEG_ERROR(errorCode, msg) \
{                                    \
if((errorCode) < (0)) {              \
av_log(NULL, AV_LOG_ERROR, (msg));   \
return -1;                           \
}else{                               \
}                                     \
}                                    \

#define REINTERPRET_CAST(type, variable) C_CAST(type, variable)
#define C_CAST(type, variable) ((type)variable)


bool GetResourcePath(const char *name, char *const pathBuffer);

/** Decode video packet get grayScale Image */
int DecodeVideoPacket_GreyFrame(AVPacket *packet, AVCodecContext *codecContext, AVFrame *avFrame);

/** Decode video packet get rgbScale Image */
int DecodeVideoPacket_RGBFrame(AVPacket *packet, AVCodecContext *pCodecContext, AVFrame *pFrame, AVFrame *pRgbFrame,
                               struct SwsContext *pSwsContext);

/** save frame gray scale image */
void SaveGreyFrameToPPM(uint8_t *pixels, int wrap, int imageHeight, int imageWidth, char *filename);

/** save frame color swscale image */
void SaveRGBFrame(unsigned char *buf, int wrap, int ySize, int xSize, char *filename);

/** Save Audio Stream data */
int DecodeAudioPacket(AVPacket *pPacket, AVCodecContext *pAudioCodecContext, AVFrame *pFrame, FILE *pAudioFile);

float ExtractAudioSample(AVCodecContext *pAudioCodecContext, uint8_t *data, int channelNumber);

int main(int argc, char **argv) {
    char resourcePath[BUFFER_MAX] = {0};
    int errorCode = -1;

    AVFormatContext *pFormatContext = NULL;
    AVPacket *pPacket = NULL;
    AVFrame *pFrame = NULL;
    AVFrame *pRGBFrame = NULL;

    AVCodecParameters *pVideoCodecParameters = NULL;
    const AVCodec *pVideoCodec;
    AVCodecContext *pVideoCodecContext = NULL;
    int videoStreamIdx = -1;

    AVCodecParameters *pAudioCodecParameter = NULL;
    const AVCodec *pAudioCodec;
    AVCodecContext *pAudioCodecContext = NULL;
    int audioStreamIdx = -1;

    /** frame image software scale structure */
    struct SwsContext *pSwsContext = NULL;


    char saveFilePath[BUFFER_MAX] = {0};

    if (!GetResourcePath("out.mp4", resourcePath)) {
        printf("Failed Get out.mp4 resource path...\r\n");
        return -1;
    }
    /** save audio file name */
    char audioResourcePath[BUFFER_MAX] = {0};
    char audioFileName[50] = {0};
#if defined(WIN32) || defined(WIN64)
    sprintf(audioFileName, "\\GeneratedAudio\\audioData.raw");
#else
    sprintf(audioFileName, "/GeneratedAudio/audioData.raw");
#endif
    if (!GetResourcePath(audioFileName, audioResourcePath)) {
        printf("Failed Make Audio File Save Path...\r\n");
        goto ffmpeg_release;
    }
    FILE *pAudioFile = NULL;
    pAudioFile = fopen(audioResourcePath, "wb+");
    assert(pAudioFile != NULL);

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
        goto ffmpeg_release;
    }

    /** packet structure memory load */
    pPacket = av_packet_alloc();
    if (pPacket == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load packet structure...\r\n");
        goto ffmpeg_release;
    }

    /** frame structure memory load */
    pFrame = av_frame_alloc();
    if (pFrame == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load frame structure...\r\n");
        goto ffmpeg_release;
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
            goto ffmpeg_release;
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

    /** soft ware scale context get */
    pSwsContext = sws_getContext(pVideoCodecContext->width, pVideoCodecContext->height, pVideoCodecContext->pix_fmt,
                                 pVideoCodecContext->width, pVideoCodecContext->height, AV_PIX_FMT_RGB24, SWS_BILINEAR,
                                 NULL, NULL, NULL);

    /** RGB Frame Structure memory load */
    pRGBFrame = av_frame_alloc();
    if (pRGBFrame == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load frame structure...\r\n");
        goto ffmpeg_release;
    }

    /** rgb channel data type defined -> get image size */
    int rgbFrameNumberOfBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pVideoCodecContext->width,
                                                         pVideoCodecContext->height, 1);
    /** get RGB Frame Buffer -> allocated image size buffer */
    uint8_t *rgbFrameBuffer = av_malloc(rgbFrameNumberOfBytes);
    /** frame buffer set image pixel */
    errorCode = av_image_fill_arrays(pRGBFrame->data, pRGBFrame->linesize, (uint8_t *) rgbFrameBuffer,
                                     AV_PIX_FMT_BGR24,
                                     pVideoCodecContext->width, pVideoCodecContext->height, 1);

    /** make color frame */
//    DecodeVideoPacket_RGBFrame(pPacket, pVideoCodecContext, pFrame, pRGBFrame, pSwsContext);
//    av_frame_unref(pRGBFrame);
//    av_free(pRGBFrameBuffer);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG_ERROR](%d) RGB Image Copy Buffer Failed...\r\n", errorCode);
    }

    /** setting rgb frame size */
    pRGBFrame->width = pVideoCodecContext->width;
    pRGBFrame->height = pVideoCodecContext->height;

    int packetCount = 0;

    /** Read Frame */
    while (av_read_frame(pFormatContext, pPacket) >= 0) {
        /** video frame read  */
        if (pPacket->stream_index == videoStreamIdx) {
//            printf("Found Video Frame Packet!\r\n");
            DecodeVideoPacket_GreyFrame(pPacket, pVideoCodecContext, pFrame);
            DecodeVideoPacket_RGBFrame(pPacket, pVideoCodecContext, pFrame, pRGBFrame, pSwsContext);
        }
            /** audio frame read */
        else if (pPacket->stream_index == audioStreamIdx) {
//            printf("Found Audio Frame Packet!\r\n");

            /** Decoding Audio File */
            DecodeAudioPacket(pPacket, pAudioCodecContext, pFrame, pAudioFile);

        }
        /** reference free */
        av_packet_unref(pPacket);
        packetCount++;

        /** 20 Frame Image and audio data */
        if (packetCount == 200) {
//            break;
        }
    }


    printf("Read Video Done!\r\n");

    ffmpeg_release:
    /** software scale context remove */
    sws_freeContext(pSwsContext);
    /** release resource */
    av_frame_free(&pFrame);
    av_frame_free(&pRGBFrame);
    av_packet_free(&pPacket);

    avcodec_free_context(&pVideoCodecContext);
    avcodec_free_context(&pAudioCodecContext);

    avformat_close_input(&pFormatContext);
    fclose(pAudioFile);
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


/** Decode video packet get grayScale Image */
int DecodeVideoPacket_GreyFrame(AVPacket *packet, AVCodecContext *codecContext, AVFrame *avFrame) {
    int result = 0;

    result = avcodec_send_packet(codecContext, packet);
    if (result < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG_ERROR](%d)Sending packet to decoder\r\n", result);
        return result;
    }

    while (result >= 0) {
        /** receive decompressed frame */
        result = avcodec_receive_frame(codecContext, avFrame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            av_frame_unref(avFrame);
            break;
        }
            /** receive frame error */
        else if (result < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG_ERROR](%d) Receive Frame Failed...\r\n", result);
            return result;
        }
            /** receive frame */
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
                   avFrame->pkt_dts
            );
            /** save file */
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

    return result;
}


/** Decode video packet get rgbScale Image */
int DecodeVideoPacket_RGBFrame(AVPacket *packet, AVCodecContext *pCodecContext, AVFrame *pFrame, AVFrame *pRgbFrame,
                               struct SwsContext *pSwsContext) {
    int result = 0;
    /** send decompressed packet for decompression */
    result = avcodec_send_packet(pCodecContext, packet);
    if (result < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG](%d)code context send to packet Error ...\r\n", result);
        return result;
    }


    while (result >= 0) {
        /** receive decompressed frame */
        result = avcodec_receive_frame(pCodecContext, pFrame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            av_frame_unref(pFrame);
            break;
        }
            /** receive frame error */
        else if (result < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG_ERROR](%d) Receive Frame Failed...\r\n", result);
        }
            /** receive frame */
        else {
            /** we have a picture */
            printf("Frame number %lld (type = %c frame, size = %dbytes, width=%d, height=%d) pts %lld key_frame %d [DTS %lld]\r\n",
                   pCodecContext->frame_num,
                   av_get_picture_type_char(pFrame->pict_type),
                   packet->size,
                   pFrame->width,
                   pFrame->height,
                   pFrame->pts,
                   pFrame->key_frame,
                   pFrame->pkt_dts
            );
            /** save file */
            char savePathBuffer[BUFFER_MAX] = {0};
            char fileNameBuffer[40] = {0,};
#if defined(WIN32) || defined(WIN64)
            sprintf(fileNameBuffer, "\\GeneratedColorImage\\testColorPPM.ppm");
#else
            sprintf(fileNameBuffer, "/GeneratedColorImage/color.ppm");
#endif
            if (!GetResourcePath(fileNameBuffer, savePathBuffer)) {
                printf("Failed Get Resource Path...\r\n");
            }
            /** 영상의 프레임을 인자로 받아서 해당 프레임을 나눠서 이미지를 스케일링을 시켜준다. */
            result = sws_scale(pSwsContext, (unsigned char const *const *) (pFrame->data), pFrame->linesize, 0,
                               pCodecContext->height, pRgbFrame->data, pRgbFrame->linesize);
            if (result < 0) {
                av_log(NULL, AV_LOG_ERROR, "[FFMPEG](%d)software scale Failed...\r\n", result);
//                av_frame_unref(pFrame);
//                av_frame_free(&pFrame);
                return result;
            }

            /** Save Color Frame */
            SaveRGBFrame(pRgbFrame->data[0], pRgbFrame->linesize[0], pRgbFrame->height, pRgbFrame->width,
                         savePathBuffer);
        }
    }

    return result;
}

/** save frame gray scale image */
void SaveGreyFrameToPPM(uint8_t *pixels, int wrap, int imageHeight, int imageWidth, char *filename) {

    FILE *pFile = fopen(filename, "w");
//    fopen_s(&pFile, filename, "w");

    if (pFile == NULL) {
        return;
    }
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
    /** compiles slow */
    int colorChannel = 0;
    char jpegFilePath[BUFFER_MAX] = {0};
    char savedFileName[50] = {0};
#if defined(WIN32) || defined(WIN64)
    sprintf(savedFileName, "GeneratedGrayImage\\stbi_jpeg_file.jpeg");
#else
    sprintf(savedFileName, "GeneratedGrayImage/stbi_jpeg_file.jpeg");
#endif
    if (!GetResourcePath(savedFileName, jpegFilePath)) {
        printf("Failed resource path...\r\n");
        return;
    }
    /** stb write jpeg file -> Load PPM File */
    unsigned char *image = stbi_load(filename, &imageWidth, &imageHeight, &colorChannel, 0);
    /** write image file */
    stbi_write_jpg(jpegFilePath, imageWidth, imageHeight, colorChannel, image, 80);
    stbi_image_free(image);
}

/** save frame color swscale image */
void SaveRGBFrame(unsigned char *buf, int wrap, int ySize, int xSize, char *filename) {
    FILE *pFile;

    pFile = fopen(filename, "wb");

    if (pFile == NULL) {
        printf("Failed file open ... %s\r\n", filename);
        return;
    }
    assert(pFile != NULL);

    /** file header information setting color */
    fprintf(pFile, "P6\n%d %d\n%d\n", xSize, ySize, 255);
    printf("write");
    /** write frame data */
    for (int i = 0; i < ySize; i++) {
        unsigned char *ch = (buf + i * wrap);
        fwrite(ch, 1, xSize * 3, pFile);
    }
    fclose(pFile);

    /** compiles slow */
    int colorChannel = 0;
    char jpegFilePath[BUFFER_MAX] = {0};
    char savedFileName[50] = {0};
#if defined(WIN32) || defined(WIN64)
    sprintf(savedFileName, "GeneratedColorImage\\stbi_jpeg_file.jpeg");
#else
    sprintf(savedFileName, "GeneratedColorImage/stbi_jpeg_file.jpeg");
#endif
    if (!GetResourcePath(savedFileName, jpegFilePath)) {
        printf("Failed resource path...\r\n");
        return;
    }
    /** stb write jpeg file -> Load PPM File */
    unsigned char *image = stbi_load(filename, &xSize, &ySize, &colorChannel, 0);
    /** write image file */
    stbi_write_jpg(jpegFilePath, xSize, ySize, colorChannel, image, 80);
    stbi_image_free(image);
}


int DecodeAudioPacket(AVPacket *pPacket, AVCodecContext *pAudioCodecContext, AVFrame *pFrame, FILE *pAudioFile) {
    int returnValue = 0;
    /** get audio data packet */
    returnValue = avcodec_send_packet(pAudioCodecContext, pPacket);
    if (returnValue < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG_ERROR](%d) sending packet to audio decode\r\n", returnValue);
    }
    /** audio data get packet */
    while (returnValue >= 0) {
        returnValue = avcodec_receive_frame(pAudioCodecContext, pFrame);
        if (returnValue == AVERROR(EAGAIN) || returnValue == AVERROR_EOF) {
            av_frame_unref(pFrame);
            break;
        } else if (returnValue < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG_ERROR](%d)Receive audio stream ...\r\n", returnValue);
        } else {
            /** we have a audio */
            printf("Frame number %lld (Samples = %d frame, size = %dbytes(%dbytes), Channels = %d) pts %lld key_frame %d [%s]\r\n",
                   pAudioCodecContext->frame_num,
                   pFrame->nb_samples,
                   pFrame->pkt_size,
                   pPacket->size,
                   pFrame->ch_layout.nb_channels,
                   pFrame->pts,
                   pFrame->key_frame,
                   av_get_sample_fmt_name(pAudioCodecContext->sample_fmt)
            );

            /** audio data size get */
            int audioDataSize = av_get_bytes_per_sample(pAudioCodecContext->sample_fmt);
            if (av_sample_fmt_is_planar(pAudioCodecContext->sample_fmt) == 1) {
                for (int i = 0; i < pFrame->nb_samples; i++) {
                    for (int j = 0; j < pAudioCodecContext->ch_layout.nb_channels; j++) {
                        float sample = ExtractAudioSample(pAudioCodecContext, pFrame->extended_data[j], i);
                        fwrite(&sample, sizeof(float), 1, pAudioFile);
                    }
                }
            }

        }
    }

    return returnValue;
}

float ExtractAudioSample(AVCodecContext *codecCtx, uint8_t *buffer, int sampleIndex) {
    int64_t val = 0;
    float ret = 0;
    int sampleSize = av_get_bytes_per_sample(codecCtx->sample_fmt);
    switch (sampleSize) {
        case 1:
            // 8bit samples are always unsigned
            val = ((uint8_t *) buffer)[sampleIndex];
            // make signeda
            val -= 127;
            break;
        case 2:
            val = ((int16_t *) buffer)[sampleIndex];
            break;

        case 4:
            val = ((int32_t *) buffer)[sampleIndex];
            break;

        case 8:
            val = ((int64_t *) buffer)[sampleIndex];
            break;
        default:
            fprintf(stderr, "Invalid sample size %d.\n", sampleSize);
            return 0;
    }

    switch (codecCtx->sample_fmt) {
        case AV_SAMPLE_FMT_U8:
        case AV_SAMPLE_FMT_S16:
        case AV_SAMPLE_FMT_S32:
        case AV_SAMPLE_FMT_U8P:
        case AV_SAMPLE_FMT_S16P:
        case AV_SAMPLE_FMT_S32P:
            // integer => Scale to [-1, 1] and convert to float.
            ret = val / ((float) ((1 << (sampleSize * 8 - 1)) - 1));
            break;

        case AV_SAMPLE_FMT_FLT:
        case AV_SAMPLE_FMT_FLTP:
            // float => reinterpret
            ret = *REINTERPRET_CAST(float*, &val);
            break;

        case AV_SAMPLE_FMT_DBL:
        case AV_SAMPLE_FMT_DBLP:
            // double => reinterpret and then static cast down
            ret = *((float *) &val);
            break;

        default:
            fprintf(stderr, "Invalid sample format %s.\n", av_get_sample_fmt_name(codecCtx->sample_fmt));
            return 0;
    }
    return ret;
}