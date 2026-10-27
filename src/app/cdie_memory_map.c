/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "cdie_memory_map.h"
#include "cdie_app.h"
#include <xxfclib/formats/executable/elf/xx_elf.h>
#include <xxfclib/formats/executable/macho/xx_macho.h>
#include <xxfclib/formats/executable/pe/xx_pe.h>
#include <xxfclib/rt/xx_rt.h>

int cdie_memory_map_read(const char *path, xx_pd_struct *pd, cdie_memory_map_data **out, char **error)
{
    xx_io_device *device = NULL;
    cdie_memory_map_data *data = NULL;
    xx_pe pe;
    xx_elf elf;
    xx_macho macho;
    Abstractformat *formats[3], *format = NULL;
    const xx_memory_map *map;
    const char *message = "Cannot read Memory Map";
    int64_t size;
    size_t i;
    int initialized = 0, ok = 0, level = -1;
    if (!out || !error)
        return 0;
    *out = NULL;
    *error = NULL;
    if (!path || !*path) {
        message = "Memory Map requires a target";
        goto finish;
    }
    if (xx_pd_is_stopped(pd))
        goto finish;
    device = xx_io_file_open(path, "rb");
    if (!device) {
        message = "Cannot open file for Memory Map";
        goto finish;
    }
    size = xx_io_total_size(device);
    if (size < 0) {
        message = "Cannot determine file size";
        goto finish;
    }
    data = xx_rt_calloc(1, sizeof(*data));
    if (!data) {
        message = "Cannot allocate Memory Map";
        goto finish;
    }
    data->file_size = (uint64_t)size;
    data->module_address = data->entry_point_address = XX_INVALID_ADDRESS;
    xx_rt_snprintf(data->file_type, sizeof(data->file_type), "Binary");
    xx_rt_snprintf(data->mode, sizeof(data->mode), "Data");
    xx_pe_init(&pe, device, 0);
    xx_elf_init(&elf, device, 0);
    xx_macho_init(&macho, device, 0);
    initialized = 1;
    formats[0] = &pe.format;
    formats[1] = &elf.format;
    formats[2] = &macho.format;
    for (i = 0; i < 3 && !xx_pd_is_stopped(pd); ++i)
        if (formats[i]->check_is_valid(formats[i], pd)) {
            format = formats[i];
            break;
        }
    if (format) {
        if (!xx_format_handle_base_info(format, pd))
            goto finish;
        map = xx_format_get_memory_map(format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
        if (!map)
            goto finish;
        if (map->record_count > CDIE_MEMORY_MAP_MAX_RECORDS || (map->record_count && !map->records)) {
            message = "Too many or invalid Memory Map records";
            goto finish;
        }
        data->module_address = map->module_address;
        data->entry_point_address = map->entry_point_address;
        if (format == &pe.format && !pe.entry_point_rva)
            data->entry_point_address = XX_INVALID_ADDRESS;
        xx_rt_snprintf(data->file_type, sizeof(data->file_type), "%s", xx_format_file_type_to_string(format->file_type));
        xx_rt_snprintf(data->mode, sizeof(data->mode), "%s", map->mode == XX_MEMORY_MAP_MODE_SECTIONS ? "Sections" : "Segments");
        level = xx_pd_enter_level(pd, map->record_count, "Reading Memory Map");
        for (i = 0; i < map->record_count; ++i) {
            const xx_memory_record *r = map->records + i;
            if (xx_pd_is_stopped(pd))
                goto finish;
            /* Preserve overlap/order, invisible and virtual-only records. A
             * virtual tail has no file range; never invent an offset for it. */
            if (r->size < 0 ||
                (r->is_virtual ? r->offset != -1
                               : (r->offset < 0 || (uint64_t)r->offset > data->file_size ||
                                  (uint64_t)r->size > data->file_size - (uint64_t)r->offset)) ||
                (r->address != XX_INVALID_ADDRESS && (uint64_t)r->size > UINT64_MAX - r->address))
                goto finish;
            data->records[data->count] = *r;
            data->records[data->count].name[XX_MEMORY_RECORD_NAME_SIZE - 1] = 0;
            ++data->count;
            xx_pd_set_current(pd, level, i + 1);
        }
    } else if (size) {
        xx_memory_record *r = data->records;
        r->offset = 0;
        r->address = XX_INVALID_ADDRESS;
        r->size = size;
        r->file_part = XX_FILE_PART_DATA;
        r->file_part_number = -1;
        xx_rt_snprintf(r->name, sizeof(r->name), "Data");
        data->count = 1;
    }
    ok = 1;
finish:
    xx_pd_leave_level(pd, level);
    if (xx_pd_is_stopped(pd)) {
        xx_pd_stop(pd);
        ok = 0;
        message = "Memory Map cancelled";
    }
    if (initialized) {
        xx_pe_destroy(&pe);
        xx_elf_destroy(&elf);
        xx_macho_destroy(&macho);
    }
    if (device)
        xx_io_close(device);
    if (ok)
        *out = data;
    else {
        xx_rt_free(data);
        *error = cdie_strdup(message);
    }
    return ok;
}
