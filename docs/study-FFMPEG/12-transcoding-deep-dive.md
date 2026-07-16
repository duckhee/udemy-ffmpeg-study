# 12. 트랜스코딩 — 코드 상세 해설

> [← 기본 문서](12-transcoding.md)

## 전체 구조

`main()`이 입력(디코딩) / 변환(스케일) / 출력(인코딩+먹싱) 세 묶음의 자원을 준비한 뒤, 메인 루프에서 패킷을 읽어 프레임 단위 처리 함수 두 개에 흘려보낸다.

| 구성 요소 | 역할 |
|---|---|
| `main()` | 자원 준비 → 트랜스코딩 루프 → 2단 flush → trailer → 해제 |
| `ScaleEncodeAndMux()` | 디코딩된 프레임 1장을 `sws_scale`로 640x360 변환 + pts 전달 후 `EncodeAndMux()` 호출 |
| `EncodeAndMux()` | `avcodec_send_frame` → `avcodec_receive_packet` 루프 → 타임스탬프 변환 → 먹서 기록. `pFrame == NULL`이면 인코더 flush |
| `EnsureGeneratedStudyDirectory()` | `resources/GeneratedStudy/` 디렉터리 생성 |
| `GetResourcePath()` | 실행 경로에서 저장소 루트의 `resources/` 경로 역산 |

```text
main
 ├─ 1. 입력 열기 + 디코더 준비 (open_input → find_best_stream → decoder open)
 ├─ 2. 출력 컨텍스트 + 인코더 준비 (libx264 or MPEG-4 fallback)
 ├─ 3. 스케일러 + 640x360 프레임 버퍼 준비
 ├─ 4. avio_open + write_header
 ├─ 5. while (av_read_frame)
 │      └─ send_packet → receive_frame 루프 → ScaleEncodeAndMux
 ├─ 6. flush: 디코더(NULL 패킷) → 인코더(NULL 프레임) → av_write_trailer
 └─ ffmpeg_release: 역순 해제
```

## 코드 블록별 해설

### 1. 출력 사양과 3그룹 변수 선언

```c
/** 트랜스코딩 출력 사양: 640x360 / 500kbps */
#define OUTPUT_WIDTH                640
#define OUTPUT_HEIGHT               360
#define OUTPUT_BIT_RATE             500000
```

```c
/** 입력(디코딩) 쪽 */
AVFormatContext *pInputContext = NULL;
const AVCodec *pDecoder = NULL;
AVCodecContext *pDecoderContext = NULL;
int videoStreamIdx = -1;
AVPacket *pInputPacket = NULL;
AVFrame *pDecodedFrame = NULL;

/** 변환(스케일) 쪽 */
struct SwsContext *pSwsContext = NULL;
AVFrame *pScaledFrame = NULL;

/** 출력(인코딩+먹싱) 쪽 */
AVFormatContext *pOutputContext = NULL;
const AVCodec *pEncoder = NULL;
AVCodecContext *pEncoderContext = NULL;
AVStream *pOutputStream = NULL;
AVPacket *pEncodedPacket = NULL;
```

트랜스코딩은 자원이 많아 실수하기 쉽다. 이 코드는 변수를 **입력 / 변환 / 출력** 세 그룹으로 나눠 선언해 파이프라인 각 단계가 어떤 자원을 쓰는지 명확히 한다. 패킷도 두 개다 — `pInputPacket`은 컨테이너에서 읽은 압축 패킷, `pEncodedPacket`은 인코더가 새로 만든 압축 패킷.

### 2. 입력 열기 + 디코더 준비

```c
videoStreamIdx = av_find_best_stream(pInputContext, AVMEDIA_TYPE_VIDEO, -1, -1, &pDecoder, 0);
if (videoStreamIdx < 0) {
    av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Video Stream Found Failed...\r\n", videoStreamIdx);
    goto ffmpeg_release;
}
pInputStream = pInputContext->streams[videoStreamIdx];
```

`av_find_best_stream()`은 스트림 인덱스를 반환하면서 다섯 번째 인자로 **적합한 디코더까지 함께** 찾아 준다. 이후 `alloc_context3 → parameters_to_context → open2`의 정석 3단계는 04 레슨과 동일하다.

### 3. 인코더 선택 — libx264와 MPEG-4 fallback

