# 07. swresample: 오디오 리샘플링 — 코드 상세 해설

> [← 기본 문서](07-resampling-audio.md)

## 전체 구조

| 함수 | 역할 |
|---|---|
| `main()` | 파일 열기 → 오디오 디코더 준비 → SwrContext 준비 → 디코딩+리샘플링 루프 → 디코더 flush → 리샘플러 드레인 |
| `ResampleAndWrite()` | 프레임 1개(또는 드레인 시 NULL)를 리샘플링해 파일에 쓰고, 쓴 샘플 수를 반환 |
| `EnsureGeneratedStudyDirectory()` | `resources/GeneratedStudy/` 디렉터리 생성 |
| `GetResourcePath()` | 실행 경로에서 `resources/` 경로를 역산하는 공통 유틸 |

```text
main
 ├─ avformat_open_input / find_stream_info
 ├─ av_find_best_stream(AUDIO) → codec context 3단계 준비
 ├─ swr_alloc_set_opts2(출력: stereo/s16/44100 ← 입력: 코덱 사양)
 ├─ swr_init
 ├─ fopen("GeneratedStudy/study-audio.pcm", "wb")
 ├─ while (av_read_frame >= 0)
 │    └─ send_packet → receive_frame 루프 → ResampleAndWrite(frame)
 ├─ avcodec_send_packet(NULL) → 남은 프레임도 ResampleAndWrite
 ├─ ResampleAndWrite(NULL)          ← 리샘플러 드레인
 └─ ffmpeg_release: file/frame/packet/swr/codec/format 해제
```

## 코드 블록별 해설

### 1. 출력 사양과 채널 레이아웃 준비

```c
SwrContext *pSwrContext = NULL;
/** FFmpeg 7.x 채널 레이아웃: 매크로로 stereo 레이아웃 초기화 */
AVChannelLayout outputChannelLayout = AV_CHANNEL_LAYOUT_STEREO;
```

FFmpeg 7.x에서 채널 레이아웃은 `AVChannelLayout` 구조체다. `AV_CHANNEL_LAYOUT_STEREO` 매크로는 이 구조체를 stereo 구성으로 초기화하는 복합 리터럴이라 별도 API 호출 없이 선언과 동시에 완성된다.

### 2. SwrContext 생성 — swr_alloc_set_opts2

```c
/**
 * SwrContext 생성 (FFmpeg 7.x는 swr_alloc_set_opts2 사용).
 * 출력 사양(레이아웃/포맷/샘플레이트) → 입력 사양 순서로 지정한다.
 */
errorCode = swr_alloc_set_opts2(&pSwrContext,
                                &outputChannelLayout, AV_SAMPLE_FMT_S16, OUTPUT_SAMPLE_RATE,
                                &pAudioCodecContext->ch_layout, pAudioCodecContext->sample_fmt,
                                pAudioCodecContext->sample_rate,
                                0, NULL);
...
errorCode = swr_init(pSwrContext);
```

- 구식 `swr_alloc_set_opts()`는 uint64 채널 마스크를 받았지만, FFmpeg 7.x의 `swr_alloc_set_opts2()`는 `AVChannelLayout *`를 받고 컨텍스트도 이중 포인터로 할당해 준다.
- 인자 순서가 **출력 먼저, 입력 나중**이라는 점에 주의한다. 입력 사양은 하드코딩하지 않고 열린 디코더 컨텍스트(`ch_layout`/`sample_fmt`/`sample_rate`)에서 그대로 읽는다.
- 옵션 설정 후 `swr_init()`을 호출해야 변환 가능한 상태가 된다. sws_getContext는 생성=초기화였지만 swresample은 설정과 초기화가 분리되어 있다.

### 3. 디코딩 → 리샘플링 루프

```c
/** 디코딩 → 리샘플링 → 파일 쓰기 루프 */
while (av_read_frame(pFormatContext, pPacket) >= 0) {
    if (pPacket->stream_index == audioStreamIdx) {
        errorCode = avcodec_send_packet(pAudioCodecContext, pPacket);
        ...
        while (true) {
            errorCode = avcodec_receive_frame(pAudioCodecContext, pFrame);
            if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
                break;
            }
            ...
            totalOutputSamples += ResampleAndWrite(pSwrContext, pFrame, pOutputFile);
            av_frame_unref(pFrame);
        }
    }
    av_packet_unref(pPacket);
}
```

05의 디코딩 루프와 같지만, 프레임 정보를 출력하는 대신 `ResampleAndWrite()`로 넘겨 변환·저장한다. 이어서 디코더 flush(NULL 패킷)도 05와 동일하게 수행해 마지막 프레임까지 리샘플러에 공급한다.

### 4. 출력 샘플 수 계산 (핵심)

```c
/**
 * 출력에 필요한 최대 샘플 수 계산.
 * swr_get_delay: 리샘플러 내부에 쌓여 있는 지연 샘플 수.
 * av_rescale_rnd: (지연 + 입력 샘플)을 출력 샘플레이트 기준으로 환산.
 */
maxOutputSamples = av_rescale_rnd(
        swr_get_delay(pSwrContext, pFrame != NULL ? pFrame->sample_rate : OUTPUT_SAMPLE_RATE) + inputSampleCount,
        OUTPUT_SAMPLE_RATE,
        pFrame != NULL ? pFrame->sample_rate : OUTPUT_SAMPLE_RATE,
        AV_ROUND_UP);
if (maxOutputSamples <= 0) {
    return 0;
}
```

이 코드가 이 레슨의 핵심 공식이다.

