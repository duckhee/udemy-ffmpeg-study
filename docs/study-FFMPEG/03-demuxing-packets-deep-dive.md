# 03. 디먹싱 — 패킷 추출 — 코드 상세 해설

> [← 기본 문서](03-demuxing-packets.md)

## 전체 구조

| 함수 / 구간 | 역할 |
|---|---|
| `main()` | 열기 → 패킷 할당 → 디먹싱 루프(출력 + 집계) → 통계 → 해제 |
| `GetResourcePath()` | 실행 경로에서 `resources/` 경로를 역산하는 유틸 (01과 동일) |
| `PRINT_PACKET_MAX` | 상세 출력할 패킷 개수 상한 (20) |

```text
main
 ├─ GetResourcePath("murage.mp4", ...)
 ├─ avformat_open_input / avformat_find_stream_info   (01~02와 동일한 골격)
 ├─ av_packet_alloc
 ├─ while (av_read_frame >= 0)
 │    ├─ 처음 20개: pts/dts/size/KEY + av_rescale_q(pts→초) 출력 (pts 없으면 N/A)
 │    ├─ streamPacketCount[stream_index]++ / totalPacketCount++
 │    └─ av_packet_unref
 ├─ 통계 출력 (total / 스트림별)
 └─ av_packet_free → avformat_close_input
```

## 코드 블록별 해설

### 1. 카운터 변수와 출력 제한

```c
int printedCount = 0;
int totalPacketCount = 0;
/** 스트림별 패킷 개수 집계 (스트림이 8개를 넘는 파일은 이 예제에서 다루지 않는다) */
int streamPacketCount[8] = {0};
```

murage.mp4는 12.78초짜리 파일인데도 패킷이 981개나 나온다. 전부 출력하면 콘솔이 넘치므로 상세 출력(`printedCount`)과 집계(`totalPacketCount`, `streamPacketCount[]`)를 분리했다. 스트림별 집계 배열은 학습 예제답게 고정 크기 8로 단순화했고, 접근 전에 `stream_index < 8`을 검사해 배열 밖 쓰기를 막는다.

### 2. AVPacket 할당

```c
/**
 * AVPacket 할당.
 * 패킷 구조체 자체는 한 번만 할당하고,
 * 매 루프에서 av_packet_unref로 내부 데이터 참조만 해제하며 재사용한다.
 */
pPacket = av_packet_alloc();
if (pPacket == NULL) {
    av_log(NULL, AV_LOG_ERROR, "Failed Load packet structure...\r\n");
    avformat_close_input(&pFormatContext);
    return -1;
}
```

`AVPacket`은 이중 구조다 — 구조체 자체(메타데이터: pts, dts, size, flags...)와 그것이 **참조하는** 압축 데이터 버퍼(레퍼런스 카운트 방식). `av_packet_alloc()`은 껍데기만 만들고, 실제 데이터 버퍼는 매번 `av_read_frame()`이 채운다. 할당 실패 시에도 이미 연 컨테이너를 닫고 나가는 정리 순서를 지킨다.

### 3. 디먹싱 루프와 pts→초 변환 (핵심)

```c
/**
 * 디먹싱 루프.
 * av_read_frame은 성공 시 0, 파일 끝(EOF)이나 에러 시 음수를 반환한다.
 * 반환된 패킷은 파일에 저장된 순서(=dts 순서)로 나온다.
 */
while (av_read_frame(pFormatContext, pPacket) >= 0) {
    if (printedCount < PRINT_PACKET_MAX) {
        AVStream *pStream = pFormatContext->streams[pPacket->stream_index];
        char ptsText[32] = {0};
        char ptsSecondsText[32] = {0};

        /**
         * 일부 컨테이너/코덱은 pts 없는 패킷을 내놓는다(AV_NOPTS_VALUE = INT64_MIN).
         * 이 값을 그대로 av_rescale_q에 넣으면 오버플로된 쓰레기 값이 나오므로
         * 반드시 검사 후 사용한다.
         */
        if (pPacket->pts == AV_NOPTS_VALUE) {
            snprintf(ptsText, sizeof(ptsText), "N/A");
            snprintf(ptsSecondsText, sizeof(ptsSecondsText), "N/A");
        } else {
            /**
             * pts는 스트림 time_base 단위이므로 초로 보려면 변환이 필요하다.
             * av_rescale_q(값, 원래 단위, 바꿀 단위) — 여기서는
             * 스트림 time_base → AV_TIME_BASE_Q(1/1,000,000초) 로 바꾼 뒤 초로 나눈다.
             */
            double ptsSeconds =
                    (double) av_rescale_q(pPacket->pts, pStream->time_base, AV_TIME_BASE_Q) / AV_TIME_BASE;
            snprintf(ptsText, sizeof(ptsText), "%lld", pPacket->pts);
            snprintf(ptsSecondsText, sizeof(ptsSecondsText), "%.3f", ptsSeconds);
        }
```

