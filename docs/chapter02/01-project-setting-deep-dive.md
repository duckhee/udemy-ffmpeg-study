# 01. 프로젝트 셋업 — 코드 상세 해설

> [← 기본 문서](01-project-setting.md)

## 전체 구조

파일은 세 부분으로 구성된다.

| 구성 요소 | 내용 |
|---|---|
| include 블록 | FFmpeg 4개 라이브러리 + stb + 표준 헤더 + Windows 분기 |
| `main()` | 확인용 문자열 출력만 수행 |
| `GetResourcePath()` | 리소스 절대 경로 계산 헬퍼 (이 레슨에서는 미호출) |

## 코드 블록별 해설

### include와 플랫폼 분기

```c
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/common.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <stb_image.h>
#include <stb_image_write.h>

#if defined(WIN32) || defined(WIN64)

#include <Windows.h>

#endif
```

FFmpeg의 4개 서브 라이브러리(avformat, avcodec, swscale, avutil) 헤더와 stb 헤더를 모두 include 한다. 아직 사용하는 코드는 없지만, 컴파일이 통과하면 include 경로 설정이 정상이라는 뜻이므로 "프로젝트 셋업 확인" 레슨의 목적에 부합한다. `WIN32`/`WIN64` 분기는 이후 `GetCurrentDirectory()`를 위해 `Windows.h`를 조건부로 포함한다.

### main — 동작 확인

```c
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
```

빌드·실행이 되는지만 확인한다. 주석으로 플랫폼별 메모리 누수 검사 명령을 기록해 두었는데, 이는 레슨 08(올바른 메모리 해제)의 복선이다.

### GetResourcePath — 리소스 경로 역산

```c
#if defined(WIN32) || defined(WIN64)
    GetCurrentDirectory(BUFFER_MAX, executeBuffer);
    pRemoveStart = strstr(executeBuffer, "\\cmake");
#else
    realpath(".", executeBuffer);
    pRemoveStart = strstr(executeBuffer, "/cmake");
#endif
```

핵심 아이디어는 "실행 파일은 항상 `<저장소 루트>/cmake-build-.../` 아래에서 실행된다"는 전제다. 현재 작업 디렉터리의 절대 경로에서 `/cmake` 부분 문자열의 시작 위치를 찾고(`strstr`), 그 앞까지(= 저장소 루트)를 `memcpy`로 복사한 뒤 `/resources/<name>` 을 이어붙인다. 이후 모든 레슨이 이 함수를 그대로 복사해 사용하므로, 각 레슨 문서에서는 반복 설명을 생략한다.

전제 조건이 있는 함수라는 점에 유의한다. 작업 디렉터리 경로에 `/cmake` 가 없으면(예: 저장소 루트에서 실행) `false`를 반환하며 프로그램이 리소스를 찾지 못한다.

## 심화

### CMake 타겟 단위 설정

이 저장소는 레슨마다 독립적인 `project()` + `add_executable()`을 두고, 상위 `chapter02/CMakeLists.txt`가 `add_subdirectory`로 모으는 구조다. `target_include_directories` / `target_link_libraries`를 `PRIVATE`으로 걸어 각 실행 파일 타겟에만 설정이 적용된다. 이런 타겟 단위 설정 방식에서는 **타겟 이름을 정확히 지정하는 것**이 곧 설정의 적용 범위를 결정한다 — 아래 특이점이 그 반례다.

### vcpkg의 find_package 변수 방식

vcpkg의 FFMPEG 포트는 imported target이 아닌 변수(`FFMPEG_INCLUDE_DIRS`, `FFMPEG_LIBRARIES`)를 제공하므로, 타겟마다 두 변수를 수동으로 연결해야 한다. stb는 헤더 온리 라이브러리라 include 경로만 있으면 된다.

## ⚠️ 코드 특이점 상세

### CMakeLists.txt — FFMPEG 설정이 엉뚱한 타겟에 적용됨

```cmake
if (${FFMPEG_FOUND})
    target_include_directories(chapter0107VideoStream PRIVATE
            ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(chapter0107VideoStream LINK_PRIVATE
            ${FFMPEG_LIBRARIES}
    )
```

`add_executable(chapter0201ProjectSetting)`으로 만든 이 레슨의 타겟이 아니라, chapter01-07의 타겟인 `chapter0107VideoStream`에 FFmpeg include/link를 걸고 있다. chapter01에서 CMakeLists.txt를 복사하면서 이 블록의 타겟명만 바꾸지 않은 것이다. 결과적으로:

- `chapter0201ProjectSetting` 타겟에는 FFmpeg include 경로와 링크 라이브러리가 걸리지 않는다.
- 이 레슨의 `main()`이 FFmpeg **함수를 호출하지 않기 때문에** 링크 에러가 나지 않고 넘어간다.
- 올바른 형태는 두 곳 모두 `chapter0201ProjectSetting`으로 지정하는 것이다. 레슨 02부터는 올바르게 수정되어 있다.

또한 Stb 블록은 `${Stb_INCLUDE_DIR}`(단수형)를 쓰는데, 레슨 02부터는 `${Stb_INCLUDE_DIRS}`(복수형)로 표기가 바뀐다.

### GetResourcePath가 호출되지 않음

`bool GetResourcePath(const char *name, char *const pathBuffer);` 선언과 정의가 모두 있지만 `main()`에서 호출하지 않는다. 다음 레슨을 위한 준비 코드로, 컴파일러에 따라 미사용 함수 경고가 나올 수 있다.
