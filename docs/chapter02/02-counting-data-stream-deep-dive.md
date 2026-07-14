# 02. AVFormatContext로 미디어 파일 열기 — 코드 상세 해설

> [← 기본 문서](02-counting-data-stream.md)

## 전체 구조

| 구성 요소 | 내용 |
|---|---|
| include 블록 | 레슨 01과 동일 (FFmpeg 4개 라이브러리 + stb) |
| `main()` | 경로 계산 → 열기 → 에러 처리 → 닫기 |
| `GetResourcePath()` | 리소스 경로 헬퍼 (레슨 01 딥다이브 참고, 이후 반복 언급 생략) |

## 코드 블록별 해설

### 변수 선언과 경로 계산

```c
int main(int argc, char **argv) {
    char resourcePath[BUFFER_MAX] = {0};
    /** FFMPEG에서 데이터를 가져올 때 담아주는 AV Format Context이다. */
    AVFormatContext *pAvFormatContext = NULL;
    int ffmpegErrorCode = 0;

    if (!GetResourcePath("out.mp4", resourcePath)) {
        printf("Failed Get out.mp4 resource path...\r\n");
        return -1;
    }
```

`pAvFormatContext`를 반드시 `NULL`로 초기화한다. `avformat_open_input()`은 넘겨받은 포인터가 `NULL`이면 내부에서 `avformat_alloc_context()`로 컨텍스트를 새로 할당하기 때문에, 쓰레기 값이 들어 있으면 기존 컨텍스트로 오인해 미정의 동작이 일어날 수 있다.

### 파일 열기와 에러 처리

```c
    /** FFMPEG 로 해당 동영상 열기 -> 열기 성공 시 0 반환 */
    ffmpegErrorCode = avformat_open_input(&pAvFormatContext, resourcePath, NULL, NULL);
    if (ffmpegErrorCode != 0) {
        printf("Failed get resource using ffmpeg : %d\r\n", ffmpegErrorCode);
        /** FFMPEG 로그 함수를 이용한 실패 로그 출력 */
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] open resource out.mp4\r\n", ffmpegErrorCode);
        return -1;
    }
```

`avformat_open_input(&ctx, url, fmt, options)`의 세 번째 인자(`AVInputFormat *`)를 `NULL`로 주면 포맷을 자동 감지(probe)하고, 네 번째 인자(`AVDictionary **`)를 `NULL`로 주면 기본 옵션을 사용한다. 실패 시 함수가 내부에서 컨텍스트를 해제하고 포인터를 `NULL`로 되돌려 주므로, 이 에러 경로에서 별도의 해제 없이 바로 `return -1` 해도 누수가 없다.

### 닫기

```c
    /** 사용을 한 후에는 해당 객체를 메모리에서 해제를 해줘야 메모리 누수가 발생하지 않는다. */
    avformat_close_input(&pAvFormatContext);

    return 0;
```

`avformat_close_input()`은 이중 포인터를 받아 컨텍스트를 해제한 뒤 `*ps = NULL`로 만들어 준다. 해제 후 실수로 재사용하는 dangling pointer 문제를 구조적으로 방지하는 FFmpeg의 관용 패턴이다.

## 심화

### avformat_open_input이 실제로 하는 일

1. URL/파일을 I/O 계층(`AVIOContext`)으로 연다.
2. 파일 앞부분을 읽어 각 디먹서(demuxer)의 `read_probe`로 포맷을 판별한다 (mp4라면 `mov,mp4,m4a,...` 디먹서).
3. 판별된 디먹서의 `read_header`를 호출해 컨테이너 헤더를 파싱하고, 발견한 스트림들을 `AVFormatContext.streams[]`에 채운다.

즉 이 시점에 이미 `nb_streams` 같은 기초 정보는 채워지지만, 코덱 상세 정보(해상도, 샘플레이트 등)는 컨테이너에 따라 불완전할 수 있다. 이를 보완하는 것이 레슨 04에서 배우는 `avformat_find_stream_info()`다.

### FFmpeg 에러 코드 체계

FFmpeg 함수는 실패 시 음수를 반환하며, 값은 `AVERROR(errno)` 매크로로 감싼 POSIX 에러이거나 `AVERROR_EOF` 같은 FFmpeg 고유 코드다. `av_strerror()` 또는 `av_err2str()` 매크로로 사람이 읽을 수 있는 문자열로 변환할 수 있다. 이 레슨에서는 숫자 코드만 출력하지만, 실무에서는 문자열 변환까지 하는 것이 일반적이다.

## ⚠️ 코드 특이점 상세

### 레슨 이름과 실제 동작의 불일치

디렉터리명(`02-counting-data-streaming-using-AVFormatContext`)과 달리 스트림 개수를 세는 코드가 없다. `pAvFormatContext->nb_streams` 출력은 레슨 04에서 처음 등장하므로, "스트림 개수 세기"라는 주제는 이 레슨이 아니라 04에서 완성된다고 보면 된다.
