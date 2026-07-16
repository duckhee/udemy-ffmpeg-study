#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
// FFMPEG Library
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/channel_layout.h>

#if defined(WIN32) || defined(WIN64)

#include <Windows.h>

#endif

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

/** 비디오 스트림 정보 출력 */
void PrintVideoStreamInfo(const AVStream *pStream, const AVCodec *pCodec);

/** 오디오 스트림 정보 출력 */
void PrintAudioStreamInfo(const AVStream *pStream, const AVCodec *pCodec);

/**
 * study-FFMPEG 02 — 스트림과 코덱 파라미터
 *
 * 컨테이너 안의 모든 스트림(AVStream)을 순회하면서
 * 각 스트림의 종류(비디오/오디오/자막), 코덱, time_base를 해석한다.
 */
int main(int argc, char **argv) {
    char resourcePath[BUFFER_MAX] = {0};
    int errorCode = -1;

    AVFormatContext *pFormatContext = NULL;

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
        avformat_close_input(&pFormatContext);
        return -1;
    }

    printf("number of stream : %u\r\n\r\n", pFormatContext->nb_streams);

    /** 모든 스트림 순회 */
    for (int idx = 0; idx < (int) pFormatContext->nb_streams; ++idx) {
        AVStream *pCurStream = pFormatContext->streams[idx];
        /**
         * AVCodecParameters:
         * 스트림 헤더에 기록된 "코덱 설정값" (디코딩 자체는 못 한다).
         * 실제 디코딩은 이 파라미터로 AVCodecContext를 만들어서 한다.
         */
        AVCodecParameters *pCodecParameters = pCurStream->codecpar;
        /** codec_id로 이 스트림을 디코딩할 수 있는 디코더를 찾는다 */
        const AVCodec *pCodec = avcodec_find_decoder(pCodecParameters->codec_id);

        printf("==== Stream #%d ====\r\n", idx);
        /** 스트림 종류를 사람이 읽을 수 있는 문자열로 변환 */
        printf("media type     : %s\r\n", av_get_media_type_string(pCodecParameters->codec_type));

        if (pCodec == NULL) {
            printf("codec          : (decoder not found, codec_id=%d)\r\n\r\n", pCodecParameters->codec_id);
            continue;
        }

        /**
         * time_base:
         * 이 스트림의 pts/dts/duration이 사용하는 시간 단위(분수).
         * 예: 1/12800 이면 pts 1 = 1/12800 초.
         */
        printf("time_base      : %d/%d\r\n", pCurStream->time_base.num, pCurStream->time_base.den);
        /** 스트림 duration(스트림 time_base 단위)을 초로 변환 */
        if (pCurStream->duration != AV_NOPTS_VALUE) {
            printf("duration       : %.2f sec\r\n", pCurStream->duration * av_q2d(pCurStream->time_base));
        }

        if (pCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            PrintVideoStreamInfo(pCurStream, pCodec);
        } else if (pCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            PrintAudioStreamInfo(pCurStream, pCodec);
        } else if (pCodecParameters->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            printf("codec          : %s\r\n", pCodec->name);
        }
        printf("\r\n");
    }

    avformat_close_input(&pFormatContext);
    printf("Stream Info Done!\r\n");
    return 0;
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

void PrintVideoStreamInfo(const AVStream *pStream, const AVCodec *pCodec) {
    const AVCodecParameters *pCodecParameters = pStream->codecpar;
    /**
     * avg_frame_rate: 컨테이너 기준 평균 프레임레이트.
     * av_q2d로 AVRational(분수)을 double로 변환한다.
     */
    double frameRate = av_q2d(pStream->avg_frame_rate);

    printf("codec          : %s (%s)\r\n", pCodec->name, pCodec->long_name);
    printf("resolution     : %dx%d\r\n", pCodecParameters->width, pCodecParameters->height);
    printf("frame rate     : %.2f fps\r\n", frameRate);
    /** 픽셀 포맷(yuv420p 등) — codecpar에서는 format 필드에 들어있다 */
    printf("pixel format   : %s\r\n", av_get_pix_fmt_name((enum AVPixelFormat) pCodecParameters->format));
    printf("bit rate       : %lld bps\r\n", pCodecParameters->bit_rate);
}

void PrintAudioStreamInfo(const AVStream *pStream, const AVCodec *pCodec) {
    const AVCodecParameters *pCodecParameters = pStream->codecpar;
    char channelLayoutText[BUFFER_MAX] = {0};

    /**
     * FFmpeg 7.x의 채널 레이아웃은 AVChannelLayout 구조체(ch_layout)다.
     * (과거의 uint64_t channel_layout 필드는 제거되었다)
     * av_channel_layout_describe로 "stereo" 같은 설명 문자열을 얻는다.
     */
    av_channel_layout_describe(&pCodecParameters->ch_layout, channelLayoutText, sizeof(channelLayoutText));

    printf("codec          : %s (%s)\r\n", pCodec->name, pCodec->long_name);
    printf("sample rate    : %d Hz\r\n", pCodecParameters->sample_rate);
    printf("channels       : %d (%s)\r\n", pCodecParameters->ch_layout.nb_channels, channelLayoutText);
    printf("bit rate       : %lld bps\r\n", pCodecParameters->bit_rate);
}
