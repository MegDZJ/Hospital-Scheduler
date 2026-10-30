#include "json_utils.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* Writer                                                              */
/* ================================================================== */

void jb_init(JsonBuf *jb)
{
    jb->data = NULL;
    jb->len = 0;
    jb->cap = 0;
    jb->oom = 0;
}

void jb_free(JsonBuf *jb)
{
    free(jb->data);
    jb_init(jb);
}

const char *jb_cstr(const JsonBuf *jb)
{
    return jb->data ? jb->data : "";
}

static int jb_reserve(JsonBuf *jb, size_t extra)
{
    size_t need = jb->len + extra + 1;
    size_t newcap;
    char *p;

    if (jb->oom)
        return 0;
    if (need <= jb->cap)
        return 1;
    newcap = jb->cap ? jb->cap : 512;
    while (newcap < need)
        newcap *= 2;
    p = realloc(jb->data, newcap);
    if (p == NULL) {
        jb->oom = 1;
        return 0;
    }
    jb->data = p;
    jb->cap = newcap;
    return 1;
}

static void jb_append(JsonBuf *jb, const char *s, size_t n)
{
    if (!jb_reserve(jb, n))
        return;
    memcpy(jb->data + jb->len, s, n);
    jb->len += n;
    jb->data[jb->len] = '\0';
}

void jb_raw(JsonBuf *jb, const char *text)
{
    jb_append(jb, text, strlen(text));
}

static void jb_printf(JsonBuf *jb, const char *fmt, ...)
{
    char tmp[128];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n > 0 && (size_t)n < sizeof tmp)
        jb_append(jb, tmp, (size_t)n);
}

/* Emits a comma unless we are right after '{', '[' or a key's ':'. */
static void jb_separator(JsonBuf *jb)
{
    char last;
    if (jb->len == 0)
        return;
    last = jb->data[jb->len - 1];
    if (last != '{' && last != '[' && last != ':')
        jb_append(jb, ",", 1);
}

static void jb_quoted(JsonBuf *jb, const char *s)
{
    const unsigned char *p = (const unsigned char *)s;

    jb_append(jb, "\"", 1);
    for (; *p; p++) {
        switch (*p) {
        case '"':  jb_append(jb, "\\\"", 2); break;
        case '\\': jb_append(jb, "\\\\", 2); break;
        case '\n': jb_append(jb, "\\n", 2);  break;
        case '\r': jb_append(jb, "\\r", 2);  break;
        case '\t': jb_append(jb, "\\t", 2);  break;
        default:
            if (*p < 0x20)
                jb_printf(jb, "\\u%04x", *p);
            else
                jb_append(jb, (const char *)p, 1);
        }
    }
    jb_append(jb, "\"", 1);
}

static void jb_key(JsonBuf *jb, const char *key)
{
    jb_separator(jb);
    if (key != NULL) {
        jb_quoted(jb, key);
        jb_append(jb, ":", 1);
    }
}

void jb_begin_object(JsonBuf *jb, const char *key)
{
    jb_key(jb, key);
    jb_append(jb, "{", 1);
}

void jb_end_object(JsonBuf *jb)
{
    jb_append(jb, "}", 1);
}

void jb_begin_array(JsonBuf *jb, const char *key)
{
    jb_key(jb, key);
    jb_append(jb, "[", 1);
}

void jb_end_array(JsonBuf *jb)
{
    jb_append(jb, "]", 1);
}

void jb_field_int(JsonBuf *jb, const char *key, long value)
{
    jb_key(jb, key);
    jb_printf(jb, "%ld", value);
}

void jb_field_double(JsonBuf *jb, const char *key, double value)
{
    jb_key(jb, key);
    jb_printf(jb, "%.2f", value);
}

void jb_field_str(JsonBuf *jb, const char *key, const char *value)
{
    jb_key(jb, key);
    jb_quoted(jb, value);
}

void jb_field_bool(JsonBuf *jb, const char *key, int value)
{
    jb_key(jb, key);
    jb_raw(jb, value ? "true" : "false");
}

void jb_field_null(JsonBuf *jb, const char *key)
{
    jb_key(jb, key);
    jb_raw(jb, "null");
}

void jb_field_int_or_null(JsonBuf *jb, const char *key, long value)
{
    if (value < 0)
        jb_field_null(jb, key);
    else
        jb_field_int(jb, key, value);
}