```c
pEncoder = avcodec_find_encoder_by_name("libx264");
if (pEncoder == NULL) {
    printf("libx264 not found → fallback to MPEG-4 encoder\r\n");
    pEncoder = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
}
```

`avcodec_find_encoder_by_name()`은 **정확한 구현체 이름**으로 인코더를 찾는다(코덱 ID로 찾는 `avcodec_find_encoder()`와 구별). 이 저장소의 vcpkg FFmpeg 빌드에는 libx264가 포함되어 있지 않아 실제로는 FFmpeg 내장 MPEG-4 인코더로 fallback된다. 둘 다 없을 때만 에러 처리한다.

### 4. 인코더 설정 — time_base 통일이 핵심

```c
pEncoderContext->width = OUTPUT_WIDTH;
pEncoderContext->height = OUTPUT_HEIGHT;
pEncoderContext->pix_fmt = AV_PIX_FMT_YUV420P;
pEncoderContext->bit_rate = OUTPUT_BIT_RATE;
/**
 * 인코더 time_base를 입력 스트림과 같게 맞춘다.
 * 그러면 디코딩된 프레임의 pts를 변환 없이 그대로 인코더에 넘길 수 있다.
 */
pEncoderContext->time_base = pInputStream->time_base;
pEncoderContext->framerate = av_guess_frame_rate(pInputContext, pInputStream, NULL);
pEncoderContext->gop_size = 25;
```

이 레슨의 가장 중요한 설계 결정이다. 디코딩된 프레임의 pts는 입력 스트림 time_base 단위인데, 인코더 time_base를 이것과 통일하면 파이프라인 중간에 pts 변환이 필요 없어진다(passthrough). `av_guess_frame_rate()`는 스트림 메타데이터에서 실제 프레임레이트(30fps)를 추정하고, `gop_size = 25`는 키프레임 간격을 25프레임으로 지정한다.

```c
if (strcmp(pEncoder->name, "libx264") == 0) {
    av_opt_set(pEncoderContext->priv_data, "preset", "fast", 0);
}
if (pOutputContext->oformat->flags & AVFMT_GLOBALHEADER) {
    pEncoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}
```

- `preset`은 libx264 전용 옵션이므로 실제 선택된 인코더 이름을 확인하고 설정한다. `priv_data`는 코덱별 private 옵션 저장소다.
- mp4처럼 코덱 초기화 정보(extradata)를 **컨테이너 헤더에 한 번만** 담는 포맷은 `AVFMT_GLOBALHEADER` 플래그가 켜져 있고, 인코더에도 `AV_CODEC_FLAG_GLOBAL_HEADER`를 알려 줘야 한다. 11 레슨에서 본 것과 같은 패턴이다.

### 5. 출력 스트림 생성

```c
pOutputStream = avformat_new_stream(pOutputContext, NULL);
if (pOutputStream == NULL) {
    goto ffmpeg_release;
}
errorCode = avcodec_parameters_from_context(pOutputStream->codecpar, pEncoderContext);
if (errorCode < 0) {
    goto ffmpeg_release;
}
pOutputStream->time_base = pEncoderContext->time_base;
```

디코딩 준비 때는 `parameters_to_context`(스트림 → 컨텍스트)였지만, 출력 스트림을 만들 때는 반대 방향인 `avcodec_parameters_from_context`(컨텍스트 → 스트림)를 쓴다. 참고로 `pOutputStream->time_base`는 힌트일 뿐이며, `avformat_write_header()`가 mp4 규칙에 맞게 바꿀 수 있다 — 그래서 나중에 `av_packet_rescale_ts()`가 필요하다.

### 6. 스케일러와 640x360 프레임 버퍼

```c
pSwsContext = sws_getContext(
        pDecoderContext->width, pDecoderContext->height, pDecoderContext->pix_fmt,
        OUTPUT_WIDTH, OUTPUT_HEIGHT, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, NULL, NULL, NULL);
```

```c
pScaledFrame->format = AV_PIX_FMT_YUV420P;
pScaledFrame->width = OUTPUT_WIDTH;
pScaledFrame->height = OUTPUT_HEIGHT;
if (av_frame_get_buffer(pScaledFrame, 0) < 0) {
    goto ffmpeg_release;
}
```

