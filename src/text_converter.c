#include "text_converter.h"

#include <string.h>

G_DEFINE_QUARK(binark-text-error-quark, binark_text_error)

static const gchar text_digits[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/";

static gboolean
is_text_separator(guchar c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\v' || c == '\f';
}

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

        if (is_text_separator(c)) {
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

    if (bytes->len > 0 &&
        memchr(bytes->data, '\0', bytes->len) != NULL) {
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

guint
binark_text_base_width(guint base)
{
    guint width = 1;
    guint64 capacity;

    if (base < 2 || base > 64)
        return 0;

    capacity = base;
    while (capacity < 256) {
        capacity *= base;
        width++;
    }
    return width;
}

gint
binark_text_digit_value(guchar c, guint base)
{
    gint value = -1;

    if (base < 2 || base > 64)
        return -1;

    if (c >= '0' && c <= '9') {
        value = c - '0';
    } else if (base <= 36) {
        if (c >= 'a' && c <= 'z')
            value = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z')
            value = c - 'A' + 10;
    } else {
        const gchar *position = strchr(text_digits, c);

        if (position != NULL)
            value = (gint)(position - text_digits);
    }

    if (value < 0 || (guint)value >= base)
        return -1;
    return value;
}

guint
binark_text_format_byte(guint8 byte, guint base, gchar buffer[8])
{
    guint width = binark_text_base_width(base);
    guint value = byte;
    guint i;

    if (width == 0 || buffer == NULL)
        return 0;

    for (i = width; i > 0; i--) {
        buffer[i - 1] = text_digits[value % base];
        value /= base;
    }
    return width;
}

static gchar *
text_encode_base_bytes_impl(const guchar *data,
                            gsize len,
                            guint base,
                            gboolean separated,
                            guint bytes_per_line,
                            gsize *out_len)
{
    guint width = binark_text_base_width(base);
    GString *out;
    gsize i;
    guint column = 0;
    gchar digits[8];

    if (width == 0 || data == NULL || len == 0) {
        if (out_len != NULL)
            *out_len = 0;
        return g_strdup("");
    }

    out = g_string_sized_new(len * (width + (separated ? 1 : 0)) + 2);

    for (i = 0; i < len; i++) {
        if (separated && column != 0)
            g_string_append_c(out, ' ');

        binark_text_format_byte(data[i], base, digits);
        g_string_append_len(out, digits, width);
        column++;

        if (separated && bytes_per_line > 0 && column >= bytes_per_line) {
            if (i + 1 < len)
                g_string_append_c(out, '\n');
            column = 0;
        }
    }

    if (out_len != NULL)
        *out_len = out->len;
    return g_string_free(out, FALSE);
}

static gboolean
text_decode_base_bytes_impl(const gchar *input,
                            gsize input_len,
                            guint base,
                            GByteArray **bytes_out,
                            GError **error)
{
    guint width = binark_text_base_width(base);
    GByteArray *bytes;
    guint value = 0;
    guint count = 0;
    gsize i;
    gsize line = 1;
    gsize column = 1;

    if (bytes_out == NULL) {
        g_set_error(error,
                    BINARK_TEXT_ERROR,
                    BINARK_TEXT_ERROR_INVALID_CHARACTER,
                    "Не задан буфер результата.");
        return FALSE;
    }
    *bytes_out = NULL;

    if (width == 0) {
        g_set_error(error,
                    BINARK_TEXT_ERROR,
                    BINARK_TEXT_ERROR_INVALID_CHARACTER,
                    "Поддерживаются системы счисления с основаниями "
                    "от 2 до 64.");
        return FALSE;
    }

    if (input == NULL)
        input_len = 0;

    bytes = g_byte_array_sized_new(input_len / width + 1);

    for (i = 0; i < input_len; i++) {
        guchar c = (guchar)input[i];
        gint digit;

        if (is_text_separator(c)) {
            if (c == '\n') {
                line++;
                column = 1;
            } else {
                column++;
            }
            continue;
        }

        digit = binark_text_digit_value(c, base);
        if (digit < 0) {
            gchar printable[64];

            if (c >= 0x20 && c <= 0x7e)
                g_snprintf(printable, sizeof(printable), "«%c»", c);
            else
                g_snprintf(printable, sizeof(printable), "байт 0x%02X", c);

            g_set_error(error,
                        BINARK_TEXT_ERROR,
                        BINARK_TEXT_ERROR_INVALID_CHARACTER,
                        "Недопустимый символ %s в строке %" G_GSIZE_FORMAT
                        ", столбце %" G_GSIZE_FORMAT
                        " для системы счисления с основанием %u.",
                        printable, line, column, base);
            g_byte_array_unref(bytes);
            return FALSE;
        }

        value = value * base + (guint)digit;
        count++;
        column++;

        if (count == width) {
            if (value > 255) {
                g_set_error(error,
                            BINARK_TEXT_ERROR,
                            BINARK_TEXT_ERROR_INVALID_CHARACTER,
                            "Значение %u в строке %" G_GSIZE_FORMAT
                            " больше 255 и не является байтом.",
                            value, line);
                g_byte_array_unref(bytes);
                return FALSE;
            }

            {
                guint8 byte = (guint8)value;
                g_byte_array_append(bytes, &byte, 1);
            }
            value = 0;
            count = 0;
        }
    }

    if (count != 0) {
        g_set_error(error,
                    BINARK_TEXT_ERROR,
                    BINARK_TEXT_ERROR_INCOMPLETE_BYTE,
                    "Неполная группа: в конце осталось цифр: %u, а нужно %u.",
                    count, width);
        g_byte_array_unref(bytes);
        return FALSE;
    }

    *bytes_out = bytes;
    return TRUE;
}

gchar *
binark_text_encode_base(const gchar *text,
                        gsize text_len,
                        guint base,
                        gboolean separated,
                        guint bytes_per_line,
                        gsize *out_len)
{
    if (base == 2)
        return binark_text_encode(text, text_len, separated, bytes_per_line,
                                  out_len);
    return text_encode_base_bytes_impl((const guchar *)text, text_len, base,
                                       separated, bytes_per_line, out_len);
}

gboolean
binark_text_decode_base(const gchar *input,
                        gsize input_len,
                        guint base,
                        gchar **text,
                        gsize *text_len,
                        GError **error)
{
    GByteArray *bytes = NULL;
    const gchar *invalid = NULL;
    gchar *result;

    g_return_val_if_fail(text != NULL, FALSE);

    *text = NULL;
    if (text_len != NULL)
        *text_len = 0;

    if (base == 2)
        return binark_text_decode(input, input_len, text, text_len, error);

    if (!text_decode_base_bytes_impl(input, input_len, base, &bytes, error))
        return FALSE;

    if (bytes->len > 0 && memchr(bytes->data, '\0', bytes->len) != NULL) {
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

gchar *
binark_text_encode_base_bytes(const GByteArray *bytes,
                              guint base,
                              gboolean separated,
                              guint bytes_per_line,
                              gsize *out_len)
{
    if (bytes == NULL)
        return text_encode_base_bytes_impl(NULL, 0, base, separated,
                                           bytes_per_line, out_len);
    return text_encode_base_bytes_impl(bytes->data, bytes->len, base,
                                       separated, bytes_per_line, out_len);
}

gboolean
binark_text_decode_base_bytes(const gchar *input,
                              gsize input_len,
                              guint base,
                              GByteArray **bytes,
                              GError **error)
{
    return text_decode_base_bytes_impl(input, input_len, base, bytes, error);
}
