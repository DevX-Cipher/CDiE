/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "cdie_file_info.h"
#include "../app/cdie_app.h"
#include <xxfclib/buf/xx_buf.h>
#include <xxfclib/formats/executable/pe/xx_pe.h>
#include <xxfclib/formats/executable/elf/xx_elf.h>
#include <xxfclib/formats/executable/macho/xx_macho.h>
#include <xxfclib/strings/xx_string.h>
#include <xxfclib/terminal/xx_terminal.h>
#include <limits.h>

#define INFO_MAX_STRUCTURES 65536U
#define INFO_MAX_FIELDS 262144U
#define INFO_MAX_OUTPUT (64U * 1024U * 1024U)

typedef struct info_reader {
    xx_pe pe;
    xx_elf elf;
    xx_macho macho;
    Abstractformat *format;
    const char *family;
    const char *type;
    uint32_t last_id;
} info_reader;

typedef struct info_writer {
    xx_buf_t buffer;
    cdie_output_format output;
    int first_structure;
    int first_field;
    const char *path;
    const char *structure;
} info_writer;

static xxfc_status_t info_sink(void *context, const char *data, size_t size)
{
    xx_buf_t *buffer = context;
    if (size > INFO_MAX_OUTPUT || buffer->size > INFO_MAX_OUTPUT - size) {
        buffer->failed = true;
        return XXFC_ERR_OUT_OF_BOUNDS;
    }
    return xx_buf_append(buffer, data, size) ? XXFC_OK : XXFC_ERR_OUT_OF_MEMORY;
}

static void escaped(info_writer *writer, const char *text)
{
    xx_terminal_writer terminal;
    xx_terminal_writer_init(&terminal, info_sink, &writer->buffer);
    if (writer->output == CDIE_OUTPUT_JSON)
        xx_terminal_writer_json_string(&terminal, text ? text : "");
    else xx_terminal_writer_xml_attribute(&terminal, text ? text : "");
    if (xx_terminal_writer_status(&terminal) != XXFC_OK) writer->buffer.failed = true;
}

static void cell(info_writer *writer, const char *text)
{
    const char *p = text ? text : "";
    xx_buf_append_char(&writer->buffer, '"');
    for (; *p; ++p) {
        if (*p == '"') xx_buf_append_char(&writer->buffer, '"');
        xx_buf_append_char(&writer->buffer, *p);
    }
    xx_buf_append_char(&writer->buffer, '"');
}

static void row(info_writer *writer, const char *structure, int64_t offset, int64_t size, uint64_t count,
    const char *field, const char *type, const char *raw, const char *value)
{
    char separator = writer->output == CDIE_OUTPUT_CSV ? ';' : '\t';
    cell(writer, writer->path); xx_buf_append_char(&writer->buffer, separator);
    cell(writer, structure); xx_buf_append_char(&writer->buffer, separator);
    xx_buf_appendf(&writer->buffer, "%lld%c%lld%c", (long long)offset, separator, (long long)size, separator);
    if (count) xx_buf_appendf(&writer->buffer, "%llu", (unsigned long long)count);
    xx_buf_append_char(&writer->buffer, separator);
    cell(writer, field); xx_buf_append_char(&writer->buffer, separator);
    cell(writer, type); xx_buf_append_char(&writer->buffer, separator);
    cell(writer, raw); xx_buf_append_char(&writer->buffer, separator);
    cell(writer, value); xx_buf_append_char(&writer->buffer, '\n');
}

