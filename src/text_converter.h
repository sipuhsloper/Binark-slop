#ifndef BINARK_TEXT_CONVERTER_H
#define BINARK_TEXT_CONVERTER_H

#include <glib.h>

G_BEGIN_DECLS

#define BINARK_TEXT_ERROR (binark_text_error_quark())

typedef enum {
    BINARK_TEXT_ERROR_INVALID_CHARACTER,
    BINARK_TEXT_ERROR_INCOMPLETE_BYTE,
    BINARK_TEXT_ERROR_INVALID_UTF8,
    BINARK_TEXT_ERROR_NUL_BYTE
} BinarkTextError;

GQuark binark_text_error_quark(void);

/*
 * Convert UTF-8 text to its byte-wise binary representation.
 * Every source byte is rendered as exactly eight '0'/'1' characters.
 * When separated is TRUE, bytes are separated by spaces and wrapped after
 * bytes_per_line bytes. When separated is FALSE output is compact.
 * out_len may be NULL. The function never returns NULL for valid arguments;
 * an empty input produces an empty, newly allocated string.
 */
gchar *binark_text_encode(const gchar *text,
                          gsize text_len,
                          gboolean separated,
                          guint bytes_per_line,
                          gsize *out_len);

/*
 * Convert a binary string to UTF-8 text. Spaces, tabs, CR and LF are allowed
 * between bits. The total number of bits must be a multiple of eight and the
 * result must be valid UTF-8. Embedded NUL bytes are rejected because this is
 * a text converter; the file converter can be used for arbitrary bytes.
 */
gboolean binark_text_decode(const gchar *bits,
                            gsize bits_len,
                            gchar **text,
                            gsize *text_len,
                            GError **error);

/*
 * Number of characters used to represent one byte in the given base.
 * Returns 0 when the base is not supported. Supported bases are 2..64.
 * For example: base 2 -> 8, base 16 -> 2, base 10 -> 3, base 64 -> 2.
 */
guint binark_text_base_width(guint base);

/*
 * Return the value of a digit in the given base, or -1 when the character is
 * not a valid digit. Bases up to 36 accept letters case-insensitively; above
 * 36 the case matters: A=10..Z=35, a=36..z=61, '+'=62, '/'=63.
 */
gint binark_text_digit_value(guchar c, guint base);

/*
 * Render one byte as exactly binark_text_base_width(base) digits into buffer
 * (which must have room for at least eight characters) and return the width.
 * Returns 0 when the base is not supported.
 */
guint binark_text_format_byte(guint8 byte, guint base, gchar buffer[8]);

/*
 * Convert UTF-8 text to its positional representation in the given base and
 * back. Each byte becomes binark_text_base_width(base) digits, most
 * significant digit first; base 2 therefore behaves exactly like
 * binark_text_encode(). When separated is TRUE, bytes are separated by spaces
 * and wrapped after bytes_per_line bytes. Decoding ignores ASCII whitespace
 * between digits.
 *
 * The byte-oriented variants are used by the file converter and do not check
 * for NUL bytes or valid UTF-8.
 */
gchar *binark_text_encode_base(const gchar *text,
                               gsize text_len,
                               guint base,
                               gboolean separated,
                               guint bytes_per_line,
                               gsize *out_len);
gboolean binark_text_decode_base(const gchar *input,
                                 gsize input_len,
                                 guint base,
                                 gchar **text,
                                 gsize *text_len,
                                 GError **error);

gchar *binark_text_encode_base_bytes(const GByteArray *bytes,
                                     guint base,
                                     gboolean separated,
                                     guint bytes_per_line,
                                     gsize *out_len);
gboolean binark_text_decode_base_bytes(const gchar *input,
                                       gsize input_len,
                                       guint base,
                                       GByteArray **bytes,
                                       GError **error);

G_END_DECLS

#endif /* BINARK_TEXT_CONVERTER_H */
