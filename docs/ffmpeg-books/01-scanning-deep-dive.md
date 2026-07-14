# 01. 스캐닝 — 코드 상세 해설

> [← 기본 문서](01-scanning.md)

## 전체 구조

함수는 두 개뿐이다. 아직 구조체 도입 이전 단계라 모든 FFmpeg 로직이 `main`에 있다.

| 함수 | 역할 |
|---|---|
| `main` | 입력 경로 결정 → 컨테이너 열기 → 스트림 정보 획득 → 스트림 순회 출력 → 정리 |
| `GetResourcePath` | 현재 작업 디렉터리에서 저장소 루트를 역산해 `resources/<name>` 절대 경로를 만든다 |

## 코드 블록별 해설

### 1) 로그 레벨 설정

```c
/** Log에 대한 사용할 레벨을 디버깅 레벨로 설정 한다. -> Debug 정보에 대해서 출력을 해준다. */
av_log_set_level(AV_LOG_DEBUG);
```

FFmpeg의 전역 로그 레벨을 DEBUG로 올린다. `avformat_open_input`이 파일 포맷을 프로브(probe)하는 과정, 스트림 파싱 내부 동작까지 콘솔에 쏟아지므로 학습용으로 유용하다. 기본값은 `AV_LOG_INFO`다.

### 2) 입력 경로 결정 — argv 지원

```c
if (argc < 2) {
    printf("usage : %s <input>\r\n", argv[0]);
    if (!GetResourcePath("out.mp4", videoPath)) {
        printf("Failed to get Resource Path...\r\n");
        return -1;
    }
}
    /** user input program set */
else {
    printf("usage : %s\r\n", argv[0]);
    strcpy(videoPath, argv[1]);
}
```

인자가 없으면 기본 리소스 `resources/out.mp4`를 사용하고, 있으면 `argv[1]`을 그대로 복사한다. usage 문자열이 두 분기 모두에서 출력되는데, 인자가 없을 때 `<input>` 안내가 나오고 인자가 있을 때는 프로그램 이름만 출력하는 형태로 뒤집혀 있어 다소 어색하지만 동작에는 문제가 없다.

### 3) 컨테이너 열기

```c
if ((errorCode = avformat_open_input(&pAvFormatContext, videoPath, NULL, NULL)) < 0) {
    printf("Failed Could not open input file : %s\r\n", videoPath);
    av_log(NULL, AV_LOG_ERROR, "[FFMPEG_ERROR %d] Failed Get Video Context\r\n", errorCode);
    return -1;
}
```

`pAvFormatContext`가 `NULL`인 상태로 넘기면 FFmpeg가 내부에서 `AVFormatContext`를 할당한다. 3·4번째 인자(`AVInputFormat`, `AVDictionary`)를 `NULL`로 두면 포맷 자동 감지 + 기본 옵션이다. 반환값이 음수면 실패이며 음수 값 자체가 `AVERROR` 에러 코드다.

### 4) 스트림 정보 획득

```c
if ((errorCode = avformat_find_stream_info(pAvFormatContext, NULL)) < 0) {
    printf("Failed to retrive input stream information\r\n");
    av_log(NULL, AV_LOG_ERROR, "[FFMPEG_ERROR %d] Failed Found Stream ....\r\n", errorCode);
    return -2;
}
```

컨테이너 헤더만으로는 코덱 파라미터가 불완전할 수 있어, 실제 패킷을 일부 읽어 각 스트림의 `codecpar`를 채운다. 이 호출 이후에야 해상도·샘플레이트 같은 값을 신뢰할 수 있다.

### 5) 스트림 순회와 타입 분류

```c
for (int index = 0; index < pAvFormatContext->nb_streams; index++) {
    /** Codec에 대한 파라미터에 대한 정보를 가져오기 */
    AVCodecParameters *pAvCodecParameters = NULL;
    pAvCodecParameters = pAvFormatContext->streams[index]->codecpar;
    /** 스트림의 타입이 비디오 타입일 경우 */
    if (AVMEDIA_TYPE_VIDEO == pAvCodecParameters->codec_type) {
        printf("-------------- Video Information --------------\r\n");
        printf("codec_id : %d\r\n", pAvCodecParameters->codec_id);
        printf("bit rate : %lld\r\n", pAvCodecParameters->bit_rate);
        printf("Image size : [%dx%d]\r\n", pAvCodecParameters->width, pAvCodecParameters->height);
        ...
```

