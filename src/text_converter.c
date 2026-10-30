#include "text_converter.h"

#include <stdint.h>

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

typedef struct {
    const gchar *alphabet;
    int digit_bits;
    int group_bytes;
    int base_kind;
} TextCodec;

static const TextCodec text_codecs[] = {
    { "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
      6, 3, 0 },
    { "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", 5, 5, 1 }
};

static int
text_char_value(guchar c, const TextCodec *codec)
{
    int value = -1;

    if (codec->base_kind == 0) {
        if (c >= 'A' && c <= 'Z')
            value = c - 'A';
        else if (c >= 'a' && c <= 'z')
            value = 26 + (c - 'a');
        else if (c >= '0' && c <= '9')
            value = 52 + (c - '0');
        else if (c == '+')
            value = 62;
        else if (c == '/')
            value = 63;
    } else {
        if (c >= 'A' && c <= 'Z')
            value = c - 'A';
        else if (c >= '2' && c <= '7')
            value = 26 + (c - '2');
    }

    if (value < 0 || value > ((1 << codec->digit_bits) - 1))
        return -1;
    return value;
}

static gchar *
text_encoded_from_bytes(const guchar *bytes,
                        gsize n,
                        const TextCodec *codec,
                        gsize *out_len)
{
    GString *out;
    gsize i = 0;
    int capacity = (int)(codec->group_bytes * 8 / codec->digit_bits);

    if (n == 0) {
        if (out_len != NULL)
            *out_len = 0;
        return g_strdup("");
    }

    out = g_string_sized_new(n + n / 3 * 4 + 4);
    while (i < n) {
        gsize glen = 0;
        uint64_t group = 0;
        int data_chars;
        int total_chars = capacity;
        uint64_t shifted;
        int j;

        while (glen < (size_t)codec->group_bytes && i < n) {
            group = (group << 8) | bytes[i];
            i++;
            glen++;
        }

        data_chars = (int)((glen * 8 + codec->digit_bits - 1) /
                           codec->digit_bits);
        shifted = group << (capacity * codec->digit_bits - (glen * 8));

        for (j = 0; j < total_chars; j++) {
            int shift = (total_chars - 1 - j) * codec->digit_bits;
            int mask = (1 << codec->digit_bits) - 1;
            int value = (int)((shifted >> shift) & mask);

            if (j < data_chars)
                g_string_append_c(out, codec->alphabet[value]);
            else
                g_string_append_c(out, '=');
        }
    }

    if (out_len != NULL)
        *out_len = out->len;
    return g_string_free(out, FALSE);
}

