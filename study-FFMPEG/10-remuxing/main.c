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

/**
 * study-FFMPEG 10 — 리먹싱 (컨테이너 변환)
 *
 * 디코딩/인코딩 없이 압축 패킷을 그대로 다른 컨테이너로 옮긴다 (mp4 → mkv).
 * ffmpeg -i in.mp4 -c copy out.mkv 와 같은 동작이다.
 * 화질 손실이 전혀 없고 매우 빠르다.
 *
 * 핵심은 타임스탬프 변환:
 * 입력 스트림과 출력 스트림의 time_base가 다를 수 있으므로
 * 패킷마다 av_packet_rescale_ts로 pts/dts/duration을 다시 계산한다.
 */
int main(int argc, char **argv) {
    char inputPath[BUFFER_MAX] = {0};
    char outputPath[BUFFER_MAX] = {0};
    int errorCode = -1;
    /** 프로그램 종료 코드: 성공 경로 끝에서만 0으로 바뀐다 */
    int exitStatus = -1;

    AVFormatContext *pInputContext = NULL;
    AVFormatContext *pOutputContext = NULL;
    AVPacket *pPacket = NULL;

    /** 입력 스트림 index → 출력 스트림 index 매핑 (복사 안 하는 스트림은 -1) */
    int *pStreamMapping = NULL;
    int outputStreamCount = 0;
    int64_t writtenPacketCount = 0;

    if (!GetResourcePath("murage.mp4", inputPath)) {
        printf("Failed Get murage.mp4 resource path...\r\n");
        return -1;
    }
    if (!EnsureGeneratedStudyDirectory()) {
        printf("Failed Create GeneratedStudy directory...\r\n");
        return -1;
    }
    if (!GetResourcePath("GeneratedStudy/study-remux.mkv", outputPath)) {
        return -1;
    }
    printf("input  : %s\r\n", inputPath);
    printf("output : %s\r\n", outputPath);

    /** ===== 입력 열기 ===== */
    errorCode = avformat_open_input(&pInputContext, inputPath, NULL, NULL);
    FFMPEG_ERROR(errorCode, "[FFMPEG ERROR] FFMPEG Open Failed...\r\n")

    errorCode = avformat_find_stream_info(pInputContext, NULL);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) find Stream Failed...\r\n", errorCode);
        goto ffmpeg_release;
    }

    /**
     * ===== 출력 컨텍스트 생성 =====
     * 포맷 이름을 NULL로 주면 파일 확장자(.mkv)로 컨테이너를 추론한다.
     */
    errorCode = avformat_alloc_output_context2(&pOutputContext, NULL, NULL, outputPath);
    if (errorCode < 0 || pOutputContext == NULL) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Allocate Output Context...\r\n", errorCode);
        goto ffmpeg_release;
    }
    printf("output format : %s\r\n", pOutputContext->oformat->name);

    pStreamMapping = av_calloc(pInputContext->nb_streams, sizeof(int));
    if (pStreamMapping == NULL) {
        goto ffmpeg_release;
    }

    /** ===== 입력 스트림을 출력 스트림으로 복사 ===== */
    for (int idx = 0; idx < (int) pInputContext->nb_streams; ++idx) {
        AVStream *pInputStream = pInputContext->streams[idx];
        AVStream *pOutputStream = NULL;
        enum AVMediaType mediaType = pInputStream->codecpar->codec_type;

        /** 비디오/오디오/자막만 복사하고 데이터 스트림 등은 건너뛴다 */
        if (mediaType != AVMEDIA_TYPE_VIDEO &&
            mediaType != AVMEDIA_TYPE_AUDIO &&
            mediaType != AVMEDIA_TYPE_SUBTITLE) {
            pStreamMapping[idx] = -1;
            continue;
        }

        pStreamMapping[idx] = outputStreamCount++;

        pOutputStream = avformat_new_stream(pOutputContext, NULL);
        if (pOutputStream == NULL) {
            av_log(NULL, AV_LOG_ERROR, "Failed Create Output Stream...\r\n");
            goto ffmpeg_release;
        }

        /**
         * 코덱 파라미터를 그대로 복사 — 이것이 "재인코딩 없음"의 핵심이다.
         * codec_tag는 컨테이너마다 다르므로 0으로 초기화해
         * 새 컨테이너(mkv)가 스스로 결정하게 한다.
         */
        errorCode = avcodec_parameters_copy(pOutputStream->codecpar, pInputStream->codecpar);
        if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Copy Codec Parameters...\r\n", errorCode);
            goto ffmpeg_release;
        }
        pOutputStream->codecpar->codec_tag = 0;
    }

    /** ===== 출력 파일 열기 + 헤더 ===== */
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
        av_log(NULL, AV_LOG_ERROR, "Failed Load packet structure...\r\n");
        goto ffmpeg_release;
    }

    /** ===== 패킷 복사 루프 ===== */
    while (av_read_frame(pInputContext, pPacket) >= 0) {
        AVStream *pInputStream = NULL;
        AVStream *pOutputStream = NULL;

        if (pPacket->stream_index >= (int) pInputContext->nb_streams ||
            pStreamMapping[pPacket->stream_index] < 0) {
            av_packet_unref(pPacket);
            continue;
        }

        pInputStream = pInputContext->streams[pPacket->stream_index];
        pPacket->stream_index = pStreamMapping[pPacket->stream_index];
        pOutputStream = pOutputContext->streams[pPacket->stream_index];

        /**
         * 타임스탬프 재계산: 입력 time_base → 출력 time_base.
         * mp4는 1/12800 같은 단위, mkv는 1/1000(밀리초) 단위를 쓰므로
         * 변환하지 않으면 재생 속도가 완전히 어긋난다.
         */
        av_packet_rescale_ts(pPacket, pInputStream->time_base, pOutputStream->time_base);
        /** pos는 원본 파일 내 위치라 새 파일에선 의미가 없다 → 미정(-1)으로 */
        pPacket->pos = -1;

        /**
         * av_interleaved_write_frame:
         * 스트림들이 dts 순서로 올바르게 섞이도록(인터리빙) 버퍼링하며 쓴다.
         */
        errorCode = av_interleaved_write_frame(pOutputContext, pPacket);
        if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Write frame\r\n", errorCode);
            break;
        }
        writtenPacketCount++;
    }

    /** 트레일러 쓰기 (mkv 인덱스 등 마무리 정보) */
    errorCode = av_write_trailer(pOutputContext);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Write Trailer...\r\n", errorCode);
    }

    printf("written packets : %lld\r\n", writtenPacketCount);
    printf("\r\n다음 명령으로 확인:\r\nffprobe \"%s\"\r\n", outputPath);

    exitStatus = 0;

    ffmpeg_release:
    av_packet_free(&pPacket);
    av_freep(&pStreamMapping);
    if (pOutputContext != NULL && pOutputContext->pb != NULL) {
        avio_closep(&pOutputContext->pb);
    }
    avformat_free_context(pOutputContext);
    avformat_close_input(&pInputContext);
    if (exitStatus == 0) {
        printf("Remuxing Done!\r\n");
    } else {
        printf("Finished with error(s)...\r\n");
    }
    return exitStatus;
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
