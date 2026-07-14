# 14. 오디오 데이터 디코딩과 PCM 저장 — 코드 상세 해설

> [← 기본 문서](14-audio-data.md)

## 전체 구조

13번 코드 골격 위에 오디오 경로가 추가되고, 비디오 저장 경로는 주석으로 꺼졌다.

```text
main
 ├─ GeneratedAudio/audioData.raw 를 wb+ 로 열기        ← 신규
 ├─ (13과 동일) 열기 → 스트림 탐색 → 코덱 컨텍스트 → sws/RGB 준비(미사용)
 ├─ while (av_read_frame)
 │    ├─ 비디오 패킷 → (Grey/RGB 호출 모두 주석 처리)
 │    └─ 오디오 패킷 → DecodeAudioPacket()             ← 신규 (핵심)
 │           └─ receive_frame → planar 검사 → ExtractAudioSample → fwrite
 └─ ffmpeg_release + fclose(pAudioFile)
```

## 코드 블록별 해설

### 1. 출력 파일 준비

```c
/** save audio file name */
char audioResourcePath[BUFFER_MAX] = {0};
char audioFileName[50] = {0};
#if defined(WIN32) || defined(WIN64)
sprintf(audioFileName, "\\GeneratedAudio\\audioData.raw");
#else
sprintf(audioFileName, "/GeneratedAudio/audioData.raw");
#endif
if (!GetResourcePath(audioFileName, audioResourcePath)) {
    printf("Failed Make Audio File Save Path...\r\n");
    goto ffmpeg_release;
}
FILE *pAudioFile = NULL;
pAudioFile = fopen(audioResourcePath, "wb+");
assert(pAudioFile != NULL);
```

이미지와 달리 오디오는 프레임마다 파일을 열지 않고 **처음에 한 번 열어 두고 계속 이어 쓴다**. 연속된 PCM 스트림이기 때문이다. `goto`가 `pAudioFile` 선언보다 앞서 있는 점은 특이점 3에서 다룬다.

### 2. 디코딩 루프의 오디오 분기

```c
/** video frame read  */
if (pPacket->stream_index == videoStreamIdx) {
//            printf("Found Video Frame Packet!\r\n");
//            DecodeVideoPacket_GreyFrame(pPacket, pVideoCodecContext, pFrame);
//            DecodeVideoPacket_RGBFrame(pPacket, pVideoCodecContext, pFrame, pRGBFrame, pSwsContext);
}
    /** audio frame read */
else if (pPacket->stream_index == audioStreamIdx) {
//            printf("Found Audio Frame Packet!\r\n");

    /** Decoding Audio File */
    DecodeAudioPacket(pPacket, pAudioCodecContext, pFrame, pAudioFile);

}
```

비디오 처리 호출이 모두 주석 처리되어 이 레슨은 오디오만 다룬다. `pFrame` 하나를 비디오/오디오 겸용으로 재사용한다(AVFrame은 픽셀·샘플 어느 쪽도 담을 수 있다).

또한 20패킷 제한이 풀렸다.

```c
/** 20 Frame Image and audio data */
if (packetCount == 200) {
//            break;
}
```

`break`가 주석이므로 **파일 전체**의 오디오가 디코딩된다.

### 3. DecodeAudioPacket — 오디오 send/receive (핵심)

```c
returnValue = avcodec_receive_frame(pAudioCodecContext, pFrame);
```

구조는 비디오와 완전히 같다. 프레임 수신 시 출력되는 정보가 오디오 관점으로 바뀐다.

```c
printf("Frame number %lld (Samples = %d frame, size = %dbytes(%dbytes), Channels = %d) pts %lld key_frame %d [%s]\r\n",
       pAudioCodecContext->frame_num,
       pFrame->nb_samples,
       pFrame->pkt_size,
       pPacket->size,
       pFrame->ch_layout.nb_channels,
       pFrame->pts,
       pFrame->key_frame,
       av_get_sample_fmt_name(pAudioCodecContext->sample_fmt)
);
```

