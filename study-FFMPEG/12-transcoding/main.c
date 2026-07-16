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
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#define BUFFER_MAX                  1024
/** 절대 경로 해석용 버퍼 크기 (Linux PATH_MAX=4096 대응) */
#define RESOURCE_PATH_MAX           4096
/** 트랜스코딩 출력 사양: 640x360 / 500kbps */
#define OUTPUT_WIDTH                640
#define OUTPUT_HEIGHT               360
#define OUTPUT_BIT_RATE             500000

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

/** 디코딩된 프레임 하나를 스케일 → 인코딩 → 먹싱한다 */
int ScaleEncodeAndMux(AVFrame *pDecodedFrame, struct SwsContext *pSwsContext, AVFrame *pScaledFrame,
                      AVCodecContext *pEncoderContext, AVStream *pOutputStream,
                      AVFormatContext *pOutputContext, AVPacket *pEncodedPacket);

/** 인코더에서 나온 패킷을 모두 먹서로 쓴다 (pFrame NULL이면 flush) */
int EncodeAndMux(AVCodecContext *pEncoderContext, AVFrame *pFrame, AVPacket *pPacket,
                 AVStream *pOutputStream, AVFormatContext *pOutputContext);

/**
 * study-FFMPEG 12 — 트랜스코딩 (해상도/비트레이트 변경)
 *
 * 지금까지 배운 모든 것을 하나의 파이프라인으로 연결한다:
 *
 *   디먹싱(03) → 디코딩(04) → 스케일링(06) → 인코딩(08) → 먹싱(11)
 *
 * murage.mp4의 비디오를 640x360 / 500kbps H.264로 재인코딩해
 * study-transcoded.mp4로 저장한다.
 * (학습 단순화를 위해 오디오 스트림은 버린다 — 오디오까지 포함하려면
 *  07의 리샘플링과 11의 인터리빙을 추가하면 된다)
 *
 * 순서가 중요한 마무리 절차:
 *   1) 디코더 flush (NULL 패킷) → 남은 프레임을 모두 인코더로
 *   2) 인코더 flush (NULL 프레임) → 남은 패킷을 모두 파일로
 *   3) av_write_trailer
 */
