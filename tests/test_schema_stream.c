/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Exercise generic schema dispatch through the public API and focused engine
 * archive. No native BMP reader or full format registry is needed. */
#include <xxfclib/formats/xx_format.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* Keep checks active in Release builds, where NDEBUG disables assert(). */
#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "schema stream line %d: %s\n", __LINE__, #expression); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void put_le16(unsigned char *bytes, unsigned value)
{
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
}

static void put_le32(unsigned char *bytes, unsigned value)
{
    unsigned i;
    for (i = 0; i < 4; ++i) bytes[i] = (unsigned char)(value >> (8 * i));
}

static void make_bmp(unsigned char bytes[58])
{
    /* One 24-bit pixel, one padding byte, and the standard 14 + 40 headers. */
    memset(bytes, 0, 58);
    bytes[0] = 'B';
    bytes[1] = 'M';
    put_le32(bytes + 2, 58);
    put_le32(bytes + 10, 54);
    put_le32(bytes + 14, 40);
    put_le32(bytes + 18, 1);
    put_le32(bytes + 22, 1);
    put_le16(bytes + 26, 1);
    put_le16(bytes + 28, 24);
    put_le32(bytes + 34, 4);
    bytes[54] = 0x33;
    bytes[55] = 0x22;
    bytes[56] = 0x11;
}

static Abstractformat *make_format(xx_io_device *device, int64_t base)
{
    Abstractformat *format = xx_format_create(device, base);
    CHECK(format);
    xx_format_set_file_type(format, XX_FILE_TYPE_BMP);
    CHECK(xx_format_has_data_structs(format));
    return format;
}

static void check_field(Abstractformat *format, const xx_data_struct *structure,
                        const wchar_t *name, uint64_t value,
                        int64_t relative_offset, int64_t width)
{
    xx_data_struct_record_state *state = xx_format_create_data_struct_records_reading(format, structure, NULL);
    unsigned found = 0;
    CHECK(state);
    do {
        const xx_data_struct_record *field = xx_format_get_current_data_struct_record(format, state);
        CHECK(field && field->name);
        if (!wcscmp(field->name, name)) {
            CHECK(xx_var_get_u64(&field->value) == value);
            CHECK(field->offset == relative_offset);
            CHECK(field->size == width);
            ++found;
        }
    } while (xx_format_data_struct_record_move_to_next(format, state, NULL));
    CHECK(found == 1);
    xx_format_free_data_struct_records_reading(format, state);
}

static void check_valid_bmp(const unsigned char *bytes, size_t size, int64_t base)
{
    xx_io_device *device = xx_io_mem_open_ro(bytes, size);
    Abstractformat *format;
    xx_data_struct_state *state;
    const xx_data_struct *structure;
    CHECK(device);
    format = make_format(device, base);
    state = xx_format_create_data_structs_reading(format, NULL);
    CHECK(state && state->total_structs == 3);

    structure = xx_format_get_current_data_struct(format, state);
    CHECK(structure);
    CHECK(!strcmp(xx_format_data_struct_id_to_string(format, structure->id), "BITMAPFILEHEADER"));
    CHECK(structure->id == xx_format_data_struct_string_to_id(format, "BITMAPFILEHEADER"));
    CHECK(structure->offset == base && structure->entry_size == 14);
    CHECK(structure->total_size == 14 && structure->count == 1);
    check_field(format, structure, L"bfType", 0x4d42, 0, 2);
    check_field(format, structure, L"bfSize", 58, 2, 4);
    check_field(format, structure, L"bfOffBits", 54, 10, 4);

    CHECK(xx_format_data_struct_move_to_next(format, state, NULL));
    structure = xx_format_get_current_data_struct(format, state);
    CHECK(structure);
    CHECK(!strcmp(xx_format_data_struct_id_to_string(format, structure->id), "BITMAPINFOHEADER"));
    CHECK(structure->offset == base + 14 && structure->entry_size == 40 && structure->total_size == 40);
    check_field(format, structure, L"biWidth", 1, 4, 4);
    check_field(format, structure, L"biBitCount", 24, 14, 2);

    CHECK(xx_format_data_struct_move_to_next(format, state, NULL));
    structure = xx_format_get_current_data_struct(format, state);
    CHECK(structure && structure->id == XX_DATA_STRUCT_ID_RAW_DATA);
    CHECK(structure->offset == base + 54 && structure->total_size == 4);
    CHECK(!xx_format_data_struct_move_to_next(format, state, NULL));

    xx_format_free_data_structs_reading(format, state);
    xx_format_free(format);
    CHECK(!xx_io_close(device));
}

int main(void)
{
    unsigned char bmp[58], embedded[65];
    xx_io_device *device;
    Abstractformat *format;
    xx_data_struct_state *state;
    xx_pd_struct progress = xx_pd_init();
    size_t size;

    make_bmp(bmp);
    check_valid_bmp(bmp, sizeof(bmp), 0);
    memset(embedded, 0xaa, 7);
    memcpy(embedded + 7, bmp, sizeof(bmp));
    check_valid_bmp(embedded, sizeof(embedded), 7);

    for (size = 0; size < sizeof(bmp); ++size) {
        device = xx_io_mem_open_ro(bmp, size);
        CHECK(device);
        format = make_format(device, 0);
        CHECK(!xx_format_create_data_structs_reading(format, NULL));
        xx_format_free(format);
        CHECK(!xx_io_close(device));
    }

    device = xx_io_mem_open_ro(bmp, sizeof(bmp));
    CHECK(device);
    format = make_format(device, 0);
    state = xx_format_create_data_structs_reading(format, &progress);
    CHECK(state);
    xx_pd_stop(&progress);
    CHECK(!xx_format_data_struct_move_to_next(format, state, &progress));
    xx_format_free_data_structs_reading(format, state);
    CHECK(!xx_format_create_data_structs_reading(format, &progress));
    xx_format_set_file_type(format, XX_FILE_TYPE_UNKNOWN);
    CHECK(!xx_format_has_data_structs(format));
    xx_format_free(format);
    CHECK(!xx_io_close(device));

    puts("PASS: generic BMP schema, decoded fields, embedded offsets, 58 truncations, cancellation");
    return EXIT_SUCCESS;
}
