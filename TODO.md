# TODO

상태: `[ ]` 대기 / `[~]` 진행 중 / `[x]` 완료. 한 번에 `[~]`는 하나만.

## 0. 세팅
- [x] AGENTS.md 정리
- [x] CMake 골격 + shell.nix + src/ 구조
- [x] 빌드 확인 (`nix-shell --run "cmake -B build -G Ninja && cmake --build build"`)

## 1. Decoder (`src/decoder`)
- [x] `open` / `close`, 메타(frame_count, fps, size)
- [x] `next()`: 디코드 → BGR `cv::Mat` (`sws_scale`)
- [x] `seek(index)`: 프레임 정확 seek (키프레임 → 목표까지 디코드)
- [x] 단위 테스트: 샘플 영상에서 index N 프레임 일치 확인

## 2. Matcher (`src/matcher`)
- [x] `similarity()` MAE 기반
- [x] `similarity()` 단위 테스트 (동일 → 1.0, 마스크 효과)
- [x] `find_loop()`: S+1부터 순차 비교, 일치 시 중단 → `[S, E-1]`
- [x] 1/4 해상도 1차 탐색 → 후보 원본 재검증
- [x] 진행률 콜백 + 취소
- [x] 비교 방식 선택: MAE / SSIM / NCC (UI 콤보박스, 방식별 기본 임계값)

## 3. UI (`src/ui`)
- [x] 영상 열기 + 프레임 표시
- [x] 프레임 슬라이더 / 기준 프레임 S 선택
- [x] 임계값 슬라이더 (0~100%)
- [x] 마스크 브러시 (칠한 영역 = 비교 제외, 브러시 크기, 지우개)
- [x] 탐색 실행 (워커 스레드) + 진행률/취소
- [x] 타임라인에 `[S, E-1]` 하이라이트
- [x] 구간 루프 재생

## 4. 이후
- [ ] (필요 시) 마스크 저장/불러오기, 오디오, 입력 상한
