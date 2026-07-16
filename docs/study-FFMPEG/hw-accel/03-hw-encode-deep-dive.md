# 03. VideoToolbox 하드웨어 인코딩 — 코드 상세 해설

> [← 기본 문서](03-hw-encode.md)

## 전체 구조

트랜스코딩(본편 12)과 같은 "입력 데믹싱 → SW 디코딩 → 인코딩 → 먹싱" 구조에서 인코더만 `h264_videotoolbox`로 바뀌었다. 파일 전체가 `#if defined(__APPLE__)` 가드 아래에 있다.

| 구성 요소 | 역할 |
|---|---|
| `main()` | 인코더 확인 → 입력/디코더 준비 → 출력/인코더 준비 → 트랜스코딩 루프 → 이중 flush → trailer |
| `EncodeAndMux()` | 프레임(NULL이면 flush) 하나를 인코딩해 나온 패킷들을 모두 먹서에 쓴다 |
| `EnsureGeneratedStudyDirectory()` / `GetResourcePath()` | 출력 디렉터리 생성 / 리소스 경로 계산 (본편과 공통) |

## 코드 블록별 해설

### 1. 이름으로 HW 인코더 찾기

```c
/** ===== 1. VideoToolbox H.264 인코더 확인 ===== */
pEncoder = avcodec_find_encoder_by_name("h264_videotoolbox");
if (pEncoder == NULL) {
    printf("h264_videotoolbox encoder not available in this FFmpeg build.\r\n");
    printf("(vcpkg ffmpeg[all]에는 포함되어 있다 — 빌드 설정을 확인하자)\r\n");
    return 0;
}
printf("encoder : %s (%s)\r\n", pEncoder->name, pEncoder->long_name);
```

`avcodec_find_encoder(AV_CODEC_ID_H264)`는 등록 순서상 기본 인코더를 돌려주므로, 특정 HW 구현을 원할 때는 반드시 **이름으로** 찾는다. 이 vcpkg 빌드에는 libx264가 없어 본편 08/12가 MPEG-4로 폴백했지만, `h264_videotoolbox`는 존재한다 — HW 인코딩이 오히려 SW H.264 인코딩보다 접근성이 좋은 상황이다. 디코딩(01~02)과 달리 인코딩은 "내장 코덱 + HW config"가 아니라 **별도 인코더 구현체**로 제공된다는 차이도 눈여겨보자.

### 2. 입력 + SW 디코더 준비

```c
videoStreamIdx = av_find_best_stream(pInputContext, AVMEDIA_TYPE_VIDEO, -1, -1, &pDecoder, 0);
if (videoStreamIdx < 0) {
    goto ffmpeg_release;
}
pInputStream = pInputContext->streams[videoStreamIdx];
```

02와 같은 `av_find_best_stream` 패턴이지만, 이번에는 디코더에 `hw_device_ctx`를 걸지 않는다 — **일부러 SW 디코딩**을 써서 HW 인코더의 효과만 분리해 관찰한다.

### 3. 출력 컨텍스트와 인코더 설정

```c
errorCode = avformat_alloc_output_context2(&pOutputContext, NULL, NULL, outputPath);
...
/** 해상도는 원본 유지, 비트레이트만 지정 */
pEncoderContext->width = pDecoderContext->width;
pEncoderContext->height = pDecoderContext->height;
/** SW 프레임을 그대로 공급 — 인코더가 내부에서 GPU로 업로드한다 */
pEncoderContext->pix_fmt = pDecoderContext->pix_fmt;
pEncoderContext->bit_rate = 2000000;
pEncoderContext->time_base = pInputStream->time_base;
pEncoderContext->framerate = av_guess_frame_rate(pInputContext, pInputStream, NULL);

if (pOutputContext->oformat->flags & AVFMT_GLOBALHEADER) {
    pEncoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}
```

