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
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>

#define BUFFER_MAX                  1024
/** 절대 경로 해석용 버퍼 크기 (Linux PATH_MAX=4096 대응) */
#define RESOURCE_PATH_MAX           4096
/** 출력 오디오 사양: 44.1kHz / stereo / s16 interleaved */
#define OUTPUT_SAMPLE_RATE          44100

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

/** 디코딩된 프레임(또는 드레인 시 NULL)을 리샘플링해 파일에 쓴다. 쓴 샘플 수를 반환. */
int ResampleAndWrite(SwrContext *pSwrContext, const AVFrame *pFrame, FILE *pOutputFile);

/**
 * study-FFMPEG 07 — swresample: 오디오 리샘플링
 *
 * 디코딩된 오디오는 보통 fltp(float planar)라 그대로 재생 장치에 쓰기 어렵다.
 * libswresample(SwrContext)로 세 가지를 한 번에 변환한다:
 *   1) 샘플 포맷    : fltp → s16 (16bit 정수, interleaved)
 *   2) 샘플레이트   : 원본 → 44,100Hz
 *   3) 채널 레이아웃: 원본 → stereo
 * 결과를 raw PCM 파일로 저장하고 ffplay로 재생해 확인한다.
 */
int main(int argc, char **argv) {
    char resourcePath[BUFFER_MAX] = {0};
    char outputPath[BUFFER_MAX] = {0};
    int errorCode = -1;
    /** 프로그램 종료 코드: 성공 경로 끝에서만 0으로 바뀐다 */
    int exitStatus = -1;

    AVFormatContext *pFormatContext = NULL;
    AVPacket *pPacket = NULL;
    AVFrame *pFrame = NULL;

    const AVCodec *pAudioCodec = NULL;
    AVCodecContext *pAudioCodecContext = NULL;
    int audioStreamIdx = -1;

    SwrContext *pSwrContext = NULL;
    /** FFmpeg 7.x 채널 레이아웃: 매크로로 stereo 레이아웃 초기화 */
    AVChannelLayout outputChannelLayout = AV_CHANNEL_LAYOUT_STEREO;

    FILE *pOutputFile = NULL;
    int64_t totalOutputSamples = 0;

    if (!GetResourcePath("murage.mp4", resourcePath)) {
        printf("Failed Get murage.mp4 resource path...\r\n");
        return -1;
    }
    if (!EnsureGeneratedStudyDirectory()) {
        printf("Failed Create GeneratedStudy directory...\r\n");
        return -1;
    }
    if (!GetResourcePath("GeneratedStudy/study-audio.pcm", outputPath)) {
        return -1;
    }
    printf("resource path - %s\r\n", resourcePath);

    errorCode = avformat_open_input(&pFormatContext, resourcePath, NULL, NULL);
    FFMPEG_ERROR(errorCode, "[FFMPEG ERROR] FFMPEG Open Failed...\r\n")

    errorCode = avformat_find_stream_info(pFormatContext, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) find Stream Failed...\r\n", errorCode);
        goto ffmpeg_release;
    }

    audioStreamIdx = av_find_best_stream(pFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, &pAudioCodec, 0);
    if (audioStreamIdx < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Audio Stream Found Failed...\r\n", audioStreamIdx);
        goto ffmpeg_release;
    }

    pAudioCodecContext = avcodec_alloc_context3(pAudioCodec);
    if (pAudioCodecContext == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load Audio Codec Context...\r\n");
        goto ffmpeg_release;
    }

    errorCode = avcodec_parameters_to_context(pAudioCodecContext,
                                              pFormatContext->streams[audioStreamIdx]->codecpar);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed codec parameter copy to context...\r\n", errorCode);
        goto ffmpeg_release;
    }

    errorCode = avcodec_open2(pAudioCodecContext, pAudioCodec, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Open Audio Codec...\r\n", errorCode);
        goto ffmpeg_release;
    }

    printf("source : %d Hz, %s, %d channels → output : %d Hz, s16, stereo\r\n",
           pAudioCodecContext->sample_rate,
           av_get_sample_fmt_name(pAudioCodecContext->sample_fmt),
           pAudioCodecContext->ch_layout.nb_channels,
           OUTPUT_SAMPLE_RATE);

    /**
     * SwrContext 생성 (FFmpeg 7.x는 swr_alloc_set_opts2 사용).
     * 출력 사양(레이아웃/포맷/샘플레이트) → 입력 사양 순서로 지정한다.
     */
    errorCode = swr_alloc_set_opts2(&pSwrContext,
                                    &outputChannelLayout, AV_SAMPLE_FMT_S16, OUTPUT_SAMPLE_RATE,
                                    &pAudioCodecContext->ch_layout, pAudioCodecContext->sample_fmt,
                                    pAudioCodecContext->sample_rate,
                                    0, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Allocate SwrContext...\r\n", errorCode);
        goto ffmpeg_release;
    }

    errorCode = swr_init(pSwrContext);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Init SwrContext...\r\n", errorCode);
        goto ffmpeg_release;
    }

    pOutputFile = fopen(outputPath, "wb");
    if (pOutputFile == NULL) {
        printf("Failed Open Output File... (%s)\r\n", outputPath);
        goto ffmpeg_release;
    }

    pPacket = av_packet_alloc();
    pFrame = av_frame_alloc();
    if (pPacket == NULL || pFrame == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load packet/frame structure...\r\n");
        goto ffmpeg_release;
    }

    /** 디코딩 → 리샘플링 → 파일 쓰기 루프 */
    while (av_read_frame(pFormatContext, pPacket) >= 0) {
        if (pPacket->stream_index == audioStreamIdx) {
            errorCode = avcodec_send_packet(pAudioCodecContext, pPacket);
            if (errorCode < 0) {
                av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Sending packet to decoder\r\n", errorCode);
                av_packet_unref(pPacket);
                break;
            }

            while (true) {
                errorCode = avcodec_receive_frame(pAudioCodecContext, pFrame);
                if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
                    break;
                } else if (errorCode < 0) {
                    av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Receive frame\r\n", errorCode);
                    break;
                }

                totalOutputSamples += ResampleAndWrite(pSwrContext, pFrame, pOutputFile);
                av_frame_unref(pFrame);
            }
        }
        av_packet_unref(pPacket);
    }

    /** 디코더 flush */
    errorCode = avcodec_send_packet(pAudioCodecContext, NULL);
    if (errorCode >= 0) {
        while (true) {
            errorCode = avcodec_receive_frame(pAudioCodecContext, pFrame);
            if (errorCode < 0) {
                break;
            }
            totalOutputSamples += ResampleAndWrite(pSwrContext, pFrame, pOutputFile);
            av_frame_unref(pFrame);
        }
    }

    /**
     * 리샘플러 드레인.
     * 샘플레이트 변환 시 SwrContext 내부에 지연된 샘플이 남는다.
     * 입력을 NULL로 주면 남은 샘플을 모두 뱉어낸다.
     */
    totalOutputSamples += ResampleAndWrite(pSwrContext, NULL, pOutputFile);

    printf("total output samples : %lld (%.2f sec)\r\n",
           totalOutputSamples, (double) totalOutputSamples / OUTPUT_SAMPLE_RATE);
    printf("\r\n다음 명령으로 재생해 확인:\r\n");
    printf("ffplay -f s16le -ar %d -ch_layout stereo \"%s\"\r\n", OUTPUT_SAMPLE_RATE, outputPath);

    exitStatus = 0;

    ffmpeg_release:
    if (pOutputFile != NULL) {
        fclose(pOutputFile);
    }
    av_frame_free(&pFrame);
    av_packet_free(&pPacket);
    swr_free(&pSwrContext);
    avcodec_free_context(&pAudioCodecContext);
    avformat_close_input(&pFormatContext);
    if (exitStatus == 0) {
        printf("Resampling Audio Done!\r\n");
    } else {
        printf("Finished with error(s)...\r\n");
    }
    return exitStatus;
}

