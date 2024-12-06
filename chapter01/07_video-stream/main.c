#include <stdlib.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <memory.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/common.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>

#define BUFFER_MAX                          1024

#define FFMPEG_ERROR(errorCode, msg)    \
({                                      \
if((errorCode) < (0)){                  \
av_log(NULL, AV_LOG_ERROR, (msg));      \
return -1;                              \
}else{                                  \
}                                       \
})

#if defined(WIN32) || defined(WIN64)
#include <Windows.h>
#endif

bool GetResourcePath(const char *name, char *const pathBuffer);

void VideoDurationPrint(int64_t duration);

void FFMPEGMetaDataPrint(AVDictionary *pAvDictionary);


int main(int argc, char **argv) {
    char pathBuffer[BUFFER_MAX] = {0};
    AVFormatContext *pContent = NULL;

    int ffmpegErrorCode = 0;

    if (!GetResourcePath("murage.mp4", pathBuffer)) {
        printf("Failed Get Resource Path... %s\r\n", "murage.mp4");
        return -1;
    }

    ffmpegErrorCode = avformat_open_input(&pContent, pathBuffer, NULL, NULL);
    if (ffmpegErrorCode < 0) {
        FFMPEG_ERROR(ffmpegErrorCode, "Failed AVFormat Open...\r\n");
    }
    /** get stream information */
    avformat_find_stream_info(pContent, NULL);

    VideoDurationPrint(pContent->duration);
    FFMPEGMetaDataPrint(pContent->metadata);

    unsigned int numberOfStream = pContent->nb_streams;
    printf("number of Stream : %d\r\n", numberOfStream);
    printf("\r\n-------Stream Meta Data-------\r\n");
    /** get stream */
    for (int i = 0; i < numberOfStream; i++) {
        printf("\r\nStream[%d]\r\n", i);
        /** stream을 구조체로 가져오기 */
        AVStream *pStream = pContent->streams[i];

        /** codec에 대한 파라미터 값을 가져오기 -> parameter에 codec에 대한 정보를 가져오기 위한 정보들이 담겨져 있다. */
        AVCodecParameters *pParameters = pStream->codecpar;

        /** Video Codec 일 경우 */
        if (AVMEDIA_TYPE_VIDEO == pParameters->codec_type) {
            printf("Found a Video Stream\r\n");
            /** video에세만 동영상에 대한 높이, 너비를 가져올 수 있다. */
            int videoWidth = pParameters->width;
            int videoHeight = pParameters->height;
            printf("video [%dx%d]\r\n", videoWidth, videoHeight);
            /** frame rate에 대한 정보를 가져오기 위해서는 av_q2d를 이용해서 가져올 수 있다.*/
            double realFrameRate = av_q2d(pStream->r_frame_rate);
            double avgFrameRate = av_q2d(pStream->avg_frame_rate);
            printf("real frame rate : %lffps, average frame rate : %lffps\r\n", realFrameRate, avgFrameRate);
        }
            /** Audio Codec 일 경우 */
        else if (AVMEDIA_TYPE_AUDIO == pParameters->codec_type) {
            printf("Found an Audio Stream\r\n");
            /** audio channel 에 대닿 넞오블 담고 있는 부분이 cb_layout에 정의가 되어 있다.*/
            AVChannelLayout channelLayout = pParameters->ch_layout;

            int audioChannels = channelLayout.nb_channels;
            printf("audio number of channel : %d, audio sample Rate : %dHz \r\n", audioChannels,
                   pParameters->sample_rate);
        }

        /** codec에 대한 정보를 가져오는 것 -> decoder */
        const AVCodec *pCodec = avcodec_find_decoder(pParameters->codec_id);
        printf("codec ID : %d,codec name : %s, Bit Rate : %lldHz\r\n", pCodec->id, pCodec->name, pParameters->bit_rate);

        FFMPEGMetaDataPrint(pStream->metadata);

        printf("Stream Duration : ");
        VideoDurationPrint(pStream->duration);
    }

    avformat_close_input(&pContent);

    return 0;
}


bool GetResourcePath(const char *name, char *const pathBuffer) {
    char executeBuffer[BUFFER_MAX] = {0};
    char *pRemoveStart;
    int removeEndIdx = 0;

#if defined(WIN32) || defined(WIN64)
    GetCurrentDirectory(BUFFER_MAX, executeBuffer);
    pRemoveStart = strstr(executeBuffer, "\\cmake");
#else
    realpath(".", executeBuffer);
    pRemoveStart = strstr(executeBuffer, "/cmake");
#endif

    if (pRemoveStart == NULL) {
        printf("Failed Get Resource Path...\r\n");
        return false;
    }

    removeEndIdx = (pRemoveStart - executeBuffer);

    memcpy(pathBuffer, executeBuffer, sizeof(char) * removeEndIdx);


#if defined(WIN32) || defined(WIN64)
    strcat(pathBuffer, "\\resources\\");
#else
    strcat(pathBuffer, "/resources/");
#endif
    strcat(pathBuffer, name);
    return true;
}


void VideoDurationPrint(int64_t duration) {
    int64_t seconds = 0;
    int64_t minutes = 0;
    int64_t hours = 0;

    seconds = duration / AV_TIME_BASE;
    minutes = seconds / 60;
    seconds %= 60;

    hours = minutes / 60;
    minutes %= 60;

    printf("duration : %lld:%lld:%lld\r\n", hours, minutes, seconds);
}

void FFMPEGMetaDataPrint(AVDictionary *pAvDictionary) {
    AVDictionaryEntry *pEntry = NULL;
    while ((pEntry = av_dict_get(pAvDictionary, "", pEntry, AV_DICT_IGNORE_SUFFIX))) {
        printf("key : [%s], VALUE : [%s]\r\n", pEntry->key, pEntry->value);
    }
}