static void begin_document(info_writer *writer, const char *path, const char *type, int64_t size)
{
    xx_buf_t *buffer = &writer->buffer;
    writer->first_structure = 1;
    writer->path = path;
    if (writer->output == CDIE_OUTPUT_JSON) {
        xx_buf_append_str(buffer, "{\"path\":"); escaped(writer, path);
        xx_buf_append_str(buffer, ",\"filetype\":"); escaped(writer, type);
        xx_buf_appendf(buffer, ",\"size\":%lld,\"structures\":[", (long long)size);
    } else if (writer->output == CDIE_OUTPUT_XML) {
        xx_buf_append_str(buffer, "<file-info path=\""); escaped(writer, path);
        xx_buf_append_str(buffer, "\" filetype=\""); escaped(writer, type);
        xx_buf_appendf(buffer, "\" size=\"%lld\">\n", (long long)size);
    } else if (writer->output == CDIE_OUTPUT_CSV || writer->output == CDIE_OUTPUT_TSV) {
        char separator = writer->output == CDIE_OUTPUT_CSV ? ';' : '\t';
        xx_buf_appendf(buffer, "Path%cStructure%cOffset%cSize%cCount%cField%cType%cRaw%cValue\n",
            separator, separator, separator, separator, separator, separator, separator, separator);
    } else {
        xx_buf_appendf(buffer, "%s\n    File: %s\n    Size: %lld\n", type, path ? path : "", (long long)size);
    }
}

static void begin_structure(info_writer *writer, const char *name, const xx_data_struct *item)
{
    xx_buf_t *buffer = &writer->buffer;
    writer->first_field = 1;
    if (writer->output == CDIE_OUTPUT_JSON) {
        if (!writer->first_structure) xx_buf_append_char(buffer, ',');
        xx_buf_append_str(buffer, "{\"name\":"); escaped(writer, name);
        xx_buf_appendf(buffer, ",\"offset\":%lld,\"size\":%lld,\"count\":%llu,\"fields\":[",
            (long long)item->offset, (long long)item->total_size, (unsigned long long)item->count);
    } else if (writer->output == CDIE_OUTPUT_XML) {
        xx_buf_append_str(buffer, "  <structure name=\""); escaped(writer, name);
        xx_buf_appendf(buffer, "\" offset=\"%lld\" size=\"%lld\" count=\"%llu\">\n",
            (long long)item->offset, (long long)item->total_size, (unsigned long long)item->count);
    } else if (writer->output == CDIE_OUTPUT_CSV || writer->output == CDIE_OUTPUT_TSV) {
        row(writer, name, item->offset, item->total_size, item->count, "", "", "", "");
    } else {
        xx_buf_appendf(buffer, "    %s [Offset=0x%llx, Size=0x%llx, Count=%llu]\n", name,
            (unsigned long long)item->offset, (unsigned long long)item->total_size, (unsigned long long)item->count);
    }
    writer->first_structure = 0;
}

static void end_structure(info_writer *writer)
{
    if (writer->output == CDIE_OUTPUT_JSON) xx_buf_append_str(&writer->buffer, "]}");
    else if (writer->output == CDIE_OUTPUT_XML) xx_buf_append_str(&writer->buffer, "  </structure>\n");
}

