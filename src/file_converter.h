#ifndef BINARK_FILE_CONVERTER_H
#define BINARK_FILE_CONVERTER_H

#include <glib.h>

G_BEGIN_DECLS

#define BINARK_FILE_ERROR (binark_file_error_quark())

typedef enum {
    BINARK_FILE_ERROR_IO,
    BINARK_FILE_ERROR_READ,
    BINARK_FILE_ERROR_WRITE,
    BINARK_FILE_ERROR_INVALID_FORMAT,
    BINARK_FILE_ERROR_CANCELLED,
    BINARK_FILE_ERROR_NO_SPACE,
    BINARK_FILE_ERROR_SAME_FILE,
    BINARK_FILE_ERROR_INVALID_PATH
} BinarkFileError;

GQuark binark_file_error_quark(void);

/*
 * Progress callback. done and total are byte counts of the input file.
 * Return FALSE to cancel the operation. Called from the worker thread, so it
 * must be thread-safe. It is invoked at least once for non-empty files.
 */
typedef gboolean (*BinarkFileProgressFunc)(guint64 done,
                                            guint64 total,
                                            gpointer user_data);

/* Estimated size of the binary text representation of input_size bytes. */
guint64 binark_file_encoded_size(guint64 input_size,
                                 gboolean compact,
                                 guint bytes_per_line);

/* Estimated size of the text representation in the given base. */
guint64 binark_file_encoded_size_base(guint64 input_size,
                                      guint base,
                                      gboolean compact,
                                      guint bytes_per_line);

/* Conservative upper bound of the decoded binary size. */
guint64 binark_file_decoded_size(guint64 input_size);

/* Conservative upper bound of the decoded size for the given base. */
guint64 binark_file_decoded_size_base(guint64 input_size, guint base);

/*
 * Verify that the filesystem containing output_path has at least needed
 * bytes available. Returns TRUE when the check is inconclusive (for example,
 * on filesystems without statvfs support).
 */
gboolean binark_file_check_space(const gchar *output_path,
                                 guint64 needed,
                                 GError **error);

/*
 * Encode a file into a textual bit representation. The output is written to a
 * temporary file in the destination directory and atomically renamed only
 * after a successful complete write. An existing output file is preserved on
 * failure.
 */
gboolean binark_file_encode(const gchar *input_path,
                            const gchar *output_path,
                            gboolean compact,
                            guint bytes_per_line,
                            BinarkFileProgressFunc progress,
                            gpointer user_data,
                            GError **error);

/*
 * Encode a file into a textual representation in the given base. Every input
 * byte becomes binark_text_base_width(base) digits; base 2 produces the usual
 * binary form. The output is written to a temporary file in the destination
 * directory and atomically renamed only after a successful complete write.
 * An existing output file is preserved on failure.
 */
gboolean binark_file_encode_base(const gchar *input_path,
                                 const gchar *output_path,
                                 guint base,
                                 gboolean compact,
                                 guint bytes_per_line,
                                 BinarkFileProgressFunc progress,
                                 gpointer user_data,
                                 GError **error);

/*
 * Decode a textual representation in the given base back to a binary file
 * using the same atomic replacement strategy.
 */
gboolean binark_file_decode_base(const gchar *input_path,
                                 const gchar *output_path,
                                 guint base,
                                 BinarkFileProgressFunc progress,
                                 gpointer user_data,
                                 GError **error);

/*
 * Decode a textual bit representation to a binary file using the same atomic
 * replacement strategy. Only '0', '1' and ASCII whitespace are accepted, and
 * the number of bits must be a multiple of eight.
 */
gboolean binark_file_decode(const gchar *input_path,
                            const gchar *output_path,
                            BinarkFileProgressFunc progress,
                            gpointer user_data,
                            GError **error);

G_END_DECLS

#endif /* BINARK_FILE_CONVERTER_H */
