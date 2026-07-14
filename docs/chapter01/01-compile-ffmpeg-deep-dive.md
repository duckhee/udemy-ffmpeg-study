# 01. FFmpeg 빌드 환경 검증 — 코드 상세 해설

> [← 기본 문서](01-compile-ffmpeg.md)

## 전체 구조

`main.c`는 헤더 포함부와 빈 `main` 함수 두 부분뿐이다. 실질적인 내용은 `CMakeLists.txt`에 있다.

## 코드 블록별 해설

### 헤더 포함부

```c
#include <stdio.h>
#include <stdlib.h>
// FFMPEG Codec
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <inttypes.h>
```

FFmpeg 4대 핵심 라이브러리(avcodec, avutil, avformat, swscale)의 헤더를 한꺼번에 포함한다. 이 중 하나라도 include 경로에 없으면 컴파일이 실패하므로, "헤더 경로가 잡혀 있는가"를 컴파일 단계에서 검증하는 셈이다. `inttypes.h`는 이후 레슨에서 `int64_t` 출력에 쓸 `PRId64` 매크로를 위한 것으로, 여기서는 미리 포함만 해 둔다.

### main 함수

```c
int main(int argc, char **argv) {

    return 0;
}
```

의도적으로 비어 있다. 다만 링커는 참조되지 않는 심벌을 요구하지 않으므로, 실제 링크 검증은 함수 호출이 시작되는 02번 레슨부터 온전히 이루어진다. 이 레슨의 CMake 구성은 링크 라인 자체가 성립하는지(라이브러리 파일 존재 여부)를 확인해 준다.

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.27)

message("chapter01-01 compile ffmpeg")

find_package(FFMPEG REQUIRED)

project(chapter0101CompileFFMPEG LANGUAGES C VERSION 0.0.1)

add_executable(chapter0101CompileFFMPEG)

target_sources(chapter0101CompileFFMPEG PRIVATE
        main.c
)

target_include_directories(chapter0101CompileFFMPEG PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(chapter0101CompileFFMPEG PRIVATE ${FFMPEG_LIBRARY_DIRS})

target_link_libraries(chapter0101CompileFFMPEG PRIVATE ${FFMPEG_LIBRARIES})
```

- `find_package(FFMPEG REQUIRED)` — vcpkg 툴체인이 제공하는 FFMPEG 패키지를 찾는다. 실패하면 구성 단계에서 즉시 중단되므로 이것만으로도 환경 검증의 절반이 끝난다.
- `add_executable(...)` 뒤에 `target_sources(...)`로 소스를 붙이는 분리형 스타일을 사용한다. 챕터 전체에서 이 패턴이 유지된다.
- include 디렉터리 / 라이브러리 디렉터리 / 라이브러리 목록 세 가지를 각각 타겟에 연결한다. 이후 레슨의 CMakeLists.txt는 이 구성의 변형이다.

## 심화

### vcpkg의 FFMPEG 패키지 변수

vcpkg의 FFmpeg 포트는 모던 CMake의 imported target(`FFmpeg::avcodec` 같은) 대신 고전적인 변수 방식을 제공한다.

| 변수 | 내용 |
|---|---|
| `FFMPEG_INCLUDE_DIRS` | `libavcodec/avcodec.h` 등을 찾을 헤더 루트 |
| `FFMPEG_LIBRARY_DIRS` | `.a`/`.lib` 파일이 있는 디렉터리 |
| `FFMPEG_LIBRARIES` | 링크할 라이브러리 이름 목록 (avformat, avcodec, avutil, swscale, swresample 및 의존 라이브러리) |

### 헤더만 포함해도 검증되는 이유

FFmpeg 헤더는 내부에서 버전 매크로와 구조체 정의를 대량으로 노출하므로, 헤더 버전과 컴파일러(C11) 호환성 문제가 있으면 이 단계에서 이미 드러난다. 즉 "빈 main"이라도 컴파일 성공은 유의미한 신호다.

## ⚠️ 코드 특이점 상세

- `stdio.h`, `stdlib.h`를 포함하지만 아무 함수도 호출하지 않는다. 빌드 검증 목적이므로 문제는 아니며, 이후 레슨의 헤더 구성을 미리 갖춰 둔 형태다.
- `project(...)` 선언이 `find_package(...)`보다 뒤에 온다. 일반적으로는 `project()`를 먼저 두는 것이 관례지만, 최상위 `CMakeLists.txt`에서 이미 `project(udemy_ffmpeg_study)`가 선언된 뒤라 동작에는 문제가 없다.
