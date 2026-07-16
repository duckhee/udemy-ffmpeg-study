# 06. swscale: 픽셀 포맷 변환과 스케일링 — 코드 상세 해설

> [← 기본 문서](06-scaling-video.md)

## 전체 구조

| 함수 | 역할 |
|---|---|
| `main()` | 파일 열기 → 비디오 디코더 준비 → SwsContext/RGB 버퍼 준비 → 디코딩 루프에서 앞 5장을 변환·저장 |
| `SavePPMImage()` | RGB24 버퍼를 stride를 고려해 PPM(P6) 파일로 저장 |
| `EnsureGeneratedStudyDirectory()` | `resources/GeneratedStudy/` 디렉터리 생성 (이미 있으면 무시) |
| `GetResourcePath()` | 실행 경로에서 `resources/` 경로를 역산하는 공통 유틸 |

```text
main
 ├─ GetResourcePath / EnsureGeneratedStudyDirectory
 ├─ avformat_open_input / find_stream_info
 ├─ av_find_best_stream(VIDEO) → codec context 3단계 준비
 ├─ sws_getContext(1280x720 yuv420p → 640x360 rgb24, SWS_BILINEAR)
 ├─ av_image_alloc(rgbData, rgbLineSize, 640, 360, RGB24, 1)
 ├─ while (5장 미만 && av_read_frame >= 0)
 │    └─ send_packet → receive_frame 루프
 │         ├─ 프레임 사양 검사 (해상도/포맷이 코덱 컨텍스트와 다르면 스킵)
 │         ├─ sws_scale(입력 data/linesize → rgbData/rgbLineSize)
 │         └─ SavePPMImage("study-scaled-NNN.ppm")
 └─ ffmpeg_release: frame/packet/RGB버퍼/sws/codec/format 해제
```

## 코드 블록별 해설

### 1. 변환 관련 변수 선언

```c
struct SwsContext *pSwsContext = NULL;
/** 변환 결과(RGB24)를 담을 버퍼: 평면 포인터 4개 + 각 평면의 stride */
uint8_t *rgbData[4] = {NULL};
int rgbLineSize[4] = {0};
```

`AVFrame`의 `data[8]`/`linesize[8]`처럼, FFmpeg의 이미지 유틸은 "평면 포인터 배열 + stride 배열" 쌍으로 픽셀 버퍼를 표현한다. YUV420P라면 3개 평면(Y/U/V)이 쓰이지만 RGB24는 단일 평면이라 `rgbData[0]`만 실제로 사용된다.

### 2. SwsContext 생성

```c
/**
 * SwsContext 생성.
 * (입력 크기/포맷, 출력 크기/포맷, 보간 알고리즘)을 지정한다.
 * SWS_BILINEAR: 속도/품질 균형이 좋은 기본 선택지.
 */
pSwsContext = sws_getContext(
        pVideoCodecContext->width, pVideoCodecContext->height, pVideoCodecContext->pix_fmt,
        OUTPUT_WIDTH, OUTPUT_HEIGHT, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, NULL, NULL, NULL);
```

입력 사양은 하드코딩하지 않고 열린 디코더 컨텍스트에서 읽는다(`width`/`height`/`pix_fmt`). 입력과 출력의 크기·포맷이 모두 다르므로 이 컨텍스트 하나가 "1280x720 YUV420P → 640x360 RGB24" 변환 전체를 담당한다. 마지막 세 NULL은 입력/출력 필터와 알고리즘 세부 파라미터로, 보통 쓸 일이 없다. 변환 사양이 고정이므로 컨텍스트는 **루프 밖에서 한 번만** 만든다.

### 3. 출력 RGB 버퍼 할당 — av_image_alloc

