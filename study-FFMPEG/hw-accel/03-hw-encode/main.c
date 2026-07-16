#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
#include <libavutil/mathematics.h>

#define BUFFER_MAX                  1024
/** 절대 경로 해석용 버퍼 크기 (Linux PATH_MAX=4096 대응) */
#define RESOURCE_PATH_MAX           4096

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

#if defined(__APPLE__)

/** 프레임(NULL이면 flush)을 인코딩해 먹서로 쓴다 */
static int EncodeAndMux(AVCodecContext *pEncoderContext, AVFrame *pFrame, AVPacket *pPacket,
                        AVStream *pOutputStream, AVFormatContext *pOutputContext);

#endif

/**
 * study-FFMPEG HW-03 — VideoToolbox 하드웨어 인코딩
 *
 * murage.mp4를 SW 디코딩한 뒤 h264_videotoolbox 인코더로 재인코딩한다
 * (12 트랜스코딩 레슨의 HW 인코더 버전, 스케일링 없음).
 *
 * h264_videotoolbox의 편리한 점:
 * SW 프레임(YUV420P/NV12)을 직접 받아 인코더가 내부에서 GPU로 올린다.
 * 따라서 hw_frames_ctx 설정 없이 SW 인코더와 거의 같은 코드로 쓸 수 있다.
 * libx264와 CPU 시간을 비교해 보는 것이 이 레슨의 목적이다.
 */
