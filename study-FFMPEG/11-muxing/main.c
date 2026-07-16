#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>

#define BUFFER_MAX                  1024
/** 절대 경로 해석용 버퍼 크기 (Linux PATH_MAX=4096 대응) */
#define RESOURCE_PATH_MAX           4096
/** 출력 영상 사양 */
#define OUTPUT_DURATION_SEC         5
#define VIDEO_WIDTH                 640
#define VIDEO_HEIGHT                360
#define VIDEO_FPS                   25
#define AUDIO_SAMPLE_RATE           44100
#define SINE_FREQUENCY              440.0
#ifndef M_PI
#define M_PI                        3.14159265358979323846
#endif

#define FFMPEG_ERROR(errorCode, msg) \
{                                    \
if((errorCode) < (0)) {              \
av_log(NULL, AV_LOG_ERROR, (msg));   \
return -1;                           \
}else{                               \
}                                     \
}                                    \


/** 출력 스트림 하나(인코더 + 스트림 + 재사용 프레임)를 묶은 구조체 */
typedef struct StudyOutputStream {
    AVStream *pStream;
    AVCodecContext *pEncoderContext;
    AVFrame *pFrame;
    /** 다음에 인코딩할 프레임의 pts (각 인코더 time_base 단위) */
    int64_t nextPts;
    /** 오디오 사인파 위상 */
    double sinePhase;
} StudyOutputStream;

bool GetResourcePath(const char *name, char *const pathBuffer);

bool EnsureGeneratedStudyDirectory(void);

/** 비디오 인코더(libx264 또는 mpeg4) + 출력 스트림 준비 */
bool OpenVideoStream(AVFormatContext *pOutputContext, StudyOutputStream *pVideoStream);

/** AAC 오디오 인코더 + 출력 스트림 준비 */
bool OpenAudioStream(AVFormatContext *pOutputContext, StudyOutputStream *pAudioStream);

/** 다음 비디오 프레임 생성(합성 그라데이션). 목표 길이에 도달하면 false */
bool MakeNextVideoFrame(StudyOutputStream *pVideoStream);

/** 다음 오디오 프레임 생성(사인파). 목표 길이에 도달하면 false */
bool MakeNextAudioFrame(StudyOutputStream *pAudioStream);

/** 프레임(NULL이면 flush)을 인코딩해 먹서로 쓴다 */
int EncodeAndMux(StudyOutputStream *pOutputStream, AVFormatContext *pOutputContext,
                 AVFrame *pFrame, AVPacket *pPacket);

/** 출력 스트림 리소스 해제 */
void CloseOutputStream(StudyOutputStream *pOutputStream);

/**
 * study-FFMPEG 11 — 먹싱 (비디오 + 오디오 → mp4)
 *
 * 08(비디오 인코딩)과 09(오디오 인코딩)를 합쳐
 * 두 스트림을 가진 완전한 mp4 파일을 만든다.
 *
 * 새로 배우는 것:
 *   1) 스트림 두 개를 한 컨테이너에 등록하는 방법
 *   2) av_compare_ts로 비디오/오디오 인코딩 순서를 결정해
 *      두 스트림이 시간순으로 고르게 섞이게(인터리빙) 하는 방법
 *   3) mp4처럼 글로벌 헤더가 필요한 컨테이너의 AV_CODEC_FLAG_GLOBAL_HEADER 처리
 */
