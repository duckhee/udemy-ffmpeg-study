# 03. FFmpeg 핵심 데이터 구조체 — 코드 상세 해설

> [← 기본 문서](03-advanced-data-structure.md)

## 전체 구조

| 구성 요소 | 내용 |
|---|---|
| `FFMPEG_ERROR` 매크로 | 음수 에러 코드 처리용 (미사용) |
| `main()` | 구조체 포인터 선언 → 열기 → (에러여도 계속) → 닫기 |
| `GetResourcePath()` | 리소스 경로 헬퍼 (레슨 01 참고) |

## 코드 블록별 해설

### FFMPEG_ERROR 매크로

```c
#define FFMPEG_ERROR(errorCode, msg)    \
{                                       \
if((errorCode) < (0)){                  \
av_log(NULL, AV_LOG_ERROR, (msg));      \
return -1;                              \
}else{                                  \
}                                       \
}
```

에러 코드가 음수면 로그를 남기고 함수에서 `-1`로 빠져나가는 매크로다. 인자를 괄호로 감싸는 등 기본기는 갖췄지만 두 가지 관용 패턴에서 벗어나 있다. 첫째, 단순 `{ }` 블록이라 `if (x) FFMPEG_ERROR(...); else ...` 같은 문맥에서 세미콜론 문제가 생길 수 있다(관용적으로는 `do { ... } while(0)` 사용). 둘째, 매크로 안에 `return -1`이 숨어 있어 호출 지점에서 제어 흐름이 바뀌는 것이 드러나지 않고, `int` 반환 함수에서만 쓸 수 있다. 이 레슨을 포함해 chapter02 전반부에서는 정의만 있고 실제 호출은 없다.

### 디코딩 파이프라인 구조체 선언

```c
    AVFormatContext *pAvFormatContext = NULL;
    /** 원본 데이터에 대해서 가지고 있는 구조체이다. 원본 데이터는 바이트 단위로 정의가 되어 있다. */
    AVPacket *pAvPacket = NULL;
    /** 원본 오디오 및 비디오 데이터를 풀어 놓은 데이터를 담는 구조체이다. */
    AVFrame *pAvFrame = NULL;
    /** 인코딩된 스트림에 대한 정보를 담고 있는 구조체이다. */
    AVCodecParameters *pVideoCodecParameters = NULL;
    /** codec에 대한 정보를 담고 있는 구조체이다. */
    AVCodec *pVideoCodec = NULL;
    AVCodecContext *pVideoCodecContext = NULL;

    int videoStreamIdx = -1;
```

이 레슨의 본론이다. 실제 사용은 없지만, 이후 레슨에서 각 구조체가 맡을 역할을 주석과 함께 미리 정리해 둔다.

- **AVPacket**: 디먹서가 컨테이너에서 꺼낸 압축 상태의 데이터 조각. 비디오라면 보통 프레임 1개 분량의 압축 비트스트림이다.
- **AVFrame**: 디코더가 패킷을 풀어낸 결과물. 비디오는 픽셀 평면(YUV 등), 오디오는 PCM 샘플을 담는다.
- **AVCodecParameters**: 스트림 헤더에서 읽은 코덱 파라미터의 스냅샷. `AVStream->codecpar`로 접근한다.
- **AVCodec**: "h264 디코더" 같은 코덱 구현체 자체를 가리키는 읽기 전용 디스크립터.
- **AVCodecContext**: 그 코덱을 실제로 구동할 때의 상태(스레드 수, 열림 여부, 내부 버퍼 등)를 담는 실행 컨텍스트.

`videoStreamIdx = -1` 초기화는 "아직 비디오 스트림을 찾지 못함"을 뜻하는 관용 표현으로, 레슨 05의 스트림 탐색을 예고한다.

### 열기 — 에러가 나도 계속 진행

