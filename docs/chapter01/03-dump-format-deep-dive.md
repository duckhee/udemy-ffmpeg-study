# 03. 포맷 정보 덤프와 자원 반환 — 코드 상세 해설

> [← 기본 문서](03-dump-format.md)

## 전체 구조

02번과 뼈대가 같다. `main()` + `GetFilePath()` 구성에 `av_dump_format` / `avformat_close_input` 두 호출이 추가된 형태다.

## 코드 블록별 해설

### 경로 계산과 존재 확인

02번과 동일한 `GetFilePath("murage.mp4", filePath)` → `fopen` 패턴이다. 헬퍼의 상세 동작은 [02번 딥다이브](02-load-resource-deep-dive.md)를 참고한다. 다만 이 레슨에서는 `fopen` 성공 직후에 출력이 하나 끼어 있다.

```c
    /** FFMPEG Error Code */
    int errorValue = 0;
    printf("File Open Success\r\n");
    /** Format Context */
    AVFormatContext *formatContext = NULL;
```

이 `printf`가 첫 번째 `File Open Success`다(특이점 참고).

### 컨테이너 열기

```c
    /** get AV File Format -> 가져온 정보를 가지고 AVFormatContext를 만들어준다. */
    errorValue = avformat_open_input(&formatContext, filePath, NULL, NULL);
    if (errorValue < 0) {
        printf("Get Failed to open file\r\n");
        av_log(NULL, AV_LOG_ERROR, "Error Opening File");
        return -1;
    }

    printf("File Open Success\r\n");
```

02번과 같은 호출이다. 여기서 두 번째 `File Open Success`가 출력된다.

### 포맷 덤프

```c
    /**
     * ffmpeg -i <파일> 기능에 대한 정보를 가져 오는 코드
     * */
    av_dump_format(formatContext, 0, filePath, 0);
```

인자 해석:

| 인자 | 값 | 의미 |
|---|---|---|
| `ic` | `formatContext` | 덤프할 컨텍스트 |
| `index` | `0` | 출력 라벨에 쓰일 인덱스 (`Input #0`) |
| `url` | `filePath` | 출력에 표시할 파일 이름 |
| `is_output` | `0` | 0 = 입력(demuxer) 컨텍스트 |

### 자원 반환

```c
    /** context 에 대한 resource 반환 */
    avformat_close_input(&formatContext);

    fclose(pFile);

    return 0;
```

`avformat_close_input`은 이중 포인터를 받아 해제 후 `*ps = NULL`로 만든다. 이 덕분에 댕글링 포인터 사용을 예방할 수 있다.

## 심화

### av_dump_format 출력 읽는 법

`murage.mp4` 기준으로 다음과 같은 구조가 출력된다.

```
Input #0, mov,mp4,m4a,3gp,3g2,mj2, from '/.../resources/murage.mp4':
  Metadata:
    major_brand     : ...
    ...
  Duration: 00:00:xx.xx, start: 0.000000, bitrate: xxxx kb/s
  Stream #0:0[0x1](und): Video: h264 (...), yuv420p, WxH, ...
  Stream #0:1[0x2](und): Audio: aac (...), 44100 Hz, stereo, ...
```

- 첫 줄의 `mov,mp4,m4a,3gp,3g2,mj2`는 하나의 demuxer가 처리하는 포맷 계열 전체 이름이다.
- `Duration`, `bitrate`는 이 시점에는 헤더 기반 추정치다. 이 값을 코드에서 직접 읽는 것은 05번에서 다룬다.
- `Stream #0:0` 표기는 `입력번호:스트림인덱스`다. 스트림을 코드로 순회하는 것은 07번에서 다룬다.

주의: 이 함수는 정보를 **출력만** 할 뿐 값을 반환하지 않는다. 프로그램에서 duration 등을 사용하려면 구조체 필드를 직접 읽어야 한다.

### avformat_close_input이 해제하는 것들

- demuxer 내부 상태 (`priv_data`)
- 모든 `AVStream`과 그 `codecpar`, 메타데이터
- 컨텍스트 자체의 metadata `AVDictionary`
- avio 컨텍스트 (열린 파일 핸들)

즉 `avformat_open_input` 성공 이후 얻은 컨텍스트 산하의 모든 것이 한 번에 정리된다. 별도로 `avformat_free_context`를 다시 부를 필요가 없다(05번 특이점 참고).

## ⚠️ 코드 특이점 상세

- **`File Open Success` 중복 출력**: `fopen` 확인 직후와 `avformat_open_input` 성공 직후에 같은 메시지가 두 번 출력된다. 02번 코드를 복사하며 `printf` 위치를 옮기다 남은 것으로 보인다. 서로 다른 단계이므로 메시지를 구분(`fopen OK` / `avformat open OK`)하거나 하나를 제거하는 것이 올바른 형태다.
- `av_log(NULL, AV_LOG_ERROR, "Error Opening File")` — 02번과 달리 개행(`\r\n`)이 없어서 에러 시 다음 출력과 한 줄에 붙는다.
- 02번과 마찬가지로 `fopen`/`fclose`는 존재 확인용 중복 열기다.
- 디렉터리에 `REAME.md`라는 오타 파일명이 존재한다(내용상 README 용도).