06 레슨에서는 YUV → RGB 픽셀 포맷 변환에 sws를 썼지만, 여기서는 **포맷은 YUV420P로 유지하고 해상도만** 1280x720 → 640x360으로 줄인다. `av_frame_get_buffer()`로 실제 픽셀 버퍼를 한 번만 할당하고, 매 프레임 재사용한다.

### 7. 트랜스코딩 메인 루프

```c
while (av_read_frame(pInputContext, pInputPacket) >= 0) {
    if (pInputPacket->stream_index == videoStreamIdx) {
        errorCode = avcodec_send_packet(pDecoderContext, pInputPacket);
        if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Sending packet to decoder\r\n", errorCode);
            av_packet_unref(pInputPacket);
            break;
        }

        while (true) {
            errorCode = avcodec_receive_frame(pDecoderContext, pDecodedFrame);
            if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
                break;
            } else if (errorCode < 0) {
                break;
            }

            writtenPacketCount += ScaleEncodeAndMux(pDecodedFrame, pSwsContext, pScaledFrame,
                                                    pEncoderContext, pOutputStream,
                                                    pOutputContext, pEncodedPacket);
            av_frame_unref(pDecodedFrame);
        }
    }
    av_packet_unref(pInputPacket);
}
```

디먹싱(03) + 디코딩(04)의 골격 그대로다. 달라진 점은 프레임을 얻자마자 `ScaleEncodeAndMux()`로 파이프라인 후반부에 넘긴다는 것뿐이다. 비디오가 아닌 패킷(오디오)은 `stream_index` 검사에서 걸러져 그대로 버려진다 — 이것이 "오디오 드롭"의 실체다.

### 8. ScaleEncodeAndMux — 스케일 + pts 전달

```c
int ScaleEncodeAndMux(AVFrame *pDecodedFrame, struct SwsContext *pSwsContext, AVFrame *pScaledFrame,
                      AVCodecContext *pEncoderContext, AVStream *pOutputStream,
                      AVFormatContext *pOutputContext, AVPacket *pEncodedPacket) {
    /**
     * 타임스탬프가 없는 프레임(AV_NOPTS_VALUE)을 그대로 인코더에 넣으면
     * 먹서에서 'non monotonically increasing dts' 에러로 기록이 끊긴다.
     * 이런 프레임은 건너뛴다.
     */
    if (pDecodedFrame->best_effort_timestamp == AV_NOPTS_VALUE) {
        printf("frame without timestamp — skip\r\n");
        return 0;
    }

    if (av_frame_make_writable(pScaledFrame) < 0) {
        return 0;
    }

    /** 해상도 변경 (픽셀 포맷은 YUV420P 유지) */
    sws_scale(pSwsContext,
              (const uint8_t *const *) pDecodedFrame->data, pDecodedFrame->linesize,
              0, pDecodedFrame->height,
              pScaledFrame->data, pScaledFrame->linesize);

    /**
     * pts 전달: 인코더 time_base = 입력 스트림 time_base로 맞췄으므로
     * 디코딩된 프레임의 pts를 그대로 사용할 수 있다.
     * (best_effort_timestamp: pts가 없는 프레임까지 고려한 최선의 타임스탬프)
     */
    pScaledFrame->pts = pDecodedFrame->best_effort_timestamp;

    return EncodeAndMux(pEncoderContext, pScaledFrame, pEncodedPacket, pOutputStream, pOutputContext);
}
```

- **NOPTS 가드**: `best_effort_timestamp`조차 `AV_NOPTS_VALUE`인 프레임은 함수 초입에서 건너뛴다. 그런 프레임을 인코더에 넣으면 pts가 `AV_NOPTS_VALUE`인 패킷이 만들어지고, 먹서가 `non monotonically increasing dts` 에러로 이후 기록을 전부 거부하기 때문이다.
- `av_frame_make_writable()`: 인코더가 이전에 넘긴 `pScaledFrame` 버퍼를 아직 참조 중일 수 있다(reference counting). 그 상태에서 `sws_scale`로 덮어쓰면 인코딩 중인 데이터가 깨지므로, 공유 중이면 새 버퍼로 복사해 단독 소유로 만든 뒤 쓴다.
- `pScaledFrame->pts = pDecodedFrame->best_effort_timestamp`: 이 한 줄이 pts passthrough의 전부다. time_base를 통일했기 때문에 산술 변환이 전혀 없다. `pts` 대신 `best_effort_timestamp`를 쓰는 이유는 pts가 `AV_NOPTS_VALUE`인 프레임도 dts 등에서 추정된 값으로 메꿔 주기 때문이다.

