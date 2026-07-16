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
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>

#define BUFFER_MAX                  1024
/** 절대 경로 해석용 버퍼 크기 (Linux PATH_MAX=4096 대응) */
#define RESOURCE_PATH_MAX           4096
/** 생성할 오디오 사양: 44.1kHz stereo, 440Hz 사인파 2초 */
#define OUTPUT_SAMPLE_RATE          44100
#define OUTPUT_DURATION_SEC         2
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


bool GetResourcePath(const char *name, char *const pathBuffer);

bool EnsureGeneratedStudyDirectory(void);

/** 프레임(또는 flush 시 NULL)을 인코딩해 ADTS 먹서로 내보낸다 */
int EncodeAndMux(AVCodecContext *pCodecContext, AVFormatContext *pOutputContext,
                 AVStream *pStream, AVFrame *pFrame, AVPacket *pPacket);

/**
 * study-FFMPEG 09 — 오디오 인코딩 (AAC)
 *
 * 440Hz 사인파 2초를 직접 만들어 AAC로 인코딩한다.
 * 비디오 인코딩(08)과 다른 오디오만의 규칙:
 *   1) frame_size : AAC는 프레임당 샘플 수가 고정(보통 1024).
 *      인코더를 연 뒤 pEncoderContext->frame_size를 읽어 그 크기로 프레임을 만들어야 한다.
 *   2) sample_fmt : FFmpeg 내장 AAC 인코더는 FLTP(planar float)만 받는다.
 * raw AAC는 그대로 재생이 안 되므로 ADTS 헤더를 붙여야 한다 —
 * 여기서는 "adts" 먹서를 사용해 다음 레슨(먹싱)을 가볍게 미리 맛본다.
 */
