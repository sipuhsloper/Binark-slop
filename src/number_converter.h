#ifndef BINARK_NUMBER_CONVERTER_H
#define BINARK_NUMBER_CONVERTER_H

#include <glib.h>

G_BEGIN_DECLS

#define BINARK_NUMBER_ERROR (binark_number_error_quark())

typedef enum {
    BINARK_NUMBER_ERROR_EMPTY,
    BINARK_NUMBER_ERROR_INVALID_BASE,
    BINARK_NUMBER_ERROR_INVALID_DIGIT,
    BINARK_NUMBER_ERROR_INVALID_PREFIX,
    BINARK_NUMBER_ERROR_PREFIX_MISMATCH
} BinarkNumberError;

GQuark binark_number_error_quark(void);

/*
 * Convert an arbitrary-precision integer between bases 2, 8, 10 and 16.
 * The input may have a leading '+' or '-', surrounding whitespace and one of
 * the prefixes 0b/0o/0x (case-insensitive). A prefix must match source_base.
 * The result never has a prefix. Hexadecimal output is uppercase when
 * uppercase_hex is TRUE, otherwise lowercase.
 */
gboolean binark_number_convert(const gchar *input,
                               guint source_base,
                               guint target_base,
                               gboolean uppercase_hex,
                               gchar **result,
                               GError **error);

G_END_DECLS

#endif /* BINARK_NUMBER_CONVERTER_H */
