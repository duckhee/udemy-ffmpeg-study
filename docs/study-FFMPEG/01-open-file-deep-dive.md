# 01. 파일 열기와 메타데이터 — 코드 상세 해설

> [← 기본 문서](01-open-file.md)

## 전체 구조

| 함수 / 구간 | 역할 |
|---|---|
| `main()` | 열기 → 스트림 정보 → 전역 정보/메타데이터 출력 → 덤프 → 닫기 |
| `GetResourcePath()` | 실행 경로에서 저장소 루트의 `resources/` 경로를 역산하는 유틸 |
| `FFMPEG_ERROR` 매크로 | 음수 에러 코드면 `av_log`로 출력하고 `return -1` |

```text
main
 ├─ GetResourcePath("murage.mp4", ...)   경로 계산
 ├─ avformat_open_input                  컨테이너 열기
 ├─ avformat_find_stream_info            스트림 정보 채우기
 ├─ 컨테이너 전역 정보 printf            format / streams / duration / bit_rate
 ├─ av_dict_get 루프                     메타데이터 전체 순회
 ├─ av_dump_format                       ffmpeg -i 스타일 요약
 └─ avformat_close_input                 닫기 + 해제
```

## 코드 블록별 해설

### 1. 에러 처리 매크로와 변수 선언

```c
#define FFMPEG_ERROR(errorCode, msg) \
{                                    \
if((errorCode) < (0)) {              \
av_log(NULL, AV_LOG_ERROR, (msg));   \
return -1;                           \
}else{                               \
}                                     \
}                                    \
```

FFmpeg API는 대부분 "성공 시 0 이상, 실패 시 음수 에러 코드"를 반환한다. 이 매크로는 그 관례를 한 줄로 처리한다. `av_log(NULL, AV_LOG_ERROR, ...)`는 FFmpeg의 표준 로깅 함수로, 첫 인자(로깅 컨텍스트)가 NULL이면 전역 로그로 출력된다.

```c
AVFormatContext *pFormatContext = NULL;
const AVDictionaryEntry *pMetaEntry = NULL;
```

`pFormatContext`를 **NULL로 초기화하는 것이 중요**하다. `avformat_open_input()`은 포인터가 NULL이면 내부에서 컨텍스트를 새로 할당하고, NULL이 아니면 이미 할당된 컨텍스트로 간주하기 때문이다. `pMetaEntry`도 NULL 초기화가 필수인데, `av_dict_get()` 순회의 시작점 역할을 하기 때문이다(아래 4번 참고).

### 2. 컨테이너 열기

```c
/**
 * 컨테이너 열기.
 * avformat_open_input은 파일 헤더만 읽어 AVFormatContext를 할당한다.
 * 성공 시 0, 실패 시 음수 에러 코드를 반환한다.
 */
errorCode = avformat_open_input(&pFormatContext, resourcePath, NULL, NULL);
FFMPEG_ERROR(errorCode, "[FFMPEG ERROR] FFMPEG Open Failed...\r\n")
```

세 번째 인자(`AVInputFormat *`)에 NULL을 주면 파일 내용을 보고 포맷을 자동 감지한다. 네 번째 인자(`AVDictionary **`)는 디먹서 옵션인데 여기서는 사용하지 않는다. "헤더만 읽는다"는 점이 핵심으로, 이 시점에는 아직 각 스트림의 상세 정보(해상도, 프레임레이트 등)가 완전하지 않을 수 있다.

### 3. 스트림 정보 채우기

```c
/**
 * 스트림 정보 채우기.
 * 일부 포맷은 헤더만으로 스트림 정보를 알 수 없어
 * 실제 패킷 몇 개를 읽어 codec/duration 등을 채운다.
 */
errorCode = avformat_find_stream_info(pFormatContext, NULL);
if (errorCode < 0) {
    av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) find Stream Failed...\r\n", errorCode);
    avformat_close_input(&pFormatContext);
    return -1;
}
```

mp4는 헤더(moov 박스)에 정보가 잘 정리되어 있지만, MPEG-TS처럼 헤더가 빈약한 포맷은 실제 데이터를 읽어봐야 스트림 구성을 알 수 있다. `avformat_find_stream_info()`는 그 차이를 흡수해 주는 함수다. 실패 시 `FFMPEG_ERROR` 매크로 대신 직접 처리하는 이유는, **이미 열린 컨테이너를 `avformat_close_input()`으로 닫은 뒤** 종료해야 하기 때문이다(매크로는 닫기 없이 바로 return 한다).

### 4. 컨테이너 전역 정보 출력

