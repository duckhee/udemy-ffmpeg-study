#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <stdbool.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/common.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>

#if defined(WIN32) || defined(WIN64)
#include <Windows.h>
#endif

#define BUFFER_MAX                      1024

bool GetResourcePath(const char *name, char *const pathBuffer);

/**
 * Linux에서는 memory 에 대한 leak 을 확인하기 위해서 valgrind 를 이용을 한다.
 * => valgrind <program>
 * Mac에서는 memory 에 대한 leak 을 확인하기 위해서 leaks 를 이용해서 메모리에 대한 확인을 한다.
 * => leaks --atExit -- <program>
 * */

int main(int argc, char **argv) {
    printf("FFMPEG Programming !\r\n");
    return 0;
}


bool GetResourcePath(const char *name, char *const pathBuffer) {
    char executeBuffer[BUFFER_MAX] = {0};
    char *pRemoveStart;
    int removeEndIdx = 0;

#if defined(WIN32) || defined(WIN64)
    GetCurrentDirectory(BUFFER_MAX, executeBuffer);
    pRemoveStart = strstr("\\cmake");
#else
    realpath(".", executeBuffer);
    pRemoveStart = strstr(executeBuffer, "/cmake");
#endif

    if (pRemoveStart == NULL) {
        printf("Failed Get Root Path...\r\n");
        return false;
    }

    removeEndIdx = (int) (pRemoveStart - executeBuffer);
    memcpy(pathBuffer, executeBuffer, sizeof(char) * removeEndIdx);

#if defined(WIN32) || defined(WIN64)
    strcat(pathBuffer, "\\resources\\");
#else
    strcat(pathBuffer, "/resources/");
#endif
    strcat(pathBuffer, name);
    return true;
}