int main(int argc, char **argv) {
    char outputPath[BUFFER_MAX] = {0};
    int errorCode = -1;
    /** 프로그램 종료 코드: 성공 경로 끝에서만 0으로 바뀐다 */
    int exitStatus = -1;

    AVFormatContext *pOutputContext = NULL;
    AVPacket *pPacket = NULL;

    StudyOutputStream videoStream = {0};
    StudyOutputStream audioStream = {0};
    bool videoDone = false;
    bool audioDone = false;
    int writtenPacketCount = 0;

    if (!EnsureGeneratedStudyDirectory()) {
        printf("Failed Create GeneratedStudy directory...\r\n");
        return -1;
    }
    if (!GetResourcePath("GeneratedStudy/study-muxed.mp4", outputPath)) {
        return -1;
    }
    printf("output : %s\r\n", outputPath);

    /** mp4 출력 컨텍스트 생성 (확장자로 포맷 추론) */
    errorCode = avformat_alloc_output_context2(&pOutputContext, NULL, NULL, outputPath);
    if (errorCode < 0 || pOutputContext == NULL) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Allocate Output Context...\r\n", errorCode);
        return -1;
    }

    /** 비디오/오디오 스트림 + 인코더 준비 */
    if (!OpenVideoStream(pOutputContext, &videoStream)) {
        goto ffmpeg_release;
    }
    if (!OpenAudioStream(pOutputContext, &audioStream)) {
        goto ffmpeg_release;
    }

    /** 출력 파일 열기 + 헤더 쓰기 */
    errorCode = avio_open(&pOutputContext->pb, outputPath, AVIO_FLAG_WRITE);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Open Output File...\r\n", errorCode);
        goto ffmpeg_release;
    }
    errorCode = avformat_write_header(pOutputContext, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Write Header...\r\n", errorCode);
        goto ffmpeg_release;
    }

    pPacket = av_packet_alloc();
    if (pPacket == NULL) {
        goto ffmpeg_release;
    }

    /**
     * ===== 인터리빙 인코딩 루프 =====
     * 비디오와 오디오 중 "다음 pts가 더 이른 쪽"을 먼저 인코딩한다.
     * av_compare_ts는 서로 다른 time_base의 타임스탬프를 비교해 준다.
     * (비디오 pts는 1/25초 단위, 오디오 pts는 1/44100초 단위)
     */
    while (!videoDone || !audioDone) {
        bool encodeVideoNext;

        if (videoDone) {
            encodeVideoNext = false;
        } else if (audioDone) {
            encodeVideoNext = true;
        } else {
            encodeVideoNext = av_compare_ts(videoStream.nextPts, videoStream.pEncoderContext->time_base,
                                            audioStream.nextPts, audioStream.pEncoderContext->time_base) <= 0;
        }

        if (encodeVideoNext) {
            if (MakeNextVideoFrame(&videoStream)) {
                writtenPacketCount += EncodeAndMux(&videoStream, pOutputContext, videoStream.pFrame, pPacket);
            } else {
                /** 목표 길이 도달 → 인코더 flush 후 이 스트림은 종료 */
                writtenPacketCount += EncodeAndMux(&videoStream, pOutputContext, NULL, pPacket);
                videoDone = true;
            }
        } else {
            if (MakeNextAudioFrame(&audioStream)) {
                writtenPacketCount += EncodeAndMux(&audioStream, pOutputContext, audioStream.pFrame, pPacket);
            } else {
                writtenPacketCount += EncodeAndMux(&audioStream, pOutputContext, NULL, pPacket);
                audioDone = true;
            }
        }
    }

    errorCode = av_write_trailer(pOutputContext);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Write Trailer...\r\n", errorCode);
    }

    printf("written packets : %d\r\n", writtenPacketCount);
    printf("\r\n다음 명령으로 재생해 확인:\r\nffplay \"%s\"\r\n", outputPath);

    exitStatus = 0;

    ffmpeg_release:
    av_packet_free(&pPacket);
    CloseOutputStream(&videoStream);
    CloseOutputStream(&audioStream);
    if (pOutputContext != NULL && pOutputContext->pb != NULL) {
        avio_closep(&pOutputContext->pb);
    }
    avformat_free_context(pOutputContext);
    if (exitStatus == 0) {
        printf("Muxing Done!\r\n");
    } else {
        printf("Finished with error(s)...\r\n");
    }
    return exitStatus;
}

