#include "i18n/i18n.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ic::i18n {

namespace {

constexpr int kLanguageCount = static_cast<int>(Language::Count);

struct LanguageInfo {
    const char* code;
    const char* nativeName;
};

constexpr LanguageInfo kLanguages[kLanguageCount] = {
    {"en", "English"}, {"ru", "Русский"}, {"zh", "中文"}, {"fr", "Français"}, {"es", "Español"},
};

// Generated from the same list as the Msg enum, so it can't drift.
constexpr const char* kTable[static_cast<int>(Msg::Count)][kLanguageCount] = {
#define IC_MSG(key, en, ru, zh, fr, es) {en, ru, zh, fr, es},
#include "i18n/messages.def"
#undef IC_MSG
};

Language g_language = Language::English;

}  // namespace

void setLanguage(Language lang) {
    if (lang != Language::Count) g_language = lang;
}

Language language() {
    return g_language;
}

const char* tr(Msg msg, Language lang) {
    const int m = static_cast<int>(msg);
    const int l = static_cast<int>(lang);
    if (m < 0 || m >= static_cast<int>(Msg::Count) || l < 0 || l >= kLanguageCount) return "";
    return kTable[m][l];
}

const char* tr(Msg msg) {
    return tr(msg, g_language);
}

std::string tr(Msg msg, std::initializer_list<std::string> args) {
    std::string out = tr(msg);
    int index = 0;
    for (const auto& arg : args) {
        const std::string token = "{" + std::to_string(index++) + "}";
        for (size_t pos = out.find(token); pos != std::string::npos; pos = out.find(token, pos + arg.size())) {
            out.replace(pos, token.size(), arg);
        }
    }
    return out;
}

const char* code(Language lang) {
    const int l = static_cast<int>(lang);
    return l >= 0 && l < kLanguageCount ? kLanguages[l].code : "";
}

const char* nativeName(Language lang) {
    const int l = static_cast<int>(lang);
    return l >= 0 && l < kLanguageCount ? kLanguages[l].nativeName : "";
}

std::optional<Language> parse(const std::string& localeOrCode) {
    // The language is the part before any '_', '-', '.' or '@'.
    std::string lang;
    for (const char c : localeOrCode) {
        if (c == '_' || c == '-' || c == '.' || c == '@') break;
        lang += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (int l = 0; l < kLanguageCount; ++l) {
        if (lang == kLanguages[l].code) return static_cast<Language>(l);
    }
    return std::nullopt;
}

Language detectSystemLanguage() {
#ifdef _WIN32
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_RUSSIAN: return Language::Russian;
        case LANG_CHINESE: return Language::Chinese;
        case LANG_FRENCH: return Language::French;
        case LANG_SPANISH: return Language::Spanish;
        default: return Language::English;
    }
#else
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(var);
        if (value && *value) return parse(value).value_or(Language::English);
    }
    return Language::English;
#endif
}

}  // namespace ic::i18n
