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
 * Convert UTF-8 text to standard Base64 (RFC 4648) and back. The result or
 * input contains only Base64 characters and padding; embedded whitespace is
 * allowed around the encoded stream.
 */
gchar *binark_text_encode_base64(const gchar *text,
                                 gsize text_len,
                                 gsize *out_len);
gboolean binark_text_decode_base64(const gchar *input,
                                   gsize input_len,
                                   gchar **text,
                                   gsize *text_len,
                                   GError **error);

/*
 * Convert UTF-8 text to standard Base32 (RFC 4648) and back. The result or
 * input contains only Base32 characters and padding; embedded whitespace is
 * allowed around the encoded stream.
 */
gchar *binark_text_encode_base32(const gchar *text,
                                 gsize text_len,
                                 gsize *out_len);
gboolean binark_text_decode_base32(const gchar *input,
                                   gsize input_len,
                                   gchar **text,
                                   gsize *text_len,
                                   GError **error);

G_END_DECLS

#endif /* BINARK_TEXT_CONVERTER_H */
