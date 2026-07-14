# 04. 컨테이너 메타데이터 순회 — 코드 상세 해설

> [← 기본 문서](04-av-dictionary.md)

## 전체 구조

03번과 동일한 골격(`main()` + `GetFilePath()`)에서 `av_dump_format`이 빠지고 메타데이터 순회 루프가 들어왔다.

## 코드 블록별 해설

### 순회용 엔트리 포인터 선언

```c
    /** */
    AVDictionaryEntry *dictionaryEntry = NULL;
```

`main` 상단에서 `NULL`로 초기화한다. 이 초기값 `NULL`이 "처음부터 순회 시작"이라는 의미로 `av_dict_get`의 `prev` 인자에 그대로 쓰인다.

### 경로 계산 → 열기

02·03번과 동일한 `GetFilePath` → `fopen` → `avformat_open_input` 흐름이다. 헬퍼 상세는 [02번 딥다이브](02-load-resource-deep-dive.md) 참고. 이 레슨에서는 `File Open Success`가 한 번만 출력된다(03번의 중복이 사라짐).

### 메타데이터 순회 루프 (이 레슨의 핵심)

```c
    printf("File Open Success\r\n");
    int count = 0;
    /** meta 정보에 대해서 가져 오기 -> 반복해서 값을 가져온다. */
    while ((dictionaryEntry = av_dict_get(formatContext->metadata, "", dictionaryEntry, AV_DICT_IGNORE_SUFFIX))) {
        printf("[%d]%s : %s\r\n", count++, dictionaryEntry->key, dictionaryEntry->value);
    }
```

동작 방식:

1. 첫 호출: `prev = NULL` → 첫 번째 항목 반환
2. 이후 호출: 직전 반환 포인터를 `prev`로 전달 → 그 다음 항목 반환
3. 더 이상 항목이 없으면 `NULL` 반환 → `while` 조건이 거짓이 되어 종료

`""` + `AV_DICT_IGNORE_SUFFIX` 조합이 핵심이다. `AV_DICT_IGNORE_SUFFIX`는 "주어진 키와 앞부분이 일치하면 매칭"으로 동작하는데, 빈 문자열은 모든 키의 접두사이므로 결과적으로 전체 항목이 순서대로 나온다.

`while` 조건식의 대입 결과를 그대로 진리값으로 쓰는 것은 C의 관용구이며, FFmpeg 공식 문서의 예제도 같은 형태다.

### 정리부

```c
    /** context 에 대한 resource 반환 */
    avformat_close_input(&formatContext);

    fclose(pFile);

    return 0;
```

03번에서 도입된 열기/닫기 짝이 그대로 유지된다. `dictionaryEntry`가 가리키던 메모리는 `avformat_close_input`이 딕셔너리와 함께 해제하므로 이후에 접근하면 안 된다.

## 심화

### AVDictionary는 어디에나 있다

`AVDictionary`는 메타데이터 저장뿐 아니라 FFmpeg API 전반의 **옵션 전달 수단**이기도 하다. 예를 들어 `avformat_open_input`의 4번째 인자도 `AVDictionary **options`다. `av_dict_set(&opts, "rtsp_transport", "tcp", 0)`처럼 옵션을 담아 넘기면 demuxer/protocol 동작을 제어할 수 있다. 같은 구조체 하나로 "읽기(메타데이터)"와 "쓰기(옵션)"를 모두 배우는 셈이다.

### 주요 플래그

| 플래그 | 의미 |
|---|---|
| `AV_DICT_MATCH_CASE` | 대소문자 구분 매칭 (기본은 무시) |
| `AV_DICT_IGNORE_SUFFIX` | 키를 접두사로 취급 — 순회에 사용 |
| `AV_DICT_MULTIKEY` | (set 시) 같은 키 중복 허용 |

### 메타데이터의 출처

MP4 컨테이너의 `moov/udta` 박스, `ftyp` 박스의 brand 정보 등이 demuxer에 의해 `major_brand`, `minor_version`, `compatible_brands`, `encoder` 같은 표준화된 키로 매핑된다. 컨테이너 종류마다 지원하는 키가 다르며, FFmpeg은 가능한 한 공통 키 이름으로 정규화한다. 스트림에도 개별 `metadata` 딕셔너리가 있는데(`AVStream->metadata`), 이는 07번 레슨에서 순회한다.

## ⚠️ 코드 특이점 상세

- **CMake `message` 문구의 레슨 번호 오타**: `chapter01/04_AvDictionaryStruct-ffmpeg/CMakeLists.txt`의 `message("chapter01-03 av dictionary struct ffmpeg")`는 03번 CMakeLists를 복사하며 번호를 고치지 않은 것이다. 올바른 문구는 `chapter01-04 ...`다. 구성 단계 로그에만 영향이 있다.
- 02·03번과 동일하게 `fopen`/`fclose` 존재 확인용 중복 열기가 유지된다.
- `av_log(NULL, AV_LOG_ERROR, "Error Opening File")`에 개행이 없는 것도 03번과 동일하다.
- `AVDictionaryEntry *dictionaryEntry` 선언 위의 `/** */` 주석은 내용이 비어 있다(작성 중 누락으로 보임).