bool OpenVideoStream(AVFormatContext *pOutputContext, StudyOutputStream *pVideoStream) {
    const AVCodec *pEncoder = NULL;
    int errorCode = 0;

    pEncoder = avcodec_find_encoder_by_name("libx264");
    if (pEncoder == NULL) {
        printf("libx264 not found → fallback to MPEG-4 encoder\r\n");
        pEncoder = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    }
    if (pEncoder == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Find Video Encoder...\r\n");
        return false;
    }
    printf("video encoder : %s\r\n", pEncoder->name);

    pVideoStream->pEncoderContext = avcodec_alloc_context3(pEncoder);
    if (pVideoStream->pEncoderContext == NULL) {
        return false;
    }

    pVideoStream->pEncoderContext->width = VIDEO_WIDTH;
    pVideoStream->pEncoderContext->height = VIDEO_HEIGHT;
    pVideoStream->pEncoderContext->pix_fmt = AV_PIX_FMT_YUV420P;
    pVideoStream->pEncoderContext->time_base = (AVRational) {1, VIDEO_FPS};
    pVideoStream->pEncoderContext->framerate = (AVRational) {VIDEO_FPS, 1};
    pVideoStream->pEncoderContext->bit_rate = 1000000;
    pVideoStream->pEncoderContext->gop_size = 25;

    if (strcmp(pEncoder->name, "libx264") == 0) {
        av_opt_set(pVideoStream->pEncoderContext->priv_data, "preset", "fast", 0);
    }

    /**
     * mp4는 코덱 설정(SPS/PPS)을 패킷이 아니라 컨테이너 헤더에 저장한다.
     * 이런 컨테이너에서는 인코더에 GLOBAL_HEADER 플래그를 꼭 줘야 한다.
     */
    if (pOutputContext->oformat->flags & AVFMT_GLOBALHEADER) {
        pVideoStream->pEncoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    errorCode = avcodec_open2(pVideoStream->pEncoderContext, pEncoder, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Open Video Encoder...\r\n", errorCode);
        return false;
    }

    pVideoStream->pStream = avformat_new_stream(pOutputContext, NULL);
    if (pVideoStream->pStream == NULL) {
        return false;
    }
    errorCode = avcodec_parameters_from_context(pVideoStream->pStream->codecpar, pVideoStream->pEncoderContext);
    if (errorCode < 0) {
        return false;
    }
    pVideoStream->pStream->time_base = pVideoStream->pEncoderContext->time_base;

    /** 재사용할 인코딩 입력 프레임 */
    pVideoStream->pFrame = av_frame_alloc();
    if (pVideoStream->pFrame == NULL) {
        return false;
    }
    pVideoStream->pFrame->format = AV_PIX_FMT_YUV420P;
    pVideoStream->pFrame->width = VIDEO_WIDTH;
    pVideoStream->pFrame->height = VIDEO_HEIGHT;
    if (av_frame_get_buffer(pVideoStream->pFrame, 0) < 0) {
        return false;
    }

    return true;
}

bool OpenAudioStream(AVFormatContext *pOutputContext, StudyOutputStream *pAudioStream) {
    const AVCodec *pEncoder = NULL;
    AVChannelLayout stereoLayout = AV_CHANNEL_LAYOUT_STEREO;
    int errorCode = 0;

    pEncoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (pEncoder == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Find AAC Encoder...\r\n");
        return false;
    }
    printf("audio encoder : %s\r\n", pEncoder->name);

    pAudioStream->pEncoderContext = avcodec_alloc_context3(pEncoder);
    if (pAudioStream->pEncoderContext == NULL) {
        return false;
    }

    pAudioStream->pEncoderContext->sample_rate = AUDIO_SAMPLE_RATE;
    pAudioStream->pEncoderContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
    pAudioStream->pEncoderContext->bit_rate = 128000;
    pAudioStream->pEncoderContext->time_base = (AVRational) {1, AUDIO_SAMPLE_RATE};
    if (av_channel_layout_copy(&pAudioStream->pEncoderContext->ch_layout, &stereoLayout) < 0) {
        return false;
    }

    if (pOutputContext->oformat->flags & AVFMT_GLOBALHEADER) {
        pAudioStream->pEncoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    errorCode = avcodec_open2(pAudioStream->pEncoderContext, pEncoder, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Open Audio Encoder...\r\n", errorCode);
        return false;
    }

    pAudioStream->pStream = avformat_new_stream(pOutputContext, NULL);
    if (pAudioStream->pStream == NULL) {
        return false;
    }
    errorCode = avcodec_parameters_from_context(pAudioStream->pStream->codecpar, pAudioStream->pEncoderContext);
    if (errorCode < 0) {
        return false;
    }
    pAudioStream->pStream->time_base = pAudioStream->pEncoderContext->time_base;

    pAudioStream->pFrame = av_frame_alloc();
    if (pAudioStream->pFrame == NULL) {
        return false;
    }
    pAudioStream->pFrame->format = pAudioStream->pEncoderContext->sample_fmt;
    pAudioStream->pFrame->sample_rate = AUDIO_SAMPLE_RATE;
    pAudioStream->pFrame->nb_samples = pAudioStream->pEncoderContext->frame_size;
    if (av_channel_layout_copy(&pAudioStream->pFrame->ch_layout, &pAudioStream->pEncoderContext->ch_layout) < 0) {
        return false;
    }
    if (av_frame_get_buffer(pAudioStream->pFrame, 0) < 0) {
        return false;
    }

    return true;
}

bool MakeNextVideoFrame(StudyOutputStream *pVideoStream) {
    AVFrame *pFrame = pVideoStream->pFrame;
    int frameIdx = (int) pVideoStream->nextPts;

    /** 목표 길이(초) 도달 검사: pts(1/25초 단위)가 5초를 넘으면 종료 */
    if (av_compare_ts(pVideoStream->nextPts, pVideoStream->pEncoderContext->time_base,
                      OUTPUT_DURATION_SEC, (AVRational) {1, 1}) >= 0) {
        return false;
    }

    if (av_frame_make_writable(pFrame) < 0) {
        return false;
    }

    /** 08 레슨과 같은 움직이는 그라데이션 합성 */
    for (int y = 0; y < pFrame->height; ++y) {
        for (int x = 0; x < pFrame->width; ++x) {
            pFrame->data[0][y * pFrame->linesize[0] + x] = (uint8_t) (x + y + frameIdx * 3);
        }
    }
    for (int y = 0; y < pFrame->height / 2; ++y) {
        for (int x = 0; x < pFrame->width / 2; ++x) {
            pFrame->data[1][y * pFrame->linesize[1] + x] = (uint8_t) (128 + y + frameIdx * 2);
            pFrame->data[2][y * pFrame->linesize[2] + x] = (uint8_t) (64 + x + frameIdx * 5);
        }
    }

    pFrame->pts = pVideoStream->nextPts;
    pVideoStream->nextPts += 1;
    return true;
}

bool MakeNextAudioFrame(StudyOutputStream *pAudioStream) {
    AVFrame *pFrame = pAudioStream->pFrame;

    if (av_compare_ts(pAudioStream->nextPts, pAudioStream->pEncoderContext->time_base,
                      OUTPUT_DURATION_SEC, (AVRational) {1, 1}) >= 0) {
        return false;
    }

    if (av_frame_make_writable(pFrame) < 0) {
        return false;
    }

    for (int sampleIdx = 0; sampleIdx < pFrame->nb_samples; ++sampleIdx) {
        float sampleValue = (float) (0.3 * sin(pAudioStream->sinePhase));
        ((float *) pFrame->data[0])[sampleIdx] = sampleValue;
        ((float *) pFrame->data[1])[sampleIdx] = sampleValue;
        pAudioStream->sinePhase += 2.0 * M_PI * SINE_FREQUENCY / AUDIO_SAMPLE_RATE;
    }

    pFrame->pts = pAudioStream->nextPts;
    pAudioStream->nextPts += pFrame->nb_samples;
    return true;
}

int EncodeAndMux(StudyOutputStream *pOutputStream, AVFormatContext *pOutputContext,
                 AVFrame *pFrame, AVPacket *pPacket) {
    int writtenCount = 0;
    int errorCode = 0;

    errorCode = avcodec_send_frame(pOutputStream->pEncoderContext, pFrame);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Sending frame to encoder\r\n", errorCode);
        return 0;
    }

    while (errorCode >= 0) {
        errorCode = avcodec_receive_packet(pOutputStream->pEncoderContext, pPacket);
        if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
            break;
        } else if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Receive packet\r\n", errorCode);
            break;
        }

        /** 인코더 time_base → 스트림 time_base (mp4 먹서가 스트림 tb를 조정했을 수 있다) */
        av_packet_rescale_ts(pPacket, pOutputStream->pEncoderContext->time_base,
                             pOutputStream->pStream->time_base);
        pPacket->stream_index = pOutputStream->pStream->index;

        errorCode = av_interleaved_write_frame(pOutputContext, pPacket);
        if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Write frame\r\n", errorCode);
            break;
        }
        writtenCount++;
    }

    return writtenCount;
}

void CloseOutputStream(StudyOutputStream *pOutputStream) {
    av_frame_free(&pOutputStream->pFrame);
    avcodec_free_context(&pOutputStream->pEncoderContext);
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
