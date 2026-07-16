# 02. 스트림과 코덱 파라미터 — 코드 상세 해설

> [← 기본 문서](02-stream-info.md)

## 전체 구조

| 함수 / 구간 | 역할 |
|---|---|
| `main()` | 열기 → 스트림 정보 → 전체 스트림 순회 → 타입별 출력 함수 호출 → 닫기 |
| `PrintVideoStreamInfo()` | 비디오 스트림의 코덱/해상도/fps/픽셀포맷/비트레이트 출력 |
| `PrintAudioStreamInfo()` | 오디오 스트림의 코덱/샘플레이트/채널 레이아웃/비트레이트 출력 |
| `GetResourcePath()` | 실행 경로에서 `resources/` 경로를 역산하는 유틸 (01과 동일) |

```text
main
 ├─ GetResourcePath("murage.mp4", ...)
 ├─ avformat_open_input / avformat_find_stream_info   (01과 동일한 골격)
 ├─ for (idx < nb_streams)
 │    ├─ streams[idx] → codecpar → avcodec_find_decoder
 │    ├─ time_base / duration(초) 출력
 │    └─ codec_type 분기 → PrintVideoStreamInfo / PrintAudioStreamInfo
 └─ avformat_close_input
```

## 코드 블록별 해설

### 1. 스트림 순회와 코덱 파라미터 획득

```c
/** 모든 스트림 순회 */
for (int idx = 0; idx < (int) pFormatContext->nb_streams; ++idx) {
    AVStream *pCurStream = pFormatContext->streams[idx];
    /**
     * AVCodecParameters:
     * 스트림 헤더에 기록된 "코덱 설정값" (디코딩 자체는 못 한다).
     * 실제 디코딩은 이 파라미터로 AVCodecContext를 만들어서 한다.
     */
    AVCodecParameters *pCodecParameters = pCurStream->codecpar;
    /** codec_id로 이 스트림을 디코딩할 수 있는 디코더를 찾는다 */
    const AVCodec *pCodec = avcodec_find_decoder(pCodecParameters->codec_id);
```

`nb_streams`는 `unsigned int`라서 `int`로 캐스팅해 비교한다(murage.mp4는 2). 여기서 세 구조체의 역할 구분이 핵심이다.

- `AVStream`: 컨테이너 안의 트랙 자체 (time_base, duration 등 컨테이너 수준 정보)
- `AVCodecParameters`: 그 트랙의 코덱 **설정값** — 순수 데이터 구조라 이것만으로는 디코딩할 수 없다
- `AVCodec`: 디코더 **구현체**에 대한 정적 기술자(이름, 지원 기능). `avcodec_find_decoder()`는 전역 레지스트리에서 찾아 오는 것이라 해제할 필요가 없고, 반환 타입도 `const AVCodec *`다.

### 2. 디코더 존재 확인과 미디어 타입 출력

```c
printf("==== Stream #%d ====\r\n", idx);
/** 스트림 종류를 사람이 읽을 수 있는 문자열로 변환 */
printf("media type     : %s\r\n", av_get_media_type_string(pCodecParameters->codec_type));

if (pCodec == NULL) {
    printf("codec          : (decoder not found, codec_id=%d)\r\n\r\n", pCodecParameters->codec_id);
    continue;
}
```

`codec_type`은 `AVMEDIA_TYPE_VIDEO`/`AVMEDIA_TYPE_AUDIO`/`AVMEDIA_TYPE_SUBTITLE` 등의 enum이고, `av_get_media_type_string()`이 "video", "audio" 같은 문자열로 바꿔준다. 디코더를 못 찾은 스트림(빌드에 코덱이 빠졌거나 독점 코덱인 경우)은 `continue`로 건너뛰어 이후 코드가 NULL 포인터를 만지지 않게 방어한다.

### 3. time_base와 duration 초 변환

```c
/**
 * time_base:
 * 이 스트림의 pts/dts/duration이 사용하는 시간 단위(분수).
 * 예: 1/12800 이면 pts 1 = 1/12800 초.
 */
printf("time_base      : %d/%d\r\n", pCurStream->time_base.num, pCurStream->time_base.den);
/** 스트림 duration(스트림 time_base 단위)을 초로 변환 */
if (pCurStream->duration != AV_NOPTS_VALUE) {
    printf("duration       : %.2f sec\r\n", pCurStream->duration * av_q2d(pCurStream->time_base));
}
```

`time_base`는 `AVRational`(분자 `num` / 분모 `den`)이다. murage.mp4에서는 비디오가 **1/15360**, 오디오가 **1/48000**으로 출력된다 — 스트림마다 단위가 다르므로 pts 값을 그대로 비교하면 안 된다는 것을 여기서 처음 체감할 수 있다. `av_q2d()`는 `num / (double) den`을 계산하는 인라인 함수로, `duration(틱 수) x 틱 하나의 초 길이 = 초`가 된다. duration이 기록되지 않은 스트림은 `AV_NOPTS_VALUE`라는 센티널 값을 가지므로 반드시 검사 후 사용한다.

### 4. 타입별 분기