```c
    FFMPEG_ErrorCode = avformat_open_input(&pAvFormatContext, imagePath, NULL, NULL);
    if (FFMPEG_ErrorCode != 0) {
        /** FFMPEG 로그 함수를 이용한 실패 로그 출력 */
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] open resource out.mp4\r\n", FFMPEG_ErrorCode);
    }


    avformat_close_input(&pAvFormatContext);

    return 0;
```

레슨 02에 있던 `return -1`이 이 레슨에서는 없다. 자세한 분석은 아래 특이점 참고.

## 심화

### 파라미터(AVCodecParameters)와 컨텍스트(AVCodecContext)를 분리한 이유

과거 FFmpeg은 `AVStream->codec`(AVCodecContext)에 스트림 정보를 직접 담았지만, "스트림이 무엇인지에 대한 정보"와 "디코더 실행 상태"가 한 구조체에 섞여 수명 관리가 어려웠다. 그래서 FFmpeg 3.1부터 불변 정보만 담는 경량 구조체 `AVCodecParameters`(`codecpar`)를 분리했고, 디코딩하려면 `avcodec_parameters_to_context()`로 파라미터를 컨텍스트에 복사해 쓰는 현재의 2단계 모델이 되었다 (레슨 06에서 실습).

### 압축 도메인과 비압축 도메인

`AVPacket`과 `AVFrame`의 구분은 FFmpeg API 전체를 관통하는 경계선이다. 디먹서(`av_read_frame`)는 패킷을 내놓고, 디코더(`avcodec_send_packet` / `avcodec_receive_frame`)가 패킷을 프레임으로 바꾼다. 반대로 인코더는 프레임을 패킷으로 바꾸고, 먹서가 패킷을 컨테이너에 쓴다. 두 구조체 모두 참조 카운팅 기반 버퍼(`AVBufferRef`)를 사용한다는 공통점이 있다 (레슨 08 딥다이브 참고).

## ⚠️ 코드 특이점 상세

### avformat_open_input 실패 시 early return 없음

```c
    FFMPEG_ErrorCode = avformat_open_input(&pAvFormatContext, imagePath, NULL, NULL);
    if (FFMPEG_ErrorCode != 0) {
        /** FFMPEG 로그 함수를 이용한 실패 로그 출력 */
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] open resource out.mp4\r\n", FFMPEG_ErrorCode);
    }
```

에러 블록 안에 `return -1`이 없어 열기에 실패해도 프로그램이 계속 진행된다. 왜 문제가 없어 "보이는지", 그리고 왜 실제로는 문제인지:

- `avformat_open_input()`은 실패 시 내부에서 할당물을 정리하고 `pAvFormatContext`를 `NULL`로 되돌린다.
- `avformat_close_input(&pAvFormatContext)`는 `*ps`가 `NULL`이면 아무 일도 하지 않으므로, **현재 코드 범위에서는** 크래시가 나지 않는다.
- 그러나 이 코드 골격에 레슨 04처럼 `avformat_find_stream_info(pAvFormatContext, ...)` 등이 추가되면 `NULL` 컨텍스트를 역참조해 즉시 크래시한다. 올바른 형태는 레슨 02처럼 에러 블록에서 `return -1` 하는 것이며, 실제로 레슨 04부터는 다시 early return이 들어간다.

### 정의만 있고 사용되지 않는 요소들

- 구조체 포인터 5종(`pAvPacket`, `pAvFrame`, `pVideoCodecParameters`, `pVideoCodec`, `pVideoCodecContext`)과 `videoStreamIdx`는 선언만 되고 사용되지 않는다 (개념 소개 목적의 레슨이므로 의도된 형태).
- `FFMPEG_ERROR` 매크로도 정의만 있고 호출되지 않는다.
- 참고로 FFmpeg 5.0 이후 `avcodec_find_decoder()` 계열은 `const AVCodec *`를 반환하므로, 이 코드처럼 `AVCodec *`(비-const)로 선언하면 이후 레슨에서 대입 시 const 한정자 경고가 발생할 수 있다.