- `swr_get_delay(swr, base)`: 리샘플러가 보간을 위해 내부에 붙잡아 둔 샘플 수를 `base` 샘플레이트 단위로 돌려준다. 48000→44100처럼 비율이 정수가 아니면 매 호출마다 소수점 이하 샘플이 내부에 누적된다.
- `av_rescale_rnd(a, b, c, AV_ROUND_UP)`: `(지연 + 입력샘플) × 44100 / 48000`을 64비트 오버플로 없이 올림 계산한다. 올림(`AV_ROUND_UP`)이라 버퍼가 모자라는 일이 없다.
- 드레인 호출(`pFrame == NULL`)에서는 입력 샘플레이트를 알 수 없으므로 `OUTPUT_SAMPLE_RATE`를 기준으로 쓴다. 이때 `inputSampleCount`는 0이므로 지연 샘플만 환산된다.

### 5. 버퍼 할당과 swr_convert

```c
/** 출력 버퍼 할당 (stereo / s16 / interleaved) */
if (av_samples_alloc(&pOutputBuffer, &outputLineSize, 2, (int) maxOutputSamples, AV_SAMPLE_FMT_S16, 1) < 0) {
    printf("Failed Allocate Resample Output Buffer...\r\n");
    return 0;
}

/**
 * 실제 변환.
 * 입력이 NULL이면 내부에 남은 샘플만 뱉어내는 드레인 동작을 한다.
 * 반환값 = 실제로 변환된 채널당 샘플 수.
 */
convertedSamples = swr_convert(pSwrContext, &pOutputBuffer, (int) maxOutputSamples,
                               ppInputData, inputSampleCount);
if (convertedSamples > 0) {
    /** s16 stereo interleaved → 샘플당 2바이트 × 2채널 */
    fwrite(pOutputBuffer, 1, (size_t) convertedSamples * 2 * sizeof(int16_t), pOutputFile);
}

av_freep(&pOutputBuffer);
```

- `av_samples_alloc()`은 `av_image_alloc()`의 오디오 버전이다. s16 interleaved는 단일 평면이므로 포인터 하나(`pOutputBuffer`)만 쓴다.
- `swr_convert()`의 반환값은 **실제로 변환된 채널당 샘플 수**로, `maxOutputSamples`보다 작을 수 있다(내부 지연 때문). 그래서 `fwrite`는 반환값 기준으로 `convertedSamples × 2채널 × 2바이트`만 쓴다.
- 입력 프레임의 `pFrame->data`는 fltp라 `data[0]`=L, `data[1]`=R 두 평면이지만, swr_convert가 평면 배열을 통째로 받아 알아서 interleave한다.
- 프레임마다 malloc/free를 반복하는 것은 학습용 단순화다. 실전에서는 버퍼를 재사용하거나 `swr_convert_frame()`을 쓴다.

### 6. 리샘플러 드레인

```c
/**
 * 리샘플러 드레인.
 * 샘플레이트 변환 시 SwrContext 내부에 지연된 샘플이 남는다.
 * 입력을 NULL로 주면 남은 샘플을 모두 뱉어낸다.
 */
totalOutputSamples += ResampleAndWrite(pSwrContext, NULL, pOutputFile);
```

`ResampleAndWrite()`는 `pFrame == NULL`이면 `ppInputData = NULL, inputSampleCount = 0`으로 `swr_convert()`를 호출한다 — 이것이 드레인이다. 디코더 flush(NULL 패킷) → 리샘플러 드레인(NULL 입력) 순서로 파이프라인의 모든 단계를 비워야 마지막 몇 밀리초가 잘리지 않는다.

### 7. 결과 확인과 자원 해제

```c
printf("total output samples : %lld (%.2f sec)\r\n",
       totalOutputSamples, (double) totalOutputSamples / OUTPUT_SAMPLE_RATE);
printf("\r\n다음 명령으로 재생해 확인:\r\n");
printf("ffplay -f s16le -ar %d -ch_layout stereo \"%s\"\r\n", OUTPUT_SAMPLE_RATE, outputPath);

exitStatus = 0;

ffmpeg_release:
if (pOutputFile != NULL) {
    fclose(pOutputFile);
}
av_frame_free(&pFrame);
av_packet_free(&pPacket);
swr_free(&pSwrContext);
avcodec_free_context(&pAudioCodecContext);
avformat_close_input(&pFormatContext);
if (exitStatus == 0) {
    printf("Resampling Audio Done!\r\n");
} else {
    printf("Finished with error(s)...\r\n");
}
return exitStatus;
```

murage.mp4 기준 총 562,598샘플이 출력된다. 입력 612,352샘플 × (44100/48000) ≈ 562,598 — 샘플레이트 비율대로 정확히 환산되었음을 검증할 수 있다(12.76초 유지). raw PCM은 헤더가 없으므로 ffplay에 `-f s16le -ar 44100 -ch_layout stereo`로 사양을 직접 알려줘야 한다.

`exitStatus`는 -1로 선언되어 성공 경로 맨 끝(`ffmpeg_release:` 직전)에서만 0이 된다. 에러로 `goto ffmpeg_release`한 경우 -1인 채로 반환되어 **실패 시 0이 아닌 종료 코드로 끝나므로 셸/CI에서 실패를 감지할 수 있고**, 마지막 메시지도 성공/실패로 분기된다.

## 심화: 왜 44.1kHz로 변환해 봤나

48kHz는 영상/방송 계열의 표준, 44.1kHz는 CD 계열의 표준이다. 두 값의 비 44100/48000 = 147/160은 정수비가 아니어서 리샘플러가 다상 필터(polyphase filter)로 보간해야 하고, 그 과정에서 지연 샘플이 생긴다. 이 레슨의 `swr_get_delay` + 드레인 처리가 필요한 이유가 바로 이 비정수비 변환 때문이며, 같은 코드로 어떤 샘플레이트 조합이든 처리할 수 있다.