이 레슨의 핵심이 `pix_fmt` 한 줄에 숨어 있다. HW 인코딩인데도 `pix_fmt`에 디코더의 SW 포맷(yuv420p)을 그대로 넣는다 — `h264_videotoolbox`가 SW 포맷을 입력으로 받아 **내부에서 CVPixelBuffer로 변환·업로드**하기 때문이다. HW 프레임(`AV_PIX_FMT_VIDEOTOOLBOX`)을 직접 공급하는 제로카피 경로도 있지만 그때는 `hw_frames_ctx` 설정이 필요하다. 이 레슨은 쉬운 경로를 택했다.

- `time_base = pInputStream->time_base`: 인코더의 시간 단위를 입력 스트림과 같게 맞춰, 디코딩된 프레임의 pts를 변환 없이 재사용할 수 있게 한다.
- `AV_CODEC_FLAG_GLOBAL_HEADER`: mp4는 SPS/PPS를 패킷 안이 아니라 컨테이너 헤더(extradata)에 두는 포맷이므로, 먹서가 요구하면(`AVFMT_GLOBALHEADER`) 인코더에도 알려줘야 한다.

### 4. 인코더 열기 — 실패 시 정상 종료

```c
errorCode = avcodec_open2(pEncoderContext, pEncoder, NULL);
if (errorCode < 0) {
    av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Open HW Encoder...\r\n", errorCode);
    printf("VideoToolbox encoder open failed — HW encoding not available on this machine.\r\n");
    goto ffmpeg_release;
}
```

인코더가 빌드에 있어도(1단계 통과) 실제 HW 세션 생성은 여기서 일어난다. 실패하면 안내 메시지를 남기고 `goto ffmpeg_release`로 자원을 정리한 뒤 종료한다 — 이 시점에는 이미 입력/출력 자원이 할당되어 있으므로 `exitStatus`가 `-1`인 채 0이 아닌 종료 코드로 끝난다(인코더 자체가 빌드에 없는 1단계와 달리 실패로 취급).

### 5. 출력 스트림 + 헤더 쓰기

```c
pOutputStream = avformat_new_stream(pOutputContext, NULL);
...
errorCode = avcodec_parameters_from_context(pOutputStream->codecpar, pEncoderContext);
...
pOutputStream->time_base = pEncoderContext->time_base;

errorCode = avio_open(&pOutputContext->pb, outputPath, AVIO_FLAG_WRITE);
...
errorCode = avformat_write_header(pOutputContext, NULL);
```

`avcodec_parameters_from_context()`는 디코딩 때 쓰던 `parameters_to_context()`의 역방향이다 — 인코더가 확정한 코덱 정보(extradata 포함)를 스트림에 복사해 먹서가 헤더를 쓸 수 있게 한다. 그래서 반드시 `avcodec_open2()` **이후**에 호출해야 한다. `pOutputStream->time_base`는 요청값일 뿐이며, `avformat_write_header()`에서 먹서가 실제 값으로 바꿀 수 있다(mp4는 보통 1/15360 같은 값으로 조정) — 그래서 6단계의 `av_packet_rescale_ts`가 필요하다.

### 6. 트랜스코딩 루프와 `EncodeAndMux`

```c
/** 타임스탬프 없는 프레임은 먹싱 에러를 일으키므로 건너뛴다 */
if (pDecodedFrame->best_effort_timestamp == AV_NOPTS_VALUE) {
    printf("frame without timestamp — skip\r\n");
    av_frame_unref(pDecodedFrame);
    continue;
}

/** 인코더 time_base = 입력 스트림 time_base → pts 그대로 전달 */
pDecodedFrame->pts = pDecodedFrame->best_effort_timestamp;
encodedFrameCount++;
writtenPacketCount += EncodeAndMux(pEncoderContext, pDecodedFrame, pEncodedPacket,
                                   pOutputStream, pOutputContext);
av_frame_unref(pDecodedFrame);
```