int ResampleAndWrite(SwrContext *pSwrContext, const AVFrame *pFrame, FILE *pOutputFile) {
    uint8_t *pOutputBuffer = NULL;
    int outputLineSize = 0;
    int convertedSamples = 0;
    int64_t maxOutputSamples = 0;

    const uint8_t **ppInputData = NULL;
    int inputSampleCount = 0;
    if (pFrame != NULL) {
        ppInputData = (const uint8_t **) pFrame->data;
        inputSampleCount = pFrame->nb_samples;
    }

    /**
     * 출력에 필요한 최대 샘플 수 계산.
     * swr_get_delay: 리샘플러 내부에 쌓여 있는 지연 샘플 수.
     * av_rescale_rnd: (지연 + 입력 샘플)을 출력 샘플레이트 기준으로 환산.
     */
    maxOutputSamples = av_rescale_rnd(
            swr_get_delay(pSwrContext, pFrame != NULL ? pFrame->sample_rate : OUTPUT_SAMPLE_RATE) + inputSampleCount,
            OUTPUT_SAMPLE_RATE,
            pFrame != NULL ? pFrame->sample_rate : OUTPUT_SAMPLE_RATE,
            AV_ROUND_UP);
    if (maxOutputSamples <= 0) {
        return 0;
    }

    /** 출력 버퍼 할당 (stereo / s16 / interleaved) */
    if (av_samples_alloc(&pOutputBuffer, &outputLineSize, 2, (int) maxOutputSamples, AV_SAMPLE_FMT_S16, 1) < 0) {
        printf("Failed Allocate Resample Output Buffer...\r\n");
        return 0;
    }

    /**
     * 실제 변환.
     * 입력이 NULL이면 내부에 남은 샘플만 뱉어내는 드레인 동작을 한다.
     * 반환값 = 실제로 변환된 채널당 샘플 수.
     */
    convertedSamples = swr_convert(pSwrContext, &pOutputBuffer, (int) maxOutputSamples,
                                   ppInputData, inputSampleCount);
    if (convertedSamples > 0) {
        /** s16 stereo interleaved → 샘플당 2바이트 × 2채널 */
        fwrite(pOutputBuffer, 1, (size_t) convertedSamples * 2 * sizeof(int16_t), pOutputFile);
    }

    av_freep(&pOutputBuffer);
    return convertedSamples > 0 ? convertedSamples : 0;
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
