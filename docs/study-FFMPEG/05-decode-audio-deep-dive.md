# 05. 오디오 디코딩과 샘플 포맷 — 코드 상세 해설

> [← 기본 문서](05-decode-audio.md)

## 전체 구조

| 함수 | 역할 |
|---|---|
| `main()` | 파일 열기 → 오디오 스트림/디코더 탐색 → 코덱 컨텍스트 준비 → 디먹싱+디코딩 루프 → flush → 통계 출력 |
| `DrainDecodedAudioFrames()` | 디코더에서 나올 수 있는 프레임을 모두 꺼내 정보를 출력하고 샘플 수를 누적. 꺼낸 프레임 수를 반환 |
| `GetResourcePath()` | 실행 경로에서 `/cmake` 앞부분을 잘라 `resources/` 경로를 역산하는 공통 유틸 |

```text
main
 ├─ GetResourcePath("murage.mp4", ...)
 ├─ avformat_open_input / find_stream_info
 ├─ av_find_best_stream(AUDIO) → audioStreamIdx + pAudioCodec
 ├─ codec context: alloc → parameters_to_context → open2
 ├─ 디코더 정보 출력 (sample_rate / sample_fmt / planar / ch_layout)
 ├─ while (av_read_frame >= 0)
 │    └─ 오디오 패킷 → send_packet → DrainDecodedAudioFrames()
 ├─ avcodec_send_packet(ctx, NULL) → DrainDecodedAudioFrames()  (flush)
 └─ ffmpeg_release: frame/packet/codec context/format 해제
```

## 코드 블록별 해설

### 1. av_find_best_stream으로 스트림과 디코더를 한 번에

```c
/** 오디오 스트림 + 디코더 찾기 */
audioStreamIdx = av_find_best_stream(pFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, &pAudioCodec, 0);
if (audioStreamIdx < 0) {
    av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Audio Stream Found Failed...\r\n", audioStreamIdx);
    goto ffmpeg_release;
}
printf("audio stream index : %d, codec : %s\r\n", audioStreamIdx, pAudioCodec->name);
```

스트림 배열을 직접 순회하며 `codec_id`로 디코더를 찾는 대신, `av_find_best_stream()`이 "가장 적합한 오디오 스트림 인덱스"와 그 스트림의 디코더를 한 번에 돌려준다. 스트림이 없으면 음수(`AVERROR_STREAM_NOT_FOUND`)를 반환하므로 인덱스 검사만으로 실패를 판별할 수 있다. `audioStreamIdx`가 `-1`로 초기화되어 있는 것도 chapter02 초반 레슨들의 `0` 초기화 문제를 바로잡은 형태다.

### 2. 코덱 컨텍스트 준비 3단계

```c
pAudioCodecContext = avcodec_alloc_context3(pAudioCodec);
...
errorCode = avcodec_parameters_to_context(pAudioCodecContext,
                                          pFormatContext->streams[audioStreamIdx]->codecpar);
...
errorCode = avcodec_open2(pAudioCodecContext, pAudioCodec, NULL);
```

비디오 때와 완전히 같은 `할당 → 파라미터 복사 → 열기` 정석 패턴이다. 컨테이너가 알고 있는 오디오 정보(샘플레이트, 채널 구성, AAC extradata 등)를 컨텍스트에 복사해야 디코더가 올바르게 초기화된다.

### 3. 디코더 파라미터 출력 — FFmpeg 7.x AVChannelLayout

```c
/** 디코더가 실제로 사용하는 오디오 파라미터 출력 */
av_channel_layout_describe(&pAudioCodecContext->ch_layout, channelLayoutText, sizeof(channelLayoutText));
printf("==== Audio Decoder Information ====\r\n");
printf("sample rate    : %d Hz\r\n", pAudioCodecContext->sample_rate);
printf("sample format  : %s\r\n", av_get_sample_fmt_name(pAudioCodecContext->sample_fmt));
printf("planar         : %s\r\n", av_sample_fmt_is_planar(pAudioCodecContext->sample_fmt) ? "yes" : "no");
printf("channels       : %d (%s)\r\n", pAudioCodecContext->ch_layout.nb_channels, channelLayoutText);
```

FFmpeg 7.x에서는 구식 `channels`/`channel_layout` 필드가 제거되고 `AVChannelLayout` 구조체(`ch_layout`) 하나로 통합되었다. 채널 수는 `ch_layout.nb_channels`로 읽고, `av_channel_layout_describe()`가 "stereo" 같은 사람이 읽을 수 있는 이름을 채워준다. murage.mp4에서는 `48000 Hz / fltp / planar yes / 2 (stereo)`가 출력된다.

### 4. 디먹싱 + 디코딩 루프

```c
/** 디먹싱 + 디코딩 루프 (04와 동일한 구조, 대상만 오디오 스트림) */
while (av_read_frame(pFormatContext, pPacket) >= 0) {
    if (pPacket->stream_index == audioStreamIdx) {
        errorCode = avcodec_send_packet(pAudioCodecContext, pPacket);
        if (errorCode < 0) {
            av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Sending packet to decoder\r\n", errorCode);
            av_packet_unref(pPacket);
            break;
        }
        decodedFrameCount += DrainDecodedAudioFrames(pAudioCodecContext, pFrame, &printedCount, &totalSamples);
    }
    av_packet_unref(pPacket);
}
```