int main(int argc, char **argv) {
    char inputPath[BUFFER_MAX] = {0};
    char outputPath[BUFFER_MAX] = {0};
    int errorCode = -1;
    /** 프로그램 종료 코드: 성공 경로 끝에서만 0으로 바뀐다 */
    int exitStatus = -1;

    /** 입력(디코딩) 쪽 */
    AVFormatContext *pInputContext = NULL;
    const AVCodec *pDecoder = NULL;
    AVCodecContext *pDecoderContext = NULL;
    int videoStreamIdx = -1;
    AVPacket *pInputPacket = NULL;
    AVFrame *pDecodedFrame = NULL;

    /** 변환(스케일) 쪽 */
    struct SwsContext *pSwsContext = NULL;
    AVFrame *pScaledFrame = NULL;

    /** 출력(인코딩+먹싱) 쪽 */
    AVFormatContext *pOutputContext = NULL;
    const AVCodec *pEncoder = NULL;
    AVCodecContext *pEncoderContext = NULL;
    AVStream *pOutputStream = NULL;
    AVPacket *pEncodedPacket = NULL;

    AVStream *pInputStream = NULL;
    int writtenPacketCount = 0;

    if (!GetResourcePath("murage.mp4", inputPath)) {
        printf("Failed Get murage.mp4 resource path...\r\n");
        return -1;
    }
    if (!EnsureGeneratedStudyDirectory()) {
        printf("Failed Create GeneratedStudy directory...\r\n");
        return -1;
    }
    if (!GetResourcePath("GeneratedStudy/study-transcoded.mp4", outputPath)) {
        return -1;
    }
    printf("input  : %s\r\n", inputPath);
    printf("output : %s\r\n", outputPath);

    /** ===== 1. 입력 열기 + 디코더 준비 ===== */
    errorCode = avformat_open_input(&pInputContext, inputPath, NULL, NULL);
    FFMPEG_ERROR(errorCode, "[FFMPEG ERROR] FFMPEG Open Failed...\r\n")

    errorCode = avformat_find_stream_info(pInputContext, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) find Stream Failed...\r\n", errorCode);
        goto ffmpeg_release;
    }

    videoStreamIdx = av_find_best_stream(pInputContext, AVMEDIA_TYPE_VIDEO, -1, -1, &pDecoder, 0);
    if (videoStreamIdx < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Video Stream Found Failed...\r\n", videoStreamIdx);
        goto ffmpeg_release;
    }
    pInputStream = pInputContext->streams[videoStreamIdx];

    pDecoderContext = avcodec_alloc_context3(pDecoder);
    if (pDecoderContext == NULL) {
        goto ffmpeg_release;
    }
    errorCode = avcodec_parameters_to_context(pDecoderContext, pInputStream->codecpar);
    if (errorCode < 0) {
        goto ffmpeg_release;
    }
    errorCode = avcodec_open2(pDecoderContext, pDecoder, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Open Decoder...\r\n", errorCode);
        goto ffmpeg_release;
    }

    printf("source : %dx%d %s @ %lld bps\r\n",
           pDecoderContext->width, pDecoderContext->height,
           pDecoder->name, pInputStream->codecpar->bit_rate);

    /** ===== 2. 출력 컨텍스트 + 인코더 준비 ===== */
    errorCode = avformat_alloc_output_context2(&pOutputContext, NULL, NULL, outputPath);
    if (errorCode < 0 || pOutputContext == NULL) {
        goto ffmpeg_release;
    }

    pEncoder = avcodec_find_encoder_by_name("libx264");
    if (pEncoder == NULL) {
        printf("libx264 not found → fallback to MPEG-4 encoder\r\n");
        pEncoder = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    }
    if (pEncoder == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Find Video Encoder...\r\n");
        goto ffmpeg_release;
    }

    pEncoderContext = avcodec_alloc_context3(pEncoder);
    if (pEncoderContext == NULL) {
        goto ffmpeg_release;
    }

    pEncoderContext->width = OUTPUT_WIDTH;
    pEncoderContext->height = OUTPUT_HEIGHT;
    pEncoderContext->pix_fmt = AV_PIX_FMT_YUV420P;
    pEncoderContext->bit_rate = OUTPUT_BIT_RATE;
    /**
     * 인코더 time_base를 입력 스트림과 같게 맞춘다.
     * 그러면 디코딩된 프레임의 pts를 변환 없이 그대로 인코더에 넘길 수 있다.
     */
    pEncoderContext->time_base = pInputStream->time_base;
    pEncoderContext->framerate = av_guess_frame_rate(pInputContext, pInputStream, NULL);
    pEncoderContext->gop_size = 25;

    if (strcmp(pEncoder->name, "libx264") == 0) {
        av_opt_set(pEncoderContext->priv_data, "preset", "fast", 0);
    }
    if (pOutputContext->oformat->flags & AVFMT_GLOBALHEADER) {
        pEncoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    errorCode = avcodec_open2(pEncoderContext, pEncoder, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Open Encoder...\r\n", errorCode);
        goto ffmpeg_release;
    }

    pOutputStream = avformat_new_stream(pOutputContext, NULL);
    if (pOutputStream == NULL) {
        goto ffmpeg_release;
    }
    errorCode = avcodec_parameters_from_context(pOutputStream->codecpar, pEncoderContext);
    if (errorCode < 0) {
        goto ffmpeg_release;
    }
    pOutputStream->time_base = pEncoderContext->time_base;

    /** ===== 3. 스케일러 + 프레임 버퍼 준비 ===== */
    pSwsContext = sws_getContext(
            pDecoderContext->width, pDecoderContext->height, pDecoderContext->pix_fmt,
            OUTPUT_WIDTH, OUTPUT_HEIGHT, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, NULL, NULL, NULL);
    if (pSwsContext == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Create SwsContext...\r\n");
        goto ffmpeg_release;
    }

    pScaledFrame = av_frame_alloc();
    if (pScaledFrame == NULL) {
        goto ffmpeg_release;
    }
    pScaledFrame->format = AV_PIX_FMT_YUV420P;
    pScaledFrame->width = OUTPUT_WIDTH;
    pScaledFrame->height = OUTPUT_HEIGHT;
    if (av_frame_get_buffer(pScaledFrame, 0) < 0) {
        goto ffmpeg_release;
    }

    /** ===== 4. 출력 파일 열기 + 헤더 ===== */
    errorCode = avio_open(&pOutputContext->pb, outputPath, AVIO_FLAG_WRITE);
    if (errorCode < 0) {
        goto ffmpeg_release;
    }
    errorCode = avformat_write_header(pOutputContext, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Write Header...\r\n", errorCode);
        goto ffmpeg_release;
    }

    pInputPacket = av_packet_alloc();
    pDecodedFrame = av_frame_alloc();
    pEncodedPacket = av_packet_alloc();
    if (pInputPacket == NULL || pDecodedFrame == NULL || pEncodedPacket == NULL) {
        goto ffmpeg_release;
    }

    /** ===== 5. 트랜스코딩 메인 루프 ===== */
    while (av_read_frame(pInputContext, pInputPacket) >= 0) {
        if (pInputPacket->stream_index == videoStreamIdx) {
            errorCode = avcodec_send_packet(pDecoderContext, pInputPacket);
            if (errorCode < 0) {
                av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Sending packet to decoder\r\n", errorCode);
                av_packet_unref(pInputPacket);
                break;
            }

            while (true) {
                errorCode = avcodec_receive_frame(pDecoderContext, pDecodedFrame);
                if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
                    break;
                } else if (errorCode < 0) {
                    break;
                }

                writtenPacketCount += ScaleEncodeAndMux(pDecodedFrame, pSwsContext, pScaledFrame,
                                                        pEncoderContext, pOutputStream,
                                                        pOutputContext, pEncodedPacket);
                av_frame_unref(pDecodedFrame);
            }
        }
        av_packet_unref(pInputPacket);
    }

    /** ===== 6. flush: 디코더 먼저, 인코더 나중 ===== */
    errorCode = avcodec_send_packet(pDecoderContext, NULL);
    if (errorCode >= 0) {
        while (true) {
            errorCode = avcodec_receive_frame(pDecoderContext, pDecodedFrame);
            if (errorCode < 0) {
                break;
            }
            writtenPacketCount += ScaleEncodeAndMux(pDecodedFrame, pSwsContext, pScaledFrame,
                                                    pEncoderContext, pOutputStream,
                                                    pOutputContext, pEncodedPacket);
            av_frame_unref(pDecodedFrame);
        }
    }
    writtenPacketCount += EncodeAndMux(pEncoderContext, NULL, pEncodedPacket, pOutputStream, pOutputContext);

    errorCode = av_write_trailer(pOutputContext);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Write Trailer...\r\n", errorCode);
    }

    printf("written packets : %d\r\n", writtenPacketCount);
    printf("\r\n다음 명령으로 재생해 확인:\r\nffplay \"%s\"\r\n", outputPath);

    exitStatus = 0;

    ffmpeg_release:
    av_packet_free(&pEncodedPacket);
    av_frame_free(&pDecodedFrame);
    av_packet_free(&pInputPacket);
    av_frame_free(&pScaledFrame);
    sws_freeContext(pSwsContext);
    avcodec_free_context(&pEncoderContext);
    if (pOutputContext != NULL && pOutputContext->pb != NULL) {
        avio_closep(&pOutputContext->pb);
    }
    avformat_free_context(pOutputContext);
    avcodec_free_context(&pDecoderContext);
    avformat_close_input(&pInputContext);
    if (exitStatus == 0) {
        printf("Transcoding Done!\r\n");
    } else {
        printf("Finished with error(s)...\r\n");
    }
    return exitStatus;
}

