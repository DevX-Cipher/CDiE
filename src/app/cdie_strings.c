/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "cdie_strings.h"
#include "cdie_app.h"
#include <xxfclib/buf/xx_buf.h>
#include <xxfclib/terminal/xx_terminal.h>

#define STRINGS_MAX_OUTPUT (32U * 1024U * 1024U)
cdie_strings_options cdie_strings_defaults(void) { cdie_strings_options o = {4, CDIE_STRINGS_ALL}; return o; }
const char *cdie_strings_encoding_name(cdie_strings_encoding encoding)
{
    return encoding == CDIE_STRINGS_ASCII ? "ASCII" : encoding == CDIE_STRINGS_UTF8 ? "UTF-8" :
        encoding == CDIE_STRINGS_UTF16LE ? "UTF-16LE" : "UTF-16BE";
}
unsigned cdie_strings_parse_encoding(const char *name)
{
    if (!name) return 0;
    if (!xx_rt_strcmp(name, "ascii")) return CDIE_STRINGS_ASCII;
    if (!xx_rt_strcmp(name, "utf8")) return CDIE_STRINGS_UTF8;
    if (!xx_rt_strcmp(name, "utf16le")) return CDIE_STRINGS_UTF16LE;
    if (!xx_rt_strcmp(name, "utf16be")) return CDIE_STRINGS_UTF16BE;
    if (!xx_rt_strcmp(name, "all")) return CDIE_STRINGS_ALL;
    return 0;
}
int cdie_strings_options_valid(const cdie_strings_options *o)
{
    return o && o->minimum_length >= 1 && o->minimum_length <= 1024 && o->encodings && !(o->encodings & ~15U) &&
        (o->encodings & 3) != 3;
}
void cdie_strings_free(cdie_strings_data *data)
{
    size_t i;
    if (!data) return;
    for (i = 0; i < data->count; ++i) xx_rt_free(data->records[i].text);
    xx_rt_free(data->records); xx_rt_free(data);
}
typedef struct strings_scan strings_scan;
typedef struct decoder {
    strings_scan *scan;
    xx_buf_t text;
    uint64_t start, end, characters, unit_start;
    uint32_t scalar, minimum, high;
    unsigned encoding, phase, first, pending, streak;
    int ascii, non_ascii, bom, capped;
} decoder;
struct strings_scan {
    cdie_strings_data *data;
    cdie_strings_options options;
    const char *error;
};
static int stopped(xx_pd_struct *pd)
{
    if (!xx_pd_is_stopped(pd)) return 0;
    xx_pd_stop(pd); return 1;
}
/* Delimit controls, surrogate codepoints, BOM, bidi/format controls and Unicode
 * noncharacters. This is a scalar policy, not a Unicode category database. */
