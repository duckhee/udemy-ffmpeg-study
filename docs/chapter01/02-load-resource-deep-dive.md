# 02. 컨테이너 파일 열기 — 코드 상세 해설

> [← 기본 문서](02-load-resource.md)

## 전체 구조

| 구성 요소 | 역할 |
|---|---|
| `main()` | 경로 계산 → 존재 확인 → `avformat_open_input` 호출 |
| `GetFilePath()` | CWD에서 저장소 루트를 역산해 `resources/<name>` 절대 경로 생성 |
| `BUFFER_MAX` (1024) | 경로 버퍼 크기 매크로 |

## 코드 블록별 해설

### 헤더와 플랫폼 분기

```c
#if defined(WIN32) || defined(WIN64)

#include <Windows.h>

#endif

#define BUFFER_MAX                      1024

bool GetFilePath(const char *name, char *pathBuffer);
```

Windows에서만 `Windows.h`를 포함한다(`GetCurrentDirectory` 사용을 위해). `stdbool.h`를 포함했으므로 C에서도 `bool`/`true`/`false`를 쓸 수 있다.

### 경로 계산과 존재 확인

```c
    char filePath[BUFFER_MAX] = {0,};
    FILE *pFile = NULL;

    if (!GetFilePath("murage.mp4", filePath)) {
        printf("Failed File Path ...\r\n");
        return -1;
    }

    pFile = fopen(filePath, "rb");
    if (pFile == NULL) {
        printf("Failed File Open ... %s\r\n", filePath);
        return -1;
    }
```

`fopen`은 FFmpeg 호출 전에 파일 존재를 확인하는 용도다. FFmpeg은 자체 I/O 계층(avio)으로 파일을 다시 열기 때문에 이 핸들은 미디어 처리에 관여하지 않는다.

### avformat_open_input 호출

```c
    /** FFMPEG Error Code */
    int errorValue = 0;

    /** Format Context */
    AVFormatContext *formatContext = NULL;

    /** get AV File Format -> 가져온 정보를 가지고 AVFormatContext를 만들어준다. */
    errorValue = avformat_open_input(&formatContext, filePath, NULL, NULL);
    if (errorValue < 0) {
        printf("Get Failed to open file\r\n");
        av_log(NULL, AV_LOG_ERROR, "Error Opening File\r\n");
        return -1;
    }

    printf("File Open Success\r\n");
```

핵심 포인트:

- `formatContext`를 반드시 `NULL`로 초기화해야 한다. `avformat_open_input`은 `*ps`가 `NULL`이면 내부에서 `avformat_alloc_context()`로 할당하고, `NULL`이 아니면 이미 할당된 컨텍스트로 간주한다. 초기화하지 않은 쓰레기 값이 들어가면 미정의 동작이다(06번 레슨에서 실제로 이 초기화가 빠진다).
- 3번째 인자(`AVInputFormat *`)가 `NULL`이므로 파일 헤더를 읽어 포맷을 자동 감지한다.
- `av_log(NULL, AV_LOG_ERROR, ...)`는 FFmpeg 로그 시스템을 사용한다. 첫 인자는 로그 컨텍스트(보통 AVClass를 가진 구조체)이며 `NULL`이면 전역 로그로 출력된다.

### 종료부

```c
    fclose(pFile);

    return 0;
```

`fclose`로 존재 확인용 핸들만 닫는다. `formatContext`는 닫지 않는다(아래 특이점 참고).

### GetFilePath — 리소스 경로 헬퍼 (챕터 공통 패턴)

이 함수는 이후 모든 레슨에 (이름만 바뀌어) 복사되는 공통 헬퍼이므로 여기서 자세히 본다.

