#include "test_util.h"
#include "json_utils.h"

static int parse(const char *text, JsonObject *o)
{
    char err[160];
    return json_parse_object(text, strlen(text), o, err, sizeof err);
}

int main(void)
{
    JsonObject o;
    JsonBuf jb;
    const JsonField *f;

    printf("JSON tests\n");

    TEST("parses a flat object");
    CHECK(parse("{\"name\":\"Ann\",\"age\":42,\"ok\":true,\"x\":null}", &o));
    CHECK(o.count == 4);
    f = json_get(&o, "name");
    CHECK(f && f->type == JV_STRING && strcmp(f->str, "Ann") == 0);
    f = json_get(&o, "age");
    CHECK(f && f->type == JV_NUMBER && f->num == 42.0);
    f = json_get(&o, "ok");
    CHECK(f && f->type == JV_BOOL && f->boolean == 1);
    CHECK(json_get(&o, "missing") == NULL);

    TEST("whitespace, negatives, decimals, escapes");
    CHECK(parse("  { \"a\" : -3.5e1 , \"s\":\"x\\ty\\u00e9\" }  ", &o));
    CHECK(json_get(&o, "a")->num == -35.0);
    CHECK(strcmp(json_get(&o, "s")->str, "x\ty\xc3\xa9") == 0);
    CHECK(parse("{}", &o) && o.count == 0);

    TEST("rejects malformed JSON");
    CHECK(!parse("", &o));
    CHECK(!parse("not json", &o));
    CHECK(!parse("{\"a\":}", &o));
    CHECK(!parse("{\"a\":1", &o));
    CHECK(!parse("{\"a\":1,}", &o));
    CHECK(!parse("{\"a\":1} trailing", &o));
    CHECK(!parse("{'a':1}", &o));
    CHECK(!parse("{\"a\":01}", &o));
    CHECK(!parse("{\"a\":\"unterminated}", &o));
    CHECK(!parse("{\"a\":\"bad\\q\"}", &o));
    CHECK(!parse("[1,2,3]", &o));

    TEST("rejects nested data and duplicate keys");
    CHECK(!parse("{\"a\":{\"b\":1}}", &o));
    CHECK(!parse("{\"a\":[1]}", &o));
    CHECK(!parse("{\"a\":1,\"a\":2}", &o));

    TEST("rejects over-long strings instead of overflowing");
    {
        char big[600];
        memset(big, 'a', sizeof big);
        memcpy(big, "{\"a\":\"", 6);
        memcpy(big + 560, "\"}", 3);
        CHECK(!json_parse_object(big, 562, &o, (char[160]){0}, 160));
    }

    TEST("writer escapes strings and places commas");
    jb_init(&jb);
    jb_begin_object(&jb, NULL);
    jb_field_str(&jb, "q", "a\"b\\c\n<script>");
    jb_field_int(&jb, "n", 5);
    jb_begin_array(&jb, "list");
    jb_begin_object(&jb, NULL);
    jb_field_bool(&jb, "t", 1);
    jb_end_object(&jb);
    jb_end_array(&jb);
    jb_field_time(&jb, "when", 545);
    jb_field_time(&jb, "never", -1);
    jb_end_object(&jb);
    CHECK(strcmp(jb_cstr(&jb),
        "{\"q\":\"a\\\"b\\\\c\\n<script>\",\"n\":5,\"list\":[{\"t\":true}],"
        "\"when\":545,\"when_str\":\"09:05\",\"never\":null,"
        "\"never_str\":null}") == 0);
    jb_free(&jb);

    TEST_DONE("test_json");
}
