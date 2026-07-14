# 08. 올바른 메모리 해제 — 코드 상세 해설

> [← 기본 문서](08-freeing-memory.md)

## 전체 구조

레슨 07과 동일한 골격에서 패킷 루프만 바뀌었다. 차이는 세 줄이다: `int packetCount = 0;` 선언, 루프 내 `av_packet_unref(pAvPacket);`, 그리고 `packetCount == 20`에서의 `break`.

## 코드 블록별 해설

### 패킷 루프 — unref와 20패킷 제한

```c
    /** packet에 대한 갯수를 확인하기 위한 변수 */
    int packetCount = 0;

    while (av_read_frame(pAvFormatContext, pAvPacket) >= 0) {
        /** Video Frame 읽어오기 */
        if (pAvPacket->stream_index == videoStreamChannelIdx) {
            printf("Found a Video packet\r\n");
            /** Decode Frame */
//            errorCode = avcodec_send_packet(pVideoCodecContext, pAvPacket);
        }
            /** Audio 정보를 읽어오기 */
        else if (pAvPacket->stream_index == audioStreamChannelIdx) {
            printf("Found a Audio packet\r\n");
        }
        /** 사용한 packet에 대한 정리 -> 연결 정보에 대한 초기화 및 포인터 초기화 */
        av_packet_unref(pAvPacket);
        packetCount++;
        if (packetCount == 20) {
            break;
        }
    }

    printf("Read Done\r\n");
```

`av_packet_unref()`는 분기(비디오/오디오/기타) 밖, 루프 본문의 마지막에 있다. 어느 스트림의 패킷이든 반복이 끝나기 전 반드시 참조를 놓는다는 뜻으로, unref 호출 위치의 정석이다. `packetCount`는 unref 직후 증가하므로 스트림 종류와 무관하게 총 20개 패킷을 읽으면 `break` 한다. `av_read_frame()`이 음수(EOF)를 반환해도 루프가 끝나므로, 20패킷보다 짧은 파일도 안전하다.

### 해제부 (레슨 07과 동일)

```c
    release:
    if (pAudioCodecContext != NULL)
        avcodec_free_context(&pAudioCodecContext);

    if (pVideoCodecContext != NULL)
        avcodec_free_context(&pVideoCodecContext);
    avformat_close_input(&pAvFormatContext);
    av_packet_free(&pAvPacket);
    av_frame_free(&pAvFrame);
//    av_free(pAvFrame);
    return 0;
```

이 시점에서 chapter02 전반부의 자원 관리가 완성된다.

| 자원 | 생성 | 해제 |
|---|---|---|
| 오디오 `AVCodecContext` | `avcodec_alloc_context3()` | `avcodec_free_context()` |
| 비디오 `AVCodecContext` | `avcodec_alloc_context3()` | `avcodec_free_context()` |
| `AVFormatContext` | `avformat_open_input()` | `avformat_close_input()` |
| `AVPacket` | `av_packet_alloc()` | `av_packet_free()` (+ 루프 내 `av_packet_unref()`) |
| `AVFrame` | `av_frame_alloc()` | `av_frame_free()` |

코덱 컨텍스트를 포맷 컨텍스트보다 먼저 해제하는 순서는 "생성의 역순" 원칙을 따른 것이다. 두 코덱 컨텍스트는 `avcodec_parameters_to_context()`로 파라미터를 **복사**받았기 때문에 포맷 컨텍스트와 수명이 독립적이지만, 역순 해제를 습관화하면 자원 간 의존이 생겨도 안전하다.

## 심화

### 참조 카운팅과 av_packet_unref의 의미

`AVPacket.buf`는 `AVBufferRef *`로, 실제 압축 데이터 버퍼(`AVBuffer`)에 대한 참조다. `AVBuffer`는 원자적 참조 카운터를 가지며, 참조가 0이 되는 순간 버퍼가 해제된다.

- `av_read_frame()`: 디먹서가 만든 버퍼의 참조를 패킷에 넘겨준다 (호출자가 소유권을 가짐).
- `av_packet_unref()`: 그 참조를 놓아 카운터를 내리고(0이면 버퍼 해제), 패킷 필드를 초기 상태로 되돌린다. 구조체 자체는 남으므로 다음 `av_read_frame()`에 재사용할 수 있다.
- `av_packet_free()`: 내부에서 unref를 수행한 뒤 구조체까지 해제하고 포인터를 `NULL`로 만든다.

이 설계 덕분에 패킷을 큐에 넣거나 다른 스레드로 넘길 때 `av_packet_ref()`로 참조만 늘리면 되고, 데이터 복사가 일어나지 않는다. 디코더에 패킷을 넘기는 `avcodec_send_packet()`도 내부적으로 참조를 잡기 때문에, 호출 직후 호출자가 unref해도 안전하다.

### unref를 빼먹으면 어떻게 되나

`av_read_frame()`의 규약상 반환된 패킷은 호출자가 더 이상 필요 없을 때 `av_packet_unref()`로 놓아야 한다. 레슨 06~07처럼 unref 없이 루프를 돌리면 이전 반복에서 받은 버퍼의 참조가 제때 정리되지 않아, 규약 위반이자 (FFmpeg 버전에 따라) 패킷 수만큼 누적되는 메모리 누수가 된다. 몇 초짜리 학습용 파일에서는 티가 안 나지만, 장시간 스트림을 읽는 프로그램이라면 치명적이다. 레슨 01 주석의 도구(`valgrind`, `leaks --atExit`)로 레슨 07과 08을 비교 실행해 보면 차이를 직접 확인할 수 있다.

### 왜 20패킷인가

디버깅 편의다. out.mp4 전체를 읽으면 수백~수천 개의 패킷 로그가 쏟아지지만, 20개면 비디오·오디오 패킷이 섞여 나오는 인터리빙 패턴과 unref 동작을 확인하기에 충분하다. 실전 코드라면 이 자리에서 EOF까지 읽은 뒤 디코더 플러시(드레이닝)를 수행한다.

## ⚠️ 코드 특이점 상세

### packetCount는 전체 패킷 기준

카운터가 비디오/오디오 분기 밖에서 증가하므로 "비디오 패킷 20개"가 아니라 "읽은 패킷 총 20개"에서 멈춘다. 의도가 스트림별 제한이라면 분기 안에서 세어야 한다.

### 이전 레슨에서 상속된 특이점 (이 레슨에서도 미수정)

- `pCurrentStream[streamIdx].r_frame_rate` 포인터 인덱싱 버그 (레슨 05 딥다이브).
- `videoStreamChannelIdx` / `audioStreamChannelIdx` 초기값 0으로 인한 죽은 미발견 검사, 그리고 `videoStreamChannelIdx < 0` 경로의 해제 없는 `return -1` (레슨 07 딥다이브).
- 오디오 정보 출력의 `Channel :` 라벨에 스트림 인덱스 출력 (레슨 07 딥다이브).
- 비디오 코덱 컨텍스트 3단계의 로그-온리 에러 처리 (레슨 06 딥다이브).
- `FFMPEG_ERROR` 매크로는 chapter02 전반부 내내 정의만 있고 사용되지 않았다.