- **pts 유효성 검사가 먼저다**: `AV_NOPTS_VALUE`(INT64_MIN)는 "pts 없음"을 뜻하는 특수값이다. 이 값을 그대로 `av_rescale_q()`에 넣으면 오버플로된 쓰레기 값이 출력되므로, 검사 후 pts가 없는 패킷은 `ptsText`/`ptsSecondsText` 버퍼에 `"N/A"`를 담아 출력한다.
- `av_read_frame()`이 채워준 `pPacket->stream_index`로 소속 스트림을 찾아, **그 스트림의 time_base**로 pts를 해석한다. 비디오 pts는 1/15360초 단위, 오디오 pts는 1/48000초 단위라서 스트림을 무시하고 변환하면 완전히 틀린 값이 나온다.
- `av_rescale_q(a, bq, cq)`는 수학적으로 `a x bq / cq`인데, 중간 곱을 128비트 정밀도로 처리해 큰 pts 값에서도 오버플로가 나지 않는다. `AV_TIME_BASE_Q`는 `{1, 1000000}` 상수이므로 결과는 마이크로초이고, 마지막에 `AV_TIME_BASE`로 나눠 초를 만든다.

### 4. 패킷 정보 출력과 키프레임 플래그

```c
printf("%-8d %-8d %-12s %-12lld %-10d %-10s %s\r\n",
       totalPacketCount,
       pPacket->stream_index,
       ptsText,
       pPacket->dts,
       pPacket->size,
       /** AV_PKT_FLAG_KEY: 이 패킷이 키프레임(I-프레임)을 담고 있음 */
       (pPacket->flags & AV_PKT_FLAG_KEY) ? "KEY" : "-",
       ptsSecondsText);
printedCount++;
```

pts 관련 두 칸은 정수/실수가 아니라 앞에서 준비한 문자열(`ptsText`, `ptsSecondsText`)로 출력한다. pts가 정상이면 숫자가, `AV_NOPTS_VALUE`였다면 `N/A`가 찍힌다.

실행하면 첫 비디오 패킷이 `pts=0, KEY, 232504 bytes`로 나온다. 관찰 포인트가 세 가지다.

- **키프레임은 크다**: 첫 I-프레임 패킷(약 227KB)은 이후 P/B 패킷보다 수십 배 크다. 다른 프레임을 참조하지 않고 그림 전체를 담기 때문이다.
- **오디오 패킷은 전부 KEY**: AAC는 프레임 간 참조가 없어 모든 패킷이 독립적으로 디코딩 가능하다.
- **비디오/오디오 패킷이 교차로 나온다**: 인터리빙된 저장 순서가 그대로 보인다. 같은 초 구간에서 오디오 패킷(1024샘플 = 약 21.3ms 간격)이 비디오보다 촘촘하다.

### 5. 집계와 unref

```c
if (pPacket->stream_index < 8) {
    streamPacketCount[pPacket->stream_index]++;
}
totalPacketCount++;

/**
 * 중요: av_read_frame이 채운 패킷 데이터의 참조를 반드시 해제한다.
 * 해제하지 않으면 매 패킷마다 메모리가 누적된다.
 */
av_packet_unref(pPacket);
```

`av_packet_unref()`는 패킷이 참조하던 데이터 버퍼의 레퍼런스 카운트를 내리고(0이 되면 해제) 구조체 필드를 초기 상태로 되돌린다. 구조체 자체는 살아 있으므로 다음 `av_read_frame()`이 재사용한다. 이 한 줄을 빠뜨리면 981개 패킷 분량의 압축 데이터가 전부 메모리에 쌓인다.

### 6. 통계 출력과 해제

```c
printf("\r\n==== Packet Summary ====\r\n");
printf("total packets  : %d\r\n", totalPacketCount);
for (int idx = 0; idx < (int) pFormatContext->nb_streams && idx < 8; ++idx) {
    printf("stream #%d (%s) : %d packets\r\n",
           idx,
           av_get_media_type_string(pFormatContext->streams[idx]->codecpar->codec_type),
           streamPacketCount[idx]);
}

/** 해제는 할당의 역순 */
av_packet_free(&pPacket);
avformat_close_input(&pFormatContext);
```

murage.mp4 결과는 다음과 같다.

| 항목 | 값 |
|---|---|
| total packets | 981 |
| stream #0 (video) | 383 packets |
| stream #1 (audio) | 598 packets |

검산: 비디오 383 패킷 / 12.78초 = 약 30 fps(스트림 정보의 30.08 fps와 일치), 오디오 598 패킷 x 1024샘플 / 48000Hz = 약 12.76초로 재생 시간과 맞아떨어진다. 해제는 할당의 역순 — 패킷을 먼저 `av_packet_free()`(내부에서 unref까지 수행 후 구조체 해제, 포인터 NULL화)하고 컨테이너를 닫는다.

## 심화: 패킷 순서는 dts 순서다

`av_read_frame()`은 패킷을 **파일에 저장된 순서 = dts(디코딩 순서)** 로 반환한다. B-프레임이 있는 스트림에서는 "미래의 프레임을 참조 대상으로 먼저 디코딩"해야 하므로 pts(표시 순서)와 dts가 어긋난다. 출력에서 비디오 패킷의 pts가 512, 2048, 1024, 1536... 처럼 왔다 갔다 하는 반면 dts는 단조 증가하는 것을 관찰할 수 있다. 표시 순서로 되돌리는 일은 디코더가 담당하며, 다음 레슨(04)에서 디코딩된 프레임의 pts가 다시 단조 증가하는 것으로 확인된다.
