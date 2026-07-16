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
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>

#define BUFFER_MAX                  1024
/** 절대 경로 해석용 버퍼 크기 (Linux PATH_MAX=4096 대응) */
#define RESOURCE_PATH_MAX           4096
/** 인코딩할 프레임 수 / 해상도 / 프레임레이트 */
#define ENCODE_FRAME_COUNT          120
#define ENCODE_WIDTH                640
#define ENCODE_HEIGHT               360
#define ENCODE_FPS                  25

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

/** 프레임(또는 flush 시 NULL)을 인코더에 넣고 나온 패킷을 모두 파일에 쓴다 */
int EncodeAndWrite(AVCodecContext *pCodecContext, AVFrame *pFrame, AVPacket *pPacket, FILE *pOutputFile);

/** index번째 합성 프레임(움직이는 그라데이션)을 YUV420P로 채운다 */
void FillSyntheticFrame(AVFrame *pFrame, int index);

/**
 * study-FFMPEG 08 — 비디오 인코딩
 *
 * 디코딩의 정확한 반대 방향 파이프라인:
 *   avcodec_send_frame()    : 비압축 프레임을 인코더에 넣는다
 *   avcodec_receive_packet(): 압축된 패킷을 꺼낸다
 * 입력 파일 없이 합성 YUV420P 프레임 120장을 만들어
 * H.264(libx264) raw 비트스트림 파일로 인코딩한다.
 * (libx264가 없으면 MPEG-4로 폴백)
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
    FILE *pOutputFile = NULL;

    int writtenPacketCount = 0;
    const char *outputName = NULL;

    if (!EnsureGeneratedStudyDirectory()) {
        printf("Failed Create GeneratedStudy directory...\r\n");
        return -1;
    }

    /**
     * 인코더 찾기.
     * 디코더와 달리 인코더는 이름으로 찾는 경우가 많다
     * (H.264 인코더는 libx264/h264_videotoolbox 등 여러 구현이 있기 때문).
     */
    pEncoder = avcodec_find_encoder_by_name("libx264");
    if (pEncoder != NULL) {
        outputName = "GeneratedStudy/study-encoded.h264";
    } else {
        printf("libx264 not found → fallback to MPEG-4 encoder\r\n");
        pEncoder = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
        outputName = "GeneratedStudy/study-encoded.m4v";
    }
    if (pEncoder == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Find Video Encoder...\r\n");
        return -1;
    }
    printf("encoder : %s (%s)\r\n", pEncoder->name, pEncoder->long_name);

    if (!GetResourcePath(outputName, outputPath)) {
        return -1;
    }

    pEncoderContext = avcodec_alloc_context3(pEncoder);
    if (pEncoderContext == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load Encoder Context...\r\n");
        return -1;
    }

    /**
     * 인코더 설정.
     * 디코딩 때는 파일에서 읽은 파라미터를 복사했지만,
     * 인코딩은 우리가 직접 출력 사양을 정해야 한다.
     */
    pEncoderContext->width = ENCODE_WIDTH;
    pEncoderContext->height = ENCODE_HEIGHT;
    pEncoderContext->pix_fmt = AV_PIX_FMT_YUV420P;
    /** time_base = 1/fps → pts 1 증가 = 1프레임 시간 */
    pEncoderContext->time_base = (AVRational) {1, ENCODE_FPS};
    pEncoderContext->framerate = (AVRational) {ENCODE_FPS, 1};
    pEncoderContext->bit_rate = 1000000;
    /** GOP: 키프레임 간격. B-프레임도 2장까지 허용 */
    pEncoderContext->gop_size = 25;
    pEncoderContext->max_b_frames = 2;

    /** libx264 전용 옵션은 priv_data에 문자열로 설정한다 */
    if (strcmp(pEncoder->name, "libx264") == 0) {
        av_opt_set(pEncoderContext->priv_data, "preset", "fast", 0);
    }

    errorCode = avcodec_open2(pEncoderContext, pEncoder, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Open Encoder...\r\n", errorCode);
        goto ffmpeg_release;
    }

    pFrame = av_frame_alloc();
    pPacket = av_packet_alloc();
    if (pFrame == NULL || pPacket == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Load frame/packet structure...\r\n");
        goto ffmpeg_release;
    }

    /**
     * 인코딩용 프레임은 직접 픽셀 버퍼를 할당해야 한다.
     * (디코딩 때는 디코더가 버퍼를 채워줬다)
     */
    pFrame->format = AV_PIX_FMT_YUV420P;
    pFrame->width = ENCODE_WIDTH;
    pFrame->height = ENCODE_HEIGHT;
    errorCode = av_frame_get_buffer(pFrame, 0);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Allocate Frame Buffer...\r\n", errorCode);
        goto ffmpeg_release;
    }

    pOutputFile = fopen(outputPath, "wb");
    if (pOutputFile == NULL) {
        printf("Failed Open Output File... (%s)\r\n", outputPath);
        goto ffmpeg_release;
    }

    /** 합성 프레임 생성 → 인코딩 루프 */
    for (int frameIdx = 0; frameIdx < ENCODE_FRAME_COUNT; ++frameIdx) {
        /**
         * 인코더가 아직 이전 프레임 버퍼를 참조 중일 수 있으므로
         * 쓰기 전에 반드시 writable 상태로 만든다 (필요 시 내부 복사 발생).
         */
        errorCode = av_frame_make_writable(pFrame);
        if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Make Frame Writable...\r\n", errorCode);
            break;
        }

        FillSyntheticFrame(pFrame, frameIdx);
        /** pts는 time_base(1/25초) 단위 → 프레임 번호가 곧 pts */
        pFrame->pts = frameIdx;

        writtenPacketCount += EncodeAndWrite(pEncoderContext, pFrame, pPacket, pOutputFile);
    }

    /** 인코더 flush: NULL 프레임을 보내 내부에 남은 패킷을 모두 꺼낸다 */
    writtenPacketCount += EncodeAndWrite(pEncoderContext, NULL, pPacket, pOutputFile);

    printf("encoded frames : %d → written packets : %d\r\n", ENCODE_FRAME_COUNT, writtenPacketCount);
    printf("output : %s\r\n", outputPath);
    printf("\r\n다음 명령으로 재생해 확인:\r\nffplay \"%s\"\r\n", outputPath);

    exitStatus = 0;

    ffmpeg_release:
    if (pOutputFile != NULL) {
        fclose(pOutputFile);
    }
    av_packet_free(&pPacket);
    av_frame_free(&pFrame);
    avcodec_free_context(&pEncoderContext);
    if (exitStatus == 0) {
        printf("Encode Video Done!\r\n");
    } else {
        printf("Finished with error(s)...\r\n");
    }
    return exitStatus;
}

