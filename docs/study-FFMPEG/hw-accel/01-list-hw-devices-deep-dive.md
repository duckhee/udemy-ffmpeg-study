# 01. 하드웨어 디바이스 열거 — 코드 상세 해설

> [← 기본 문서](01-list-hw-devices.md)

## 전체 구조

`main()` 하나로 구성된 짧은 조사 프로그램이다. 세 단계가 차례로 실행된다.

| 단계 | 내용 | 핵심 API |
|---|---|---|
| 1 | 빌드에 포함된 HW 디바이스 타입 열거 | `av_hwdevice_iterate_types` |
| 2 | VideoToolbox 디바이스 컨텍스트 생성 (macOS 전용) | `av_hwdevice_ctx_create` |
| 3 | H.264 디코더의 HW 설정(AVCodecHWConfig) 순회 | `avcodec_get_hw_config` |

2~3단계는 `#if defined(__APPLE__)`로 감싸여 있어 다른 OS에서는 건너뛴다.

## 코드 블록별 해설

### 1. 지원 HW 디바이스 타입 열거

```c
enum AVHWDeviceType deviceType = AV_HWDEVICE_TYPE_NONE;
...
printf("==== Available HW Device Types ====\r\n");
while ((deviceType = av_hwdevice_iterate_types(deviceType)) != AV_HWDEVICE_TYPE_NONE) {
    printf("- %s\r\n", av_hwdevice_get_type_name(deviceType));
}
```

`av_hwdevice_iterate_types()`는 "직전 타입"을 받아 "다음 타입"을 돌려주는 이터레이터다. `AV_HWDEVICE_TYPE_NONE`부터 시작해 다시 NONE이 나올 때까지 돌리면 이 빌드에 컴파일된 모든 타입을 얻는다. 본편 01 레슨의 메타데이터 순회(`av_dict_get` 이터레이터 패턴)와 같은 관용구다. macOS arm64 vcpkg 빌드에서는 `videotoolbox` 하나만 출력된다 — HW 가속 지원은 런타임이 아니라 **FFmpeg 빌드 구성**에서 먼저 결정된다는 것을 보여준다.

### 2. VideoToolbox 디바이스 컨텍스트 생성

```c
#if defined(__APPLE__)
    printf("\r\n==== Create VideoToolbox Device ====\r\n");
    errorCode = av_hwdevice_ctx_create(&pHwDeviceContext, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, NULL, NULL, 0);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Create VideoToolbox Device...\r\n", errorCode);
        return -1;
    }
    printf("VideoToolbox device created successfully!\r\n");
```

`av_hwdevice_ctx_create(&ref, type, device, opts, flags)`의 인자를 보자.

- `device`(세 번째): CUDA라면 `"0"`(GPU 번호), VAAPI라면 `"/dev/dri/renderD128"` 같은 경로를 주지만, VideoToolbox는 시스템에 하나뿐이라 **NULL로 충분하다**.
- 반환된 `pHwDeviceContext`는 `AVBufferRef *`다. 내부의 `AVHWDeviceContext`가 참조 카운트로 관리되므로, 이후 여러 코덱 컨텍스트가 `av_buffer_ref()`로 같은 디바이스를 공유할 수 있다(02 레슨에서 사용).

1단계에서 타입이 열거되더라도 실제 하드웨어 초기화는 여기서 일어나므로, 이 호출의 성공이 곧 "이 머신에서 HW 가속 사용 가능"의 확정이다.

### 3. H.264 디코더의 HW 설정 순회

```c
    pDecoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    ...
    printf("\r\n==== H.264 Decoder (%s) HW Configs ====\r\n", pDecoder->name);
    while (true) {
        const AVCodecHWConfig *pHwConfig = avcodec_get_hw_config(pDecoder, configIdx);
        if (pHwConfig == NULL) {
            break;
        }
        printf("config #%d : device=%s, pix_fmt=%s, methods=0x%x%s\r\n",
               configIdx,
               av_hwdevice_get_type_name(pHwConfig->device_type),
               av_get_pix_fmt_name(pHwConfig->pix_fmt),
               pHwConfig->methods,
               /** HW_DEVICE_CTX 방식: codecContext->hw_device_ctx에 디바이스를 걸어주는 방식 (다음 레슨에서 사용) */
               (pHwConfig->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) ? " [HW_DEVICE_CTX]" : "");
        configIdx++;
    }
    if (configIdx == 0) {
        printf("(no HW config — this decoder is SW only)\r\n");
    }
```

