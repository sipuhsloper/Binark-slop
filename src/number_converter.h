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
 * Convert an arbitrary-precision integer between bases 2 and 64.
 * The input may have a leading '-' (and '+' for bases up to 63), surrounding
 * whitespace and one of the prefixes 0b/0o/0x (case-insensitive). A prefix must
 * match source_base and is only recognised up to base 16.
 * Digits are 0–9, A–Z, a–z, and additionally + and / for base 64. Bases up to
 * 36 accept letters in either case; above 36 the case is significant
 * (A=10…Z=35, a=36…z=61, +=62, /=63). The result never has a prefix. For
 * targets up to base 36 letters are uppercase when uppercase is TRUE, otherwise
 * lowercase; above base 36 the canonical mixed-case alphabet is always used.
 */
gboolean binark_number_convert(const gchar *input,
                               guint source_base,
                               guint target_base,
                               gboolean uppercase,
                               gchar **result,
                               GError **error);

G_END_DECLS

#endif /* BINARK_NUMBER_CONVERTER_H */