int EncodeAndWrite(AVCodecContext *pCodecContext, AVFrame *pFrame, AVPacket *pPacket, FILE *pOutputFile) {
    int writtenCount = 0;
    int errorCode = 0;

    /** NULL 프레임 = "입력 끝" 신호 (flush 시작) */
    errorCode = avcodec_send_frame(pCodecContext, pFrame);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Sending frame to encoder\r\n", errorCode);
        return 0;
    }

    while (errorCode >= 0) {
        errorCode = avcodec_receive_packet(pCodecContext, pPacket);
        /**
         * EAGAIN: 패킷을 내놓으려면 프레임 입력이 더 필요함
         * (B-프레임/lookahead 때문에 인코더도 지연이 있다)
         */
        if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
            break;
        } else if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Receive packet\r\n", errorCode);
            break;
        }

        /** raw 비트스트림이므로 패킷 데이터를 그대로 이어 쓰면 된다 */
        fwrite(pPacket->data, 1, pPacket->size, pOutputFile);
        writtenCount++;
        av_packet_unref(pPacket);
    }

    return writtenCount;
}

void FillSyntheticFrame(AVFrame *pFrame, int index) {
    /** Y 평면: 대각선 방향으로 흐르는 그라데이션 */
    for (int y = 0; y < pFrame->height; ++y) {
        for (int x = 0; x < pFrame->width; ++x) {
            pFrame->data[0][y * pFrame->linesize[0] + x] = (uint8_t) (x + y + index * 3);
        }
    }
    /** Cb/Cr 평면: YUV420P는 색차 평면이 가로/세로 절반 크기다 */
    for (int y = 0; y < pFrame->height / 2; ++y) {
        for (int x = 0; x < pFrame->width / 2; ++x) {
            pFrame->data[1][y * pFrame->linesize[1] + x] = (uint8_t) (128 + y + index * 2);
            pFrame->data[2][y * pFrame->linesize[2] + x] = (uint8_t) (64 + x + index * 5);
        }
    }
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
