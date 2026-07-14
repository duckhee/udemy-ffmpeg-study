# 12. SwsContext 설정 — 코드 상세 해설

> [← 기본 문서](12-swscale-setting.md)

## 전체 구조

11번 코드에 SwsContext의 생성/해제만 더해진 최소 변경이다.

```text
main
 ├─ (11과 동일) 열기 → 스트림 탐색(idx 초기값 -1) → 코덱 컨텍스트 준비
 ├─ pSwsContext = sws_getContext(...)      ← 신규 (핵심)
 ├─ RGB 버퍼 계산·할당·fill_arrays·즉시 해제 (11과 동일)
 ├─ while (av_read_frame) → DecodeVideoPacket_GreyFrame
 └─ ffmpeg_release: sws_freeContext(pSwsContext) ← 신규
```

## 코드 블록별 해설

### 1. 스트림 인덱스 초기화 수정

```c
int videoStreamIdx = -1;
...
int audioStreamIdx = -1;
```

09~11에서 `0`으로 초기화되어 무의미했던 `if (videoStreamIdx < 0)` / `if (audioStreamIdx < 0)` 검사가 이번 레슨부터 실제로 동작한다. "유효하지 않은 인덱스"의 관용적 표현인 `-1`을 쓰는 것이 옳다는 것을 보여주는 수정이다.

### 2. SwsContext 생성 (핵심)

```c
/** frame image software scale structure */
struct SwsContext *pSwsContext = NULL;
```

```c
/** soft ware scale context get */
pSwsContext = sws_getContext(pVideoCodecContext->width, pVideoCodecContext->height, pVideoCodecContext->pix_fmt,
                             pVideoCodecContext->width,
                             pVideoCodecContext->height, AV_PIX_FMT_RGB24, SWS_BILINEAR,
                             NULL, NULL, NULL);
```

인자를 순서대로 해석하면 다음과 같다.

| 인자 | 값 | 의미 |
|---|---|---|
| srcW, srcH | `pVideoCodecContext->width/height` | 원본(디코딩 프레임) 해상도 |
| srcFormat | `pVideoCodecContext->pix_fmt` | 원본 픽셀 포맷(H.264라면 보통 `yuv420p`) |
| dstW, dstH | 원본과 동일 | 해상도 변경 없음 — 포맷 변환만 |
| dstFormat | `AV_PIX_FMT_RGB24` | 대상 포맷: packed RGB 3바이트/픽셀 |
| flags | `SWS_BILINEAR` | 스케일링 보간 알고리즘 |
| srcFilter, dstFilter, param | `NULL, NULL, NULL` | 고급 필터 옵션 미사용 |

`SwsContext`는 변환 테이블·중간 버퍼 등을 미리 계산해 들고 있는 비싼 객체이므로, **프레임마다 만들지 말고 한 번 만들어 재사용**하는 것이 원칙이다. 이 코드도 루프 밖에서 한 번만 생성한다.

### 3. 해제

```c
ffmpeg_release:
/** software scale context remove */
sws_freeContext(pSwsContext);
/** release resource */
av_frame_free(&pFrame);
av_frame_free(&pRGBFrame);
av_packet_free(&pPacket);

avcodec_free_context(&pVideoCodecContext);
avcodec_free_context(&pAudioCodecContext);

avformat_close_input(&pFormatContext);
```

`sws_freeContext()`는 NULL을 안전하게 무시하므로 goto로 건너뛴 경우에도 문제없다.

## 심화: swscale 플래그 선택 기준

`sws_getContext`의 `flags`는 해상도를 바꿀 때의 보간 품질을 결정한다. 이 레슨처럼 해상도가 동일한 순수 포맷 변환에서는 체감 차이가 거의 없지만, 알아 두면 좋은 대표 플래그는 다음과 같다.

| 플래그 | 특징 |
|---|---|
| `SWS_FAST_BILINEAR` | 가장 빠름, 품질 낮음. 실시간 미리보기용 |
| `SWS_BILINEAR` | 속도/품질 균형. 일반적 기본값 |
| `SWS_BICUBIC` | 더 부드러운 확대 품질, 다소 느림 |
| `SWS_LANCZOS` | 고품질 다운/업스케일, 가장 느린 축 |
| `SWS_POINT` | 최근접 픽셀. 픽셀아트 확대 등 특수 용도 |

또 하나 기억할 점은 YUV→RGB 변환이 단순 재배열이 아니라 **색공간 행렬 연산 + 크로마 업샘플링**이라는 것이다. yuv420p의 U/V 평면은 해상도가 1/4이므로, RGB 픽셀 하나를 만들려면 색차 값을 보간해 끌어올린 뒤 변환 행렬(BT.601/BT.709)을 적용해야 한다. swscale은 이를 SIMD로 최적화해 처리해 준다.

## ⚠️ 코드 특이점 상세

1. **`sws_scale()` 미호출 — 의도된 단계적 구성**
   SwsContext를 만들어 놓고 디코딩 루프에서는 사용하지 않는다. 실행 결과는 11과 동일하게 그레이 PPM뿐이다. 이는 실수가 아니라 "12에서 변환기를 준비하고 13에서 돌린다"는 강의의 점진적 구성이므로, 13번 레슨에서 `DecodeVideoPacket_RGBFrame()`이 이 컨텍스트로 `sws_scale()`을 호출하는 것을 확인하면 된다.

2. **`sws_getContext()` NULL 검사 부재**
   지원하지 않는 포맷 조합이거나 해상도가 0이면 NULL이 반환된다. 이 값을 검사 없이 13에서 `sws_scale()`에 넘기면 크래시한다. 올바른 형태는 생성 직후 `if (pSwsContext == NULL) { ... goto ffmpeg_release; }`다.

3. **11의 RGB 버퍼 특이점 유지**
   `av_image_get_buffer_size(AV_PIX_FMT_RGB24, ...)`로 계산하고 `av_image_fill_arrays(..., AV_PIX_FMT_RGB4, ...)`로 연결하는 불일치, 그리고 fill 직후의 `av_frame_unref(pRGBFrame); av_free(pRGBFrameBuffer);` 즉시 해제가 그대로 남아 있다. 상세한 분석은 [11 딥다이브](11-color-image-deep-dive.md#-코드-특이점-상세) 참고.

4. **상속된 특이점**: 그레이 PPM 단일 파일 덮어쓰기(마지막 프레임만 남음), 텍스트 모드 `"w"` 쓰기, `pCurStream[idx].r_frame_rate` 이중 인덱싱, 디코더 flush 누락, `main`의 `return` 누락.
