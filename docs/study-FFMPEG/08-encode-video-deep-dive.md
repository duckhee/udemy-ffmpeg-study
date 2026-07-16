# 08. 비디오 인코딩 — 코드 상세 해설

> [← 기본 문서](08-encode-video.md)

## 전체 구조

| 함수 | 역할 |
|---|---|
| `main()` | 인코더 탐색(폴백 포함) → 파라미터 설정 → open → 프레임 버퍼 준비 → 120장 인코딩 루프 → flush |
| `EncodeAndWrite()` | 프레임(또는 flush 시 NULL)을 send하고 나온 패킷을 모두 파일에 쓴다. 쓴 패킷 수를 반환 |
| `FillSyntheticFrame()` | index번째 합성 프레임(움직이는 그라데이션)을 YUV420P 세 평면에 채운다 |
| `EnsureGeneratedStudyDirectory()` | `resources/GeneratedStudy/` 디렉터리 생성 |
| `GetResourcePath()` | 실행 경로에서 `resources/` 경로를 역산하는 공통 유틸 |

```text
main
 ├─ avcodec_find_encoder_by_name("libx264")  → 없으면 MPEG4 폴백
 ├─ avcodec_alloc_context3
 ├─ 인코더 파라미터 직접 설정 (width/height/pix_fmt/time_base/bit_rate/gop/b-frames)
 ├─ avcodec_open2
 ├─ av_frame_alloc → format/width/height 지정 → av_frame_get_buffer
 ├─ for (frameIdx = 0..119)
 │    ├─ av_frame_make_writable
 │    ├─ FillSyntheticFrame + pts = frameIdx
 │    └─ EncodeAndWrite(frame)
 ├─ EncodeAndWrite(NULL)              ← 인코더 flush
 └─ ffmpeg_release: file/packet/frame/encoder context 해제
```

디코딩 레슨들과 달리 `AVFormatContext`가 없다 — 입력 파일도, 출력 컨테이너도 없이 코덱 레이어만 사용한다.

## 코드 블록별 해설

### 1. 인코더 탐색과 폴백

```c
/**
 * 인코더 찾기.
 * 디코더와 달리 인코더는 이름으로 찾는 경우가 많다
 * (H.264 인코더는 libx264/h264_videotoolbox 등 여러 구현이 있기 때문).
 */
pEncoder = avcodec_find_encoder_by_name("libx264");
if (pEncoder != NULL) {
    outputName = "GeneratedStudy/study-encoded.h264";
} else {
    printf("libx264 not found → fallback to MPEG-4 encoder\r\n");
    pEncoder = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    outputName = "GeneratedStudy/study-encoded.m4v";
}
```

`avcodec_find_encoder_by_name()`은 **빌드에 포함된** 인코더만 찾을 수 있다. 이 저장소의 vcpkg ffmpeg에는 libx264가 빠져 있어 NULL이 반환되고, `avcodec_find_encoder(AV_CODEC_ID_MPEG4)`(FFmpeg 내장 MPEG-4 Part 2 인코더)로 폴백한다. 출력 확장자도 코덱에 맞춰 `.h264`/`.m4v`로 갈라진다 — raw 비트스트림은 확장자가 곧 포맷 힌트이기 때문이다.

### 2. 인코더 파라미터 직접 설정 (핵심)

```c
/**
 * 인코더 설정.
 * 디코딩 때는 파일에서 읽은 파라미터를 복사했지만,
 * 인코딩은 우리가 직접 출력 사양을 정해야 한다.
 */
pEncoderContext->width = ENCODE_WIDTH;
pEncoderContext->height = ENCODE_HEIGHT;
pEncoderContext->pix_fmt = AV_PIX_FMT_YUV420P;
/** time_base = 1/fps → pts 1 증가 = 1프레임 시간 */
pEncoderContext->time_base = (AVRational) {1, ENCODE_FPS};
pEncoderContext->framerate = (AVRational) {ENCODE_FPS, 1};
pEncoderContext->bit_rate = 1000000;
/** GOP: 키프레임 간격. B-프레임도 2장까지 허용 */
pEncoderContext->gop_size = 25;
pEncoderContext->max_b_frames = 2;

/** libx264 전용 옵션은 priv_data에 문자열로 설정한다 */
if (strcmp(pEncoder->name, "libx264") == 0) {
    av_opt_set(pEncoderContext->priv_data, "preset", "fast", 0);
}
```