04의 비디오 디코딩 루프와 구조가 같다. `stream_index`로 오디오 패킷만 골라 send하고, receive는 별도 함수로 위임한다. 비디오 패킷은 아무 처리 없이 `av_packet_unref()`만 하고 넘어간다.

### 5. 디코더 flush — 마지막 프레임까지 회수

```c
/** 디코더 flush */
errorCode = avcodec_send_packet(pAudioCodecContext, NULL);
if (errorCode >= 0) {
    decodedFrameCount += DrainDecodedAudioFrames(pAudioCodecContext, pFrame, &printedCount, &totalSamples);
}
```

`av_read_frame()`이 EOF를 반환한 뒤 NULL 패킷을 보내면 디코더가 flush 모드로 들어가 내부 버퍼에 남은 프레임을 모두 내보낸다. chapter02 초반 레슨들이 생략했던 부분으로, 이걸 해야 `총 샘플 수 ÷ sample_rate`가 실제 오디오 길이와 정확히 맞아떨어진다.

### 6. DrainDecodedAudioFrames — receive 루프와 planar 샘플 읽기 (핵심)

```c
while (errorCode >= 0) {
    errorCode = avcodec_receive_frame(pCodecContext, pFrame);
    if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
        break;
    } else if (errorCode < 0) {
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR](%d) Receive frame\r\n", errorCode);
        break;
    }

    if (*pPrintedCount < PRINT_FRAME_MAX) {
        /**
         * 첫 샘플 값 확인:
         * fltp(planar float)라면 data[0]이 첫 번째 채널의 float 배열이다.
         */
        float firstSample = 0.0f;
        if (pFrame->format == AV_SAMPLE_FMT_FLTP && pFrame->data[0] != NULL) {
            firstSample = ((float *) pFrame->data[0])[0];
        }
        printf("Frame %-4lld pts=%-8lld nb_samples=%-5d fmt=%s first_sample=%f\r\n",
               pCodecContext->frame_num,
               pFrame->pts,
               pFrame->nb_samples,
               av_get_sample_fmt_name((enum AVSampleFormat) pFrame->format),
               firstSample);
        (*pPrintedCount)++;
    }

    *pTotalSamples += pFrame->nb_samples;
    receivedCount++;
    av_frame_unref(pFrame);
}
```

- `EAGAIN`(입력 더 필요)과 `AVERROR_EOF`(flush 완료)는 정상 탈출이고, 그 외 음수만 진짜 에러다.
- **planar 포맷 접근**: `fltp`에서는 `data[0]`이 첫 번째 채널(L)의 float 샘플 배열, `data[1]`이 두 번째 채널(R)이다. interleaved(`flt`)였다면 `data[0]`에 LRLR... 순으로 섞여 있었을 것이다. 여기서는 `data[0]`의 첫 float 하나만 찍어 실제 파형 값(-1.0~1.0)이 들어 있음을 확인한다.
- `nb_samples`는 **채널당** 샘플 수다. AAC는 프레임당 1024가 기본이라 출력에서 `nb_samples=1024`가 반복된다.
- 프레임 사용 후 `av_frame_unref()`로 참조를 해제해야 같은 `AVFrame` 구조체를 다음 receive에 재사용할 수 있다.

### 7. 통계 출력과 자원 해제

```c
printf("total decoded frames  : %d\r\n", decodedFrameCount);
printf("total samples/channel : %lld\r\n", totalSamples);
/** 샘플 수 / 샘플레이트 = 실제 오디오 길이(초) — 컨테이너 duration과 비교해 보자 */
printf("audio length          : %.2f sec\r\n", (double) totalSamples / pAudioCodecContext->sample_rate);

exitStatus = 0;

ffmpeg_release:
av_frame_free(&pFrame);
av_packet_free(&pPacket);
avcodec_free_context(&pAudioCodecContext);
avformat_close_input(&pFormatContext);
if (exitStatus == 0) {
    printf("Decode Audio Done!\r\n");
} else {
    printf("Finished with error(s)...\r\n");
}
return exitStatus;
```

murage.mp4 기준 `598프레임 × 1024샘플 ≈ 612,352샘플`, `612352 ÷ 48000 = 12.76초`다(마지막 프레임은 1024보다 짧을 수 있다). 해제는 frame → packet → codec context → format 순이며, chapter02 초반과 달리 `avcodec_free_context()`까지 챙긴다.

`exitStatus`는 -1로 선언되어 성공 경로 맨 끝(`ffmpeg_release:` 직전)에서만 0이 된다. 에러로 `goto ffmpeg_release`한 경우 -1인 채로 반환되어 **실패 시 0이 아닌 종료 코드로 끝나므로 셸/CI에서 실패를 감지할 수 있고**, 마지막 메시지도 성공/실패로 분기된다.

## 심화: 왜 오디오는 planar가 기본일까

AAC/MP3 같은 코덱은 채널별로 독립 처리(변환, 스케일링)를 하는 편이 알고리즘상 자연스러워 디코더 출력이 planar(`fltp`)인 경우가 많다. 반면 사운드카드/재생 API는 대부분 interleaved 정수(`s16`)를 기대한다. 이 간극을 메우는 것이 다음다음 레슨(07)의 **swresample**이다.
