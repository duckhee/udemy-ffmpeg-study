# 11. 컬러 이미지를 위한 RGB 버퍼 준비 — 코드 상세 해설

> [← 기본 문서](11-color-image.md)

## 전체 구조

10번 코드에 RGB 프레임 준비 블록이 삽입되고, 자원 정리가 `goto ffmpeg_release` 중심으로 재편되었다.

```text
main
 ├─ 열기 → 스트림 탐색 → 코덱 컨텍스트 준비 (10과 동일, 에러 시 goto로 통일)
 ├─ pRGBFrame = av_frame_alloc()                    ← 신규
 ├─ RGB 버퍼 크기 계산 → av_malloc → fill_arrays    ← 신규 (핵심)
 ├─ (버퍼 즉시 해제 — 특이점)
 ├─ while (av_read_frame) → DecodeVideoPacket_GreyFrame  (10과 동일)
 └─ ffmpeg_release: avcodec_free_context 추가       ← 신규
```

## 코드 블록별 해설

### 1. 세 번째 프레임: pRGBFrame

```c
/** RGB Frame Structure memory load */
pRGBFrame = av_frame_alloc();
if (pRGBFrame == NULL) {
    av_log(NULL, AV_LOG_ERROR, "Failed Load frame structure...\r\n");
    goto ffmpeg_release;
}
```

`av_frame_alloc()`은 구조체 껍데기만 만든다. 디코더가 채워 주는 `pFrame`과 달리, 변환 출력용 프레임은 픽셀 버퍼를 프로그래머가 직접 마련해 연결해야 한다.

### 2. 버퍼 크기 계산과 할당 (핵심)

```c
/** rgb channel data type defined -> get image size */
int rgbFrameNumberOfByte = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pVideoCodecContext->width,
                                                    pVideoCodecContext->height, 1);
/** get RGB Frame Buffer -> allocated image size buffer */
uint8_t *pRGBFrameBuffer = av_malloc(rgbFrameNumberOfByte);
```

- `AV_PIX_FMT_RGB24`: 픽셀당 R/G/B 각 1바이트, 총 3바이트인 packed 포맷. 정렬(align) 1 기준으로 크기는 `width * height * 3`이다.
- `av_malloc()`은 SIMD 연산에 필요한 정렬을 보장하는 FFmpeg 전용 할당자다. `malloc` 대신 이것을 써야 이후 swscale 같은 라이브러리가 안전하게 접근한다.

### 3. 버퍼를 프레임에 연결

```c
/** frame buffer set image pixel */
errorCode = av_image_fill_arrays(pRGBFrame->data, pRGBFrame->linesize, (uint8_t *) pRGBFrameBuffer, AV_PIX_FMT_RGB4,
                                 pVideoCodecContext->width, pVideoCodecContext->height, 1);
```

`av_image_fill_arrays()`는 연속 버퍼를 받아 픽셀 포맷 규칙에 맞게 `data[0..3]` 포인터와 `linesize[0..3]` 값을 계산해 넣는다. RGB 같은 packed 1평면 포맷이면 `data[0] = buf`, `linesize[0] = 픽셀당 바이트 × width`가 된다. **여기서 포맷이 `AV_PIX_FMT_RGB4`로 잘못 들어간 것이 이 레슨의 대표 특이점이다**(아래 상세).

### 4. 즉시 해제와 크기 설정

```c
av_frame_unref(pRGBFrame);
av_free(pRGBFrameBuffer);
if (errorCode < 0) {
    av_log(NULL, AV_LOG_ERROR, "[FFMPEG_ERROR](%d) RGB Image Copy Buffer Failed...\r\n", errorCode);
}

/** setting rgb frame size */
pRGBFrame->width = pVideoCodecContext->width;
pRGBFrame->height = pVideoCodecContext->height;
```

연결이 끝나자마자 프레임 참조를 풀고 버퍼를 해제한다. 이 레슨에서는 pRGBFrame이 이후 사용되지 않아서 눈에 보이는 문제는 없지만, 만약 사용했다면 해제된 메모리를 가리키는 `data[0]`에 접근하는 use-after-free가 된다. 13번 레슨에서는 이 두 줄이 주석 처리되어 버퍼가 유지된다.

