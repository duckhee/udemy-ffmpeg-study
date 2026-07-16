#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
// FFMPEG Library
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>

#if defined(WIN32) || defined(WIN64)

#include <Windows.h>

#endif

#define BUFFER_MAX                  1024
/** 절대 경로 해석용 버퍼 크기 (Linux PATH_MAX=4096 대응) */
#define RESOURCE_PATH_MAX           4096
/** 상세 정보를 출력할 프레임 개수 */
#define PRINT_FRAME_MAX             10

#define FFMPEG_ERROR(errorCode, msg) \
{                                    \
if((errorCode) < (0)) {              \
av_log(NULL, AV_LOG_ERROR, (msg));   \
return -1;                           \
}else{                               \
}                                     \
}                                    \


bool GetResourcePath(const char *name, char *const pathBuffer);

/** 디코더에서 오디오 프레임을 모두 꺼내 정보 출력. 꺼낸 프레임 수를 반환한다. */
int DrainDecodedAudioFrames(AVCodecContext *pCodecContext, AVFrame *pFrame, int *pPrintedCount, int64_t *pTotalSamples);

/**
 * study-FFMPEG 05 — 오디오 디코딩과 샘플 포맷
 *
 * 비디오(04)와 완전히 같은 send/receive 파이프라인으로 오디오를 디코딩한다.
 * 대신 프레임에서 보는 정보가 다르다:
 *   - nb_samples : 이 프레임에 담긴 채널당 샘플 수
 *   - sample_fmt : 샘플 표현 방식 (fltp = float planar 등)
 *   - ch_layout  : FFmpeg 7.x의 채널 레이아웃 구조체 (AVChannelLayout)
 *
 * planar(fltp)와 interleaved(flt)의 차이:
 *   planar      : data[0]=L채널 전체, data[1]=R채널 전체  (LLLL... RRRR...)
 *   interleaved : data[0]에 LRLRLR... 로 섞여 저장
 */
int main(int argc, char **argv) {
    char resourcePath[BUFFER_MAX] = {0};
    int errorCode = -1;
    /** 프로그램 종료 코드: 성공 경로 끝에서만 0으로 바뀐다 */
    int exitStatus = -1;

    AVFormatContext *pFormatContext = NULL;
    AVPacket *pPacket = NULL;
    AVFrame *pFrame = NULL;

    const AVCodec *pAudioCodec = NULL;
    AVCodecContext *pAudioCodecContext = NULL;
    int audioStreamIdx = -1;

    int decodedFrameCount = 0;
    int printedCount = 0;
    int64_t totalSamples = 0;
    char channelLayoutText[BUFFER_MAX] = {0};

    if (!GetResourcePath("murage.mp4", resourcePath)) {
        printf("Failed Get murage.mp4 resource path...\r\n");
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

    /** 오디오 스트림 + 디코더 찾기 */
    audioStreamIdx = av_find_best_stream(pFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, &pAudioCodec, 0);
    if (audioStreamIdx < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Audio Stream Found Failed...\r\n", audioStreamIdx);
        goto ffmpeg_release;
    }
    printf("audio stream index : %d, codec : %s\r\n", audioStreamIdx, pAudioCodec->name);

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

    /** 디코더가 실제로 사용하는 오디오 파라미터 출력 */
    av_channel_layout_describe(&pAudioCodecContext->ch_layout, channelLayoutText, sizeof(channelLayoutText));
    printf("==== Audio Decoder Information ====\r\n");
    printf("sample rate    : %d Hz\r\n", pAudioCodecContext->sample_rate);
    printf("sample format  : %s\r\n", av_get_sample_fmt_name(pAudioCodecContext->sample_fmt));
    printf("planar         : %s\r\n", av_sample_fmt_is_planar(pAudioCodecContext->sample_fmt) ? "yes" : "no");
    printf("channels       : %d (%s)\r\n", pAudioCodecContext->ch_layout.nb_channels, channelLayoutText);

    pPacket = av_packet_alloc();
    pFrame = av_frame_alloc();
    if (pPacket == NULL || pFrame == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load packet/frame structure...\r\n");
        goto ffmpeg_release;
    }

    printf("==== Decoded Audio Frames (first %d) ====\r\n", PRINT_FRAME_MAX);

    /** 디먹싱 + 디코딩 루프 (04와 동일한 구조, 대상만 오디오 스트림) */
    while (av_read_frame(pFormatContext, pPacket) >= 0) {
        if (pPacket->stream_index == audioStreamIdx) {
            errorCode = avcodec_send_packet(pAudioCodecContext, pPacket);
            if (errorCode < 0) {
                av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Sending packet to decoder\r\n", errorCode);
                av_packet_unref(pPacket);
                break;
            }
            decodedFrameCount += DrainDecodedAudioFrames(pAudioCodecContext, pFrame, &printedCount, &totalSamples);
        }
        av_packet_unref(pPacket);
    }

    /** 디코더 flush */
    errorCode = avcodec_send_packet(pAudioCodecContext, NULL);
    if (errorCode >= 0) {
        decodedFrameCount += DrainDecodedAudioFrames(pAudioCodecContext, pFrame, &printedCount, &totalSamples);
    }

    printf("total decoded frames  : %d\r\n", decodedFrameCount);
    printf("total samples/channel : %lld\r\n", totalSamples);
    /** 샘플 수 / 샘플레이트 = 실제 오디오 길이(초) — 컨테이너 duration과 비교해 보자 */
    printf("audio length          : %.2f sec\r\n", (double) totalSamples / pAudioCodecContext->sample_rate);

    exitStatus = 0;

    ffmpeg_release:
    av_frame_free(&pFrame);
    av_packet_free(&pPacket);
    avcodec_free_context(&pAudioCodecContext);
    avformat_close_input(&pFormatContext);
    if (exitStatus == 0) {
        printf("Decode Audio Done!\r\n");
    } else {
        printf("Finished with error(s)...\r\n");
    }
    return exitStatus;
}

int DrainDecodedAudioFrames(AVCodecContext *pCodecContext, AVFrame *pFrame, int *pPrintedCount, int64_t *pTotalSamples) {
    int receivedCount = 0;
    int errorCode = 0;

    while (errorCode >= 0) {
        errorCode = avcodec_receive_frame(pCodecContext, pFrame);
        if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
            break;
        } else if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Receive frame\r\n", errorCode);
            break;
        }

        if (*pPrintedCount < PRINT_FRAME_MAX) {
            /**
             * 첫 샘플 값 확인:
             * fltp(planar float)라면 data[0]이 첫 번째 채널의 float 배열이다.
             */
            float firstSample = 0.0f;
            if (pFrame->format == AV_SAMPLE_FMT_FLTP && pFrame->data[0] != NULL) {
                firstSample = ((float *) pFrame->data[0])[0];
            }
            printf("Frame %-4lld pts=%-8lld nb_samples=%-5d fmt=%s first_sample=%f\r\n",
                   pCodecContext->frame_num,
                   pFrame->pts,
                   pFrame->nb_samples,
                   av_get_sample_fmt_name((enum AVSampleFormat) pFrame->format),
                   firstSample);
            (*pPrintedCount)++;
        }

        *pTotalSamples += pFrame->nb_samples;
        receivedCount++;
        av_frame_unref(pFrame);
    }

    return receivedCount;
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