`codec_type`으로 `AVMEDIA_TYPE_VIDEO` / `AVMEDIA_TYPE_AUDIO` / `AVMEDIA_TYPE_SUBTITLE`을 분기한다. 비디오는 해상도, 오디오는 샘플레이트와 채널 수(`ch_layout.nb_channels` — FFmpeg 5.1 이후의 신규 채널 레이아웃 API)를 출력한다. 자막 분기는 헤더 라인만 출력하는 빈 껍데기다.

### 6) 정리

```c
FFMPEG_RELEASE:

if (pAvFormatContext != NULL) {
    avformat_close_input(&pAvFormatContext);
}
```

`avformat_close_input`은 컨텍스트를 닫고 포인터를 `NULL`로 만든다.

### 7) GetResourcePath — 리소스 경로 역산

```c
#if defined(WIN32) || defined(WIN64)
    GetCurrentDirectory(BUFFER_MAX, tempBuffer);
    pRemoveStart = strstr(tempBuffer, "\\cmake");
#else
    realpath(".", tempBuffer);
    pRemoveStart = strstr(tempBuffer, "/cmake");
#endif
```

현재 작업 디렉터리의 절대 경로에서 `"/cmake"`(Windows는 `"\cmake"`) 부분 문자열을 찾아, 그 앞까지를 저장소 루트로 간주하고 `"/resources/" + name`을 이어 붙인다. CLion 기본 빌드 디렉터리 `cmake-build-debug`에서 실행하는 것을 전제로 한 트릭이다.

## 심화 — 프로브와 스트림 스캔

`avformat_open_input`은 파일 시그니처를 읽어 디먹서(demuxer)를 고르고, `avformat_find_stream_info`는 짧게 디먹싱·부분 디코딩을 수행해 스트림 메타데이터를 확정한다. 이후 레슨의 모든 프로그램이 이 두 호출을 전제로 시작한다.

```mermaid
flowchart LR
    A["파일 (out.mp4)"] --> B["avformat_open_input<br/>포맷 프로브"]
    B --> C["avformat_find_stream_info<br/>스트림 메타데이터 확정"]
    C --> D["streams[0..nb_streams-1]<br/>codecpar 접근 가능"]
```

## ⚠️ 코드 특이점 상세

### 1) 사용되지 않는 `FFMPEG_RELEASE:` 레이블 — 에러 경로 메모리 누수

레이블은 선언되어 있지만 `goto FFMPEG_RELEASE;` 구문이 코드 어디에도 없다. 그 결과 `avformat_find_stream_info`가 실패하면 `return -2;`로 즉시 종료하는데, 이 시점에 `pAvFormatContext`는 이미 `avformat_open_input`으로 할당된 상태라 닫히지 않고 누수된다. 올바른 형태는 에러 분기에서 `goto FFMPEG_RELEASE;`로 점프해 공통 정리 코드를 태우고 에러 코드를 반환하는 것이다. (03번 레슨의 `goto RELEASE;` 패턴이 이 방향으로 개선된 형태다.)

### 2) `GetResourcePath`의 `"/cmake"` 의존

경로 문자열에 `cmake`가 없는 위치(예: 소스 디렉터리, `build/` 라는 이름의 빌드 폴더)에서 실행하면 `strstr`이 `NULL`을 반환해 기본 리소스 로딩이 실패한다. 이 레슨은 argv로 우회할 수 있지만, 02~04번은 argv 지원이 없어 반드시 `cmake`가 경로에 포함된 디렉터리에서 실행해야 한다.

### 3) `bit_rate` 포맷 지정자

`pAvCodecParameters->bit_rate`는 `int64_t`인데 `%lld`(long long)로 출력한다. 대부분의 데스크톱 플랫폼에서는 동작하지만 이식성 관점의 정석은 `<inttypes.h>`의 `%" PRId64 "`다.

### 4) 주석 처리된 바깥 스코프 선언

`main` 상단에 `AVCodecParameters *pAvCodecParameters`가 주석으로 남아 있고, 실제 선언은 루프 안으로 옮겨졌다. 스트림마다 새로 받아오는 포인터이므로 루프 내부 선언이 맞으며, 리팩터링 흔적이다.

---
[← 기본 문서](01-scanning.md) · [개요](README.md)
