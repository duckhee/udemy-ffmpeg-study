# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Udemy FFmpeg 강의 및 서적(FFMPEG-Books) 학습용 저장소. 각 레슨이 독립된 C(C11) 실행 파일 하나로 구성되며, FFmpeg 7.x C API 사용법을 단계적으로 실습한다. 주석은 한국어로 작성한다.

단계별 학습 문서는 `docs/`에 있다 (레슨별 기본 문서 + `-deep-dive.md` 상세 해설, mermaid 흐름도 포함). 진입점: `docs/README.md`. 레슨 코드를 수정하면 대응하는 docs 문서도 갱신한다.

## Build

vcpkg manifest 모드를 사용하므로 configure 시 vcpkg 툴체인 파일이 필수다 (CLion이 관리하는 vcpkg 설치본 사용):

```bash
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=/Users/duckheewon/.vcpkg-clion/vcpkg/scripts/buildsystems/vcpkg.cmake

# 단일 타겟 빌드 (타겟명은 PascalCase, 예: chapter0107VideoStream)
cmake --build cmake-build-debug --target chapter0107VideoStream

# 전체 빌드
cmake --build cmake-build-debug
```

- 최초 configure는 vcpkg가 ffmpeg[all] 의존성 ~39개를 소스 빌드하므로 30분~1시간 걸릴 수 있다.
- 테스트/린트 설정은 없다.

## Run

빌드된 실행 파일은 `cmake-build-debug/<chapter>/<lesson>/` 아래에 생긴다. 각 프로그램은 `GetResourcePath()` 헬퍼로 실행 파일의 빌드 경로에서 저장소 루트의 `resources/`를 역산해 미디어를 찾으므로, 빌드 트리 안의 실행 파일을 그대로 실행하면 된다:

```bash
./cmake-build-debug/chapter01/07_video-stream/chapter0107VideoStream
```

- 입력 샘플: `resources/murage.mp4`
- 출력물: `resources/GeneratedAudio/`, `resources/GeneratedGrayImage/`, `resources/GeneratedColorImage/`, `resources/GeneratedStudy/`(study-FFMPEG 트랙, 자동 생성), `resources/out.mp4`

## Architecture

- 루트 `CMakeLists.txt`가 `chapter01/`, `chapter02/`, `FFMPEG-Books/`, `study-FFMPEG/`를 `add_subdirectory`로 포함하고, 각 챕터가 다시 레슨 디렉터리를 포함한다.
- `study-FFMPEG/`는 기초부터 트랜스코딩·필터·시킹까지 완주하는 독립 트랙(01~14)이며, `study-FFMPEG/hw-accel/`(VideoToolbox HW 가속 3개 레슨)은 `if(APPLE)` 가드로 macOS에서만 빌드된다. 타겟명은 `studyFFMPEG01OpenFile` / `studyFFMPEGHW01ListHwDevices` 형식. 이 vcpkg FFmpeg 빌드에는 libx264가 없어 인코딩 레슨(08/11/12)은 MPEG-4로 폴백한다.
- 레슨 디렉터리 구조는 동일한 패턴: `CMakeLists.txt` + `main.c` 하나.
  - `project(chapterXXYYName LANGUAGES C VERSION 0.0.1)` — 타겟명 = 프로젝트명 (챕터+레슨 번호 PascalCase)
  - `find_package(FFMPEG REQUIRED)` → `${FFMPEG_INCLUDE_DIRS}` / `${FFMPEG_LIBRARIES}` 링크 (vcpkg 제공 FindFFMPEG, pkg-config 아님)
  - 이미지 출력이 필요한 레슨은 추가로 `find_package(Stb REQUIRED)`
- 새 레슨 추가 시: 레슨 디렉터리 생성 → 위 패턴의 CMakeLists.txt 작성 → 챕터 CMakeLists.txt에 `add_subdirectory` 추가.
- 의존성은 `vcpkg.json`으로 관리 (ffmpeg[all] ≥7.0.2, stb). 새 라이브러리는 여기에 추가하면 configure 시 자동 설치된다.
- `main.c` 공통 관례: `FFMPEG_ERROR(errorCode, msg)` 매크로로 `av_log` 에러 처리, `GetResourcePath()`로 플랫폼별(macOS/Windows) 리소스 경로 해석.