### 9. EncodeAndMux — 인코딩과 먹싱 (flush 겸용)

```c
int EncodeAndMux(AVCodecContext *pEncoderContext, AVFrame *pFrame, AVPacket *pPacket,
                 AVStream *pOutputStream, AVFormatContext *pOutputContext) {
    int writtenCount = 0;
    int errorCode = 0;

    errorCode = avcodec_send_frame(pEncoderContext, pFrame);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Sending frame to encoder\r\n", errorCode);
        return 0;
    }

    while (errorCode >= 0) {
        errorCode = avcodec_receive_packet(pEncoderContext, pPacket);
        if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
            break;
        } else if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Receive packet\r\n", errorCode);
            break;
        }

        av_packet_rescale_ts(pPacket, pEncoderContext->time_base, pOutputStream->time_base);
        pPacket->stream_index = pOutputStream->index;

        errorCode = av_interleaved_write_frame(pOutputContext, pPacket);
        if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Write frame\r\n", errorCode);
            break;
        }
        writtenCount++;
    }

    return writtenCount;
}
```

디코딩의 send/receive를 거울처럼 뒤집은 구조다 — 프레임을 넣고(`send_frame`) 패킷을 꺼낸다(`receive_packet`). 주목할 점:

- **`pFrame == NULL`이면 flush 모드**: `avcodec_send_frame(ctx, NULL)`은 "더 이상 입력이 없다"는 신호로, 인코더가 내부에 지연시켜 둔 패킷을 모두 내보내기 시작한다. 같은 함수가 일반 인코딩과 flush를 겸한다.
- **`av_packet_rescale_ts()`**: 인코더가 만든 패킷의 pts/dts/duration은 인코더 time_base 단위다. `avformat_write_header()` 이후 확정된 출력 스트림 time_base로 여기서 한 번만 변환한다.
- **`pPacket->stream_index = pOutputStream->index`**: 인코더는 스트림 개념을 모르므로 먹서에 넘기기 전 패킷이 속할 스트림 번호를 우리가 지정한다.
- **`av_interleaved_write_frame()`**: 먹서가 패킷을 내부 큐에 모아 dts 순서로 인터리빙해 기록한다. 이 예제는 비디오 단일 스트림이지만 오디오를 추가할 경우를 대비한 올바른 습관이다.

### 10. 2단 flush와 trailer — 순서가 생명

```c
/** ===== 6. flush: 디코더 먼저, 인코더 나중 ===== */
errorCode = avcodec_send_packet(pDecoderContext, NULL);
if (errorCode >= 0) {
    while (true) {
        errorCode = avcodec_receive_frame(pDecoderContext, pDecodedFrame);
        if (errorCode < 0) {
            break;
        }
        writtenPacketCount += ScaleEncodeAndMux(pDecodedFrame, pSwsContext, pScaledFrame,
                                                pEncoderContext, pOutputStream,
                                                pOutputContext, pEncodedPacket);
        av_frame_unref(pDecodedFrame);
    }
}
writtenPacketCount += EncodeAndMux(pEncoderContext, NULL, pEncodedPacket, pOutputStream, pOutputContext);

errorCode = av_write_trailer(pOutputContext);
```

순서를 바꾸면 안 되는 이유를 따라가 보자.

1. **디코더 flush가 먼저**: 디코더에 남은 마지막 프레임들은 아직 인코더를 거치지 않았다. 이 프레임들을 꺼내 `ScaleEncodeAndMux()`로 인코더에 마저 넣어야 한다.
2. **인코더 flush는 그다음**: 디코더에서 나온 마지막 프레임까지 모두 받은 뒤에야 인코더에 "끝"을 알릴 수 있다. 인코더를 먼저 flush하면 이후 `send_frame`이 `AVERROR_EOF`로 거부된다.
3. **trailer는 마지막**: `av_write_trailer()`는 mp4의 moov 인덱스 등을 확정한다. 이후에 쓴 패킷은 파일에 반영되지 않는다.

