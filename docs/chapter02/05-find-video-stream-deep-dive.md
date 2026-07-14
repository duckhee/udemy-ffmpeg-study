# 05. 비디오 스트림 찾기 — 코드 상세 해설

> [← 기본 문서](05-find-video-stream.md)

## 전체 구조

| 구성 요소 | 내용 |
|---|---|
| `FFMPEG_ERROR` 매크로 | 레슨 03과 동일 (미사용) |
| `main()` 전반부 | 열기 → find_stream_info → packet/frame 할당 (레슨 04와 동일 골격) |
| `main()` 스트림 루프 | codecpar → 디코더 검색 → 타입 분기 → 비디오 정보 출력 |
| `release:` 레이블 | goto로 모이는 공통 해제 지점 |
| `GetResourcePath()` | 리소스 경로 헬퍼 (레슨 01 참고) |

## 코드 블록별 해설

### 스트림 순회와 디코더 검색

```c
    for (int streamIdx = 0; streamIdx < pAvFormatContext->nb_streams; streamIdx++) {
        AVCodecParameters *pCurrentCodecParameter = NULL;
        AVCodec *pCurrentCodec = NULL;
        AVStream *pCurrentStream = NULL;
        /** stream 에 대한 정보 가져오기 */
        pCurrentStream = pAvFormatContext->streams[streamIdx];
        /** stream 에서 codecParameter 가져오기 */
        pCurrentCodecParameter = pCurrentStream->codecpar;
        /** codecParameter 저장되어 있는 codec_id 를 가지고 codec 에 대한 값 가져오기 */
        pCurrentCodec = avcodec_find_decoder(pCurrentCodecParameter->codec_id);
        if (pCurrentCodec == NULL) {
            printf("Failed Get Current Codec ...\r\n");
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] find decoder codec [%s]\r\n", errorCode, resourcePath);
            goto release;
        }
```

스트림 → `codecpar` → 디코더의 3단계 접근이다. `avcodec_find_decoder()`는 빌드된 FFmpeg에 해당 디코더가 포함되어 있지 않으면 `NULL`을 반환한다. 디코더가 하나라도 없으면 `goto release`로 전체 순회를 중단하고 해제 지점으로 건너뛴다 — 남은 스트림을 건너뛰고 프로그램 자체를 끝내는 다소 보수적인 처리다.

### 비디오 스트림 분기

```c
        /** codec 에 대한 정보가 비디오 일 경우 */
        if (pCurrentCodecParameter->codec_type == AVMEDIA_TYPE_VIDEO) {
            printf("Found Video Stream!\r\n");
            videoStreamChannelIdx = streamIdx;
            /** Frame 에 대한 rate 을 가져오기 -> av_q2d는 AVRational 데이터를 double 형태의 값으로 변환을 하는 함수이다. */
            /**
             * stream 에 있는 r_frame_rate 는 실제 frame 에 대한 rate 값을 가져온다.
             * stream 에 있는 avg_frame_rate 는 frame 에 대한 평균 rate 값을 가져온다.
             * */
            double frameRate = av_q2d(pCurrentStream[streamIdx].r_frame_rate);

            pVideoCode = pCurrentCodec;
            pVideoCodecParameters = pCurrentCodecParameter;

            /** 가져온 정보를 확인하기 위한 출력 */
            printf("codec ID : %d\r\nCodec : %s, BitRate : %lld\r\nWidth: %d, Height: %d, FrameRate : %lf fps\r\n",
                   pCurrentCodecParameter->codec_id, pCurrentCodec->name, pCurrentCodecParameter->bit_rate,
                   pCurrentCodecParameter->width,
                   pCurrentCodecParameter->height,
                   frameRate);
        }
```

비디오 스트림을 찾으면 인덱스와 디코더(`pVideoCode`), 파라미터(`pVideoCodecParameters`)를 바깥 변수에 보존한다 — 레슨 06에서 코덱 컨텍스트를 만들 때 그대로 사용된다. `av_q2d(...r_frame_rate)`로 fps를 계산하는데, 여기의 `pCurrentStream[streamIdx]` 인덱싱은 버그다 (특이점 참고).

### 오디오·자막 분기와 미발견 검사

```c
            /** codec 에 대한 정보가 오디오 일 경우 */
        else if (pCurrentCodecParameter->codec_type == AVMEDIA_TYPE_AUDIO) {
            printf("Found Audio Stream!\r\n");
        }
            /** codec 에 대한 정보가 서브 타이틀 일 경우 */
        else if (pCurrentCodecParameter->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            printf("Found subtitle Stream!\r\n");
        }
    }

    /** not found video stream error */
    if (videoStreamChannelIdx < 0) {
        av_log(NULL, AV_LOG_ERROR, "Video Stream Found Failed...\r\n");
    }
```