### 5. 통일된 자원 해제

```c
ffmpeg_release:
/** release resource */
av_frame_free(&pFrame);
av_frame_free(&pRGBFrame);

avcodec_free_context(&pVideoCodecContext);
avcodec_free_context(&pAudioCodecContext);

av_packet_free(&pPacket);
avformat_close_input(&pFormatContext);
```

09~10에서 빠져 있던 `avcodec_free_context()`가 추가되어 코덱 컨텍스트 누수가 해결되었다. `av_frame_free`/`av_packet_free`/`avcodec_free_context`는 모두 NULL 포인터를 안전하게 무시하므로, 초기화 순서에 상관없이 하나의 정리 블록으로 모을 수 있다 — 이것이 `goto` 단일 출구 패턴의 장점이다.

## 심화: av_image_fill_arrays가 하는 일

RGB24, 폭 4, 높이 2 이미지라면 버퍼와 프레임의 관계는 다음과 같다.

```text
pRGBFrameBuffer (연속 24바이트)
┌─────────────────────────────────────────────┐
│ R G B  R G B  R G B  R G B │ R G B  R G B ... │
└─────────────────────────────────────────────┘
  ▲ data[0] = buf,  linesize[0] = 4 * 3 = 12

data[1..3] = NULL, linesize[1..3] = 0  (packed 포맷은 평면이 하나)
```

yuv420p 같은 planar 포맷이었다면 같은 함수가 버퍼를 세 구간으로 나눠 `data[0]`(Y), `data[1]`(U), `data[2]`(V)에 배치했을 것이다. 즉 이 함수는 "포맷별 메모리 레이아웃 계산기"이며, 복사는 일어나지 않는다.

## ⚠️ 코드 특이점 상세

1. **크기 계산은 RGB24, 연결은 RGB4 — 픽셀 포맷 불일치**
   `av_image_get_buffer_size(AV_PIX_FMT_RGB24, ...)`로 `w*h*3` 바이트를 할당했지만 `av_image_fill_arrays(..., AV_PIX_FMT_RGB4, ...)`로 연결했다. `AV_PIX_FMT_RGB4`는 픽셀당 4bit(1바이트에 2픽셀) 포맷이라 linesize가 RGB24의 1/6 수준으로 계산된다. 버퍼 크기가 남아돌아 오버플로는 없지만, 이 프레임을 RGB24 출력 대상으로 썼다면 stride가 틀려 이미지가 완전히 깨졌을 것이다. 올바른 형태는 두 호출 모두 `AV_PIX_FMT_RGB24`를 쓰는 것이며, 13번 레슨에서는 `AV_PIX_FMT_BGR24`로 바뀐다(그것도 완전한 수정은 아니다 — 13 딥다이브 참고).

2. **fill 직후 `av_frame_unref` + `av_free` — 준비한 버퍼를 바로 파괴**
   `pRGBFrame->data`가 해제된 메모리를 가리키는 상태로 남는다. 이 레슨에서는 pRGBFrame을 쓰지 않으므로 무해하지만, "버퍼를 연결했으면 사용이 끝날 때까지 유지해야 한다"는 원칙에 어긋난다. 또한 `av_frame_unref()`는 fill_arrays로 수동 연결한 포인터를 지워 버리므로(dst 배열이 초기화됨) unref 후의 프레임은 어차피 빈 껍데기다.

3. **에러 검사가 해제 뒤에 위치**
   `errorCode`(fill_arrays 반환값) 검사가 `av_free` 다음에 있다. 실패했더라도 이미 해제가 끝난 뒤라 취할 수 있는 조치가 없다. 검사 → 사용 → 해제 순서가 올바르다.

4. **`main`의 `return` 누락**
   `avformat_close_input(&pFormatContext);`로 함수가 끝나고 `return`이 없다. C99 이후 `main`은 특례로 0을 반환하지만 명시적 `return 0;`이 바람직하다.

5. **상속된 특이점**: `videoStreamIdx`/`audioStreamIdx`의 `0` 초기화(12에서 수정), 그레이 PPM 덮어쓰기, 텍스트 모드 `"w"`, `pCurStream[idx]` 이중 인덱싱, 디코더 flush 누락은 그대로다.
