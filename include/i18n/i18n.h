#ifndef IC_I18N_H
#define IC_I18N_H

// User interface translations.
//
// Every message lives in one place, i18n/messages.def, as one X-macro entry
// carrying all translations side by side. The same list generates the
// Msg enum and the lookup table, so a key can't exist without text, and an
// entry missing a language doesn't compile (wrong macro arity). Code never
// contains UI literals; it asks for tr(Msg::Something).
//
// Adding a language: add it to Language, kLanguages (code + native name)
// and give every entry in messages.def one more argument.

#include <initializer_list>
#include <optional>
#include <string>

namespace ic::i18n {

enum class Language { English, Russian, Chinese, French, Spanish, Count };

enum class Msg {
#define IC_MSG(key, en, ru, zh, fr, es) key,
#include "i18n/messages.def"
#undef IC_MSG
    Count
};

void setLanguage(Language lang);
Language language();

// The message in the current language (UTF-8).
const char* tr(Msg msg);
// With "{0}", "{1}", ... replaced by `args` in order.
std::string tr(Msg msg, std::initializer_list<std::string> args);
// A message in a specific language (for tests and the language menu).
const char* tr(Msg msg, Language lang);

const char* code(Language lang);        // "en", "ru", "zh", "fr", "es"
const char* nativeName(Language lang);  // "English", "Русский", "中文", ...

// Accepts "ru", "RU", "ru_RU.UTF-8", "zh-CN", "fr-FR", ...
std::optional<Language> parse(const std::string& localeOrCode);

// The operating system's UI language, English if it isn't one we have.
Language detectSystemLanguage();

}  // namespace ic::i18n

namespace ic {
using i18n::Msg;
using i18n::tr;
}  // namespace ic

#endif  // IC_I18N_H