오디오/자막은 발견 메시지만 출력한다 (오디오 상세 처리는 레슨 07). 미발견 검사는 초기값 문제로 동작하지 않는다 (특이점 참고).

### 해제 지점

```c
    release:
    avformat_close_input(&pAvFormatContext);
    av_packet_free(&pAvPacket);
//    av_frame_free(&pAvFrame);
    av_free(pAvFrame);
    return 0;
```

`goto release` 패턴은 C에서 자원 해제를 한 곳에 모으는 관용적인 방법이다. 정상 흐름과 에러 흐름이 같은 해제 코드를 공유하므로 누수 위험이 줄어든다.

## 심화

### r_frame_rate vs avg_frame_rate

- `r_frame_rate`: 타임스탬프가 표현 가능한 최소 프레임 간격에서 유도한 "실제(real base)" 프레임레이트. 모든 타임스탬프가 이 값의 정수배 간격이 되도록 잡은 추정치다.
- `avg_frame_rate`: 전체 재생 시간과 프레임 수로 계산한 평균값. 가변 프레임레이트(VFR) 영상에서는 두 값이 크게 다를 수 있으며, 일반적인 고정 프레임레이트 mp4에서는 같다.

### 왜 AVRational인가

29.97fps(NTSC)는 실제로 30000/1001이다. 이를 `double` 29.97로 저장하면 수백만 프레임 뒤 타임스탬프 계산에서 오차가 누적된다. FFmpeg이 프레임레이트·타임베이스를 전부 분수(`AVRational`)로 유지하고, 표시용으로만 `av_q2d()` 변환을 쓰는 이유다.

## ⚠️ 코드 특이점 상세

### pCurrentStream[streamIdx] — 포인터 인덱싱 버그

```c
            double frameRate = av_q2d(pCurrentStream[streamIdx].r_frame_rate);
```

`pCurrentStream`은 이미 `pAvFormatContext->streams[streamIdx]`로 얻은, **해당 스트림 하나를 가리키는 포인터**다. 여기에 다시 `[streamIdx]`를 붙이면 `*(pCurrentStream + streamIdx)`, 즉 그 스트림 구조체로부터 `streamIdx`개만큼 떨어진 메모리를 읽는다. `streams[]`는 `AVStream*`의 배열(포인터 배열)이지 `AVStream` 구조체가 연속 배치된 배열이 아니므로, `streamIdx > 0`이면 유효하지 않은 메모리를 읽어 쓰레기 fps가 나오거나 크래시할 수 있다.

- 이 저장소의 out.mp4는 비디오가 스트림 0이라 `pCurrentStream[0]` == `*pCurrentStream`이 되어 우연히 올바른 값이 나온다.
- 올바른 형태: `av_q2d(pCurrentStream->r_frame_rate)`
- 이 코드는 레슨 06~08에도 그대로 복사되어 있다.

### videoStreamChannelIdx 초기값 0 — 죽은 미발견 검사

```c
    int videoStreamChannelIdx = 0;
    ...
    if (videoStreamChannelIdx < 0) {
        av_log(NULL, AV_LOG_ERROR, "Video Stream Found Failed...\r\n");
    }
```

레슨 03에서는 `videoStreamIdx = -1`(미발견 표시)로 초기화했지만 이 레슨은 `0`으로 초기화한다. 비디오 스트림이 하나도 없어도 값이 0이므로 `< 0` 검사는 절대 참이 되지 않고, 이후 로직은 "스트림 0이 비디오"라고 잘못 가정한 채 진행하게 된다. 올바른 형태는 `-1`로 초기화하는 것이다.

### FFmpeg 5.0+ const 경고

`avcodec_find_decoder()`는 FFmpeg 5.0부터 `const AVCodec *`를 반환한다. 이 코드는 결과를 비-const `AVCodec *pCurrentCodec`에 대입하므로 최신 FFmpeg에서는 const 한정자 폐기 경고가 발생한다.

### 기타

- 프레임 해제가 여전히 `av_free(pAvFrame)`다 — 레슨 04 딥다이브 참고.
- `goto release` 시에도 `return 0`으로 종료하므로, 디코더 미발견 같은 에러 상황에서도 종료 코드는 성공(0)이다.