`best_effort_timestamp`는 pts가 없는 프레임에서도 FFmpeg이 dts 등으로 추정한 최선의 표시 시각이다. 인코더 time_base를 입력 스트림과 맞춰 두었으므로 이 값을 그대로 인코더에 넘긴다. 다만 그 추정값조차 없는(`AV_NOPTS_VALUE`) 프레임은 인코더에 넣지 않고 건너뛴다 — 그대로 넣으면 pts 없는 패킷이 만들어져 먹서가 `non monotonically increasing dts` 에러로 기록을 거부하기 때문이다.

```c
static int EncodeAndMux(AVCodecContext *pEncoderContext, AVFrame *pFrame, AVPacket *pPacket,
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
            break;
        }
        writtenCount++;
    }

    return writtenCount;
}
```

디코딩 파이프라인의 완전한 거울상이다 — `send_frame`으로 넣고 `receive_packet`으로 꺼내며, `EAGAIN`은 "프레임을 더 보내라", `AVERROR_EOF`는 "flush 완료"다. `pFrame == NULL`이면 `avcodec_send_frame(ctx, NULL)`이 flush 신호가 되므로, 같은 함수가 일반 인코딩과 flush를 모두 처리한다. 패킷마다 인코더 time_base → 출력 스트림 time_base로 타임스탬프를 변환하고 `stream_index`를 지정한 뒤 `av_interleaved_write_frame()`에 넘긴다(패킷 소유권도 먹서로 넘어가므로 별도 unref가 필요 없다).

### 7. 이중 flush와 trailer

```c
/** flush: 디코더 → 인코더 순서 */
errorCode = avcodec_send_packet(pDecoderContext, NULL);
if (errorCode >= 0) {
    while (avcodec_receive_frame(pDecoderContext, pDecodedFrame) >= 0) {
        if (pDecodedFrame->best_effort_timestamp == AV_NOPTS_VALUE) {
            av_frame_unref(pDecodedFrame);
            continue;
        }
        pDecodedFrame->pts = pDecodedFrame->best_effort_timestamp;
        encodedFrameCount++;
        writtenPacketCount += EncodeAndMux(pEncoderContext, pDecodedFrame, pEncodedPacket,
                                           pOutputStream, pOutputContext);
        av_frame_unref(pDecodedFrame);
    }
}
writtenPacketCount += EncodeAndMux(pEncoderContext, NULL, pEncodedPacket, pOutputStream, pOutputContext);

errorCode = av_write_trailer(pOutputContext);
```

파이프라인이 2단(디코더→인코더)이므로 flush도 2단이다. 디코더를 먼저 비워 남은 프레임을 모두 인코더에 밀어 넣고, 그다음 인코더를 비워 남은 패킷을 모두 파일에 쓴다. 순서를 바꾸면 디코더에 남아 있던 프레임들이 이미 flush된 인코더에 들어가지 못해 유실된다. flush로 나온 프레임에도 메인 루프와 같은 `AV_NOPTS_VALUE` 스킵 가드를 적용한다. 마지막 `av_write_trailer()`가 mp4의 moov 인덱스를 완성한다 — 이것이 빠지면 재생 불가능한 파일이 된다. 실측 결과 `encoded frames : 383 → written packets : 383`으로 프레임 유실이 없음을 확인할 수 있다.

### 8. 성능 출력과 자원 해제

```c
elapsedSeconds = (double) (clock() - startClock) / CLOCKS_PER_SEC;
printf("encoded frames : %d → written packets : %d\r\n", encodedFrameCount, writtenPacketCount);
printf("elapsed : %.3f sec (CPU time) → %.1f fps\r\n",
       elapsedSeconds, elapsedSeconds > 0 ? encodedFrameCount / elapsedSeconds : 0.0);
```

실측 1.59초(240.8 fps)는 02의 HW 디코딩(0.091초)보다 크지만, 이 수치에는 **SW 디코딩**과 프레임 데이터 복사, 먹싱 비용이 포함되어 있다. 인코딩 연산 자체는 GPU가 수행하므로, 같은 작업을 libx264 같은 SW 인코더로 하면 CPU 시간이 몇 배로 뛴다.

