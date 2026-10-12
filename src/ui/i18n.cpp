#include "ui/i18n.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace dfd::i18n {

namespace {

struct Entry {
    const char* ko;
    const char* en;
};

// Keys are the exact source strings passed to tr().
const Entry kTable[] = {
    {"열기...",
     "Open..."},
    {"파일 경로 (비어 있으면 탐색기)",
     "File path (empty opens a file dialog)"},
    {"현재 프레임을 S로",
     "Set current frame as S"},
    {"프레임",
     "Frame"},
    {"픽셀 차이 (MAE)",
     "Pixel difference (MAE)"},
    {"픽셀 값이 거의 같아야 일치. 무손실/고화질 영상에 적합",
     "Match only when pixel values are nearly identical. Best for lossless / high-quality video"},
    {"구조 (SSIM)",
     "Structure (SSIM)"},
    {"모양·윤곽 기준. 압축 노이즈, 블러가 있는 영상에 적합",
     "Based on shapes and edges. Best for video with compression noise or blur"},
    {"밝기 무관 (NCC)",
     "Brightness-invariant (NCC)"},
    {"밝기·대비 변화 무시. 페이드, 자동 노출 변화가 있는 영상에 적합",
     "Ignores brightness/contrast changes. Best for video with fades or auto exposure"},
    {"비교 방식",
     "Metric"},
    {"일치도",
     "Threshold"},
    {"브러시",
     "Brush"},
    {"지우개",
     "Eraser"},
    {"마스크 지우기",
     "Clear mask"},
    {"크기",
     "Size"},
    {"탐색",
     "Search"},
    {"E 이후 S 일치 찾기",
     "Find next S match after E"},
    {"S는 그대로 두고, 현재 끝(E) 다음 프레임부터 S와 일치하는 프레임을 찾아 구간 끝을 늘림",
     "Keeps S and searches from the frame after the current end (E) for another frame matching S, extending the loop end"},
    {"취소",
     "Cancel"},
    {"구간 반복 재생",
     "Play loop"},
    {"재생 속도",
     "Playback speed"},
    {"구간 추출 (무손실)",
     "Export loop (lossless)"},
    {"FFV1 / MKV · 원본 픽셀 포맷 그대로 · 디코드 결과가 원본과 비트 단위로 동일",
     "FFV1 / MKV · keeps the source pixel format · decoded result is bit-identical to the source"},
    {"원본 저장 (고품질 PNG)",
     "Save original (high-quality PNG)"},
    {"PNG로 저장",
     "Save as PNG"},
    {"겹쳐 보기",
     "Overlay"},
    {"원본 위에 반복 구간 화면을 반투명으로 겹쳐 표시 (O)",
     "Blend the loop view translucently over the original (O)"},
    {"겹친 화면의 불투명도",
     "Opacity of the overlaid view"},
    {"겹친 화면에 색상 필터 적용 (누를 때마다 없음 → 빨강 → 초록 → 파랑 → 시안). 원본과 다른 색으로 겹쳐 차이를 구분",
     "Apply a colour filter to the overlaid view (cycles none → red → green → blue → cyan). Overlaying in a different colour makes differences easier to see"},
    {"도움말",
     "Help"},
    {"사용법과 단축키 (F1)",
     "Usage and shortcuts (F1)"},
    {"영상 열기",
     "Open video"},
    {"영상 (*.mp4 *.mkv *.mov *.avi *.webm *.gif);;모든 파일 (*)",
     "Video (*.mp4 *.mkv *.mov *.avi *.webm *.gif);;All files (*)"},
    {"<h3>반복 구간 찾기</h3>",
     "<h3>Finding a loop</h3>"},
    {"<p>기준 프레임 <b>S</b>를 고르면 S+1부터 차례로 비교해서 S와 같은 프레임 <b>E</b>를 찾습니다. 반복 구간은 <b>[S, E-1]</b>입니다.</p>",
     "<p>Pick a reference frame <b>S</b> and the program compares S+1, S+2, ... in order until it finds a frame <b>E</b> identical to S. The loop is <b>[S, E-1]</b>.</p>"},
    {"<h4>사용 순서</h4>",
     "<h4>Steps</h4>"},
    {"<li><b>열기...</b>로 영상을 엽니다. 경로 칸에 직접 입력하거나 파일을 창에 끌어다 놓아도 됩니다.</li>",
     "<li>Open a video with <b>Open...</b>. You can also type a path or drop a file onto the window.</li>"},
    {"<li>타임라인을 클릭하거나 프레임 칸/단축키로 원하는 프레임에 이동한 뒤 <b>현재 프레임을 S로</b>를 누릅니다.</li>",
     "<li>Click the timeline, or use the frame box / shortcuts, to go to a frame, then press <b>Set current frame as S</b>.</li>"},
    {"<li>자막, 시계처럼 바뀌는 부분은 화면에 <b>브러시</b>로 칠해 비교에서 뺍니다. <b>지우개</b>로 일부만 지우거나 <b>마스크 지우기</b>로 전부 지웁니다. 마스크를 고치면 이전 결과는 사라집니다.</li>",
     "<li>Paint parts that change (subtitles, clocks) with the <b>brush</b> to exclude them from the comparison. Use the <b>eraser</b> to undo part of it or <b>Clear mask</b> to remove it all. Editing the mask discards the previous result.</li>"},
    {"<li><b>비교 방식</b>과 <b>일치도</b>를 고른 뒤 <b>탐색</b>을 누릅니다.</li>",
     "<li>Choose a <b>metric</b> and <b>threshold</b>, then press <b>Search</b>.</li>"},
    {"<li>찾으면 타임라인에 구간이 표시되고, 화면이 왼쪽 <b>원본</b>과 오른쪽 <b>반복 구간</b>으로 나뉩니다. 탐색 중에는 <b>취소</b>할 수 있고, 끝까지 없으면 \"반복 없음\"이 표시됩니다.</li>",
     "<li>When found, the loop is marked on the timeline and the view splits into the <b>original</b> on the left and the <b>loop</b> on the right. You can <b>cancel</b> while searching; if nothing matches, \"No loop\" is shown.</li>"},
    {"<h4>결과 활용</h4>",
     "<h4>Using the result</h4>"},
    {"<li><b>구간 반복 재생</b>: 원본은 계속 재생되고, 오른쪽 화면은 [S, E-1]만 반복합니다.</li>",
     "<li><b>Play loop</b>: the original keeps playing while the right view repeats only [S, E-1].</li>"},
    {"<li><b>겹쳐 보기</b>: 원본 위에 반복 구간 화면을 반투명으로 겹쳐 두 화면의 차이를 확인합니다. 옆 슬라이더로 불투명도를, <b>색상 필터</b> 버튼으로 겹친 화면의 색(빨강/초록/파랑/시안)을 바꿉니다. 다시 누르면 좌우 분할로 돌아갑니다.</li>",
     "<li><b>Overlay</b>: blends the loop view translucently over the original so you can check the difference. Adjust opacity with the slider, and change the colour of the overlaid view (red/green/blue/cyan) with the <b>colour filter</b> button. Press again to return to the split view.</li>"},
    {"<li><b>E 이후 S 일치 찾기</b>: S는 그대로 두고 E 다음 프레임부터 S와 일치하는 프레임을 더 찾아 구간 끝을 늘립니다.</li>",
     "<li><b>Find next S match after E</b>: keeps S and searches from the frame after E for further frames matching S, extending the loop end.</li>"},
    {"<li><b>구간 추출 (무손실)</b>: 찾은 구간을 FFV1/MKV로 저장합니다.</li>",
     "<li><b>Export loop (lossless)</b>: saves the found loop as FFV1/MKV.</li>"},
    {"<li><b>PNG로 저장</b> / <b>원본 저장 (고품질 PNG)</b>: 현재 프레임을 저장합니다. 고품질 쪽은 영상의 색 행렬/범위와 10비트 이상 색심도를 반영합니다.</li>",
     "<li><b>Save as PNG</b> / <b>Save original (high-quality PNG)</b>: saves the current frame. The high-quality one respects the video's colour matrix/range and colour depth of 10 bits or more.</li>"},
    {"<h4>비교 방식</h4>",
     "<h4>Metrics</h4>"},
    {"<li><b>MAE</b>: 픽셀 값 차이. 무손실/고화질 영상</li>",
     "<li><b>MAE</b>: pixel value difference. Lossless / high-quality video</li>"},
    {"<li><b>SSIM</b>: 구조 기준. 압축 노이즈, 블러가 있는 영상</li>",
     "<li><b>SSIM</b>: structure based. Video with compression noise or blur</li>"},
    {"<li><b>NCC</b>: 밝기·대비 변화 무시. 페이드, 자동 노출</li>",
     "<li><b>NCC</b>: ignores brightness/contrast changes. Fades, auto exposure</li>"},
    {"<p>방식을 바꾸면 일치도가 그 방식의 기본값으로 돌아갑니다. 못 찾으면 일치도를 낮추고, 엉뚱한 곳에서 멈추면 높이세요.</p>",
     "<p>Changing the metric resets the threshold to that metric's default. If nothing is found, lower the threshold; if it stops at the wrong place, raise it.</p>"},
    {"<h4>단축키</h4>",
     "<h4>Shortcuts</h4>"},
    {"<tr><td><b>W</b> / <b>→</b></td><td>다음 프레임</td></tr>",
     "<tr><td><b>W</b> / <b>→</b></td><td>Next frame</td></tr>"},
    {"<tr><td><b>B</b> / <b>←</b></td><td>이전 프레임</td></tr>",
     "<tr><td><b>B</b> / <b>←</b></td><td>Previous frame</td></tr>"},
    {"<tr><td><b>Shift+W</b></td><td>5프레임 앞으로</td></tr>",
     "<tr><td><b>Shift+W</b></td><td>Forward 5 frames</td></tr>"},
    {"<tr><td><b>Shift+B</b></td><td>5프레임 뒤로</td></tr>",
     "<tr><td><b>Shift+B</b></td><td>Back 5 frames</td></tr>"},
    {"<tr><td><b>Space</b></td><td>구간 반복 재생 / 정지</td></tr>",
     "<tr><td><b>Space</b></td><td>Play / stop loop</td></tr>"},
    {"<tr><td><b>O</b> / <b>Tab</b></td><td>겹쳐 보기 켜기/끄기</td></tr>",
     "<tr><td><b>O</b> / <b>Tab</b></td><td>Toggle overlay</td></tr>"},
    {"<tr><td><b>D</b></td><td>재생 속도 한 단계 빠르게</td></tr>",
     "<tr><td><b>D</b></td><td>Playback speed up one step</td></tr>"},
    {"<tr><td><b>F</b></td><td>재생 속도 한 단계 느리게</td></tr>",
     "<tr><td><b>F</b></td><td>Playback speed down one step</td></tr>"},
    {"<tr><td><b>F1</b></td><td>도움말</td></tr>",
     "<tr><td><b>F1</b></td><td>Help</td></tr>"},
    {"%1 로딩중...",
     "Loading %1..."},
    {"영상을 열 수 없습니다: %1",
     "Cannot open video: %1"},
    {"없음",
     "None"},
    {"빨강",
     "Red"},
    {"초록",
     "Green"},
    {"파랑",
     "Blue"},
    {"시안",
     "Cyan"},
    {"색상 필터: %1",
     "Colour filter: %1"},
    {"S: 없음",
     "S: none"},
    {"반복 구간",
     "Loop"},
    {"반복 구간 [%1, %2] · %3",
     "Loop [%1, %2] · %3"},
    {"원본",
     "Original"},
    {"탐색 중...",
     "Searching..."},
    {"원본 파일을 덮어쓸 수 없습니다",
     "Cannot overwrite the source file"},
    {"추출 중...",
     "Exporting..."},
    {"현재 프레임 PNG 저장",
     "Save current frame as PNG"},
    {"프레임 %1 저장 완료: %2",
     "Saved frame %1: %2"},
    {"PNG 저장 실패: %1",
     "Failed to save PNG: %1"},
    {"이 픽셀 포맷은 원본 저장을 지원하지 않습니다: %1",
     "This pixel format is not supported for original saving: %1"},
    {"프레임 %1 원본 저장 완료 (%2bit): %3",
     "Saved original of frame %1 (%2-bit): %3"},
    {"원본 저장 실패: %1",
     "Failed to save original: %1"},
    {"픽셀 포맷: -",
     "Pixel format: -"},
    {"권장: PNG로 저장 (원본이 RGB 8bit라 손실 없음)",
     "Recommended: Save as PNG (source is 8-bit RGB, so it is lossless)"},
    {"권장: 원본 저장 (정확한 색 변환%1)",
     "Recommended: Save original (exact colour conversion%1)"},
    {"픽셀 포맷: %1 · %2bit · %3 · %4  →  %5",
     "Pixel format: %1 · %2-bit · %3 · %4  →  %5"},
    {"원본 픽셀과 동일한 무손실 PNG",
     "Lossless PNG identical to the source pixels"},
    {"화면 표시용 빠른 변환(8bit, 기본 색 행렬)을 그대로 저장한 PNG. 마스크 미포함",
     "PNG of the fast on-screen conversion (8-bit, default colour matrix) saved as is. Mask not included"},
    {"원본 프레임에서 따로 변환한 컬러 PNG: 영상의 색 행렬/범위 반영, 정확한 반올림, 크로마 풀 보간, 10bit 이상이면 16bit PNG. 마스크 미포함",
     "Colour PNG converted separately from the source frame: respects the video's colour matrix/range, exact rounding, full chroma interpolation, 16-bit PNG for 10-bit or deeper sources. Mask not included"},
    {"추출 완료: %1",
     "Export finished: %1"},
    {"추출 취소됨",
     "Export cancelled"},
    {"추출 실패: %1",
     "Export failed: %1"},
    {"반복 구간 [%1, %2] · %3프레임 · 길이 %4 · 유사도 %5%",
     "Loop [%1, %2] · %3 frames · duration %4 · similarity %5%"},
    {"E 이후 일치 프레임 없음",
     "No matching frame after E"},
    {"반복 없음",
     "No loop"},
    {"취소됨",
     "Cancelled"},
    {"오류: 탐색 실패",
     "Error: search failed"},
    {"영상 없음",
     "No video"},
    {"영상을 여세요",
     "Open a video"},
};

class EnglishTranslator : public QTranslator {
public:
    EnglishTranslator() {
        for (const auto& e : kTable) table_.insert(QString::fromUtf8(e.ko), QString::fromUtf8(e.en));
    }
    // Context is ignored: every dfd text is unique. Unknown (Qt's own) texts fall through.
    QString translate(const char*, const char* source, const char*, int) const override {
        return table_.value(QString::fromUtf8(source));
    }
    bool isEmpty() const override { return false; }

private:
    QHash<QString, QString> table_;
};

EnglishTranslator& translator() {
    static EnglishTranslator t;
    return t;
}

Lang current = Lang::Korean;

QSettings settings() { return QSettings("dup_frame_detector", "dup_frame_detector"); }

}  // namespace

Lang preferred_language() {
    const QString saved = settings().value("language").toString();
    if (saved == "en") return Lang::English;
    if (saved == "ko") return Lang::Korean;
    return QLocale::system().language() == QLocale::Korean ? Lang::Korean : Lang::English;
}

Lang language() { return current; }

void set_language(Lang lang, bool persist) {
    if (lang == current) return;
    // Removing then re-adding would fire two LanguageChange events; one per call is enough.
    if (lang == Lang::English) QCoreApplication::installTranslator(&translator());
    else QCoreApplication::removeTranslator(&translator());
    current = lang;
    if (persist) settings().setValue("language", lang == Lang::English ? "en" : "ko");
}

}  // namespace dfd::i18n