```c
/** 컨테이너 전역 정보 출력 */
printf("==== Container Information ====\r\n");
printf("format name    : %s (%s)\r\n", pFormatContext->iformat->name, pFormatContext->iformat->long_name);
printf("stream count   : %u\r\n", pFormatContext->nb_streams);
/**
 * duration은 AV_TIME_BASE(1,000,000) 단위의 마이크로초 값이다.
 * 초 단위로 보려면 AV_TIME_BASE로 나눈다.
 */
printf("duration       : %.2f sec\r\n", (double) pFormatContext->duration / AV_TIME_BASE);
printf("bit rate       : %lld bps\r\n", pFormatContext->bit_rate);
```

- `iformat->name`은 짧은 이름(`mov,mp4,m4a,3gp,3g2,mj2` — mp4 계열 디먹서 하나가 여러 확장자를 담당한다), `long_name`은 사람이 읽기 좋은 이름이다.
- `duration`은 `AV_TIME_BASE`(1,000,000) 단위이므로 나눠서 초로 만든다. murage.mp4는 약 **12.78 sec**가 출력된다.
- `bit_rate`는 컨테이너 전체(비디오+오디오) 비트레이트로 약 **8523 kbps**가 나온다.

### 5. 메타데이터 전체 순회

```c
/**
 * 메타데이터(AVDictionary) 전체 순회.
 * AV_DICT_IGNORE_SUFFIX 플래그 + 빈 키("")를 주면
 * 이전 엔트리에서 이어서 모든 엔트리를 차례로 돌 수 있다.
 */
printf("==== Metadata ====\r\n");
while ((pMetaEntry = av_dict_get(pFormatContext->metadata, "", pMetaEntry, AV_DICT_IGNORE_SUFFIX)) != NULL) {
    printf("%-20s : %s\r\n", pMetaEntry->key, pMetaEntry->value);
}
```

`av_dict_get()`은 원래 "키로 값을 찾는" 함수지만, 관용적인 이터레이터 사용법이 있다.

- 두 번째 인자(키)에 빈 문자열 `""`을 주고,
- 네 번째 인자에 `AV_DICT_IGNORE_SUFFIX`를 주면 "키가 `""`로 시작하는 모든 엔트리" = **모든 엔트리**가 매칭된다.
- 세 번째 인자(`prev`)에 직전에 받은 엔트리를 넘기면 그 **다음** 엔트리를 반환한다. 처음에는 NULL이므로 첫 엔트리부터 시작하고, 더 이상 없으면 NULL이 반환되어 루프가 끝난다.

mp4에서는 `major_brand`, `minor_version`, `compatible_brands`, `encoder` 같은 엔트리를 볼 수 있다.

### 6. av_dump_format과 닫기

```c
/**
 * ffmpeg -i 와 동일한 형태의 요약 덤프.
 * 마지막 인자 0 = 입력 파일 기준.
 */
printf("==== av_dump_format ====\r\n");
av_dump_format(pFormatContext, 0, resourcePath, 0);

/**
 * 열었던 컨테이너 닫기.
 * avformat_open_input으로 연 것은 반드시 avformat_close_input으로 닫는다.
 * (내부에서 AVFormatContext까지 해제하고 포인터를 NULL로 만든다)
 */
avformat_close_input(&pFormatContext);
```

`av_dump_format()`은 커맨드라인에서 `ffmpeg -i murage.mp4`를 쳤을 때 보이는 것과 같은 요약을 stderr로 출력한다. murage.mp4에서는 다음과 같은 두 줄이 핵심이다.

```text
Stream #0:0 ... Video: h264 (High) ..., yuv420p, 1280x720 ..., 30.08 fps ...
Stream #0:1 ... Audio: aac (LC) ..., 48000 Hz, stereo, fltp ...
```

두 번째 인자(0)는 스트림 인덱스 힌트, 마지막 인자 0은 "입력 파일"이라는 뜻이다(먹싱된 출력 파일을 덤프할 때는 1). `avformat_close_input()`은 이중 포인터를 받아 내부 해제 후 `pFormatContext`를 NULL로 만들어 주므로, 닫은 뒤 실수로 재사용하는 사고(dangling pointer)를 예방한다.

### 7. GetResourcePath — 리소스 경로 역산

```c
#if defined(WIN32) || defined(WIN64)
    GetCurrentDirectory(BUFFER_MAX, executeBuffer);
    pRemoveStart = strstr(executeBuffer, "\\cmake");
#else
    realpath(".", executeBuffer);
    pRemoveStart = strstr(executeBuffer, "/cmake");
#endif
```

현재 작업 디렉터리에서 `/cmake` 문자열을 찾아 그 앞부분(= 저장소 루트)만 잘라내고 `/resources/` + 파일명을 붙인다. 예를 들어 `.../udemy-ffmpeg-study/cmake-build-debug/study-FFMPEG/01-open-file`에서 실행하면 `.../udemy-ffmpeg-study/resources/murage.mp4`가 만들어진다. 따라서 **빌드 트리(`cmake-build-*`) 안에서 실행해야만** 경로 계산이 성공하며, 경로에 `/cmake`가 없으면 `false`를 반환해 프로그램이 즉시 종료된다.