static int emit_field(info_writer *writer, const char *structure, const xx_data_struct_record *record)
{
    char *name = xx_str_unicode_to_utf8(record->name ? record->name : L"");
    char *type = xx_str_unicode_to_utf8(record->type ? record->type : L"");
    char *raw = xx_str_unicode_to_utf8(record->raw_value ? record->raw_value : L"");
    char *value = xx_str_unicode_to_utf8(record->display_value ? record->display_value : L"");
    xx_buf_t *buffer = &writer->buffer;
    int ok = name && type && raw && value;
    if (!ok) goto done;
    if (writer->output == CDIE_OUTPUT_JSON) {
        if (!writer->first_field) xx_buf_append_char(buffer, ',');
        xx_buf_append_str(buffer, "{\"name\":"); escaped(writer, name);
        xx_buf_append_str(buffer, ",\"type\":"); escaped(writer, type);
        xx_buf_appendf(buffer, ",\"offset\":%lld,\"size\":%lld,\"raw\":", (long long)record->offset, (long long)record->size);
        escaped(writer, raw); xx_buf_append_str(buffer, ",\"value\":"); escaped(writer, value);
        xx_buf_append_char(buffer, '}');
    } else if (writer->output == CDIE_OUTPUT_XML) {
        xx_buf_append_str(buffer, "    <field name=\""); escaped(writer, name);
        xx_buf_append_str(buffer, "\" type=\""); escaped(writer, type);
        xx_buf_appendf(buffer, "\" offset=\"%lld\" size=\"%lld\" raw=\"", (long long)record->offset, (long long)record->size);
        escaped(writer, raw); xx_buf_append_str(buffer, "\" value=\""); escaped(writer, value);
        xx_buf_append_str(buffer, "\"/>\n");
    } else if (writer->output == CDIE_OUTPUT_CSV || writer->output == CDIE_OUTPUT_TSV) {
        row(writer, structure, record->offset, record->size, 0, name, type, raw, value);
    } else xx_buf_appendf(buffer, "        %s: %s [%s, +0x%llx, %lld bytes, raw=%s]\n",
        name, value, type, (unsigned long long)record->offset, (long long)record->size, raw);
    writer->first_field = 0;
    ok = xx_buf_ok(buffer) && buffer->size <= INFO_MAX_OUTPUT;
done:
    xx_str_free(name); xx_str_free(type); xx_str_free(raw); xx_str_free(value);
    return ok;
}

static void select_family(info_reader *reader, int family)
{
    reader->format = family == 0 ? &reader->pe.format : family == 1 ? &reader->elf.format : &reader->macho.format;
    reader->family = family == 0 ? "PE" : family == 1 ? "ELF" : "MACHO";
    reader->last_id = family == 0 ? XX_PE_DATA_STRUCT_LAST : family == 1 ? XX_ELF_DATA_STRUCT_LAST : XX_MACHO_DATA_STRUCT_LAST;
}