AAC 스트림이라면 `Samples = 1024`, 포맷 이름 `fltp`가 출력된다. `ch_layout.nb_channels`는 FFmpeg 5.1+에서 기존 `channels` 필드를 대체한 신형 채널 레이아웃 API다.

### 4. planar 검사와 샘플 기록

```c
/** audio data size get */
int audioDataSize = av_get_bytes_per_sample(pAudioCodecContext->sample_fmt);
if (av_sample_fmt_is_planar(pAudioCodecContext->sample_fmt) == 1) {
    for (int i = 0; i < pFrame->nb_samples; i++) {
        for (int j = 0; j < pAudioCodecContext->ch_layout.nb_channels; j++) {
            float sample = ExtractAudioSample(pAudioCodecContext, pFrame->extended_data[j], i);
            fwrite(&sample, sizeof(float), 1, pAudioFile);
        }
    }
}
```

- 바깥 루프 = 샘플 인덱스, 안쪽 루프 = 채널. 따라서 파일에는 `L0 R0 L1 R1 ...` 순서(인터리브)로 기록된다.
- planar 포맷에서 j번 채널의 버퍼는 `pFrame->extended_data[j]`이고, 같은 인덱스 `i`가 각 채널에서 "같은 시각"의 샘플을 가리킨다.
- 어떤 입력 포맷이든 결과 파일은 **float32 인터리브 PCM**으로 통일된다.

### 5. ExtractAudioSample — 포맷별 읽기와 정규화

```c
int sampleSize = av_get_bytes_per_sample(codecCtx->sample_fmt);
switch (sampleSize) {
    case 1:
        // 8bit samples are always unsigned
        val = ((uint8_t *) buffer)[sampleIndex];
        // make signeda
        val -= 127;
        break;
    case 2:
        val = ((int16_t *) buffer)[sampleIndex];
        break;
```

1단계: 샘플 크기에 맞는 타입으로 캐스팅해 `int64_t val`에 담는다. 8bit는 무부호이므로 127을 빼서 부호화한다.

```c
    case AV_SAMPLE_FMT_U8:
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S32:
    ...
        // integer => Scale to [-1, 1] and convert to float.
        ret = val / ((float) ((1 << (sampleSize * 8 - 1)) - 1));
        break;

    case AV_SAMPLE_FMT_FLT:
    case AV_SAMPLE_FMT_FLTP:
        // float => reinterpret
        ret = *REINTERPRET_CAST(float*, &val);
        break;
```

2단계: 포맷 계열별 변환.

- 정수 계열: 최대값(`2^(bits-1) - 1`)으로 나눠 `[-1, 1]`로 정규화한다. S16이면 32767로 나눈다.
- float 계열: `val`에 담긴 4바이트 비트 패턴을 `float`으로 재해석한다. 리틀엔디언에서 `int64_t`의 하위 4바이트에 원래 float 비트가 그대로 들어 있으므로 동작한다.
- double 계열: 같은 재해석을 시도하지만 이는 잘못된 방법이다(특이점 2).

## 심화: planar vs packed 샘플 포맷

### 메모리 배치 비교 (스테레오 기준)

```text
packed  (AV_SAMPLE_FMT_FLT)          planar (AV_SAMPLE_FMT_FLTP)
data[0]: L0 R0 L1 R1 L2 R2 ...       extended_data[0]: L0 L1 L2 L3 ...
                                     extended_data[1]: R0 R1 R2 R3 ...
```

| 구분 | packed | planar |
|---|---|---|
| 포맷 이름 | `u8, s16, s32, flt, dbl` | `u8p, s16p, s32p, fltp, dblp` |
| 버퍼 개수 | 1개 (인터리브) | 채널 수만큼 |
| 접근 방법 | `data[0][i*ch + j]` | `extended_data[j][i]` |
| 주 용도 | 최종 재생 장치, WAV 파일 | 디코더 내부 처리, 채널별 필터링 |

