#pragma once

namespace dfd::i18n {

// UI texts are written in Korean in the sources (tr("...")); English is a lookup table.
enum class Lang { Korean, English };

// Language saved by the last switch, else the system locale (Korean -> Korean, otherwise English).
Lang preferred_language();
Lang language();
// Installs/removes the English translator; open windows get a LanguageChange event.
void set_language(Lang lang, bool persist = true);

}  // namespace dfd::i18n