int cdie_file_info_visit(const char *path, const char *selector, int inventory,
    const cdie_file_info_visitor *visitor, void *user, xx_pd_struct *external_progress, char **error)
{
    info_reader reader;
    xx_io_device *device = NULL;
    xx_data_struct_state *state = NULL;
    xx_data_struct_record_state *fields = NULL;
    xx_pd_struct private_progress = {0};
    xx_pd_struct *progress = external_progress ? external_progress : &private_progress;
    const xx_data_struct *item;
    const xx_data_struct_record *record;
    const char *message = "Cannot read DataStructs", *field_filter = NULL;
    char *selection = NULL, *qualified, *hash;
    uint32_t selected_id = 0;
    size_t structures = 0, visited = 0, field_count = 0, matched_fields = 0;
    int family, found = 0, ok = 0;
    int64_t size = -1;
    if (!error) return 0;
    *error = NULL;
    if (!visitor || !visitor->begin_file || !visitor->begin_structure || !visitor->field || !visitor->end_structure) {
        *error = cdie_strdup("Invalid File Information visitor"); return 0;
    }
    xx_rt_memset(&reader, 0, sizeof(reader));
    if (xx_pd_is_stopped(progress)) { message = "File Information cancelled"; goto finish; }
    if (path) {
        device = xx_io_file_open(path, "rb");
        if (!device) { message = "Cannot open file"; goto finish; }
        size = xx_io_total_size(device);
        if (size < 0) { message = "Cannot determine file size"; goto finish; }
    } else if (!inventory) { message = "File information requires a target"; goto finish; }
    xx_pe_init(&reader.pe, device, 0);
    xx_elf_init(&reader.elf, device, 0);
    xx_macho_init(&reader.macho, device, 0);
    if (!path) {
        if (!visitor->begin_file("", "Supported DataStructs", -1, user)) goto collect_failure;
        for (family = 0; family < 3; ++family) {
            uint32_t id;
            select_family(&reader, family);
            for (id = 1; id <= reader.last_id; ++id) {
                const char *name = xx_format_data_struct_id_to_string(reader.format, id);
                char qualified_name[256];
                xx_data_struct catalog = {0};
                if (!name || !*name) continue;
                catalog.offset = catalog.address = catalog.total_size = -1;
                xx_rt_snprintf(qualified_name, sizeof(qualified_name), "%s::%s", reader.family, name);
                if (!visitor->begin_structure(qualified_name, &catalog, user) || !visitor->end_structure(user)) goto collect_failure;
            }
        }
        ok = 1; goto finish;
    }
    for (family = 0; family < 3; ++family) {
        select_family(&reader, family);
        if (reader.format->check_is_valid(reader.format, NULL)) { found = 1; break; }
    }
    if (!found) { message = "File information supports PE, ELF and thin Mach-O only"; goto finish; }
    if (!xx_format_handle_base_info(reader.format, progress) || !xx_format_has_data_structs(reader.format)) goto finish;
    reader.type = xx_format_file_type_to_string(reader.format->file_type);
    if (selector && xx_rt_strcmp(selector, "Info")) {
        selection = cdie_strdup(selector);
        if (!selection) { message = "Out of memory"; goto finish; }
        qualified = xx_rt_strstr(selection, "::");
        if (qualified) {
            *qualified = 0;
            if (xx_rt_strcmp(selection, reader.family)) { message = "Structure belongs to a different format"; goto finish; }
            qualified += 2;
        } else qualified = selection;
        hash = xx_rt_strchr(qualified, '#');
        if (hash) { *hash = 0; field_filter = hash + 1; }
        selected_id = xx_format_data_struct_string_to_id(reader.format, qualified);
        if (!selected_id || (field_filter && !*field_filter)) { message = "Unknown structure or empty field selector"; goto finish; }
    }
    if (!visitor->begin_file(path, reader.type, size, user)) goto collect_failure;
    state = xx_format_create_data_structs_reading(reader.format, progress);
    if (!state) goto finish;
    found = 0;
    while ((item = xx_format_get_current_data_struct(reader.format, state)) != NULL) {
        const char *name;
        if (++visited > INFO_MAX_STRUCTURES) { message = "DataStructs structure limit exceeded"; goto finish; }
        if (!selected_id || item->id == selected_id) {
            uint64_t occurrence, occurrences = 1;
            name = xx_format_data_struct_id_to_string(reader.format, item->id);
            if (!name) name = "Unknown";
            found = 1;
            if (!inventory && item->type != XX_DATA_STRUCT_TYPE_RAW_DATA && item->count > 1 && item->entry_size > 0) {
                if (item->total_size <= 0 || item->count > (uint64_t)(item->total_size / item->entry_size)) {
                    message = "Invalid DataStructs table extent"; goto finish;
                }
                occurrences = item->count;
            }
            for (occurrence = 0; occurrence < occurrences; ++occurrence) {
                xx_data_struct entry = *item;
                if (++structures > INFO_MAX_STRUCTURES) { message = "DataStructs structure limit exceeded"; goto finish; }
                if (occurrences > 1) {
                    int64_t delta = (int64_t)occurrence * item->entry_size;
                    if (item->offset < 0 || item->offset > INT64_MAX - delta) {
                        message = "DataStructs table offset overflow"; goto finish;
                    }
                    entry.offset += delta;
                    entry.address = item->address >= 0 && item->address <= INT64_MAX - delta ? item->address + delta : -1;
                    entry.total_size = entry.entry_size;
                    entry.count = 1;
                }
                if (!visitor->begin_structure(name, &entry, user)) goto collect_failure;
                /* Native RAW_DATA descriptors intentionally have no named-field
                 * schema (DOS stubs, padding, overlays and opaque payloads). */
                if (!inventory && item->type != XX_DATA_STRUCT_TYPE_RAW_DATA) {
                    fields = xx_format_create_data_struct_records_reading(reader.format, &entry, progress);
                    if (!fields) goto finish;
                    while ((record = xx_format_get_current_data_struct_record(reader.format, fields)) != NULL) {
                        char *field_name;
                        int match;
                        if (++field_count > INFO_MAX_FIELDS) { message = "DataStructs field limit exceeded"; goto finish; }
                        field_name = xx_str_unicode_to_utf8(record->name ? record->name : L"");
                        if (!field_name) { message = "Out of memory"; goto finish; }
                        match = !field_filter || !xx_rt_strcmp(field_name, field_filter);
                        xx_str_free(field_name);
                        if (match) {
                            ++matched_fields;
                            if (!visitor->field(record, user)) goto collect_failure;
                        }
                        if (!xx_format_data_struct_record_move_to_next(reader.format, fields, progress)) break;
                    }
                    xx_format_free_data_struct_records_reading(reader.format, fields); fields = NULL;
                }
                if (!visitor->end_structure(user)) goto collect_failure;
            }
        }
        if (!xx_format_data_struct_move_to_next(reader.format, state, progress)) break;
    }
    if (progress->last_error || progress->is_stop) goto finish;
    if (selected_id && !found) { message = "Structure is not present in this file"; goto finish; }
    if (field_filter && !matched_fields) { message = "Field is not present in the selected structure"; goto finish; }
    ok = 1;
    goto finish;
collect_failure:
    message = "File Information collector failed";
finish:
    /* A stop requested by an observer is returned by is_stopped without
     * necessarily setting the sticky flag. Preserve it for GUI callers. */
    if (xx_pd_is_stopped(progress)) {
        xx_pd_stop(progress);
        ok = 0;
        message = "File Information cancelled";
    }
    if (fields) xx_format_free_data_struct_records_reading(reader.format, fields);
    if (state) xx_format_free_data_structs_reading(reader.format, state);
    xx_pe_destroy(&reader.pe); xx_elf_destroy(&reader.elf); xx_macho_destroy(&reader.macho);
    if (device) xx_io_close(device);
    xx_rt_free(selection);
    if (!ok) *error = cdie_strdup(progress->error_string[0] ? progress->error_string : message);
    return ok;
}

