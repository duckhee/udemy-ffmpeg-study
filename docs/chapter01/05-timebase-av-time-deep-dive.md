# 05. 스트림 정보와 시간 단위 — 코드 상세 해설

> [← 기본 문서](05-timebase-av-time.md)

## 전체 구조

| 구성 요소 | 역할 |
|---|---|
| `main()` | 열기 → 메타데이터 출력 → 스트림 정보 로드 → bit_rate/duration 출력 |
| `GetResourcePath()` | 리소스 경로 헬퍼 (02번 `GetFilePath`의 개정판) |
| `FormatDuration()` | `AV_TIME_BASE` 단위 duration을 H:M:S로 변환 출력 |

## 코드 블록별 해설

### 변수 선언

```c
    char videoPath[BUFFER_MAX] = "../../resources/murage.mp4";
    FILE *pFile = NULL;
    AVFormatContext *pAvContext = NULL;
    AVDictionaryEntry *dictionaryEntry = NULL;
    int ffmpegErrorCode = 0;
```

`videoPath`가 상대 경로 리터럴로 초기화되어 있지만, 바로 다음의 `GetResourcePath`가 버퍼 전체를 덮어쓰므로 이 초기값이 실제로 쓰이는 일은 없다(개발 과정에서 하드코딩으로 시작했다가 헬퍼로 교체한 흔적으로 보인다). `pAvContext = NULL` 초기화는 `avformat_open_input`이 새 컨텍스트를 할당하게 하는 필수 조건이다.

### 열기와 메타데이터 순회

```c
    ffmpegErrorCode = avformat_open_input(&pAvContext, videoPath, NULL, NULL);
    if (ffmpegErrorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed Get Video Context\r\n");
        return -1;
    }


    /** get video meta data */
    while ((dictionaryEntry = av_dict_get(pAvContext->metadata, "", dictionaryEntry, AV_DICT_IGNORE_SUFFIX))) {
        printf("%s : %s\r\n", dictionaryEntry->key, dictionaryEntry->value);
    }
```

04번에서 배운 순회 관용구가 그대로 재사용된다(카운터 없이 key : value만 출력). 상세는 [04번 딥다이브](04-av-dictionary-deep-dive.md) 참고.

### 스트림 정보 로드 (이 레슨의 핵심 1)

```c
    /** duration 과 bit rate 에 대한 정보를 가져오기 위해서는 스트림에 대한 정보를 가져와야 확인을 할 수 있다. */
    ffmpegErrorCode = avformat_find_stream_info(pAvContext, NULL);
    if (ffmpegErrorCode < 0) {
        printf("Failed Load Stream Information\r\n");
        return -1;
    }
```

`avformat_find_stream_info(ctx, NULL)`는 파일 앞부분의 패킷을 읽고 필요하면 일부 디코딩까지 수행해 각 스트림의 코덱 파라미터, 프레임레이트, duration을 채운다. 두 번째 인자는 스트림별 코덱 옵션 배열로, `NULL`이면 기본값이다. MP4처럼 헤더가 충실한 포맷은 빠르지만 MPEG-TS 같은 포맷은 이 호출이 상대적으로 오래 걸릴 수 있다.

### bit_rate / duration 읽기와 출력

```c
    int64_t bitRate = pAvContext->bit_rate;
    int64_t duration = pAvContext->duration;


    printf("bit rate : %"PRId64"\r\n", bitRate);
    printf("duration : %"PRId64"\r\n", duration);

    FormatDuration(duration);
```

`PRId64`는 `inttypes.h`가 제공하는 `int64_t` 전용 포맷 지정자다. 문자열 리터럴 이어붙이기(`"%"PRId64"..."`)로 사용한다.

### FormatDuration (이 레슨의 핵심 2)

```c
void FormatDuration(int64_t duration) {
    int64_t hours = 0;
    int64_t minutes = 0;
    int64_t seconds = 0;

    seconds = duration / AV_TIME_BASE;
    minutes = seconds / 60;
    seconds %= 60;
    hours = minutes / 60;
    minutes %= 60;

    printf("duration %lld:%lld:%lld\r\n", hours, minutes, seconds);
}
```

1. `duration / AV_TIME_BASE` → 총 초
2. 초를 60으로 나눠 분/초 분리, 분을 60으로 나눠 시/분 분리

`AV_TIME_BASE`가 1,000,000이므로 마이크로초 이하 정밀도는 버려진다(내림).

### 정리부

```c
    avformat_close_input(&pAvContext);
    avformat_free_context(pAvContext);
    fclose(pFile);
    return 0;
```

