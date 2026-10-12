# dup_frame_detector

[한국어](#한국어) · [English](#english)

---

## 한국어

동영상에서 **반복되는 구간(loop)** 을 찾아 하이라이트하고 반복 재생하는 데스크톱 프로그램입니다.

사용자가 기준 프레임 `S`를 고르면, `S+1, S+2, ...` 순으로 비교하다가 `S`와 같은 프레임 `E`를 만나면 멈춥니다.
반복 구간은 **`[S, E-1]`** 입니다 (`E`는 다음 반복의 시작이므로 제외).

### 주요 기능

- 기준 프레임 `S` 선택 → 순차 탐색 → 일치 시 즉시 중단
- 찾은 구간 `[S, E-1]`을 타임라인에 하이라이트, 구간 루프 재생
- 찾은 루프를 원본 옆에 같은 크기로 표시, 불투명도 슬라이더와 색상 필터로 원본 위에 겹쳐 보기
- **마스크 브러시**: 칠한 영역은 비교에서 제외 (지우개 지원)
- **일치도(임계값)** 0~100% 슬라이더
- **비교 방식 선택**
  | 방식 | 특징 |
  |---|---|
  | MAE (기본) | `1 - mean(absdiff)/255`. 픽셀 값이 거의 같아야 일치. 무손실/고화질 영상에 적합 |
  | SSIM | 구조 기준. 압축 노이즈·블러에 강함 |
  | NCC | 밝기·대비 변화 무시. 페이드·자동 노출 변화에 강함 |

  방식마다 스케일이 달라, 방식을 바꾸면 임계값이 해당 방식의 기본값으로 리셋됩니다.
- 구간 무손실 추출 (Matroska), 현재 프레임 PNG 저장, 원본 픽셀 포맷 그대로 고품질 PNG 저장
- 한국어 / English UI 전환

### 단축키

| 키 | 동작 |
|---|---|
| `W` / `→` | 다음 프레임 |
| `B` / `←` | 이전 프레임 |
| `Shift+W` | 5프레임 앞으로 |
| `Shift+B` | 5프레임 뒤로 |
| `Space` | 구간 반복 재생 / 정지 (루프를 찾은 뒤) |
| `O` / `Tab` | 겹쳐 보기 켜기/끄기 (루프를 찾은 뒤) |
| `F` | 재생 속도 한 단계 빠르게 |
| `D` | 재생 속도 한 단계 느리게 |
| `F1` | 도움말 |

타임라인은 클릭/드래그로 프레임 이동, 마스크는 왼쪽 마우스 버튼으로 칠합니다.

### 동작 방식

- 비교 대상은 항상 **기준 프레임 S vs 현재 프레임 i** (인접 프레임끼리 비교 아님).
- 그레이스케일 기준, 마스크 밖 픽셀만 사용.
- 속도: 1/4 해상도에서 1차 탐색 → 후보만 원본 해상도로 재검증.
- 탐색은 워커 스레드에서 실행되며 진행률 표시와 취소를 지원.
- 끝까지 일치 프레임이 없으면 "반복 없음"으로 보고.
- 프레임 번호는 디코딩 순서가 아닌 프레임 정확(frame-accurate) 인덱스 기준.

### 기술 스택

C++23 · CMake · Qt 6 (UI) · FFmpeg (디코딩, 프레임 정확 seek) · OpenCV (마스크 비교)

### 빌드

의존성: Qt 6 Widgets, FFmpeg (`libavformat libavcodec libavutil libswscale`), OpenCV (`core imgproc`), CMake ≥ 3.25, pkg-config.

NixOS / nix-shell:

```sh
nix-shell --run "cmake -B build -G Ninja && cmake --build build"
./build/dup_frame_detector
```

테스트:

```sh
nix-shell --run "ctest --test-dir build"
```

### 프로젝트 구조

```
src/
  decoder/   FFmpeg 래퍼 (open, seek, next) — Qt 비의존
  matcher/   마스크 비교, 유사도 계산, 루프 탐색 — Qt 비의존
  ui/        Qt 위젯 (뷰어, 마스크 브러시, 타임라인, i18n)
  app/       main
tests/       decoder / matcher / loop / export / UI smoke 테스트
```

---

## English

A desktop app that finds a **repeating section (loop)** in a video, highlights it, and plays it back in a loop.

Pick a reference frame `S`; the app compares `S+1, S+2, ...` against it and stops at the first frame `E` that matches `S`.
The loop is **`[S, E-1]`** (`E` is the start of the next repetition, so it is excluded).

### Features

- Pick reference frame `S` → sequential search → stops at the first match
- Highlights `[S, E-1]` on the timeline and plays the section in a loop
- Shows the found loop next to the original at equal size, or overlays it on the original with an opacity slider and colour filter
- **Mask brush**: painted areas are excluded from comparison (eraser included)
- **Threshold** slider, 0–100%
- **Selectable metric**
  | Metric | Notes |
  |---|---|
  | MAE (default) | `1 - mean(absdiff)/255`. Pixel values must be nearly identical. Good for lossless / high-quality video |
  | SSIM | Structure-based. Robust to compression noise and blur |
  | NCC | Ignores brightness/contrast shifts. Robust to fades and auto-exposure |

  Scales differ per metric, so changing the metric resets the threshold to that metric's default.
- Lossless section export (Matroska), current-frame PNG save, and high-quality PNG save in the source pixel format
- Korean / English UI switch

### Hotkeys

| Key | Action |
|---|---|
| `W` / `→` | Next frame |
| `B` / `←` | Previous frame |
| `Shift+W` | Forward 5 frames |
| `Shift+B` | Back 5 frames |
| `Space` | Play / stop the loop (after a loop is found) |
| `O` / `Tab` | Toggle overlay (after a loop is found) |
| `F` | Playback speed up one step |
| `D` | Playback speed down one step |
| `F1` | Help |

Click or drag on the timeline to seek; paint the mask with the left mouse button.

### How it works

- Always compares **reference frame S vs. current frame i** (not adjacent frames).
- Grayscale comparison, using only pixels outside the mask.
- Speed: first pass at 1/4 resolution, then only candidates are re-verified at full resolution.
- Search runs on a worker thread with progress reporting and cancellation.
- Reports "no loop" if no matching frame is found before the end.
- Frame numbers are frame-accurate indices, not decode order.

### Tech stack

C++23 · CMake · Qt 6 (UI) · FFmpeg (decoding, frame-accurate seek) · OpenCV (masked comparison)

### Build

Dependencies: Qt 6 Widgets, FFmpeg (`libavformat libavcodec libavutil libswscale`), OpenCV (`core imgproc`), CMake ≥ 3.25, pkg-config.

NixOS / nix-shell:

```sh
nix-shell --run "cmake -B build -G Ninja && cmake --build build"
./build/dup_frame_detector
```

Tests:

```sh
nix-shell --run "ctest --test-dir build"
```

### Layout

```
src/
  decoder/   FFmpeg wrapper (open, seek, next) — Qt-free
  matcher/   masked comparison, similarity, loop search — Qt-free
  ui/        Qt widgets (viewer, mask brush, timeline, i18n)
  app/       main
tests/       decoder / matcher / loop / export / UI smoke tests
```