flush 루프의 receive는 `errorCode < 0`이면 무조건 탈출한다 — flush 모드에서는 `EAGAIN` 없이 프레임이 계속 나오다가 `AVERROR_EOF`로 끝나기 때문에 단순 비교로 충분하다.

### 11. 자원 해제 — 파이프라인 역순

```c
exitStatus = 0;

ffmpeg_release:
av_packet_free(&pEncodedPacket);
av_frame_free(&pDecodedFrame);
av_packet_free(&pInputPacket);
av_frame_free(&pScaledFrame);
sws_freeContext(pSwsContext);
avcodec_free_context(&pEncoderContext);
if (pOutputContext != NULL && pOutputContext->pb != NULL) {
    avio_closep(&pOutputContext->pb);
}
avformat_free_context(pOutputContext);
avcodec_free_context(&pDecoderContext);
avformat_close_input(&pInputContext);
if (exitStatus == 0) {
    printf("Transcoding Done!\r\n");
} else {
    printf("Finished with error(s)...\r\n");
}
return exitStatus;
```

출력 쪽은 `avio_open()`으로 우리가 직접 연 파일이므로 `avio_closep()`로 직접 닫고, `avformat_free_context()`로 컨텍스트를 해제한다. 입력 쪽은 `avformat_close_input()` 하나가 파일 닫기와 컨텍스트 해제를 모두 처리한다 — 열 때의 API가 다르면 닫을 때의 API도 다르다.

`exitStatus`는 `-1`로 시작해 성공 경로 끝에서만 `0`으로 바뀐다. 중간에 에러로 `goto ffmpeg_release`를 타면 `-1`인 채 0이 아닌 종료 코드로 끝나므로 셸/CI에서 실패를 감지할 수 있다.

## 심화: 파이프라인을 흐르는 타임스탬프

```mermaid
flowchart LR
    A["입력 패킷 pts<br/>(입력 스트림 tb)"] --> B["디코딩된 프레임<br/>best_effort_timestamp<br/>(입력 스트림 tb)"]
    B -->|"그대로 대입<br/>(tb 통일 덕분)"| C["스케일된 프레임 pts<br/>(인코더 tb = 입력 스트림 tb)"]
    C --> D["인코딩된 패킷 pts/dts<br/>(인코더 tb)"]
    D -->|"av_packet_rescale_ts"| E["파일에 기록되는 pts/dts<br/>(출력 스트림 tb)"]
```

타임스탬프 변환은 파이프라인 전체에서 **마지막 한 곳**(`av_packet_rescale_ts`)에만 존재한다. 인코더 time_base를 입력 스트림과 통일한 설계 덕분이다. 만약 인코더 time_base를 `1/framerate` 같은 다른 값으로 잡았다면, 프레임을 인코더에 넣기 전에 `av_rescale_q()`로 pts를 변환하는 단계가 하나 더 필요했을 것이다.

## ⚠️ 코드 특이점 상세

1. **libx264 부재 → MPEG-4 fallback (환경 의존)**
   vcpkg의 ffmpeg[all]에는 GPL 라이선스 문제로 libx264가 기본 포함되지 않는다. 따라서 이 환경에서 결과물은 항상 MPEG-4 Part 2 코덱이다. 실측: 383개 패킷 기록, ffprobe 확인 결과 `mpeg4 640x360`. H.264 출력이 필요하면 vcpkg feature에 x264를 추가해 FFmpeg을 다시 빌드해야 한다.

2. **오디오 스트림은 조용히 버려진다**
   메인 루프가 `stream_index == videoStreamIdx`인 패킷만 처리하므로 오디오 패킷은 `av_packet_unref()`로 바로 해제된다. 출력 mp4에는 오디오 스트림 자체가 없다(스트림을 만들지 않았으므로).

3. **`ScaleEncodeAndMux()`의 `av_frame_make_writable()` 실패 시 프레임 유실**
   버퍼를 writable로 만들지 못하면 `return 0`으로 그 프레임을 조용히 건너뛴다. 학습용으로는 무방하지만 프로덕션이라면 에러 전파가 필요하다.

4. **`FFMPEG_ERROR` 매크로는 첫 호출(open_input)에만 사용**
   이후 에러는 모두 `goto ffmpeg_release` 패턴으로 자원 해제를 보장한다. 매크로는 `return -1`을 하므로 자원 할당 이후에 쓰면 누수가 생기기 때문이다.