int main(int argc, char **argv) {
#if !defined(__APPLE__)
    printf("This lesson requires macOS (VideoToolbox). Skipped.\r\n");
    return 0;
#else
    char inputPath[BUFFER_MAX] = {0};
    char outputPath[BUFFER_MAX] = {0};
    int errorCode = -1;
    /** 프로그램 종료 코드: 성공 경로 끝에서만 0으로 바뀐다 */
    int exitStatus = -1;

    AVFormatContext *pInputContext = NULL;
    const AVCodec *pDecoder = NULL;
    AVCodecContext *pDecoderContext = NULL;
    int videoStreamIdx = -1;
    AVStream *pInputStream = NULL;
    AVPacket *pInputPacket = NULL;
    AVFrame *pDecodedFrame = NULL;

    AVFormatContext *pOutputContext = NULL;
    const AVCodec *pEncoder = NULL;
    AVCodecContext *pEncoderContext = NULL;
    AVStream *pOutputStream = NULL;
    AVPacket *pEncodedPacket = NULL;

    int encodedFrameCount = 0;
    int writtenPacketCount = 0;
    clock_t startClock;
    double elapsedSeconds = 0.0;

    if (!GetResourcePath("murage.mp4", inputPath)) {
        printf("Failed Get murage.mp4 resource path...\r\n");
        return -1;
    }
    if (!EnsureGeneratedStudyDirectory()) {
        printf("Failed Create GeneratedStudy directory...\r\n");
        return -1;
    }
    if (!GetResourcePath("GeneratedStudy/study-hw-encoded.mp4", outputPath)) {
        return -1;
    }
    printf("input  : %s\r\n", inputPath);
    printf("output : %s\r\n", outputPath);

    /** ===== 1. VideoToolbox H.264 인코더 확인 ===== */
    pEncoder = avcodec_find_encoder_by_name("h264_videotoolbox");
    if (pEncoder == NULL) {
        printf("h264_videotoolbox encoder not available in this FFmpeg build.\r\n");
        printf("(vcpkg ffmpeg[all]에는 포함되어 있다 — 빌드 설정을 확인하자)\r\n");
        return 0;
    }
    printf("encoder : %s (%s)\r\n", pEncoder->name, pEncoder->long_name);

    /** ===== 2. 입력 + SW 디코더 준비 ===== */
    errorCode = avformat_open_input(&pInputContext, inputPath, NULL, NULL);
    FFMPEG_ERROR(errorCode, "[FFMPEG ERROR] FFMPEG Open Failed...\r\n")

    errorCode = avformat_find_stream_info(pInputContext, NULL);
    if (errorCode < 0) {
        goto ffmpeg_release;
    }

    videoStreamIdx = av_find_best_stream(pInputContext, AVMEDIA_TYPE_VIDEO, -1, -1, &pDecoder, 0);
    if (videoStreamIdx < 0) {
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
        goto ffmpeg_release;
    }

    /** ===== 3. 출력 + HW 인코더 준비 ===== */
    errorCode = avformat_alloc_output_context2(&pOutputContext, NULL, NULL, outputPath);
    if (errorCode < 0 || pOutputContext == NULL) {
        goto ffmpeg_release;
    }

    pEncoderContext = avcodec_alloc_context3(pEncoder);
    if (pEncoderContext == NULL) {
        goto ffmpeg_release;
    }

    /** 해상도는 원본 유지, 비트레이트만 지정 */
    pEncoderContext->width = pDecoderContext->width;
    pEncoderContext->height = pDecoderContext->height;
    /** SW 프레임을 그대로 공급 — 인코더가 내부에서 GPU로 업로드한다 */
    pEncoderContext->pix_fmt = pDecoderContext->pix_fmt;
    pEncoderContext->bit_rate = 2000000;
    pEncoderContext->time_base = pInputStream->time_base;
    pEncoderContext->framerate = av_guess_frame_rate(pInputContext, pInputStream, NULL);

    if (pOutputContext->oformat->flags & AVFMT_GLOBALHEADER) {
        pEncoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    errorCode = avcodec_open2(pEncoderContext, pEncoder, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Open HW Encoder...\r\n", errorCode);
        printf("VideoToolbox encoder open failed — HW encoding not available on this machine.\r\n");
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

    errorCode = avio_open(&pOutputContext->pb, outputPath, AVIO_FLAG_WRITE);
    if (errorCode < 0) {
        goto ffmpeg_release;
    }
    errorCode = avformat_write_header(pOutputContext, NULL);
    if (errorCode < 0) {
        goto ffmpeg_release;
    }

    pInputPacket = av_packet_alloc();
    pDecodedFrame = av_frame_alloc();
    pEncodedPacket = av_packet_alloc();
    if (pInputPacket == NULL || pDecodedFrame == NULL || pEncodedPacket == NULL) {
        goto ffmpeg_release;
    }

    /** ===== 4. 디코딩 → HW 인코딩 루프 ===== */
    startClock = clock();

    while (av_read_frame(pInputContext, pInputPacket) >= 0) {
        if (pInputPacket->stream_index == videoStreamIdx) {
            errorCode = avcodec_send_packet(pDecoderContext, pInputPacket);
            if (errorCode < 0) {
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

                /** 타임스탬프 없는 프레임은 먹싱 에러를 일으키므로 건너뛴다 */
                if (pDecodedFrame->best_effort_timestamp == AV_NOPTS_VALUE) {
                    printf("frame without timestamp — skip\r\n");
                    av_frame_unref(pDecodedFrame);
                    continue;
                }

                /** 인코더 time_base = 입력 스트림 time_base → pts 그대로 전달 */
                pDecodedFrame->pts = pDecodedFrame->best_effort_timestamp;
                encodedFrameCount++;
                writtenPacketCount += EncodeAndMux(pEncoderContext, pDecodedFrame, pEncodedPacket,
                                                   pOutputStream, pOutputContext);
                av_frame_unref(pDecodedFrame);
            }
        }
        av_packet_unref(pInputPacket);
    }

    /** flush: 디코더 → 인코더 순서 */
    errorCode = avcodec_send_packet(pDecoderContext, NULL);
    if (errorCode >= 0) {
        while (avcodec_receive_frame(pDecoderContext, pDecodedFrame) >= 0) {
            if (pDecodedFrame->best_effort_timestamp == AV_NOPTS_VALUE) {
                av_frame_unref(pDecodedFrame);
                continue;
            }
            pDecodedFrame->pts = pDecodedFrame->best_effort_timestamp;
            encodedFrameCount++;
            writtenPacketCount += EncodeAndMux(pEncoderContext, pDecodedFrame, pEncodedPacket,
                                               pOutputStream, pOutputContext);
            av_frame_unref(pDecodedFrame);
        }
    }
    writtenPacketCount += EncodeAndMux(pEncoderContext, NULL, pEncodedPacket, pOutputStream, pOutputContext);

    errorCode = av_write_trailer(pOutputContext);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Write Trailer...\r\n", errorCode);
    }

    elapsedSeconds = (double) (clock() - startClock) / CLOCKS_PER_SEC;
    printf("encoded frames : %d → written packets : %d\r\n", encodedFrameCount, writtenPacketCount);
    printf("elapsed : %.3f sec (CPU time) → %.1f fps\r\n",
           elapsedSeconds, elapsedSeconds > 0 ? encodedFrameCount / elapsedSeconds : 0.0);
    printf("(12 레슨의 libx264 트랜스코딩과 CPU 시간을 비교해 보자)\r\n");
    printf("\r\n다음 명령으로 재생해 확인:\r\nffplay \"%s\"\r\n", outputPath);

    exitStatus = 0;

    ffmpeg_release:
    av_packet_free(&pEncodedPacket);
    av_frame_free(&pDecodedFrame);
    av_packet_free(&pInputPacket);
    avcodec_free_context(&pEncoderContext);
    if (pOutputContext != NULL && pOutputContext->pb != NULL) {
        avio_closep(&pOutputContext->pb);
    }
    avformat_free_context(pOutputContext);
    avcodec_free_context(&pDecoderContext);
    avformat_close_input(&pInputContext);
    if (exitStatus == 0) {
        printf("HW Encode Done!\r\n");
    } else {
        printf("Finished with error(s)...\r\n");
    }
    return exitStatus;
#endif
}

#if defined(__APPLE__)

static int EncodeAndMux(AVCodecContext *pEncoderContext, AVFrame *pFrame, AVPacket *pPacket,
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
            break;
        }
        writtenCount++;
    }

    return writtenCount;
}

#endif

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