AAC/MP3 등 최신 디코더는 채널별 독립 처리가 쉬운 planar(`fltp`)로 출력하는 것이 일반적이고, 사운드카드와 대부분의 파일 포맷은 packed를 기대한다. 그래서 이 코드처럼 저장 시 인터리브로 재배열하거나, 실무에서는 `libswresample`(`swr_convert`)로 포맷·샘플레이트·채널 레이아웃을 한 번에 변환한다.

### 왜 채널이 2개인데 extended_data인가

`AVFrame->data`는 8칸 고정 배열이라 8채널 이상을 담을 수 없다. `extended_data`는 채널 수가 많으면 별도 배열을 가리키고, 8채널 이하면 `data`와 같은 곳을 가리킨다. 오디오에서는 항상 `extended_data`로 접근하는 것이 안전한 관례다.

## ⚠️ 코드 특이점 상세

1. **packed 포맷이면 조용히 아무것도 저장하지 않음**
   `if (av_sample_fmt_is_planar(...) == 1)`만 처리하고 else가 없다. 디코더가 packed 포맷을 내놓는 스트림에서는 `.raw` 파일이 0바이트가 된다. packed 분기(`data[0][i * nb_channels + j]` 접근)를 추가하는 것이 올바르다.

2. **double(DBL/DBLP) 재해석 버그**
   `case 8:`에서 8바이트를 `int64_t`로 읽은 뒤 `ret = *((float *) &val);`로 앞 4바이트만 float으로 재해석한다. double의 비트 배치는 float과 달라 완전히 다른 값이 나온다. 올바른 형태는 `double d; memcpy(&d, &buffer[sampleIndex * 8], 8); ret = (float) d;`처럼 double로 읽고 나서 float으로 **수치 변환**하는 것이다.

3. **`goto`가 `pAudioFile` 초기화를 건너뜀**
   `GetResourcePath(audioFileName, ...)` 실패 시의 `goto ffmpeg_release;`는 `FILE *pAudioFile = NULL;` 선언·초기화보다 앞에 있다. C에서 goto로 선언을 건너뛰면 변수는 존재하되 값이 미정(indeterminate)이므로, 정리 블록 끝의 `fclose(pAudioFile);`이 쓰레기 포인터를 닫는 미정의 동작이 된다. 파일 선언을 함수 맨 위로 올리고 `if (pAudioFile != NULL) fclose(pAudioFile);`로 방어하는 것이 올바르다.

4. **CMake `LANGUAGES CXX`와 C 소스의 조합**

   ```cmake
   project(chapter0214IntroductionAudioData LANGUAGES CXX VERSION 0.0.1)
   ...
   target_sources(chapter0214IntroductionAudioData PRIVATE
           main.c
   )
   ```

   하위 프로젝트는 CXX만 선언하지만, 루트 `CMakeLists.txt`의 `project(udemy_ffmpeg_study)`(LANGUAGES 미지정 = C/CXX 기본 활성화) 덕분에 C 컴파일 규칙이 이미 존재해 `main.c`가 정상적으로 C로 빌드된다. 이 디렉터리를 독립 프로젝트로 구성하면 C 언어가 비활성 상태가 되어 문제가 생길 수 있으므로, 의도를 명확히 하려면 `LANGUAGES C` 또는 `LANGUAGES C CXX`로 선언해야 한다.

5. **`audioDataSize` 미사용** — 계산만 하고 쓰지 않는다(`ExtractAudioSample` 내부에서 같은 값을 다시 계산한다).

6. **13의 잔재 코드**: SwsContext 생성과 RGB 버퍼 fill이 그대로 실행되지만 비디오 경로가 주석 처리되어 전혀 사용되지 않는다. `rgbFrameBuffer` 누수, fill 포맷 불일치(BGR24 vs RGB24), `pCurStream[idx]` 이중 인덱싱, 디코더 flush 누락도 그대로 상속된다.
