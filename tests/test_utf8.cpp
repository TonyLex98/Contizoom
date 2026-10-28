#include "test.h"
#include "util/utf8.h"

using namespace ic;

TEST(utf8_length_counts_code_points) {
    CHECK_EQ(utf8::length(""), 0);
    CHECK_EQ(utf8::length("abc"), 3);
    CHECK_EQ(utf8::length("Привет"), 6);      // 2 bytes each
    CHECK_EQ(utf8::length("a→b"), 3);         // 3-byte arrow
    CHECK_EQ(utf8::length("\xF0\x9F\x98\x80"), 1);  // emoji, 4 bytes
}

TEST(utf8_insert_and_erase_by_index) {
    std::string s = "Пир";
    utf8::insertAt(s, 1, U'р');
    CHECK_EQ(s, std::string("Прир"));
    utf8::eraseAt(s, 2);
    CHECK_EQ(s, std::string("Прр"));
    utf8::eraseAt(s, 10);  // past the end: no-op
    CHECK_EQ(s, std::string("Прр"));
    CHECK_EQ(utf8::prefix("Привет", 3), std::string("При"));
}

TEST(utf8_decode_is_safe_on_truncated_input) {
    const std::string bad = "ok\xD0";  // lead byte without its continuation
    size_t pos = 2;
    CHECK_EQ(static_cast<unsigned>(utf8::decode(bad, pos)), 0xFFFDu);
    CHECK_EQ(pos, bad.size());
}

TEST(utf8_encode_round_trips) {
    for (char32_t cp : {U'A', U'ж', U'€', U'\U0001F600'}) {
        const std::string s = utf8::encode(cp);
        size_t pos = 0;
        CHECK_EQ(static_cast<unsigned>(utf8::decode(s, pos)), static_cast<unsigned>(cp));
        CHECK_EQ(pos, s.size());
    }
}