void jb_field_time(JsonBuf *jb, const char *key, int minutes)
{
    char name[48];
    char text[8];

    snprintf(name, sizeof name, "%s_str", key);
    if (minutes < 0 || minutes >= 1440) {
        jb_field_null(jb, key);
        jb_field_null(jb, name);
        return;
    }
    snprintf(text, sizeof text, "%02d:%02d", minutes / 60, minutes % 60);
    jb_field_int(jb, key, minutes);
    jb_field_str(jb, name, text);
}

/* ================================================================== */
/* Parser (one flat object)                                            */
/* ================================================================== */

typedef struct {
    const char *p;
    const char *end;
    char       *err;
    size_t      errlen;
} Cursor;

static int fail(Cursor *c, const char *msg)
{
    snprintf(c->err, c->errlen, "Invalid JSON: %s", msg);
    return 0;
}

static void skip_ws(Cursor *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' ||
                             *c->p == '\n' || *c->p == '\r'))
        c->p++;
}

static int hex4(const char *s, unsigned *out)
{
    unsigned v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        char ch = s[i];
        v <<= 4;
        if (ch >= '0' && ch <= '9')       v |= (unsigned)(ch - '0');
        else if (ch >= 'a' && ch <= 'f')  v |= (unsigned)(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F')  v |= (unsigned)(ch - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

static int put_utf8(char *out, size_t cap, size_t *n, unsigned cp)
{
    unsigned char buf[4];
    size_t k, i;

    if (cp < 0x80) {
        buf[0] = (unsigned char)cp; k = 1;
    } else if (cp < 0x800) {
        buf[0] = (unsigned char)(0xC0 | (cp >> 6));
        buf[1] = (unsigned char)(0x80 | (cp & 0x3F)); k = 2;
    } else if (cp < 0x10000) {
        buf[0] = (unsigned char)(0xE0 | (cp >> 12));
        buf[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (unsigned char)(0x80 | (cp & 0x3F)); k = 3;
    } else {
        buf[0] = (unsigned char)(0xF0 | (cp >> 18));
        buf[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (unsigned char)(0x80 | (cp & 0x3F)); k = 4;
    }
    if (*n + k >= cap)
        return 0;
    for (i = 0; i < k; i++)
        out[(*n)++] = (char)buf[i];
    return 1;
}

static int parse_string(Cursor *c, char *out, size_t cap)
{
    size_t n = 0;

    if (c->p >= c->end || *c->p != '"')
        return fail(c, "expected a string");
    c->p++;
    while (c->p < c->end && *c->p != '"') {
        unsigned char ch = (unsigned char)*c->p;
        if (ch < 0x20)
            return fail(c, "control character in string");
        if (ch == '\\') {
            unsigned cp;
            c->p++;
            if (c->p >= c->end)
                return fail(c, "unterminated escape");
            switch (*c->p) {
            case '"': case '\\': case '/':
                cp = (unsigned char)*c->p; c->p++; break;
            case 'b': cp = '\b'; c->p++; break;
            case 'f': cp = '\f'; c->p++; break;
            case 'n': cp = '\n'; c->p++; break;
            case 'r': cp = '\r'; c->p++; break;
            case 't': cp = '\t'; c->p++; break;
            case 'u':
                if (c->end - c->p < 5 || !hex4(c->p + 1, &cp))
                    return fail(c, "bad \\u escape");
                c->p += 5;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    unsigned lo;
                    if (c->end - c->p < 6 || c->p[0] != '\\' || c->p[1] != 'u' ||
                        !hex4(c->p + 2, &lo) || lo < 0xDC00 || lo > 0xDFFF)
                        return fail(c, "bad surrogate pair");
                    c->p += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return fail(c, "bad surrogate pair");
                }
                break;
            default:
                return fail(c, "unknown escape");
            }
            if (cp == 0)
                return fail(c, "NUL character in string");
            if (!put_utf8(out, cap, &n, cp))
                return fail(c, "string too long");
        } else {
            if (n + 1 >= cap)
                return fail(c, "string too long");
            out[n++] = (char)ch;
            c->p++;
        }
    }
    if (c->p >= c->end)
        return fail(c, "unterminated string");
    c->p++;                 /* closing quote */
    out[n] = '\0';
    return 1;
}

static int parse_number(Cursor *c, double *out)
{
    const char *start = c->p;
    char buf[40];
    size_t n;

    if (c->p < c->end && *c->p == '-')
        c->p++;
    if (c->p >= c->end || !isdigit((unsigned char)*c->p))
        return fail(c, "bad number");
    if (*c->p == '0') {
        c->p++;
    } else {
        while (c->p < c->end && isdigit((unsigned char)*c->p))
            c->p++;
    }
    if (c->p < c->end && *c->p == '.') {
        c->p++;
        if (c->p >= c->end || !isdigit((unsigned char)*c->p))
            return fail(c, "bad number");
        while (c->p < c->end && isdigit((unsigned char)*c->p))
            c->p++;
    }
    if (c->p < c->end && (*c->p == 'e' || *c->p == 'E')) {
        c->p++;
        if (c->p < c->end && (*c->p == '+' || *c->p == '-'))
            c->p++;
        if (c->p >= c->end || !isdigit((unsigned char)*c->p))
            return fail(c, "bad number");
        while (c->p < c->end && isdigit((unsigned char)*c->p))
            c->p++;
    }
    n = (size_t)(c->p - start);
    if (n >= sizeof buf)
        return fail(c, "number too long");
    memcpy(buf, start, n);
    buf[n] = '\0';
    *out = strtod(buf, NULL);
    return 1;
}

static int match_word(Cursor *c, const char *word)
{
    size_t n = strlen(word);
    if ((size_t)(c->end - c->p) >= n && memcmp(c->p, word, n) == 0) {
        c->p += n;
        return 1;
    }
    return 0;
}

int json_parse_object(const char *text, size_t len, JsonObject *out,
                      char *err, size_t errlen)
{
    Cursor c;
    int i;

    c.p = text;
    c.end = text + len;
    c.err = err;
    c.errlen = errlen;
    out->count = 0;

    skip_ws(&c);
    if (c.p >= c.end)
        return fail(&c, "empty request body");
    if (*c.p != '{')
        return fail(&c, "a JSON object is required");
    c.p++;
    skip_ws(&c);

    if (c.p < c.end && *c.p == '}') {
        c.p++;
    } else {
        for (;;) {
            JsonField *f;
            if (out->count >= JSON_MAX_FIELDS)
                return fail(&c, "too many fields");
            f = &out->fields[out->count];
            memset(f, 0, sizeof *f);

            skip_ws(&c);
            if (!parse_string(&c, f->key, sizeof f->key))
                return 0;
            for (i = 0; i < out->count; i++) {
                if (strcmp(out->fields[i].key, f->key) == 0)
                    return fail(&c, "duplicate key");
            }
            skip_ws(&c);
            if (c.p >= c.end || *c.p != ':')
                return fail(&c, "expected ':'");
            c.p++;
            skip_ws(&c);
            if (c.p >= c.end)
                return fail(&c, "missing value");

            if (*c.p == '"') {
                f->type = JV_STRING;
                if (!parse_string(&c, f->str, sizeof f->str))
                    return 0;
            } else if (*c.p == '-' || isdigit((unsigned char)*c.p)) {
                f->type = JV_NUMBER;
                if (!parse_number(&c, &f->num))
                    return 0;
            } else if (match_word(&c, "true")) {
                f->type = JV_BOOL; f->boolean = 1;
            } else if (match_word(&c, "false")) {
                f->type = JV_BOOL; f->boolean = 0;
            } else if (match_word(&c, "null")) {
                f->type = JV_NULL;
            } else {
                return fail(&c, "unsupported value (nested data is not allowed)");
            }
            out->count++;

            skip_ws(&c);
            if (c.p < c.end && *c.p == ',') {
                c.p++;
                continue;
            }
            if (c.p < c.end && *c.p == '}') {
                c.p++;
                break;
            }
            return fail(&c, "expected ',' or '}'");
        }
    }
    skip_ws(&c);
    if (c.p != c.end)
        return fail(&c, "unexpected data after the object");
    return 1;
}

const JsonField *json_get(const JsonObject *obj, const char *key)
{
    int i;
    for (i = 0; i < obj->count; i++) {
        if (strcmp(obj->fields[i].key, key) == 0)
            return &obj->fields[i];
    }
    return NULL;
}