```c
bool GetFilePath(const char *name, char *pathBuffer) {
    char tempBuffer[BUFFER_MAX] = {0,};
    char executeBuffer[BUFFER_MAX] = {0,};
    char *pRemoveStart = NULL;
    int removeEndIdx = 0;

#if defined(WIN32) || defined(WIN64)
    GetCurrentDirectory(BUFFER_MAX, executeBuffer);
    pRemoveStart = strstr(executeBuffer, "\\cmake");
#else
    realpath(".", executeBuffer);
    pRemoveStart = strstr(executeBuffer, "/cmake");
#endif

    if (pRemoveStart == NULL) {
        printf("Failed Get Path...\r\n");
        return false;
    }

    removeEndIdx = (int) (pRemoveStart - executeBuffer);

    memcpy(tempBuffer, executeBuffer, sizeof(char) * removeEndIdx);

#if defined(WIN32) || defined(WIN64)
    strcat(tempBuffer, "\\resources\\");
#else
    strcat(tempBuffer, "/resources/");
#endif

    strcat(tempBuffer, name);
    strcpy(pathBuffer, tempBuffer);
    return true;
}
```

동작 단계:

1. `realpath(".", ...)` — 현재 작업 디렉터리의 절대 경로를 얻는다. 예: `/Users/.../udemy-ffmpeg-study/cmake-build-debug/chapter01/02_load-resource-ffmpeg`
2. `strstr(..., "/cmake")` — 경로에서 `/cmake` 문자열이 처음 나오는 위치를 찾는다. CLion 기본 빌드 디렉터리 이름이 `cmake-build-debug`라는 점에 의존한다.
3. 그 앞부분(= 저장소 루트)만 `memcpy`로 복사하고 `/resources/` + 파일명을 이어 붙인다.

결과: `/Users/.../udemy-ffmpeg-study/resources/murage.mp4`

**제약**: CWD 경로에 `/cmake`가 없으면 실패한다. 즉 저장소 루트에서 `./cmake-build-debug/.../chapter0102LoadResource`처럼 실행하면 CWD가 루트라서 `Failed Get Path...`가 뜬다. 반드시 빌드 디렉터리 내부로 `cd` 한 뒤 실행해야 한다. 또한 저장소를 `cmake`라는 문자열이 포함된 상위 경로에 두면 잘못된 위치를 자를 수 있다.

## 심화

### avformat_open_input이 하는 일

1. `avformat_alloc_context()`로 컨텍스트 할당 (인자가 `NULL`일 때)
2. avio(프로토콜 계층)로 URL을 연다 — 로컬 파일이면 `file:` 프로토콜
3. 파일 앞부분을 읽어 등록된 demuxer들의 `read_probe`로 포맷 점수를 매겨 자동 감지
4. 선택된 demuxer의 `read_header`로 헤더를 파싱해 스트림 목록의 뼈대를 구성

이 시점에는 헤더 정보만 있으므로, 정확한 duration·프레임레이트 등은 아직 신뢰할 수 없는 경우가 있다. 그래서 05번에서 `avformat_find_stream_info`가 추가로 필요하다.

### AVERROR 에러 코드

FFmpeg의 음수 반환값은 `AVERROR(ENOENT)`처럼 POSIX errno를 음수로 감싼 값이거나 `AVERROR_INVALIDDATA` 같은 FFmpeg 고유 코드다. `av_strerror()` 또는 `av_err2str()` 매크로로 사람이 읽을 수 있는 문자열로 바꿀 수 있다(이 레슨에서는 사용하지 않는다).

## ⚠️ 코드 특이점 상세

- **`avformat_close_input(&formatContext)`가 없다.** `avformat_open_input`이 성공하면 반드시 짝으로 `avformat_close_input`을 호출해야 한다. 이 레슨에서는 프로그램이 곧바로 종료되어 OS가 메모리를 회수하므로 실행상 문제는 없지만, 장시간 실행되는 프로그램이라면 명백한 메모리·파일 핸들 누수다. 올바른 형태는 `fclose(pFile);` 앞에 `avformat_close_input(&formatContext);`를 추가하는 것이며, 실제로 03번 레슨에서 이 호출이 도입된다.
- `fopen`으로 연 핸들은 FFmpeg과 무관한 존재 확인용이다. 같은 파일이 두 번 열리는 셈이며(READ 모드라 무해), FFmpeg 에러 코드로도 파일 부재를 판별할 수 있으므로 필수적인 코드는 아니다.
- `memory.h`는 비표준 헤더로 `string.h`와 중복이다(대부분 플랫폼에서 `string.h`를 다시 포함).