```c
/**
 * 출력 RGB 버퍼 할당.
 * av_image_alloc이 포맷에 맞는 평면 포인터와 stride(lineSize)를 채워준다.
 * RGB24는 단일 평면이므로 rgbData[0]만 사용된다.
 */
errorCode = av_image_alloc(rgbData, rgbLineSize, OUTPUT_WIDTH, OUTPUT_HEIGHT, AV_PIX_FMT_RGB24, 1);
```

`av_image_alloc()`은 지정한 크기·포맷에 필요한 총 바이트를 계산해 하나의 연속 버퍼로 할당하고, 평면 포인터와 stride를 채운다. 마지막 인자 `1`은 정렬(align) 값이다 — `1`이면 stride가 정확히 `width × 3`이 되고, `32` 같은 값을 주면 SIMD 최적화를 위해 stride가 더 커질 수 있다. 반환값은 할당된 총 바이트 수(음수면 실패)다.

### 4. 디코딩 + 변환 루프

```c
/** 디코딩하며 앞에서부터 SAVE_FRAME_MAX 장을 PPM으로 저장 */
while (savedFrameCount < SAVE_FRAME_MAX && av_read_frame(pFormatContext, pPacket) >= 0) {
    if (pPacket->stream_index == videoStreamIdx) {
        errorCode = avcodec_send_packet(pVideoCodecContext, pPacket);
        ...
        while (savedFrameCount < SAVE_FRAME_MAX) {
            errorCode = avcodec_receive_frame(pVideoCodecContext, pFrame);
            if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
                break;
            }
            ...
```

바깥 while 조건에 `savedFrameCount < SAVE_FRAME_MAX`가 들어 있어 5장을 저장하는 즉시 파일 읽기를 중단한다. 필요한 만큼만 디코딩하므로 12.78초짜리 전체 영상을 다 읽지 않는다. receive 루프에도 같은 조건이 걸려 있어 한 패킷에서 여러 프레임이 나와도 초과 저장하지 않는다.

### 5. sws_scale — 실제 변환 (핵심)

```c
/**
 * SwsContext는 코덱 컨텍스트의 초기 사양으로 만들었다.
 * 스트림 중간에 해상도/포맷이 바뀔 수 있으므로(H.264에서 합법)
 * 실제 프레임 사양이 다르면 잘못된 메모리 접근을 피하기 위해 건너뛴다.
 */
if (pFrame->width != pVideoCodecContext->width ||
    pFrame->height != pVideoCodecContext->height ||
    pFrame->format != pVideoCodecContext->pix_fmt) {
    printf("frame spec changed (%dx%d) — skip\r\n", pFrame->width, pFrame->height);
    av_frame_unref(pFrame);
    continue;
}

/**
 * 실제 변환 수행.
 * 입력 프레임의 평면(data)과 stride(linesize)를 주면
 * 출력 버퍼에 변환/스케일된 픽셀이 채워진다.
 * 세 번째/네 번째 인자(0, height)는 처리할 입력의 세로 범위다.
 */
sws_scale(pSwsContext,
          (const uint8_t *const *) pFrame->data, pFrame->linesize,
          0, pFrame->height,
          rgbData, rgbLineSize);
```

- **변환 전 프레임 사양 검사**: SwsContext는 루프 밖에서 코덱 컨텍스트의 초기 사양(1280x720 yuv420p)으로 만들어졌다. 그런데 H.264는 스트림 중간에 해상도/픽셀 포맷이 바뀌는 것이 합법이라, 실제 프레임의 `width`/`height`/`format`이 컨텍스트와 다르면 그대로 `sws_scale()`에 넘겼을 때 잘못된 메모리 접근이 일어날 수 있다. 그래서 사양이 다른 프레임은 unref 후 건너뛴다.
- 입력은 디코딩된 프레임의 `data`(Y/U/V 세 평면 포인터)와 `linesize`(각 평면의 stride)다. 디코더가 채워준 값을 그대로 넘기면 된다.
- `0, pFrame->height`는 "입력의 0번째 줄부터 height줄 처리" — 슬라이스 단위 처리를 지원하기 위한 인자지만 보통 전체를 한 번에 넘긴다. 세로 범위는 실제로 처리하는 **프레임 자신의 높이**(`pFrame->height`)를 넘긴다(사양 검사를 통과했으므로 컨텍스트 높이와 같지만, 입력의 출처를 프레임으로 일관시키는 편이 안전하다).
- 출력 쪽도 같은 형태(평면 포인터 배열 + stride 배열)로 받는다. 호출이 끝나면 `rgbData[0]`에 640x360 RGB24 픽셀이 채워져 있다.