- `time_base = {1, 25}`로 두면 pts의 단위가 1/25초가 되어 **프레임 번호를 그대로 pts로 쓸 수 있다**. 인코딩에서 time_base 설계는 타임스탬프 계산 전체를 좌우하는 가장 중요한 결정이다.
- `gop_size = 25`: 25프레임(1초)마다 I-프레임을 넣는다. `max_b_frames = 2`: P-프레임 사이에 B-프레임을 최대 2장 허용 — 압축률은 오르지만 인코더 지연이 생긴다.
- 공통 필드에 없는 코덱 전용 옵션(libx264의 `preset` 등)은 `AVCodecContext->priv_data`에 `av_opt_set()`으로 문자열 키/값을 넣는다. 인코더 이름을 확인하고 설정하므로 MPEG-4 폴백 시에는 건너뛴다.

### 3. 인코딩용 프레임 버퍼 할당

```c
/**
 * 인코딩용 프레임은 직접 픽셀 버퍼를 할당해야 한다.
 * (디코딩 때는 디코더가 버퍼를 채워줬다)
 */
pFrame->format = AV_PIX_FMT_YUV420P;
pFrame->width = ENCODE_WIDTH;
pFrame->height = ENCODE_HEIGHT;
errorCode = av_frame_get_buffer(pFrame, 0);
```

`av_frame_alloc()`은 구조체만 만들 뿐 픽셀 버퍼는 비어 있다. `format`/`width`/`height`를 먼저 지정한 뒤 `av_frame_get_buffer()`를 부르면 그 사양에 맞는 참조 카운트 버퍼가 `data[]`/`linesize[]`에 채워진다. 두 번째 인자 0은 "정렬은 알아서"라는 뜻이다.

### 4. 인코딩 루프 — make_writable과 pts

```c
/** 합성 프레임 생성 → 인코딩 루프 */
for (int frameIdx = 0; frameIdx < ENCODE_FRAME_COUNT; ++frameIdx) {
    /**
     * 인코더가 아직 이전 프레임 버퍼를 참조 중일 수 있으므로
     * 쓰기 전에 반드시 writable 상태로 만든다 (필요 시 내부 복사 발생).
     */
    errorCode = av_frame_make_writable(pFrame);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Make Frame Writable...\r\n", errorCode);
        break;
    }

    FillSyntheticFrame(pFrame, frameIdx);
    /** pts는 time_base(1/25초) 단위 → 프레임 번호가 곧 pts */
    pFrame->pts = frameIdx;

    writtenPacketCount += EncodeAndWrite(pEncoderContext, pFrame, pPacket, pOutputFile);
}
```

`avcodec_send_frame()`은 프레임 버퍼의 **참조**를 가져간다. B-프레임 재정렬 때문에 인코더는 이전 프레임을 한동안 붙잡고 있는데, 그 상태에서 같은 버퍼에 다음 그림을 덮어쓰면 이미 제출한 프레임이 오염된다. `av_frame_make_writable()`은 버퍼가 공유 중(refcount > 1)일 때만 새 버퍼로 복사(copy-on-write)해 이 문제를 막는다. 하나의 `AVFrame`을 120번 재사용할 수 있는 것도 이 덕분이다.

### 5. 합성 프레임 생성 — stride 기반 YUV420P 쓰기

```c
void FillSyntheticFrame(AVFrame *pFrame, int index) {
    /** Y 평면: 대각선 방향으로 흐르는 그라데이션 */
    for (int y = 0; y < pFrame->height; ++y) {
        for (int x = 0; x < pFrame->width; ++x) {
            pFrame->data[0][y * pFrame->linesize[0] + x] = (uint8_t) (x + y + index * 3);
        }
    }
    /** Cb/Cr 평면: YUV420P는 색차 평면이 가로/세로 절반 크기다 */
    for (int y = 0; y < pFrame->height / 2; ++y) {
        for (int x = 0; x < pFrame->width / 2; ++x) {
            pFrame->data[1][y * pFrame->linesize[1] + x] = (uint8_t) (128 + y + index * 2);
            pFrame->data[2][y * pFrame->linesize[2] + x] = (uint8_t) (64 + x + index * 5);
        }
    }
}
```

- 06에서 읽기로 배운 stride 규칙을 이번에는 **쓰기**에 적용한다: 픽셀 (x, y)의 주소는 `data[평면][y * linesize[평면] + x]`다. `y * width + x`로 쓰면 stride 패딩이 있는 환경에서 그림이 어긋난다.
- YUV420P의 색차 평면(`data[1]`=Cb, `data[2]`=Cr)은 가로·세로 각각 절반이므로 루프 범위도 `/2`다.
- `index`가 수식에 곱해져 있어 프레임마다 그라데이션이 흘러가는 애니메이션이 된다. 프레임 간 차이가 규칙적이라 인터프레임 압축(P/B-프레임)의 효과를 관찰하기 좋은 입력이다.