int ScaleEncodeAndMux(AVFrame *pDecodedFrame, struct SwsContext *pSwsContext, AVFrame *pScaledFrame,
                      AVCodecContext *pEncoderContext, AVStream *pOutputStream,
                      AVFormatContext *pOutputContext, AVPacket *pEncodedPacket) {
    /**
     * 타임스탬프가 없는 프레임(AV_NOPTS_VALUE)을 그대로 인코더에 넣으면
     * 먹서에서 'non monotonically increasing dts' 에러로 기록이 끊긴다.
     * 이런 프레임은 건너뛴다.
     */
    if (pDecodedFrame->best_effort_timestamp == AV_NOPTS_VALUE) {
        printf("frame without timestamp — skip\r\n");
        return 0;
    }

    if (av_frame_make_writable(pScaledFrame) < 0) {
        return 0;
    }

    /** 해상도 변경 (픽셀 포맷은 YUV420P 유지) */
    sws_scale(pSwsContext,
              (const uint8_t *const *) pDecodedFrame->data, pDecodedFrame->linesize,
              0, pDecodedFrame->height,
              pScaledFrame->data, pScaledFrame->linesize);

    /**
     * pts 전달: 인코더 time_base = 입력 스트림 time_base로 맞췄으므로
     * 디코딩된 프레임의 pts를 그대로 사용할 수 있다.
     * (best_effort_timestamp: pts가 없는 프레임까지 고려한 최선의 타임스탬프)
     */
    pScaledFrame->pts = pDecodedFrame->best_effort_timestamp;

    return EncodeAndMux(pEncoderContext, pScaledFrame, pEncodedPacket, pOutputStream, pOutputContext);
}

int EncodeAndMux(AVCodecContext *pEncoderContext, AVFrame *pFrame, AVPacket *pPacket,
                 AVStream *pOutputStream, AVFormatContext *pOutputContext) {
    int writtenCount = 0;
    int errorCode = 0;

    errorCode = avcodec_send_frame(pEncoderContext, pFrame);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Sending frame to encoder\r\n", errorCode);
        return 0;
    }

    while (errorCode >= 0) {
        errorCode = avcodec_receive_packet(pEncoderContext, pPacket);
        if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
            break;
        } else if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Receive packet\r\n", errorCode);
            break;
        }

        av_packet_rescale_ts(pPacket, pEncoderContext->time_base, pOutputStream->time_base);
        pPacket->stream_index = pOutputStream->index;

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