static int format_begin_file(const char *path, const char *type, int64_t size, void *user)
{
    info_writer *writer = user;
    begin_document(writer, path, type, size);
    return xx_buf_ok(&writer->buffer);
}
static int format_begin_structure(const char *name, const xx_data_struct *item, void *user)
{
    info_writer *writer = user;
    writer->structure = name;
    begin_structure(writer, name, item);
    return xx_buf_ok(&writer->buffer) && writer->buffer.size <= INFO_MAX_OUTPUT;
}
static int format_field(const xx_data_struct_record *record, void *user)
{
    info_writer *writer = user;
    return emit_field(writer, writer->structure, record);
}
static int format_end_structure(void *user)
{
    info_writer *writer = user;
    end_structure(writer);
    return xx_buf_ok(&writer->buffer) && writer->buffer.size <= INFO_MAX_OUTPUT;
}

int cdie_file_info(const char *path, const char *selector, int inventory,
    cdie_output_format output, char **text, char **error)
{
    static const cdie_file_info_visitor visitor = {
        format_begin_file, format_begin_structure, format_field, format_end_structure
    };
    info_writer writer;
    int ok;
    if (!text || !error) return 0;
    *text = NULL;
    xx_buf_init(&writer.buffer); writer.output = output;
    ok = cdie_file_info_visit(path, selector, inventory, &visitor, &writer, NULL, error);
    if (ok) {
        if (output == CDIE_OUTPUT_JSON) xx_buf_append_str(&writer.buffer, "]}\n");
        else if (output == CDIE_OUTPUT_XML) xx_buf_append_str(&writer.buffer, "</file-info>\n");
        *text = xx_buf_detach(&writer.buffer, NULL);
        ok = *text != NULL;
        if (!ok) *error = cdie_strdup("Cannot allocate File Information report");
    }
    xx_buf_free(&writer.buffer);
    return ok;
}