```c
if (pCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
    PrintVideoStreamInfo(pCurStream, pCodec);
} else if (pCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
    PrintAudioStreamInfo(pCurStream, pCodec);
} else if (pCodecParameters->codec_type == AVMEDIA_TYPE_SUBTITLE) {
    printf("codec          : %s\r\n", pCodec->name);
}
```

비디오와 오디오는 봐야 할 파라미터가 완전히 다르므로 함수를 분리했다. murage.mp4에는 자막 스트림이 없어 SUBTITLE 분기는 실행되지 않는다.

### 5. 비디오 스트림 정보 출력

```c
void PrintVideoStreamInfo(const AVStream *pStream, const AVCodec *pCodec) {
    const AVCodecParameters *pCodecParameters = pStream->codecpar;
    /**
     * avg_frame_rate: 컨테이너 기준 평균 프레임레이트.
     * av_q2d로 AVRational(분수)을 double로 변환한다.
     */
    double frameRate = av_q2d(pStream->avg_frame_rate);

    printf("codec          : %s (%s)\r\n", pCodec->name, pCodec->long_name);
    printf("resolution     : %dx%d\r\n", pCodecParameters->width, pCodecParameters->height);
    printf("frame rate     : %.2f fps\r\n", frameRate);
    /** 픽셀 포맷(yuv420p 등) — codecpar에서는 format 필드에 들어있다 */
    printf("pixel format   : %s\r\n", av_get_pix_fmt_name((enum AVPixelFormat) pCodecParameters->format));
    printf("bit rate       : %lld bps\r\n", pCodecParameters->bit_rate);
}
```

- **`avg_frame_rate`**: 컨테이너가 계산한 평균 프레임레이트. 이것도 `AVRational`이라 `av_q2d()`로 변환한다. murage.mp4는 30.08 fps다. (chapter02 예제들이 쓰던 `r_frame_rate`는 "추정된 최소 공배수 프레임레이트"로 의미가 다르다.)
- **`format` 필드**: `AVCodecParameters`에는 `pix_fmt`라는 이름의 필드가 없고, 비디오면 픽셀 포맷 / 오디오면 샘플 포맷이 공용 필드 `format`(int)에 들어간다. 그래서 `enum AVPixelFormat`으로 캐스팅한 뒤 `av_get_pix_fmt_name()`으로 이름(yuv420p)을 얻는다.
- murage.mp4 출력: `h264 (H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10)`, 1280x720, yuv420p.

### 6. 오디오 스트림 정보 출력 — FFmpeg 7.x AVChannelLayout

```c
void PrintAudioStreamInfo(const AVStream *pStream, const AVCodec *pCodec) {
    const AVCodecParameters *pCodecParameters = pStream->codecpar;
    char channelLayoutText[BUFFER_MAX] = {0};

    /**
     * FFmpeg 7.x의 채널 레이아웃은 AVChannelLayout 구조체(ch_layout)다.
     * (과거의 uint64_t channel_layout 필드는 제거되었다)
     * av_channel_layout_describe로 "stereo" 같은 설명 문자열을 얻는다.
     */
    av_channel_layout_describe(&pCodecParameters->ch_layout, channelLayoutText, sizeof(channelLayoutText));

    printf("codec          : %s (%s)\r\n", pCodec->name, pCodec->long_name);
    printf("sample rate    : %d Hz\r\n", pCodecParameters->sample_rate);
    printf("channels       : %d (%s)\r\n", pCodecParameters->ch_layout.nb_channels, channelLayoutText);
    printf("bit rate       : %lld bps\r\n", pCodecParameters->bit_rate);
}
```

FFmpeg 5.1에서 채널 레이아웃 API가 전면 개편되어, 비트마스크(`uint64_t channel_layout`)와 `channels` 필드가 `AVChannelLayout` 구조체 하나로 통합되었고 7.x에서는 구 필드가 완전히 제거되었다. 채널 수는 `ch_layout.nb_channels`, 배치 설명은 `av_channel_layout_describe()`가 버퍼에 써 주는 문자열("stereo", "5.1" 등)로 읽는다. murage.mp4 출력: `aac (AAC (Advanced Audio Coding))`, 48000 Hz, 2 (stereo). 샘플 포맷은 fltp(planar float)지만 이 예제에서는 출력하지 않는다.

## 심화: AVCodecParameters는 왜 따로 존재하는가

과거 FFmpeg에서는 `AVStream` 안에 `AVCodecContext`(디코더 상태 전체)가 통째로 들어 있었다. 이 설계는 "정보를 읽기만 하고 싶은데 디코더 구조체가 딸려 오는" 문제와 스레드 안전성 문제를 낳았고, 그래서 **순수 데이터**만 담는 `AVCodecParameters`가 분리되었다. 흐름은 다음과 같다.

```mermaid
flowchart LR
    A["AVStream->codecpar\n(파일 헤더의 설정값)"] -->|avcodec_parameters_to_context| B["AVCodecContext\n(실제 디코더 상태)"]
    B -->|avcodec_open2| C["디코딩 가능"]
```

이 레슨은 왼쪽(파라미터 읽기)까지만 다루고, 오른쪽(컨텍스트 생성과 디코딩)은 04 레슨에서 완성한다.