```c
exitStatus = 0;

ffmpeg_release:
av_packet_free(&pEncodedPacket);
av_frame_free(&pDecodedFrame);
av_packet_free(&pInputPacket);
avcodec_free_context(&pEncoderContext);
if (pOutputContext != NULL && pOutputContext->pb != NULL) {
    avio_closep(&pOutputContext->pb);
}
avformat_free_context(pOutputContext);
avcodec_free_context(&pDecoderContext);
avformat_close_input(&pInputContext);
if (exitStatus == 0) {
    printf("HW Encode Done!\r\n");
} else {
    printf("Finished with error(s)...\r\n");
}
return exitStatus;
```

출력 쪽은 입력과 해제 함수가 다르다 — `avio_open()`으로 연 파일 핸들은 `avio_closep()`로, `avformat_alloc_output_context2()`로 만든 컨텍스트는 `avformat_free_context()`로 정리한다(입력의 `avformat_close_input()`과 혼동 주의).

`exitStatus`는 `-1`로 시작해 성공 경로 끝에서만 `0`으로 바뀐다. 에러로 `goto ffmpeg_release`를 타면 `-1`인 채 0이 아닌 종료 코드로 끝나므로 셸/CI에서 실패를 감지할 수 있다. (인코더 자체가 빌드에 없는 경우는 예외적으로 초반에 `return 0`으로 정상 종료한다.)

## 심화: HW 디코딩과 HW 인코딩의 API 비대칭

| 항목 | HW 디코딩 (02) | HW 인코딩 (03) |
|---|---|---|
| 코덱 선택 | 내장 `h264` 디코더 + HW config | 별도 인코더 `h264_videotoolbox` |
| HW 디바이스 연결 | `hw_device_ctx` 필수 | 불필요 (인코더 내부 처리) |
| 포맷 협상 | `get_format` 콜백 필수 | `pix_fmt`에 SW 포맷 지정 |
| 프레임 위치 | GPU (`videotoolbox_vld`) → 필요 시 다운로드 | SW 프레임 입력 → 내부 업로드 |
| 폴백 | 콜백에서 SW 포맷 반환 | 인코더 부재/열기 실패 시 종료 |

디코딩은 "기존 디코더에 HW를 끼워 넣는" 구조라 설정 지점이 많고, 인코딩은 "HW 전용 구현체를 통째로 쓰는" 구조라 오히려 단순하다. 제로카피 트랜스코딩(HW 디코딩 프레임을 다운로드 없이 HW 인코더에 직결)을 하려면 `hw_frames_ctx`까지 다뤄야 하며, 이는 이 부록의 범위를 넘는 다음 단계 주제다.

## ⚠️ 코드 특이점 상세

1. **`EncodeAndMux`의 에러가 호출자에게 전달되지 않음**
   send/receive/write가 실패해도 0(쓴 패킷 수)만 반환하므로 `main`은 실패를 모른 채 루프를 계속 돈다. 학습용으로는 충분하지만, 실전이라면 음수 에러 코드를 반환해 중단시키는 편이 좋다.

2. **오디오 스트림이 버려진다**
   비디오 패킷만 처리하므로 출력 mp4에는 오디오가 없다. murage.mp4의 aac 스트림을 유지하려면 본편 10(리먹싱)처럼 오디오 패킷을 복사하는 두 번째 경로가 필요하다.

3. **`time_base`를 입력 스트림 값으로 쓰는 방식의 한계**
   인코더 time_base로는 보통 `1/framerate`나 프레임레이트의 역수를 권장한다. 입력 스트림 time_base(mp4는 1/15360 등)를 그대로 쓰면 이 예제처럼 pts 재사용이 편하지만, VFR 입력이나 프레임 드롭 상황에서는 duration 계산이 어긋날 수 있다.

4. **주석의 "먹서" 표기**
   `EncodeAndMux` 선언부 주석의 "먹서로 쓴다"는 muxer(먹서)를 뜻한다 — 인코딩된 패킷을 컨테이너에 기록한다는 의미다.