특이점 참고 — `avformat_free_context` 호출은 중복이다.

### GetResourcePath — 02번 헬퍼와의 차이

로직은 [02번의 `GetFilePath`](02-load-resource-deep-dive.md)와 동일하되, 중간 버퍼(`tempBuffer`) 없이 `memset(pathBuffer, '\0', ...)` 후 출력 버퍼에 바로 조립하도록 정리됐고, 두 번째 매개변수가 `char *const pathBuffer`로 선언됐다(포인터 자체를 함수 내에서 바꾸지 않겠다는 표시).

## 심화

### AV_TIME_BASE vs time_base (AVRational)

FFmpeg에는 시간 단위가 두 층위로 존재한다.

| 구분 | 단위 | 사용처 |
|---|---|---|
| `AV_TIME_BASE` (1/1,000,000초 고정) | 마이크로초 | `AVFormatContext->duration`, `start_time`, 시킹 API의 기본 단위 |
| `time_base` (`AVRational`, 스트림마다 다름) | 예: 1/90000, 1/44100 | `AVStream->duration`, 패킷/프레임의 `pts`, `dts` |

`AVRational`은 유리수 구조체다.

```c
typedef struct AVRational {
    int num;   // 분자
    int den;   // 분모
} AVRational;
```

시간 값의 실제 초 = `값 × time_base.num / time_base.den`. 부동소수점 누적 오차 없이 정확한 타임스탬프 연산을 하기 위해 유리수를 쓴다. 예를 들어 비디오 스트림의 `time_base`가 1/90000이고 어떤 프레임의 `pts`가 180000이면 그 프레임의 표시 시각은 2.0초다.

단위가 다른 값끼리 변환할 때는 `av_rescale_q(value, from_tb, to_tb)`를 쓴다. `AV_TIME_BASE`에 대응하는 `AVRational` 상수는 `AV_TIME_BASE_Q`(= `{1, AV_TIME_BASE}`)다. 이 구분을 놓치면 07번 레슨의 특이점(스트림 duration을 `AV_TIME_BASE`로 나누는 실수)과 같은 버그가 생긴다.

### duration 값의 출처

MP4의 경우 `mvhd` 박스에 컨테이너 duration이 기록되어 있어 `avformat_open_input` 직후에도 값이 있을 수 있지만, 포맷에 따라(예: 원시 H.264, 일부 TS) 패킷을 읽어 봐야 추정이 가능하다. `avformat_find_stream_info`를 거치면 demuxer가 스트림별 duration으로부터 가장 긴 값을 컨테이너 duration으로 갱신하므로 값의 신뢰도가 올라간다.

## ⚠️ 코드 특이점 상세

- **`avformat_close_input` + `avformat_free_context` 중복 호출**: `avformat_close_input(&pAvContext)`는 내부에서 `avformat_free_context`를 호출하고 `pAvContext`를 `NULL`로 만든다. 뒤이은 `avformat_free_context(pAvContext)`는 `avformat_free_context(NULL)`이 되어 아무 일도 하지 않는다(FFmpeg은 NULL 인자에 안전). 우연히 무해할 뿐, `close_input`을 쓰는 경우 `free_context`를 다시 부르지 않는 것이 올바른 형태다. 반대로 `avformat_open_input`이 **실패**한 경우에는 컨텍스트가 이미 해제·NULL 처리되므로 역시 별도 해제가 필요 없다.
- **`videoPath` 리터럴 초기화는 사용되지 않는 값(dead value)**: `GetResourcePath`가 `memset` 후 전체를 다시 쓴다. `= {0}` 초기화로 충분하다.
- **`%lld` vs `PRId64` 혼용**: `main`에서는 `PRId64`, `FormatDuration`에서는 `%lld`를 쓴다. `int64_t`가 `long`인 플랫폼(LP64 리눅스)에서는 `%lld`가 타입 불일치 경고를 낸다. `PRId64`로 통일하는 것이 이식성 있는 형태다.
- `#define _CRT_SECURE_NO_WARNINGS`는 MSVC의 `fopen`/`strcpy` 보안 경고 억제용으로, macOS/리눅스 빌드에서는 의미가 없다.
- 02~04번과 마찬가지로 `fopen`/`fclose` 존재 확인용 중복 열기가 유지된다. 또한 `avcodec.h` 대신 `libavcodec/codec.h`를 포함하는 점이 이전 레슨과 다르다(디코더 탐색 전 단계라 어느 쪽이든 무방).