### 6. EncodeAndWrite — send_frame / receive_packet (핵심)

```c
int EncodeAndWrite(AVCodecContext *pCodecContext, AVFrame *pFrame, AVPacket *pPacket, FILE *pOutputFile) {
    int writtenCount = 0;
    int errorCode = 0;

    /** NULL 프레임 = "입력 끝" 신호 (flush 시작) */
    errorCode = avcodec_send_frame(pCodecContext, pFrame);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Sending frame to encoder\r\n", errorCode);
        return 0;
    }

    while (errorCode >= 0) {
        errorCode = avcodec_receive_packet(pCodecContext, pPacket);
        /**
         * EAGAIN: 패킷을 내놓으려면 프레임 입력이 더 필요함
         * (B-프레임/lookahead 때문에 인코더도 지연이 있다)
         */
        if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
            break;
        } else if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Receive packet\r\n", errorCode);
            break;
        }

        /** raw 비트스트림이므로 패킷 데이터를 그대로 이어 쓰면 된다 */
        fwrite(pPacket->data, 1, pPacket->size, pOutputFile);
        writtenCount++;
        av_packet_unref(pPacket);
    }

    return writtenCount;
}
```

디코딩의 `DecodeVideoPacket`(chapter02-09)과 나란히 놓고 보면 send/receive의 방향만 바뀐 대칭 구조임이 보인다.

- 초반 몇 프레임은 send해도 `EAGAIN`만 나온다 — B-프레임 결정과 lookahead를 위해 인코더가 입력을 모으는 구간이다. 이 지연 때문에 "프레임 1개 넣으면 패킷 1개 나온다"고 가정하면 안 되고 receive를 항상 루프로 돈다.
- 출력이 raw 비트스트림이라 패킷 데이터를 그대로 이어 붙이면 재생 가능한 파일이 된다. 컨테이너(mp4)에 담으려면 `av_interleaved_write_frame()` 등 muxing 단계가 필요하다(11번 레슨).
- 사용한 패킷은 `av_packet_unref()`로 해제해 다음 receive에 재사용한다.

### 7. flush와 마무리

```c
/** 인코더 flush: NULL 프레임을 보내 내부에 남은 패킷을 모두 꺼낸다 */
writtenPacketCount += EncodeAndWrite(pEncoderContext, NULL, pPacket, pOutputFile);

printf("encoded frames : %d → written packets : %d\r\n", ENCODE_FRAME_COUNT, writtenPacketCount);
...
exitStatus = 0;

ffmpeg_release:
if (pOutputFile != NULL) {
    fclose(pOutputFile);
}
av_packet_free(&pPacket);
av_frame_free(&pFrame);
avcodec_free_context(&pEncoderContext);
if (exitStatus == 0) {
    printf("Encode Video Done!\r\n");
} else {
    printf("Finished with error(s)...\r\n");
}
return exitStatus;
```

`avcodec_send_frame(ctx, NULL)`이 "입력 끝" 신호다. 인코더는 붙잡고 있던 프레임들을 모두 인코딩해 내보낸 뒤 `AVERROR_EOF`를 반환한다. flush까지 마치면 **120프레임 → 120패킷**으로 입력과 출력 개수가 일치한다. flush를 빼먹으면 B-프레임/lookahead 지연만큼 마지막 패킷들이 유실되어 영상 끝이 잘린다.

해제는 file → packet → frame → encoder context 순이며, 입력이 없으므로 `avformat_close_input()`은 등장하지 않는다.

`exitStatus`는 `-1`로 시작해 **성공 경로의 끝에서만** `0`으로 바뀐다. 중간에 에러로 `goto ffmpeg_release`를 타면 `-1` 그대로 남아 프로그램이 0이 아닌 종료 코드로 끝나므로, 셸/CI에서 실패를 감지할 수 있다.

## 심화: 인코더 지연을 숫자로 관찰하기

콘솔 출력 없이도 확인하는 방법: `EncodeAndWrite()`의 반환값을 프레임 인덱스와 함께 찍어 보면, 첫 몇 프레임은 0을 반환하다가 어느 순간부터 1씩 나오고, 마지막 flush 호출이 밀린 패킷 여러 개를 한꺼번에 반환한다. `max_b_frames`를 0으로 바꾸면 지연이 거의 사라지고, 반대로 키우면 flush가 뱉는 패킷 수가 늘어난다 — 인코더 지연이 B-프레임 설정의 함수임을 실험으로 확인할 수 있다.
