#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
// FFMPEG Library
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>

/**
 * study-FFMPEG HW-01 — 하드웨어 디바이스 열거
 *
 * FFmpeg의 하드웨어 가속은 "디바이스 컨텍스트(AVHWDeviceContext)"를 중심으로 돈다.
 *   1) 이 빌드가 지원하는 HW 디바이스 타입 목록 확인
 *   2) macOS의 VideoToolbox 디바이스 컨텍스트 생성 시도
 *   3) H.264 디코더가 지원하는 HW 설정(AVCodecHWConfig) 열거
 *
 * 이 레슨은 파일을 열지 않는다 — 시스템의 HW 가속 능력만 조사한다.
 */
int main(int argc, char **argv) {
    enum AVHWDeviceType deviceType = AV_HWDEVICE_TYPE_NONE;
    AVBufferRef *pHwDeviceContext = NULL;
    const AVCodec *pDecoder = NULL;
    int errorCode = 0;
    int configIdx = 0;

    /**
     * ===== 1. 지원하는 HW 디바이스 타입 열거 =====
     * av_hwdevice_iterate_types는 NONE부터 시작해
     * 이 FFmpeg 빌드에 컴파일된 디바이스 타입을 차례로 돌려준다.
     * (macOS vcpkg 빌드라면 videotoolbox가 보여야 한다)
     */
    printf("==== Available HW Device Types ====\r\n");
    while ((deviceType = av_hwdevice_iterate_types(deviceType)) != AV_HWDEVICE_TYPE_NONE) {
        printf("- %s\r\n", av_hwdevice_get_type_name(deviceType));
    }

#if defined(__APPLE__)
    /**
     * ===== 2. VideoToolbox 디바이스 컨텍스트 생성 =====
     * VideoToolbox는 macOS/iOS의 HW 코덱 프레임워크다.
     * (CUDA/VAAPI와 달리 디바이스 경로 지정이 필요 없어 NULL로 충분하다)
     */
    printf("\r\n==== Create VideoToolbox Device ====\r\n");
    errorCode = av_hwdevice_ctx_create(&pHwDeviceContext, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, NULL, NULL, 0);
    if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Failed Create VideoToolbox Device...\r\n", errorCode);
        return -1;
    }
    printf("VideoToolbox device created successfully!\r\n");

    /**
     * ===== 3. H.264 디코더의 HW 설정 열거 =====
     * 같은 디코더라도 여러 HW 방식(hw_device_ctx / hw_frames_ctx / 내부 처리)을
     * 지원할 수 있다. avcodec_get_hw_config로 하나씩 조회한다.
     */
    pDecoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (pDecoder == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed Find H.264 Decoder...\r\n");
        av_buffer_unref(&pHwDeviceContext);
        return -1;
    }

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

    av_buffer_unref(&pHwDeviceContext);
#else
    printf("\r\nThis lesson requires macOS (VideoToolbox). Skipped.\r\n");
    (void) pHwDeviceContext;
    (void) pDecoder;
    (void) errorCode;
    (void) configIdx;
#endif

    printf("List HW Devices Done!\r\n");
    return 0;
}