`avcodec_get_hw_config()`는 디코더가 지원하는 HW 설정을 인덱스로 조회하며, 범위를 넘으면 NULL을 반환한다. 실측 출력은 다음과 같다.

```text
config #0 : device=videotoolbox, pix_fmt=videotoolbox_vld, methods=0xb [HW_DEVICE_CTX]
```

각 필드의 의미는 이렇다.

- `device=videotoolbox`: 이 설정이 요구하는 HW 디바이스 타입. 2단계에서 만든 컨텍스트와 짝이 맞아야 한다.
- `pix_fmt=videotoolbox_vld`: HW 디코딩 시 `get_format` 콜백에 제안되는 HW 픽셀 포맷(`AV_PIX_FMT_VIDEOTOOLBOX`)이다. 02 레슨의 콜백에서 이 포맷을 골라야 HW 경로가 켜진다.
- `methods=0xb`: 비트마스크. `AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX`(0x01) 비트가 켜져 있으므로 "디바이스 컨텍스트만 걸어주면 되는" 가장 간단한 방식이 지원된다. 나머지 비트는 `HW_FRAMES_CTX`, `INTERNAL` 등 다른 연결 방식이다.

### 4. 해제와 비-macOS 분기

```c
    av_buffer_unref(&pHwDeviceContext);
#else
    printf("\r\nThis lesson requires macOS (VideoToolbox). Skipped.\r\n");
    (void) pHwDeviceContext;
    (void) pDecoder;
    (void) errorCode;
    (void) configIdx;
#endif
```

- 디바이스 컨텍스트는 `AVBufferRef`이므로 `av_buffer_unref()`로 참조를 내린다. 마지막 참조가 사라지는 순간 FFmpeg이 내부 `AVHWDeviceContext`와 실제 디바이스 자원을 정리한다.
- `#else` 쪽의 `(void)` 캐스트들은 macOS가 아닐 때 "변수를 선언만 하고 안 씀" 경고를 막기 위한 관용구다. CMake의 `if (APPLE)` 가드 때문에 실제로 이 분기가 빌드될 일은 거의 없지만, 소스 단독으로도 이식성을 갖게 하는 이중 안전장치다.

## 심화: 빌드 지원 vs 런타임 지원 vs 코덱 지원

HW 가속이 실제로 동작하려면 세 단계가 모두 통과해야 하며, 이 레슨의 세 단계가 정확히 그 검사에 대응한다.

| 검사 | 실패하는 경우 | 이 레슨의 확인 방법 |
|---|---|---|
| 빌드에 포함되어 있는가 | FFmpeg configure 시 해당 프레임워크 비활성화 | `av_hwdevice_iterate_types` 목록 |
| 이 머신에서 열리는가 | 하드웨어/드라이버 부재 | `av_hwdevice_ctx_create` 성공 여부 |
| 이 코덱이 지원하는가 | 코덱에 HW 구현이 없음 | `avcodec_get_hw_config` 결과 |

02 레슨의 HW 디코딩 코드는 이 세 검사가 모두 통과한다는 전제 위에 서 있다.

## ⚠️ 코드 특이점 상세

1. **에러 시 디바이스 컨텍스트 해제 경로가 하나뿐**
   H.264 디코더를 못 찾는 경우에는 `av_buffer_unref(&pHwDeviceContext)` 후 `return -1` 하지만, 그 외 조기 종료 경로는 없다. 프로그램이 짧아 문제가 되지는 않지만, 경로가 늘어나면 02 레슨처럼 `goto` 정리 블록으로 모으는 편이 안전하다.

2. **`avcodec_find_decoder(AV_CODEC_ID_H264)`는 기본(내장 SW) 디코더를 반환**
   FFmpeg의 H.264 디코딩은 별도의 `h264_videotoolbox` 디코더가 아니라, 내장 `h264` 디코더에 HW 설정이 붙는 구조다(그래서 config 목록을 조회하는 것이다). 인코딩은 반대로 `h264_videotoolbox`라는 **별도 인코더**로 제공된다 — 03 레슨에서 확인한다.
