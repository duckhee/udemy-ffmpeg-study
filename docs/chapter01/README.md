# Chapter 01 — FFmpeg 기초: 컨테이너 열기와 메타데이터

FFmpeg 라이브러리(libav*)를 C 코드에서 직접 사용하는 첫 챕터다. 빌드 환경 검증에서 시작해, 미디어 컨테이너를 열고(`avformat_open_input`), 포맷 정보를 덤프하고(`av_dump_format`), 메타데이터(`AVDictionary`)를 순회하고, 스트림 정보(`avformat_find_stream_info`)에서 재생 시간·비트레이트를 읽고, 마지막으로 개별 스트림을 순회하며 비디오/오디오를 분류하고 디코더를 찾는(`avcodec_find_decoder`) 데까지 도달한다. 모든 레슨은 `resources/murage.mp4`를 입력으로 사용한다.

## 레슨 진행 흐름

```mermaid
flowchart LR
    L01["01<br/>빌드 검증"] --> L02["02<br/>컨테이너 열기"]
    L02 --> L03["03<br/>포맷 덤프 + 닫기"]
    L03 --> L04["04<br/>메타데이터 순회"]
    L04 --> L05["05<br/>스트림 정보 · 시간 변환"]
    L05 --> L06["06<br/>함수 · 매크로 리팩터링"]
    L06 --> L07["07<br/>스트림 순회 · 디코더 탐색"]
```

## 레슨 목록

| # | 레슨 | 주제 | 핵심 API | 문서 |
|---|---|---|---|---|
| 01 | compile-ffmpeg | FFmpeg 헤더/라이브러리 링크 검증 | `#include <libav*>` | [기본](01-compile-ffmpeg.md) · [해설](01-compile-ffmpeg-deep-dive.md) |
| 02 | load-resource-ffmpeg | 컨테이너 파일 열기 | `avformat_open_input`, `av_log` | [기본](02-load-resource.md) · [해설](02-load-resource-deep-dive.md) |
| 03 | dump-format-ffmpeg | 포맷 정보 덤프와 자원 반환 | `av_dump_format`, `avformat_close_input` | [기본](03-dump-format.md) · [해설](03-dump-format-deep-dive.md) |
| 04 | AvDictionaryStruct-ffmpeg | 컨테이너 메타데이터 순회 | `av_dict_get`, `AV_DICT_IGNORE_SUFFIX` | [기본](04-av-dictionary.md) · [해설](04-av-dictionary-deep-dive.md) |
| 05 | TimeBase-AV-Time | 스트림 정보 로드, 시간 단위 변환 | `avformat_find_stream_info`, `AV_TIME_BASE` | [기본](05-timebase-av-time.md) · [해설](05-timebase-av-time-deep-dive.md) |
| 06 | function-macro-avDictionaryStruct | 헬퍼 함수 + 에러 매크로 리팩터링 | `FFMPEG_CHECK_ERROR` 매크로 | [기본](06-function-macro.md) · [해설](06-function-macro-deep-dive.md) |
| 07 | video-stream | 스트림 순회, 코덱 파라미터, 디코더 | `AVCodecParameters`, `avcodec_find_decoder`, `av_q2d` | [기본](07-video-stream.md) · [해설](07-video-stream-deep-dive.md) |

## 공통 사항

- 소스 위치: `chapter01/<레슨 디렉터리>/main.c`
- 빌드 시스템: 최상위 CMake 프로젝트(`udemy_ffmpeg_study`)가 `chapter01`을 서브디렉터리로 포함하며, 각 레슨이 독립 실행 파일 타겟이다. FFmpeg은 `find_package(FFMPEG REQUIRED)`(vcpkg)로 탐색한다.
- 입력 파일: `resources/murage.mp4` — 각 프로그램은 실행 시 현재 작업 디렉터리 경로에서 `/cmake` 이후를 잘라내 저장소 루트를 추정한 뒤 `resources/`를 붙여 파일을 찾는다. 따라서 **빌드 디렉터리(`cmake-build-debug`) 내부에서 실행해야 한다**.

---
[← 전체 로드맵](../README.md) · [다음: Chapter 02 →](../chapter02/README.md)
