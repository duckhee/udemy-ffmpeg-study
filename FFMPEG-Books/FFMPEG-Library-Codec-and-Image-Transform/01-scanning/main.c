#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <memory.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#if defined(WIN32) || defined(WIN64)

#include <Windows.h>

#endif

#define BUFFER_MAX                          1024

bool GetResourcePath(const char *name, char *const pathBuffer);

int main(int argc, char **argv) {
    /** FFMPEG 에서 동영상에 대한 Container 를 담아 주기 위한 구조체 선언 */
    AVFormatContext *pAvFormatContext = NULL;
    int errorCode = 0;
    /** Log에 대한 사용할 레벨을 디버깅 레벨로 설정 한다. -> Debug 정보에 대해서 출력을 해준다. */
    av_log_set_level(AV_LOG_DEBUG);

    char videoPath[BUFFER_MAX] = {0};

    if (argc < 2) {
        printf("usage : %s <input>\r\n", argv[0]);
        if (!GetResourcePath("out.mp4", videoPath)) {
            printf("Failed to get Resource Path...\r\n");
            return -1;
        }
    }
        /** user input program set */
    else {
        printf("usage : %s\r\n", argv[0]);
        strcpy(videoPath, argv[1]);
    }

    /** 주어진 파일로 부터 Container 를 가져 오기 */
    if ((errorCode = avformat_open_input(&pAvFormatContext, videoPath, NULL, NULL)) < 0) {
        printf("Failed Could not open input file : %s\r\n", videoPath);
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG_ERROR %d] Failed Get Video Context\r\n", errorCode);
        return -1;
    }

    /** 유효한 AVFormatContext로부터 유효한 스트림이 있는지 확인을 하는 코드 */
    if ((errorCode = avformat_find_stream_info(pAvFormatContext, NULL)) < 0) {
        printf("Failed to retrive input stream information\r\n");
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG_ERROR %d] Failed Found Stream ....\r\n", errorCode);
        return -2;
    }


    /** 유효한 스트림에 대해서 순회를 하면서 정보 출력 */
    for (int index = 0; index < pAvFormatContext->nb_streams; index++) {
        /** Codec에 대한 파라미터에 대한 정보를 가져오기 */
        AVCodecParameters *pAvCodecParameters = pAvFormatContext->streams[index]->codecpar;

        /** 스트림의 타입이 비디오 타입일 경우 */
        if (AVMEDIA_TYPE_VIDEO == pAvCodecParameters->codec_type) {
            printf("-------------- Video Information --------------\r\n");
            printf("codec_id : %d\r\n", pAvCodecParameters->codec_id);
            printf("bit rate : %lld\r\n", pAvCodecParameters->bit_rate);
            printf("Image size : [%dx%d]\r\n", pAvCodecParameters->width, pAvCodecParameters->height);
            printf("------------------------------------------\r\n\r\n");
        }
            /** 스트림의 타입이 오디오 타입일 경우 */
        else if (AVMEDIA_TYPE_AUDIO == pAvCodecParameters->codec_type) {
            printf("-------------- Audio Information --------------\r\n");
            printf("codec_id : %d\r\n", pAvCodecParameters->codec_id);
            printf("bit rate : %lld\r\n", pAvCodecParameters->bit_rate);
            printf("sample rate : %d\r\n", pAvCodecParameters->sample_rate);
            printf("number of audio channel : %d\r\n", pAvCodecParameters->ch_layout.nb_channels);
            printf("------------------------------------------\r\n\r\n");
        }
            /** 스트림의 타입이 자막 타입일 경우 */
        else if (AVMEDIA_TYPE_SUBTITLE == pAvCodecParameters->codec_type) {
            printf("-------------- SubTitle Information --------------\r\n");

            printf("------------------------------------------\r\n\r\n");
        }
    }

    return 0;
}


bool GetResourcePath(const char *name, char *const pathBuffer) {

    char tempBuffer[BUFFER_MAX] = {0,};
    char *pRemoveStart = NULL;
    int removeEndIdx = 0;


#if defined(WIN32) || defined(WIN64)
    GetCurrentDirectory(BUFFER_MAX, tempBuffer);
    pRemoveStart = strstr(tempBuffer, "\\cmake");
#else
    realpath(".", tempBuffer);
    pRemoveStart = strstr(tempBuffer, "/cmake");
#endif

    if (pRemoveStart == NULL) {
        return false;
    }

    removeEndIdx = (int) (pRemoveStart - tempBuffer);

    memcpy(pathBuffer, tempBuffer, sizeof(char) * removeEndIdx);

#if defined(WIN32) || defined(WIN64)
    strcat(pathBuffer, "\\resources\\");
#else
    strcat(pathBuffer, "/resources/");
#endif
    strcat(pathBuffer, name);
    return true;
}