static gboolean
text_decoded_from_base(const gchar *input,
                       gsize input_len,
                       const TextCodec *codec,
                       GByteArray **bytes_out,
                       GError **error)
{
    GString *clean = NULL;
    GByteArray *bytes = NULL;
    gsize len = 0;
    gsize groups;
    gsize g;

    clean = g_string_new("");
    for (gsize i = 0; i < input_len; i++) {
        guchar c = (guchar)input[i];

        if (is_binary_separator(c))
            continue;
        g_string_append_c(clean, (char)c);
    }
    len = clean->len;

    if (len == 0) {
        *bytes_out = g_byte_array_sized_new(0);
        g_string_free(clean, TRUE);
        return TRUE;
    }

    {
        int capacity = (int)(codec->group_bytes * 8 / codec->digit_bits);

        if (len % capacity != 0) {
            g_set_error(error,
                        BINARK_TEXT_ERROR,
                        BINARK_TEXT_ERROR_INVALID_CHARACTER,
                        "Некорректная длина зашифрованной строки.");
            g_string_free(clean, TRUE);
            return FALSE;
        }
    }

    bytes = g_byte_array_sized_new(len / 8 + 1);
    {
        int capacity = (int)(codec->group_bytes * 8 / codec->digit_bits);
        groups = len / (gsize)capacity;

        for (g = 0; g < groups; g++) {
            const gchar *s = clean->str + g * (gsize)capacity;
            int pad = 0;
            int data_chars;
            int bits;
            int emit;
            uint64_t value = 0;
            int j;
            int k;

            while (pad < capacity && s[capacity - 1 - pad] == '=')
                pad++;
            data_chars = capacity - pad;
            if (pad > 0 && g != groups - 1) {
                g_set_error(error,
                            BINARK_TEXT_ERROR,
                            BINARK_TEXT_ERROR_INVALID_CHARACTER,
                            "Недопустительные символы «=» в зашифрованной строке.");
                goto cleanup;
            }
            if (data_chars == 0) {
                g_set_error(error,
                            BINARK_TEXT_ERROR,
                            BINARK_TEXT_ERROR_INVALID_CHARACTER,
                            "Пустая зашифрованная строка.");
                goto cleanup;
            }

            for (j = 0; j < data_chars; j++) {
                int digit = text_char_value((guchar)s[j], codec);

                if (digit < 0) {
                    g_set_error(error,
                                BINARK_TEXT_ERROR,
                                BINARK_TEXT_ERROR_INVALID_CHARACTER,
                                "Недопустимый символ в зашифрованной строке.");
                    goto cleanup;
                }
                value = (value << codec->digit_bits) | (uint64_t)digit;
            }

            bits = data_chars * codec->digit_bits;
            emit = bits / 8;
            for (k = 0; k < emit; k++) {
                int shift = bits - 8 * (k + 1);
                guint8 byte = (guint8)((value >> shift) & 255);
                g_byte_array_append(bytes, &byte, 1);
            }
        }
    }

    *bytes_out = bytes;
    g_string_free(clean, TRUE);
    return TRUE;

cleanup:
    g_byte_array_unref(bytes);
    g_string_free(clean, TRUE);
    return FALSE;
}

static gboolean
text_decode_base_impl(const gchar *input,
                      gsize input_len,
                      const TextCodec *codec,
                      gchar **text,
                      gsize *text_len,
                      GError **error)
{
    GByteArray *bytes = NULL;
    const gchar *invalid = NULL;
    gchar *result = NULL;

    *text = NULL;
    if (text_len != NULL)
        *text_len = 0;

    if (!text_decoded_from_base(input, input_len, codec, &bytes, error)) {
        g_clear_error(error);
        g_set_error(error,
                    BINARK_TEXT_ERROR,
                    BINARK_TEXT_ERROR_INVALID_CHARACTER,
                    "Некорректное зашифрованное значение.");
        return FALSE;
    }

    if (bytes->len == 0) {
        result = g_strdup("");
        *text = result;
        *text_len = 0;
        g_byte_array_unref(bytes);
        return TRUE;
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
    *text = result;
    *text_len = bytes->len;
    g_byte_array_unref(bytes);
    return TRUE;
}

gchar *
binark_text_encode_base64(const gchar *text, gsize text_len, gsize *out_len)
{
    GByteArray *bytes = g_byte_array_sized_new(text_len);
    gchar *result;

    for (gsize i = 0; i < text_len; i++) {
        guchar byte = (guchar)text[i];
        g_byte_array_append(bytes, &byte, 1);
    }
    result = text_encoded_from_bytes(bytes->data, bytes->len,
                                    &text_codecs[0], out_len);
    g_byte_array_unref(bytes);
    return result;
}

gboolean
binark_text_decode_base64(const gchar *input, gsize input_len, gchar **text,
                          gsize *text_len, GError **error)
{
    return text_decode_base_impl(input, input_len, &text_codecs[0], text,
                                 text_len, error);
}

gchar *
binark_text_encode_base32(const gchar *text, gsize text_len, gsize *out_len)
{
    GByteArray *bytes = g_byte_array_sized_new(text_len);
    gchar *result;

    for (gsize i = 0; i < text_len; i++) {
        guchar byte = (guchar)text[i];
        g_byte_array_append(bytes, &byte, 1);
    }
    result = text_encoded_from_bytes(bytes->data, bytes->len,
                                    &text_codecs[1], out_len);
    g_byte_array_unref(bytes);
    return result;
}

gboolean
binark_text_decode_base32(const gchar *input, gsize input_len, gchar **text,
                          gsize *text_len, GError **error)
{
    return text_decode_base_impl(input, input_len, &text_codecs[1], text,
                                 text_len, error);
}
