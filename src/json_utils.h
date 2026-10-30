/*
 * json_utils.h - a growable JSON writer and a deliberately small parser.
 *
 * Writer: build objects/arrays with the jb_* helpers; commas are inserted
 *         automatically.
 * Parser: accepts ONE flat JSON object whose values are strings, numbers,
 *         true/false or null (all request bodies of this API look like that).
 *         Nested objects/arrays are rejected on purpose.
 */
#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stddef.h>

/* ---------------- writer ---------------- */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
    int    oom;      /* set if an allocation failed */
} JsonBuf;

void        jb_init(JsonBuf *jb);
void        jb_free(JsonBuf *jb);
const char *jb_cstr(const JsonBuf *jb);

void jb_raw(JsonBuf *jb, const char *text);          /* verbatim */
void jb_begin_object(JsonBuf *jb, const char *key);  /* key may be NULL */
void jb_end_object(JsonBuf *jb);
void jb_begin_array(JsonBuf *jb, const char *key);
void jb_end_array(JsonBuf *jb);

void jb_field_int(JsonBuf *jb, const char *key, long value);
void jb_field_double(JsonBuf *jb, const char *key, double value);
void jb_field_str(JsonBuf *jb, const char *key, const char *value);
void jb_field_bool(JsonBuf *jb, const char *key, int value);
void jb_field_null(JsonBuf *jb, const char *key);
/* minutes < 0 => null, otherwise key=minutes and key_str="HH:MM" */
void jb_field_time(JsonBuf *jb, const char *key, int minutes);
/* value < 0 => null */
void jb_field_int_or_null(JsonBuf *jb, const char *key, long value);

/* ---------------- parser ---------------- */

#define JSON_MAX_FIELDS   16
#define JSON_KEY_LEN      32
#define JSON_STRING_LEN   256

typedef enum { JV_STRING, JV_NUMBER, JV_BOOL, JV_NULL } JsonType;

typedef struct {
    char     key[JSON_KEY_LEN];
    JsonType type;
    char     str[JSON_STRING_LEN];
    double   num;
    int      boolean;
} JsonField;

typedef struct {
    JsonField fields[JSON_MAX_FIELDS];
    int       count;
} JsonObject;

/* Returns 1 on success.  On failure returns 0 and writes a message to err. */
int json_parse_object(const char *text, size_t len, JsonObject *out,
                      char *err, size_t errlen);

const JsonField *json_get(const JsonObject *obj, const char *key);

#endif