int main(int argc, char **argv) {
    char outputPath[BUFFER_MAX] = {0};
    int errorCode = -1;
    /** 프로그램 종료 코드: 성공 경로 끝에서만 0으로 바뀐다 */
    int exitStatus = -1;

    const AVCodec *pEncoder = NULL;
    AVCodecContext *pEncoderContext = NULL;
    AVFrame *pFrame = NULL;
    AVPacket *pPacket = NULL;

    AVFormatContext *pOutputContext = NULL;
    AVStream *pStream = NULL;

    AVChannelLayout stereoLayout = AV_CHANNEL_LAYOUT_STEREO;

    int64_t nextPts = 0;
    int totalFrameCount = 0;
    double sinePhase = 0.0;

    if (!EnsureGeneratedStudyDirectory()) {
        printf("Failed Create GeneratedStudy directory...\r\n");
        return -1;
    }
    if (!GetResourcePath("GeneratedStudy/study-encoded.aac", outputPath)) {
        return -1;
    }

    /** FFmpeg 내장 AAC 인코더 */
    pEncoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (pEncoder == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Find AAC Encoder...\r\n");
        return -1;
    }
    printf("encoder : %s (%s)\r\n", pEncoder->name, pEncoder->long_name);

    pEncoderContext = avcodec_alloc_context3(pEncoder);
    if (pEncoderContext == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load Encoder Context...\r\n");
        return -1;
    }

    /** 인코더 출력 사양 설정 */
    pEncoderContext->sample_rate = OUTPUT_SAMPLE_RATE;
    pEncoderContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
    pEncoderContext->bit_rate = 128000;
    /** time_base = 1/샘플레이트 → pts 단위가 "샘플 번호"가 된다 */
    pEncoderContext->time_base = (AVRational) {1, OUTPUT_SAMPLE_RATE};
    /** FFmpeg 7.x: AVChannelLayout은 대입이 아니라 copy 함수로 설정 */
    errorCode = av_channel_layout_copy(&pEncoderContext->ch_layout, &stereoLayout);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Copy Channel Layout...\r\n", errorCode);
        goto ffmpeg_release;
    }

    errorCode = avcodec_open2(pEncoderContext, pEncoder, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Open Encoder...\r\n", errorCode);
        goto ffmpeg_release;
    }

    /** AAC는 프레임 크기가 고정 — 인코더가 정한 값을 반드시 따른다 */
    printf("encoder frame_size : %d samples\r\n", pEncoderContext->frame_size);

    /** ADTS 먹서 준비 (컨테이너 쓰기의 최소 형태) */
    errorCode = avformat_alloc_output_context2(&pOutputContext, NULL, "adts", outputPath);
    if (errorCode < 0 || pOutputContext == NULL) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Allocate Output Context...\r\n", errorCode);
        goto ffmpeg_release;
    }

    pStream = avformat_new_stream(pOutputContext, NULL);
    if (pStream == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Create Output Stream...\r\n");
        goto ffmpeg_release;
    }
    /** 인코더 설정 → 스트림 파라미터로 복사 (13/10 레슨의 parameters_copy와 방향이 다름에 주의) */
    errorCode = avcodec_parameters_from_context(pStream->codecpar, pEncoderContext);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Copy Parameters To Stream...\r\n", errorCode);
        goto ffmpeg_release;
    }
    pStream->time_base = pEncoderContext->time_base;

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

    pFrame = av_frame_alloc();
    pPacket = av_packet_alloc();
    if (pFrame == NULL || pPacket == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load frame/packet structure...\r\n");
        goto ffmpeg_release;
    }

    /** 프레임 사양은 인코더가 정한 frame_size에 맞춘다 */
    pFrame->format = pEncoderContext->sample_fmt;
    pFrame->sample_rate = pEncoderContext->sample_rate;
    pFrame->nb_samples = pEncoderContext->frame_size;
    errorCode = av_channel_layout_copy(&pFrame->ch_layout, &pEncoderContext->ch_layout);
    if (errorCode < 0) {
        goto ffmpeg_release;
    }
    errorCode = av_frame_get_buffer(pFrame, 0);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Allocate Frame Buffer...\r\n", errorCode);
        goto ffmpeg_release;
    }

    /** 사인파 생성 → 인코딩 루프 */
    while (nextPts < (int64_t) OUTPUT_SAMPLE_RATE * OUTPUT_DURATION_SEC) {
        errorCode = av_frame_make_writable(pFrame);
        if (errorCode < 0) {
            break;
        }

        /**
         * FLTP는 planar: data[0]=왼쪽 채널, data[1]=오른쪽 채널.
         * 두 채널에 같은 사인파를 채운다 (진폭 0.3으로 클리핑 방지).
         */
        for (int sampleIdx = 0; sampleIdx < pFrame->nb_samples; ++sampleIdx) {
            float sampleValue = (float) (0.3 * sin(sinePhase));
            ((float *) pFrame->data[0])[sampleIdx] = sampleValue;
            ((float *) pFrame->data[1])[sampleIdx] = sampleValue;
            sinePhase += 2.0 * M_PI * SINE_FREQUENCY / OUTPUT_SAMPLE_RATE;
        }

        /** pts = 지금까지 보낸 샘플 수 (time_base가 1/샘플레이트이므로) */
        pFrame->pts = nextPts;
        nextPts += pFrame->nb_samples;

        totalFrameCount += EncodeAndMux(pEncoderContext, pOutputContext, pStream, pFrame, pPacket);
    }

    /** 인코더 flush */
    totalFrameCount += EncodeAndMux(pEncoderContext, pOutputContext, pStream, NULL, pPacket);

    /** 트레일러(파일 마무리) 쓰기 */
    errorCode = av_write_trailer(pOutputContext);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Write Trailer...\r\n", errorCode);
    }

    printf("written packets : %d\r\n", totalFrameCount);
    printf("output : %s\r\n", outputPath);
    printf("\r\n다음 명령으로 재생해 확인:\r\nffplay \"%s\"\r\n", outputPath);

    exitStatus = 0;

    ffmpeg_release:
    av_packet_free(&pPacket);
    av_frame_free(&pFrame);
    if (pOutputContext != NULL && pOutputContext->pb != NULL) {
        avio_closep(&pOutputContext->pb);
    }
    avformat_free_context(pOutputContext);
    avcodec_free_context(&pEncoderContext);
    if (exitStatus == 0) {
        printf("Encode Audio Done!\r\n");
    } else {
        printf("Finished with error(s)...\r\n");
    }
    return exitStatus;
}

int EncodeAndMux(AVCodecContext *pCodecContext, AVFormatContext *pOutputContext,
                 AVStream *pStream, AVFrame *pFrame, AVPacket *pPacket) {
    int writtenCount = 0;
    int errorCode = 0;

    errorCode = avcodec_send_frame(pCodecContext, pFrame);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Sending frame to encoder\r\n", errorCode);
        return 0;
    }

    while (errorCode >= 0) {
        errorCode = avcodec_receive_packet(pCodecContext, pPacket);
        if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
            break;
        } else if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Receive packet\r\n", errorCode);
            break;
        }

        /** 인코더 time_base → 스트림 time_base로 타임스탬프 변환 */
        av_packet_rescale_ts(pPacket, pCodecContext->time_base, pStream->time_base);
        pPacket->stream_index = pStream->index;

        /** 먹서를 통해 쓰기 (raw fwrite가 아니라 ADTS 헤더가 자동으로 붙는다) */
        errorCode = av_interleaved_write_frame(pOutputContext, pPacket);
        if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Write frame\r\n", errorCode);
            break;
        }
        writtenCount++;
    }

    return writtenCount;
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
