#include "text_converter.h"

#include <string.h>

G_DEFINE_QUARK(binark-text-error-quark, binark_text_error)

gchar *
binark_text_encode(const gchar *text,
                   gsize text_len,
                   gboolean separated,
                   guint bytes_per_line,
                   gsize *out_len)
{
    static const gchar *const bit_table[16] = {
        "0000", "0001", "0010", "0011",
        "0100", "0101", "0110", "0111",
        "1000", "1001", "1010", "1011",
        "1100", "1101", "1110", "1111"
    };
    GString *result;
    gsize i;
    guint column = 0;

    if (text == NULL || text_len == 0) {
        if (out_len != NULL)
            *out_len = 0;
        return g_strdup("");
    }

    /* Eight bits plus at most one separator per source byte. */
    result = g_string_sized_new(text_len * 9 + 1);

    for (i = 0; i < text_len; i++) {
        guint8 byte = (guint8)text[i];

        if (separated && column != 0)
            g_string_append_c(result, ' ');

        g_string_append(result, bit_table[byte >> 4]);
        g_string_append(result, bit_table[byte & 0x0f]);
        column++;

        if (separated && bytes_per_line > 0 && column >= bytes_per_line) {
            if (i + 1 < text_len)
                g_string_append_c(result, '\n');
            column = 0;
        }
    }

    if (out_len != NULL)
        *out_len = result->len;

    return g_string_free(result, FALSE);
}

static gboolean
is_binary_separator(guchar c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\v' || c == '\f';
}

gboolean
binark_text_decode(const gchar *bits,
                   gsize bits_len,
                   gchar **text,
                   gsize *text_len,
                   GError **error)
{
    GByteArray *bytes;
    guint current = 0;
    guint bit_count = 0;
    gsize i;
    gsize line = 1;
    gsize column = 1;
    gchar *result;
    const gchar *invalid = NULL;

    g_return_val_if_fail(text != NULL, FALSE);

    *text = NULL;
    if (text_len != NULL)
        *text_len = 0;

    if (bits == NULL)
        bits_len = 0;

    bytes = g_byte_array_sized_new(bits_len / 8 + 1);

    for (i = 0; i < bits_len; i++) {
        guchar c = (guchar)bits[i];

        if (is_binary_separator(c)) {
            if (c == '\n') {
                line++;
                column = 1;
            } else {
                column++;
            }
            continue;
        }

        if (c != '0' && c != '1') {
            g_set_error(error,
                        BINARK_TEXT_ERROR,
                        BINARK_TEXT_ERROR_INVALID_CHARACTER,
                        "Недопустимый символ в строке %" G_GSIZE_FORMAT
                        ", столбце %" G_GSIZE_FORMAT
                        ". Разрешены только 0, 1 и пробельные символы.",
                        line, column);
            g_byte_array_unref(bytes);
            return FALSE;
        }

        current = (current << 1) | (c - '0');
        bit_count++;
        column++;

        if (bit_count == 8) {
            guint8 byte = (guint8)current;
            g_byte_array_append(bytes, &byte, 1);
            current = 0;
            bit_count = 0;
        }
    }

    if (bit_count != 0) {
        g_set_error(error,
                    BINARK_TEXT_ERROR,
                    BINARK_TEXT_ERROR_INCOMPLETE_BYTE,
                    "Неполный байт: в конце осталось %u бит(а) вместо 8.",
                    bit_count);
        g_byte_array_unref(bytes);
        return FALSE;
    }

    if (memchr(bytes->data, '\0', bytes->len) != NULL) {
        g_set_error(error,
                    BINARK_TEXT_ERROR,
                    BINARK_TEXT_ERROR_NUL_BYTE,
                    "Результат содержит нулевой байт и не может быть показан "
                    "как текст. Используйте режим «Файлы».");
        g_byte_array_unref(bytes);
        return FALSE;
    }

    if (!g_utf8_validate((const gchar *)bytes->data, bytes->len, &invalid)) {
        gsize offset = invalid != NULL
                           ? (gsize)(invalid - (const gchar *)bytes->data)
                           : 0;

        g_set_error(error,
                    BINARK_TEXT_ERROR,
                    BINARK_TEXT_ERROR_INVALID_UTF8,
                    "Полученная последовательность не является корректным "
                    "UTF-8 (ошибка в байте %" G_GSIZE_FORMAT ").",
                    offset + 1);
        g_byte_array_unref(bytes);
        return FALSE;
    }

    result = g_strndup((const gchar *)bytes->data, bytes->len);
    if (text_len != NULL)
        *text_len = bytes->len;
    g_byte_array_unref(bytes);

    *text = result;
    return TRUE;
}