static int printable(uint32_t cp)
{
    return (cp >= 32 && cp < 127) || (cp >= 160 && cp <= 0x10ffff &&
        !(cp >= 0xd800 && cp <= 0xdfff) && !(cp >= 0xfdd0 && cp <= 0xfdef) &&
        (cp & 0xffff) < 0xfffe && cp != 0xfeff && !(cp >= 0x200b && cp <= 0x200f) &&
        !(cp >= 0x2028 && cp <= 0x202e) && !(cp >= 0x2060 && cp <= 0x206f));
}
static int finish_run(decoder *d)
{
    strings_scan *scan = d->scan;
    cdie_strings_data *data = scan->data;
    int accepted = d->characters >= scan->options.minimum_length;
    if ((d->encoding == CDIE_STRINGS_UTF16LE || d->encoding == CDIE_STRINGS_UTF16BE) &&
        scan->options.encodings == CDIE_STRINGS_ALL && (unsigned)d->ascii < scan->options.minimum_length && !d->bom) accepted = 0;
    if (accepted) {
        cdie_string_record *r;
        if (d->capped || data->count == CDIE_STRINGS_MAX_RECORDS ||
            d->text.size + 1 > CDIE_STRINGS_MAX_TEXT - data->text_bytes) {
            scan->error = "Strings result limit exceeded"; return 0;
        }
        if (data->count == data->capacity) {
            size_t capacity = data->capacity ? data->capacity * 2 : 64;
            void *records;
            if (capacity > CDIE_STRINGS_MAX_RECORDS) capacity = CDIE_STRINGS_MAX_RECORDS;
            records = xx_rt_realloc(data->records, capacity * sizeof(*data->records));
            if (!records) { scan->error = "Cannot allocate strings"; return 0; }
            data->records = records; data->capacity = capacity;
        }
        r = data->records + data->count;
        r->offset = d->start; r->byte_size = d->end - d->start; r->character_length = d->characters;
        r->encoding = d->encoding == CDIE_STRINGS_UTF8 && !d->non_ascii ? CDIE_STRINGS_ASCII : (cdie_strings_encoding)d->encoding;
        r->text = cdie_strdup(d->text.data ? d->text.data : "");
        if (!r->text) { scan->error = "Cannot allocate strings"; return 0; }
        data->text_bytes += d->text.size + 1; ++data->count;
    }
    xx_buf_clear(&d->text); d->characters = 0; d->streak = 0; d->ascii = d->non_ascii = d->bom = d->capped = 0;
    return 1;
}
static int feed_scalar(decoder *d, uint32_t cp, uint64_t offset, unsigned bytes)
{
    char encoded[4];
    size_t length;
    if (!printable(cp)) {
        if (!finish_run(d)) return 0;
        d->bom = cp == 0xfeff; return 1;
    }
    if (!d->characters) d->start = offset;
    d->end = offset + bytes; ++d->characters;
    if (cp < 127) {
        if (d->streak < d->scan->options.minimum_length) ++d->streak;
        if (d->streak > (unsigned)d->ascii) d->ascii = (int)d->streak;
    } else { d->non_ascii = 1; d->streak = 0; }
    if (cp < 0x80) { encoded[0] = (char)cp; length = 1; }
    else if (cp < 0x800) { encoded[0] = (char)(0xc0 | (cp >> 6)); encoded[1] = (char)(0x80 | (cp & 63)); length = 2; }
    else if (cp < 0x10000) {
        encoded[0] = (char)(0xe0 | (cp >> 12)); encoded[1] = (char)(0x80 | ((cp >> 6) & 63)); encoded[2] = (char)(0x80 | (cp & 63)); length = 3;
    } else {
        encoded[0] = (char)(0xf0 | (cp >> 18)); encoded[1] = (char)(0x80 | ((cp >> 12) & 63));
        encoded[2] = (char)(0x80 | ((cp >> 6) & 63)); encoded[3] = (char)(0x80 | (cp & 63)); length = 4;
    }
    if (d->text.size > CDIE_STRINGS_MAX_STRING - length) d->capped = 1;
    if (!d->capped && !xx_buf_append(&d->text, encoded, length)) { d->scan->error = "Cannot allocate strings"; return 0; }
    return 1;
}
static int feed_utf8(decoder *d, unsigned byte, uint64_t offset)
{
    if (d->encoding == CDIE_STRINGS_ASCII) return feed_scalar(d, byte < 128 ? byte : 0, offset, 1);
    if (d->pending) {
        if ((byte & 0xc0) != 0x80) {
            d->pending = 0;
            if (!finish_run(d)) return 0;
            return feed_utf8(d, byte, offset);
        }
        d->scalar = (d->scalar << 6) | (byte & 63);
        if (--d->pending) return 1;
        if (d->scalar < d->minimum || d->scalar > 0x10ffff || (d->scalar >= 0xd800 && d->scalar <= 0xdfff))
            return feed_scalar(d, 0, offset, 1);
        return feed_scalar(d, d->scalar, d->unit_start, (unsigned)(offset - d->unit_start + 1));
    }
    if (byte < 128) return feed_scalar(d, byte, offset, 1);
    d->unit_start = offset;
    if (byte >= 0xc2 && byte <= 0xdf) { d->scalar = byte & 31; d->pending = 1; d->minimum = 0x80; }
    else if (byte >= 0xe0 && byte <= 0xef) { d->scalar = byte & 15; d->pending = 2; d->minimum = 0x800; }
    else if (byte >= 0xf0 && byte <= 0xf4) { d->scalar = byte & 7; d->pending = 3; d->minimum = 0x10000; }
    else return feed_scalar(d, 0, offset, 1);
    return 1;
}
static int feed_utf16(decoder *d, unsigned byte, uint64_t offset)
{
    uint32_t unit;
    if ((offset & 1) == d->phase) { d->first = byte; d->pending = 1; return 1; }
    if (!d->pending) return 1;
    d->pending = 0;
    unit = d->encoding == CDIE_STRINGS_UTF16LE ? d->first | (byte << 8) : (d->first << 8) | byte;
    if (d->high) {
        uint32_t high = d->high; d->high = 0;
        if (unit >= 0xdc00 && unit <= 0xdfff)
            return feed_scalar(d, 0x10000 + ((high - 0xd800) << 10) + unit - 0xdc00, offset - 3, 4);
        if (!finish_run(d)) return 0;
    }
    if (unit >= 0xd800 && unit <= 0xdbff) { d->high = unit; return 1; }
    return feed_scalar(d, unit >= 0xdc00 && unit <= 0xdfff ? 0 : unit, offset - 1, 2);
}
static int compare_records(const void *left, const void *right)
{
    const cdie_string_record *a = left, *b = right;
    if (a->offset != b->offset) return a->offset < b->offset ? -1 : 1;
    return (int)a->encoding - (int)b->encoding;
}
int cdie_strings_read_device(xx_io_device *device, const cdie_strings_options *options,
    xx_pd_struct *pd, cdie_strings_data **out, char **error)
{
    strings_scan scan;
    decoder decoders[5];
    unsigned char *buffer = NULL;
    int64_t size;
    uint64_t done = 0;
    size_t i, j, count = 0;
    unsigned encoding, phase;
    int ok = 0, level = -1;
    if (!out || !error) return 0;
    *out = NULL; *error = NULL;
    xx_rt_memset(&scan, 0, sizeof(scan)); xx_rt_memset(decoders, 0, sizeof(decoders));
    scan.options = options ? *options : cdie_strings_defaults(); scan.error = "Cannot read strings";
    if (!cdie_strings_options_valid(&scan.options)) { scan.error = "Invalid strings options"; goto finish; }
    if (stopped(pd)) goto finish;
    if (!device || (size = xx_io_total_size(device)) < 0) { scan.error = "Cannot determine file size"; goto finish; }
    scan.data = xx_rt_calloc(1, sizeof(*scan.data)); buffer = xx_rt_malloc(65536);
    if (!scan.data || !buffer) { scan.error = "Cannot allocate strings"; goto finish; }
    scan.data->size = (uint64_t)size;
    if (scan.options.encodings & 3) decoders[count++].encoding = scan.options.encodings & 3;
    for (encoding = CDIE_STRINGS_UTF16LE; encoding <= CDIE_STRINGS_UTF16BE; encoding *= 2)
        if (scan.options.encodings & encoding) for (phase = 0; phase < 2; ++phase) {
            decoders[count].encoding = encoding; decoders[count++].phase = phase;
        }
    for (i = 0; i < count; ++i) decoders[i].scan = &scan;
    level = xx_pd_enter_level(pd, scan.data->size, "Extracting strings");
    while (done < scan.data->size) {
        size_t chunk = scan.data->size - done < 65536 ? (size_t)(scan.data->size - done) : 65536;
        if (stopped(pd) || !xx_io_read_at(device, (int64_t)done, buffer, chunk)) goto finish;
        for (i = 0; i < chunk; ++i) for (j = 0; j < count; ++j) {
            int fed = decoders[j].encoding & 3 ? feed_utf8(&decoders[j], buffer[i], done + i) : feed_utf16(&decoders[j], buffer[i], done + i);
            if (!fed) goto finish;
        }
        done += chunk; xx_pd_set_current(pd, level, done);
    }
    if (stopped(pd)) goto finish;
    for (i = 0; i < count; ++i) if (!finish_run(&decoders[i])) goto finish;
    if (scan.data->count > 1) xx_rt_qsort(scan.data->records, scan.data->count, sizeof(*scan.data->records), compare_records);
    ok = 1;
finish:
    xx_pd_leave_level(pd, level);
    if (stopped(pd)) { ok = 0; scan.error = "Strings extraction cancelled"; }
    for (i = 0; i < count; ++i) xx_buf_free(&decoders[i].text);
    xx_rt_free(buffer);
    if (ok) *out = scan.data;
    else { cdie_strings_free(scan.data); *error = cdie_strdup(scan.error); }
    return ok;
}
int cdie_strings_read(const char *path, const cdie_strings_options *options,
    xx_pd_struct *pd, cdie_strings_data **out, char **error)
{
    xx_io_device *device;
    int ok;
    if (!out || !error) return 0;
    *out = NULL; *error = NULL;
    if (stopped(pd)) { *error = cdie_strdup("Strings extraction cancelled"); return 0; }
    device = path && *path ? xx_io_file_open(path, "rb") : NULL;
    if (!device) { *error = cdie_strdup("Cannot open file"); return 0; }
    ok = cdie_strings_read_device(device, options, pd, out, error); xx_io_close(device); return ok;
}
static xxfc_status_t sink(void *user, const char *text, size_t size)
{
    xx_buf_t *buffer = user;
    if (size > STRINGS_MAX_OUTPUT || buffer->size > STRINGS_MAX_OUTPUT - size) { buffer->failed = true; return XXFC_ERR_OUT_OF_BOUNDS; }
    return xx_buf_append(buffer, text, size) ? XXFC_OK : XXFC_ERR_OUT_OF_MEMORY;
}
static void escaped(xx_buf_t *buffer, const char *text, int json)
{
    xx_terminal_writer writer;
    xx_terminal_writer_init(&writer, sink, buffer);
    if (json) xx_terminal_writer_json_string(&writer, text); else xx_terminal_writer_xml_attribute(&writer, text);
    if (xx_terminal_writer_status(&writer) != XXFC_OK) buffer->failed = true;
}
static void cell(xx_buf_t *buffer, const char *text)
{
    xx_buf_append_char(buffer, '"');
    for (; *text && xx_buf_ok(buffer); ++text) {
        if (buffer->size > STRINGS_MAX_OUTPUT) { buffer->failed = true; break; }
        if (*text == '"') xx_buf_append_char(buffer, '"');
        xx_buf_append_char(buffer, *text);
    }
    xx_buf_append_char(buffer, '"');
}
int cdie_strings_report(const char *path, const cdie_strings_options *options,
    cdie_output_format format, char **text, char **error)
{
    cdie_strings_data *data;
    xx_buf_t buffer;
    size_t i;
    if (!text || !error) return 0;
    *text = NULL; *error = NULL;
    if (!cdie_strings_read(path, options, NULL, &data, error)) return 0;
    xx_buf_init(&buffer);
    if (format == CDIE_OUTPUT_JSON) {
        xx_buf_append_str(&buffer, "{\"path\":"); escaped(&buffer, path, 1);
        xx_buf_appendf(&buffer, ",\"size\":%llu,\"strings\":[", (unsigned long long)data->size);
    } else if (format == CDIE_OUTPUT_XML) {
        xx_buf_append_str(&buffer, "<file-strings path=\""); escaped(&buffer, path, 0);
        xx_buf_appendf(&buffer, "\" size=\"%llu\">\n", (unsigned long long)data->size);
    } else if (format == CDIE_OUTPUT_CSV || format == CDIE_OUTPUT_TSV) {
        char sep = format == CDIE_OUTPUT_CSV ? ';' : '\t';
        xx_buf_appendf(&buffer, "Path%cOffset%cBytes%cCharacters%cEncoding%cString\n", sep, sep, sep, sep, sep);
    } else xx_buf_appendf(&buffer, "Strings: %s\n", path);
    for (i = 0; i < data->count && xx_buf_ok(&buffer); ++i) {
        const cdie_string_record *r = data->records + i;
        const char *name = cdie_strings_encoding_name(r->encoding);
        if (format == CDIE_OUTPUT_JSON) {
            xx_buf_appendf(&buffer, "%s{\"offset\":%llu,\"byte_size\":%llu,\"character_length\":%llu,\"encoding\":\"%s\",\"text\":",
                i ? "," : "", (unsigned long long)r->offset, (unsigned long long)r->byte_size, (unsigned long long)r->character_length, name);
            escaped(&buffer, r->text, 1); xx_buf_append_char(&buffer, '}');
        } else if (format == CDIE_OUTPUT_XML) {
            xx_buf_appendf(&buffer, "  <string offset=\"%llu\" byte_size=\"%llu\" character_length=\"%llu\" encoding=\"%s\">",
                (unsigned long long)r->offset, (unsigned long long)r->byte_size, (unsigned long long)r->character_length, name);
            escaped(&buffer, r->text, 0); xx_buf_append_str(&buffer, "</string>\n");
        } else if (format == CDIE_OUTPUT_CSV || format == CDIE_OUTPUT_TSV) {
            char sep = format == CDIE_OUTPUT_CSV ? ';' : '\t';
            cell(&buffer, path);
            xx_buf_appendf(&buffer, "%c%llu%c%llu%c%llu%c%s%c", sep, (unsigned long long)r->offset,
                sep, (unsigned long long)r->byte_size, sep, (unsigned long long)r->character_length, sep, name, sep);
            cell(&buffer, r->text); xx_buf_append_char(&buffer, '\n');
        } else xx_buf_appendf(&buffer, "0x%016llx [%s, %llu chars, %llu bytes] %s\n", (unsigned long long)r->offset,
            name, (unsigned long long)r->character_length, (unsigned long long)r->byte_size, r->text);
        if (buffer.size > STRINGS_MAX_OUTPUT) buffer.failed = true;
    }
    if (format == CDIE_OUTPUT_JSON) xx_buf_append_str(&buffer, "]}\n");
    else if (format == CDIE_OUTPUT_XML) xx_buf_append_str(&buffer, "</file-strings>\n");
    *text = xx_buf_detach(&buffer, NULL); xx_buf_free(&buffer); cdie_strings_free(data);
    if (!*text) *error = cdie_strdup("Strings report limit or allocation failure");
    return *text != NULL;
}
