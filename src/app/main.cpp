#include "ui/i18n.hpp"
#include "ui/main_window.hpp"

#include <QApplication>
#include <QLoggingCategory>

int main(int argc, char** argv) {
    // Harmless: fonts without OpenType tables for some scripts; Qt falls back.
    QLoggingCategory::setFilterRules("qt.text.font.db.warning=false");
    QApplication app(argc, argv);
    dfd::i18n::set_language(dfd::i18n::preferred_language(), /*persist=*/false);
    dfd::MainWindow w;
    w.show();
    if (argc > 1) w.open_video(QString::fromLocal8Bit(argv[1]));
    return app.exec();
}
