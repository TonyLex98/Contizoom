#include "test.h"
#include "util/json.h"

using namespace ic;

TEST(json_parses_nested_documents) {
    const json::Value v = json::parse(R"({"a": [1, 2.5, -3e2], "b": {"c": true, "d": null}, "e": "x"})");
    CHECK(v.isObject());
    const json::Value* a = v.find("a");
    CHECK(a && a->isArray() && a->asArray().size() == 3);
    CHECK_NEAR(a->asArray()[2].asNumber(), -300.0, 1e-12);
    CHECK(v.find("b")->getBool("c"));
    CHECK(v.find("b")->find("d")->isNull());
    CHECK_EQ(v.getString("e"), std::string("x"));
}

TEST(json_decodes_escapes_and_surrogate_pairs) {
    const json::Value v = json::parse(R"(["a\"b\\c\n", "\u0416", "\ud83d\ude00"])");
    CHECK_EQ(v.asArray()[0].asString(), std::string("a\"b\\c\n"));
    CHECK_EQ(v.asArray()[1].asString(), std::string("Ж"));
    CHECK_EQ(v.asArray()[2].asString(), std::string("\xF0\x9F\x98\x80"));
}

TEST(json_rejects_malformed_input) {
    CHECK_THROWS(json::parse("{\"a\": 1,}"), json::ParseError);
    CHECK_THROWS(json::parse("[1, 2"), json::ParseError);
    CHECK_THROWS(json::parse("\"unterminated"), json::ParseError);
    CHECK_THROWS(json::parse("01"), json::ParseError);
    CHECK_THROWS(json::parse("{} extra"), json::ParseError);
    CHECK_THROWS(json::parse("\"\\ud83d\""), json::ParseError);  // lone surrogate
    CHECK_THROWS(json::parse("\"tab\there\""), json::ParseError);  // raw control char
}

TEST(json_round_trips_values_exactly) {
    json::Value v = json::Value::object();
    v.set("pi", 3.141592653589793);
    v.set("big", 1234567.8912345);
    v.set("int", 42);
    v.set("ts", static_cast<uint64_t>(1761650000));
    v.set("text", "line1\nline2\t\"quoted\" \x01 Юникод");
    json::Value arr = json::Value::array();
    arr.push(true);
    arr.push(nullptr);
    v.set("arr", arr);

    for (int indent : {0, 2}) {
        const json::Value back = json::parse(json::serialize(v, indent));
        CHECK_EQ(back.getNumber("pi"), 3.141592653589793);
        CHECK_EQ(back.getNumber("big"), 1234567.8912345);
        CHECK_EQ(back.getInt("int"), 42);
        CHECK_EQ(back.getUInt64("ts"), static_cast<uint64_t>(1761650000));
        CHECK_EQ(back.getString("text"), v.getString("text"));
        CHECK_EQ(back.find("arr")->asArray().size(), static_cast<size_t>(2));
    }
}

TEST(json_keeps_object_key_order) {
    json::Value v = json::Value::object();
    v.set("z", 1);
    v.set("a", 2);
    v.set("z", 3);  // replace keeps position
    CHECK_EQ(json::serialize(v, 0), std::string("{\"z\":3,\"a\":2}"));
}

TEST(json_typed_getters_fall_back_on_wrong_types) {
    const json::Value v = json::parse(R"({"n": "not a number", "s": 5})");
    CHECK_EQ(v.getNumber("n", 7.0), 7.0);
    CHECK_EQ(v.getString("s", "dflt"), std::string("dflt"));
    CHECK_EQ(v.getInt("missing", -1), -1);
}
