#include "number_converter.h"

#include <string.h>

G_DEFINE_QUARK(binark-number-error-quark, binark_number_error)

static const gchar number_digits[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/";

static gboolean
base_is_supported(guint base)
{
    /* 0–9, A–Z, a–z and two extra symbols give us up to 64 digits. */
    return base >= 2 && base <= 64;
}

static gint
digit_value(guchar c, guint base)
{
    if (c >= '0' && c <= '9')
        return c - '0';

    if (base <= 36) {
        /* For the familiar bases letters are case-insensitive. */
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'Z')
            return c - 'A' + 10;
        return -1;
    }

    /* Above base 36 the case matters: A=10…Z=35, a=36…z=61, +=62, /=63. */
    {
        const gchar *position = strchr(number_digits, c);

        if (position != NULL)
            return (gint)(position - number_digits);
    }
    return -1;
}

gboolean
binark_number_convert(const gchar *input,
                      guint source_base,
                      guint target_base,
                      gboolean uppercase,
                      gchar **result,
                      GError **error)
{
    static const gchar upper_digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const gchar lower_digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    const gchar *digits_table;
    gchar *copy;
    gchar *body;
    gboolean negative = FALSE;
    guint prefix_base = 0;
    gsize body_len;
    gsize i;
    gsize start;
    gsize significant;
    guint8 *magnitude;
    GArray *output_digits;
    GString *out;
    gsize digit_count;

    g_return_val_if_fail(result != NULL, FALSE);

    *result = NULL;

    if (!base_is_supported(source_base) || !base_is_supported(target_base)) {
        g_set_error(error,
                    BINARK_NUMBER_ERROR,
                    BINARK_NUMBER_ERROR_INVALID_BASE,
                    "Поддерживаются системы счисления с основаниями "
                    "от 2 до 64.");
        return FALSE;
    }

    if (input == NULL)
        input = "";

    copy = g_strdup(input);
    body = g_strstrip(copy);

    if (*body == '\0') {
        g_set_error(error,
                    BINARK_NUMBER_ERROR,
                    BINARK_NUMBER_ERROR_EMPTY,
                    "Введите целое число.");
        g_free(copy);
        return FALSE;
    }

    if (*body == '-') {
        negative = TRUE;
        body++;
    } else if (*body == '+' && source_base <= 63) {
        /* In base 64 '+' is itself a digit (62), so it is not a sign there. */
        body++;
    } else if (g_str_has_prefix(body, "\xE2\x88\x92")) {
        /* Accept the typographic minus sign (U+2212) as used in the UI. */
        negative = TRUE;
        body += strlen("\xE2\x88\x92");
    }

    /*
     * Prefixes are only meaningful for the bases in which b/o/x are not
     * digits. Above base 16 these letters are ordinary digits.
     */
    if (source_base <= 16) {
        if (body[0] == '0' && (body[1] == 'b' || body[1] == 'B')) {
            prefix_base = 2;
            body += 2;
        } else if (body[0] == '0' && (body[1] == 'o' || body[1] == 'O')) {
            prefix_base = 8;
            body += 2;
        } else if (body[0] == '0' && (body[1] == 'x' || body[1] == 'X')) {
            prefix_base = 16;
            body += 2;
        }
    }

    if (prefix_base != 0 && prefix_base != source_base) {
        g_set_error(error,
                    BINARK_NUMBER_ERROR,
                    BINARK_NUMBER_ERROR_PREFIX_MISMATCH,
                    "Префикс %s не соответствует выбранной системе счисления "
                    "с основанием %u.",
                    prefix_base == 2 ? "0b" : (prefix_base == 8 ? "0o" : "0x"),
                    source_base);
        g_free(copy);
        return FALSE;
    }

    body_len = strlen(body);
    if (body_len == 0) {
        g_set_error(error,
                    BINARK_NUMBER_ERROR,
                    BINARK_NUMBER_ERROR_EMPTY,
                    "После префикса и знака должна следовать хотя бы одна "
                    "цифра.");
        g_free(copy);
        return FALSE;
    }

    magnitude = g_new(guint8, body_len);
    for (i = 0; i < body_len; i++) {
        gint value = digit_value((guchar)body[i], source_base);

        if (value < 0 || (guint)value >= source_base) {
            g_set_error(error,
                        BINARK_NUMBER_ERROR,
                        BINARK_NUMBER_ERROR_INVALID_DIGIT,
                        "Недопустимая цифра «%c» в позиции %" G_GSIZE_FORMAT
                        " для системы счисления с основанием %u.",
                        g_ascii_isprint((guchar)body[i]) ? body[i] : '?',
                        i + 1,
                        source_base);
            g_free(magnitude);
            g_free(copy);
            return FALSE;
        }
        magnitude[i] = (guint8)value;
    }

    significant = 0;
    while (significant < body_len && magnitude[significant] == 0)
        significant++;

    if (significant == body_len) {
        g_free(magnitude);
        g_free(copy);
        *result = g_strdup("0");
        return TRUE;
    }

    output_digits = g_array_new(FALSE, FALSE, sizeof(guint8));
    start = significant;

    while (start < body_len) {
        guint remainder = 0;
        gsize j;

        for (j = start; j < body_len; j++) {
            guint current = remainder * source_base + magnitude[j];
            magnitude[j] = (guint8)(current / target_base);
            remainder = current % target_base;
        }

        {
            guint8 digit = (guint8)remainder;
            g_array_append_val(output_digits, digit);
        }

        while (start < body_len && magnitude[start] == 0)
            start++;
    }

    g_free(magnitude);
    g_free(copy);

    if (target_base <= 36)
        digits_table = uppercase ? upper_digits : lower_digits;
    else
        digits_table = number_digits;
    digit_count = output_digits->len;
    out = g_string_sized_new(digit_count + (negative ? 2 : 1));
    if (negative)
        g_string_append_c(out, '-');

    for (i = digit_count; i > 0; i--) {
        guint8 digit = g_array_index(output_digits, guint8, i - 1);
        g_string_append_c(out, digits_table[digit]);
    }

    g_array_unref(output_digits);
    *result = g_string_free(out, FALSE);
    return TRUE;
}
