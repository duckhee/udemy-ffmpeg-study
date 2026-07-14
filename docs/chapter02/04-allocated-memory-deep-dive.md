# 04. AVPacket / AVFrame 메모리 할당과 스트림 개수 확인 — 코드 상세 해설

> [← 기본 문서](04-allocated-memory.md)

## 전체 구조

| 구성 요소 | 내용 |
|---|---|
| `FFMPEG_ERROR` 매크로 | 레슨 03과 동일 (여전히 미사용) |
| `main()` | 열기 → 스트림 정보 수집 → 패킷/프레임 할당 → nb_streams 출력 → 해제 |
| `GetResourcePath()` | 리소스 경로 헬퍼 (레슨 01 참고) |

## 코드 블록별 해설

### 스트림 정보 수집

```c
    /** Get FFMPEG Stream Data Get */
    errorCode = avformat_find_stream_info(pAvFormatContext, NULL);
    if (errorCode != 0) {
        printf("Failed find [%s] Stream Information ...\r\n", resourcePath);
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] find stream [%s]\r\n", errorCode, resourcePath);

        avformat_close_input(&pAvFormatContext);
        return -1;
    }
```

`avformat_open_input()`이 만든 스트림 목록에 코덱 파라미터 등 상세 정보를 채운다. mp4처럼 헤더(moov 박스)에 정보가 잘 담긴 컨테이너는 빠르게 끝나지만, MPEG-TS처럼 자기 기술이 약한 포맷은 실제 패킷을 읽고 디코딩해 보면서 정보를 추론하므로 시간이 걸릴 수 있다. 에러 시 이미 열린 컨텍스트를 `avformat_close_input()`으로 닫고 반환하는 정리 순서가 지켜졌다. 다만 반환값 검사가 `!= 0`인 점은 특이점이다 (아래 참고).

### 패킷 할당

```c
    /** Allocated packet data in memory */
    /** av_packet_alloc() 함수는 기본 필드 값을 가져와서 메모리에 올려주기 때문에 추후에 packet 의 필드에 접근 시 기본 값을 확인을 할 수 있다. */
    pAvPacket = av_packet_alloc();
    if (pAvPacket == NULL) {
        printf("Failed Load packet data...\r\n");
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] allocating packet [%s]\r\n", errorCode, resourcePath);
        avformat_close_input(&pAvFormatContext);
        return -1;
    }
```

`av_packet_alloc()`은 `AVPacket` 구조체를 할당하고 `av_init_packet` 상당의 기본값(data=NULL, size=0 등)으로 초기화한다. 반환값이 `NULL`이면 메모리 부족이다. 이 시점의 패킷은 "빈 그릇"이며, 실제 압축 데이터 버퍼는 레슨 06의 `av_read_frame()`이 채운다.

### 프레임 할당

```c
    /** allocated frame data in memory */
    pAvFrame = av_frame_alloc();
    if (pAvFrame == NULL) {
        printf("Failed Load frame data...\r\n");
        av_log(NULL, AV_LOG_ERROR, "[FFMPEG ERROR][%d] allocating frame [%s]\r\n", errorCode, resourcePath);
        avformat_close_input(&pAvFormatContext);
        return -1;
    }
```

패킷과 동일한 패턴이다. 단, 이 실패 경로는 `pAvPacket`을 해제하지 않는다 (특이점 참고).

### 스트림 개수 출력과 해제

```c
    /** stream 갯수를 출력 */
    printf("number of stream : %d\r\n", pAvFormatContext->nb_streams);


    avformat_close_input(&pAvFormatContext);
    av_packet_free(&pAvPacket);
//    av_frame_free(&pAvFrame);
    av_free(pAvFrame);
    return 0;
```

`nb_streams`는 `streams[]` 배열의 길이다. out.mp4는 h264 비디오(스트림 0) + aac 오디오(스트림 1)로 `2`가 출력된다. 해제부에서 `av_frame_free()`가 주석 처리되고 `av_free()`가 쓰인 점이 이 레슨의 대표 특이점이다.

## 심화

### alloc이 만드는 것과 만들지 않는 것

`av_packet_alloc()` / `av_frame_alloc()`은 **구조체 자체**만 할당한다. 미디어 데이터가 담기는 실제 버퍼는 참조 카운팅되는 `AVBufferRef`로 별도 관리되며, 패킷은 `av_read_frame()`이, 프레임은 디코더가 채운다. 그래서 해제도 2단계다 — 버퍼 참조를 놓는 `av_packet_unref()` / `av_frame_unref()`와, 구조체까지 없애는 `av_packet_free()` / `av_frame_free()` (free 계열은 내부적으로 unref를 먼저 수행한다). 이 구분은 레슨 08의 핵심 주제다.

### avformat_find_stream_info의 반환값

문서상 이 함수는 "성공 시 0 이상"을 반환한다. 즉 양수도 성공이다. 따라서 성공/실패 판별은 `< 0`으로 해야 하며, 이 레슨의 `!= 0` 검사는 양수 반환 시 성공을 실패로 오판할 수 있다. 레슨 05부터 `< 0`으로 바뀐다.

## ⚠️ 코드 특이점 상세

### av_frame_free 대신 av_free 사용

```c
    av_packet_free(&pAvPacket);
//    av_frame_free(&pAvFrame);
    av_free(pAvFrame);
```

`av_free()`는 단순히 구조체가 차지한 메모리만 놓는 범용 해제 함수다. 반면 `av_frame_free()`는 프레임이 참조하는 데이터 버퍼(`AVBufferRef`)를 먼저 unref한 뒤 구조체를 해제하고 포인터를 `NULL`로 만든다. 이 레슨의 프레임은 디코딩 데이터가 담긴 적이 없는 빈 프레임이라 `av_free()`로도 실질적인 누수는 없지만, 프레임에 데이터가 담기는 순간부터는 버퍼가 통째로 누수된다. 올바른 형태는 주석 처리된 `av_frame_free(&pAvFrame)`이며, 레슨 07부터는 실제로 그렇게 바뀐다.

### av_frame_alloc 실패 경로에서 패킷 미해제

`pAvFrame == NULL` 분기는 `avformat_close_input()`만 호출하고 `return -1` 하므로, 직전에 할당한 `pAvPacket`이 해제되지 않는다. 발생 확률이 극히 낮은 경로지만, 올바른 형태는 이 분기에서도 `av_packet_free(&pAvPacket)`을 호출하는 것이다.

### avformat_find_stream_info 검사 조건

위 심화 절 참고 — `!= 0`은 양수 반환(성공)을 에러로 처리할 수 있다. 올바른 형태는 `< 0`.

### 기타

- `nb_streams`는 `unsigned int`인데 `%d`(signed)로 출력한다. 실용상 문제는 없으나 정확한 서식은 `%u`다.
- `FFMPEG_ERROR` 매크로는 이번에도 정의만 있고 사용되지 않는다.