### 6. PPM 저장 — stride를 고려한 한 줄씩 쓰기

```c
bool SavePPMImage(const char *fileName, const uint8_t *rgbData, int lineSize, int width, int height) {
    FILE *pFile = fopen(fileName, "wb");
    ...
    /** PPM(P6) 헤더: 매직넘버, 가로 세로, 최대 색상값 */
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    /**
     * stride(lineSize)는 정렬 때문에 width*3보다 클 수 있으므로
     * 한 줄씩 width*3 바이트만 잘라서 쓴다.
     */
    for (int y = 0; y < height; ++y) {
        fwrite(rgbData + (ptrdiff_t) y * lineSize, 1, (size_t) width * 3, pFile);
    }

    fclose(pFile);
    return true;
}
```

버퍼 전체를 한 번에 `fwrite`하지 않고 **한 줄씩 `width * 3` 바이트만** 쓰는 것이 포인트다. 이 레슨은 align=1이라 `lineSize == width*3`이지만, align을 키우거나 다른 환경에서 stride에 패딩이 붙으면 통째로 쓴 파일은 깨진 이미지가 된다. stride 기반 순회는 FFmpeg 픽셀 처리의 기본 습관이다. 결과 파일 크기는 헤더 15바이트 + `640×360×3 = 691,200`바이트 = 691,215바이트다.

### 7. 자원 해제

```c
exitStatus = 0;

ffmpeg_release:
av_frame_free(&pFrame);
av_packet_free(&pPacket);
/** av_image_alloc으로 받은 버퍼는 av_freep(첫 평면 포인터)로 해제 */
av_freep(&rgbData[0]);
sws_freeContext(pSwsContext);
avcodec_free_context(&pVideoCodecContext);
avformat_close_input(&pFormatContext);
if (exitStatus == 0) {
    printf("Scaling Video Done!\r\n");
} else {
    printf("Finished with error(s)...\r\n");
}
return exitStatus;
```

`av_image_alloc()`은 여러 평면을 **하나의 연속 버퍼**로 할당하므로 첫 평면 포인터(`rgbData[0]`) 하나만 `av_freep()`하면 전체가 해제된다. `sws_freeContext()`는 NULL을 받아도 안전하므로 goto 경로에서도 문제없다.

`exitStatus`는 -1로 선언되어 성공 경로 맨 끝(`ffmpeg_release:` 직전)에서만 0이 된다. 에러로 `goto ffmpeg_release`한 경우 -1인 채로 반환되어 **실패 시 0이 아닌 종료 코드로 끝나므로 셸/CI에서 실패를 감지할 수 있고**, 마지막 메시지도 성공/실패로 분기된다.

## 심화: YUV420P가 3평면인 이유

YUV420P의 P는 planar를 뜻한다. `data[0]`에 Y(밝기) 평면이 원본 해상도로, `data[1]`/`data[2]`에 Cb/Cr(색차) 평면이 가로·세로 각각 절반 해상도로 저장된다. 픽셀당 평균 12비트로 RGB24(24비트)의 절반이다. `sws_scale()`은 이 세 평면을 읽어 색공간 변환 행렬을 적용하고 색차를 업샘플링해 픽셀당 3바이트 RGB로 재조립한다 — 오디오의 planar/interleaved 구분(05)과 정확히 같은 개념이 비디오에도 있는 셈이다.
