#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "file_converter.h"

typedef struct {
    guint calls;
    guint64 last_done;
    guint64 last_total;
    gboolean monotonic;
    guint64 cancel_at;
} ProgressState;

static gboolean
progress_cb(guint64 done, guint64 total, gpointer user_data)
{
    ProgressState *state = user_data;

    if (state->calls > 0 && done < state->last_done)
        state->monotonic = FALSE;
    state->calls++;
    state->last_done = done;
    state->last_total = total;

    if (state->cancel_at != 0 && done >= state->cancel_at)
        return FALSE;
    return TRUE;
}

static gchar *
path_join(const gchar *dir, const gchar *name)
{
    return g_build_filename(dir, name, NULL);
}

static void
write_bytes(const gchar *path, const guint8 *data, gsize len)
{
    GError *error = NULL;

    g_assert_true(g_file_set_contents(path, (const gchar *)data,
                                      (gssize)len, &error));
    g_assert_no_error(error);
}

static guint8 *
read_bytes(const gchar *path, gsize *length)
{
    gchar *data = NULL;
    gsize len = 0;
    GError *error = NULL;

    g_assert_true(g_file_get_contents(path, &data, &len, &error));
    g_assert_no_error(error);
    *length = len;
    return (guint8 *)data;
}

static void
test_files_roundtrip(void)
{
    GError *error = NULL;
    gchar *dir = g_dir_make_tmp("binark-test-XXXXXX", &error);
    gchar *input = NULL;
    gchar *encoded = NULL;
    gchar *decoded = NULL;
    guint8 source[] = {0x00, 0x01, 0xff, 'A', 0x80, 0x00, 0x7f};
    guint8 *result;
    gsize result_len;
    gchar *text;
    gsize text_len;
    ProgressState progress = {0, 0, 0, TRUE, 0};

    g_assert_no_error(error);
    g_assert_nonnull(dir);

    input = path_join(dir, "input.bin");
    encoded = path_join(dir, "output.bits");
    decoded = path_join(dir, "restored.bin");

    write_bytes(input, source, sizeof(source));

    g_assert_true(binark_file_encode(input, encoded, FALSE, 4, progress_cb,
                                     &progress, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(progress.calls, >, 0);
    g_assert_true(progress.monotonic);
    g_assert_cmpuint(progress.last_total, ==, sizeof(source));
    g_assert_cmpuint(progress.last_done, ==, sizeof(source));

    g_assert_true(g_file_get_contents(encoded, &text, &text_len, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(text_len, ==, 62);
    g_assert_true(g_str_has_prefix(text, "00000000 00000001 11111111 01000001\n"));
    g_free(text);

    progress = (ProgressState){0, 0, 0, TRUE, 0};
    g_assert_true(binark_file_decode(encoded, decoded, progress_cb, &progress,
                                     &error));
    g_assert_no_error(error);
    g_assert_cmpuint(progress.calls, >, 0);

    result = read_bytes(decoded, &result_len);
    g_assert_cmpuint(result_len, ==, sizeof(source));
    g_assert_cmpmem(result, result_len, source, sizeof(source));

    g_free(result);
    g_remove(input);
    g_remove(encoded);
    g_remove(decoded);
    g_rmdir(dir);
    g_free(input);
    g_free(encoded);
    g_free(decoded);
    g_free(dir);
}

static void
test_files_empty_and_compact(void)
{
    GError *error = NULL;
    gchar *dir = g_dir_make_tmp("binark-test-XXXXXX", &error);
    gchar *input = NULL;
    gchar *encoded = NULL;
    gchar *decoded = NULL;
    guint8 *result;
    gsize len;

    g_assert_no_error(error);
    g_assert_nonnull(dir);
    input = path_join(dir, "empty.bin");
    encoded = path_join(dir, "empty.bits");
    decoded = path_join(dir, "empty.out");

    write_bytes(input, (const guint8 *)"", 0);

    g_assert_true(binark_file_encode(input, encoded, FALSE, 8, NULL, NULL,
                                     &error));
    g_assert_no_error(error);
    result = read_bytes(encoded, &len);
    g_assert_cmpuint(len, ==, 0);
    g_free(result);

    g_assert_true(binark_file_decode(encoded, decoded, NULL, NULL, &error));
    g_assert_no_error(error);
    result = read_bytes(decoded, &len);
    g_assert_cmpuint(len, ==, 0);
    g_free(result);

    write_bytes(input, (const guint8 *)"AB", 2);
    g_assert_true(binark_file_encode(input, encoded, TRUE, 1, NULL, NULL,
                                     &error));
    g_assert_no_error(error);
    g_assert_true(g_file_get_contents(encoded, (gchar **)&result, &len,
                                      &error));
    g_assert_no_error(error);
    g_assert_cmpmem(result, len, "0100000101000010", 16);
    g_free(result);

    g_remove(input);
    g_remove(encoded);
    g_remove(decoded);
    g_rmdir(dir);
    g_free(input);
    g_free(encoded);
    g_free(decoded);
    g_free(dir);
}

static void
test_files_invalid_preserves_output(void)
{
    GError *error = NULL;
    gchar *dir = g_dir_make_tmp("binark-test-XXXXXX", &error);
    gchar *input = NULL;
    gchar *output = NULL;
    gchar *data;
    gsize len;

    g_assert_no_error(error);
    g_assert_nonnull(dir);
    input = path_join(dir, "bad.bits");
    output = path_join(dir, "result.bin");

    write_bytes(input, (const guint8 *)"01000001 2", 11);
    write_bytes(output, (const guint8 *)"KEEP", 4);

    g_assert_false(binark_file_decode(input, output, NULL, NULL, &error));
    g_assert_error(error, BINARK_FILE_ERROR,
                   BINARK_FILE_ERROR_INVALID_FORMAT);
    g_clear_error(&error);

    g_assert_true(g_file_get_contents(output, &data, &len, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(len, ==, 4);
    g_assert_cmpmem(data, len, "KEEP", 4);
    g_free(data);

    write_bytes(input, (const guint8 *)"0100000", 7);
    g_assert_false(binark_file_decode(input, output, NULL, NULL, &error));
    g_assert_error(error, BINARK_FILE_ERROR,
                   BINARK_FILE_ERROR_INVALID_FORMAT);
    g_clear_error(&error);

    g_assert_true(g_file_get_contents(output, &data, &len, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(len, ==, 4);
    g_assert_cmpmem(data, len, "KEEP", 4);
    g_free(data);

    g_remove(input);
    g_remove(output);
    g_rmdir(dir);
    g_free(input);
    g_free(output);
    g_free(dir);
}

static void
test_files_cancel(void)
{
    GError *error = NULL;
    gchar *dir = g_dir_make_tmp("binark-test-XXXXXX", &error);
    gchar *input = NULL;
    gchar *output = NULL;
    guint8 *payload;
    gsize size = 300 * 1024;
    ProgressState progress = {0, 0, 0, TRUE, 1};

    g_assert_no_error(error);
    g_assert_nonnull(dir);
    input = path_join(dir, "large.bin");
    output = path_join(dir, "cancelled.bits");

    payload = g_malloc(size);
    memset(payload, 0xaa, size);
    write_bytes(input, payload, size);
    g_free(payload);

    g_assert_false(binark_file_encode(input, output, FALSE, 8, progress_cb,
                                      &progress, &error));
    g_assert_error(error, BINARK_FILE_ERROR, BINARK_FILE_ERROR_CANCELLED);
    g_clear_error(&error);
    g_assert_false(g_file_test(output, G_FILE_TEST_EXISTS));

    g_remove(input);
    g_rmdir(dir);
    g_free(input);
    g_free(output);
    g_free(dir);
}

static void
test_files_errors(void)
{
    GError *error = NULL;
    gchar *dir = g_dir_make_tmp("binark-test-XXXXXX", &error);
    gchar *input = NULL;
    gchar *output = NULL;
    gchar *missing = NULL;
    gchar *bad_dir = NULL;

    g_assert_no_error(error);
    g_assert_nonnull(dir);
    input = path_join(dir, "input.bin");
    output = path_join(dir, "output.bits");
    missing = path_join(dir, "missing.bin");
    bad_dir = g_build_filename(dir, "no-such-dir", "output.bits", NULL);

    write_bytes(input, (const guint8 *)"data", 4);

    g_assert_false(binark_file_encode(missing, output, FALSE, 8, NULL, NULL,
                                      &error));
    g_assert_error(error, BINARK_FILE_ERROR, BINARK_FILE_ERROR_READ);
    g_clear_error(&error);

    g_assert_false(binark_file_encode(input, bad_dir, FALSE, 8, NULL, NULL,
                                      &error));
    g_assert_error(error, BINARK_FILE_ERROR,
                   BINARK_FILE_ERROR_INVALID_PATH);
    g_clear_error(&error);

    g_assert_false(binark_file_encode(input, input, FALSE, 8, NULL, NULL,
                                      &error));
    g_assert_error(error, BINARK_FILE_ERROR, BINARK_FILE_ERROR_SAME_FILE);
    g_clear_error(&error);

    g_assert_false(binark_file_encode(input, "", FALSE, 8, NULL, NULL,
                                      &error));
    g_assert_error(error, BINARK_FILE_ERROR,
                   BINARK_FILE_ERROR_INVALID_PATH);
    g_clear_error(&error);

    g_remove(input);
    g_rmdir(dir);
    g_free(input);
    g_free(output);
    g_free(missing);
    g_free(bad_dir);
    g_free(dir);
}

static void
test_files_large_roundtrip(void)
{
    GError *error = NULL;
    gchar *dir = g_dir_make_tmp("binark-test-XXXXXX", &error);
    gchar *input = NULL;
    gchar *encoded = NULL;
    gchar *decoded = NULL;
    guint8 *payload;
    guint8 *restored;
    gsize size = 1024 * 1024 + 17;
    gsize restored_len;
    gsize i;

    g_assert_no_error(error);
    g_assert_nonnull(dir);
    input = path_join(dir, "large.bin");
    encoded = path_join(dir, "large.bits");
    decoded = path_join(dir, "large.out");

    payload = g_malloc(size);
    for (i = 0; i < size; i++)
        payload[i] = (guint8)((i * 31 + 7) & 0xff);
    write_bytes(input, payload, size);
    g_free(payload);

    g_assert_true(binark_file_encode(input, encoded, FALSE, 32, NULL, NULL,
                                     &error));
    g_assert_no_error(error);
    g_assert_true(binark_file_decode(encoded, decoded, NULL, NULL, &error));
    g_assert_no_error(error);

    restored = read_bytes(decoded, &restored_len);
    g_assert_cmpuint(restored_len, ==, size);
    g_assert_true(g_file_get_contents(input, (gchar **)&payload, &size,
                                      &error));
    g_assert_no_error(error);
    g_assert_cmpmem(restored, restored_len, payload, size);
    g_free(payload);
    g_free(restored);

    g_remove(input);
    g_remove(encoded);
    g_remove(decoded);
    g_rmdir(dir);
    g_free(input);
    g_free(encoded);
    g_free(decoded);
    g_free(dir);
}

static void
test_files_base_roundtrip(void)
{
    GError *error = NULL;
    gchar *dir = g_dir_make_tmp("binark-test-XXXXXX", &error);
    gchar *input = NULL;
    gchar *encoded = NULL;
    gchar *decoded = NULL;
    guint8 source[] = {0x00, 0x01, 0xff, 'A', 0x80};
    guint8 *result;
    gsize result_len;
    gchar *text = NULL;
    gsize text_len = 0;

    g_assert_no_error(error);
    g_assert_nonnull(dir);

    input = path_join(dir, "input.bin");
    encoded = path_join(dir, "output.base10");
    decoded = path_join(dir, "restored.bin");

    write_bytes(input, source, sizeof(source));

    g_assert_true(binark_file_encode_base(input, encoded, 10, FALSE, 8, NULL,
                                          NULL, &error));
    g_assert_no_error(error);
    g_assert_true(g_file_get_contents(encoded, &text, &text_len, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(text, ==, "000 001 255 065 128");
    g_free(text);

    g_assert_true(binark_file_decode_base(encoded, decoded, 10, NULL, NULL,
                                          &error));
    g_assert_no_error(error);
    result = read_bytes(decoded, &result_len);
    g_assert_cmpuint(result_len, ==, sizeof(source));
    g_assert_cmpmem(result, result_len, source, sizeof(source));
    g_free(result);

    /* Compact base 64 representation of the same bytes. */
    g_assert_true(binark_file_encode_base(input, encoded, 64, TRUE, 1, NULL,
                                          NULL, &error));
    g_assert_no_error(error);
    g_assert_true(g_file_get_contents(encoded, &text, &text_len, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(text, ==, "00013/1120");
    g_free(text);

    g_assert_true(binark_file_decode_base(encoded, decoded, 64, NULL, NULL,
                                          &error));
    g_assert_no_error(error);
    result = read_bytes(decoded, &result_len);
    g_assert_cmpuint(result_len, ==, sizeof(source));
    g_assert_cmpmem(result, result_len, source, sizeof(source));
    g_free(result);

    g_remove(input);
    g_remove(encoded);
    g_remove(decoded);
    g_rmdir(dir);
    g_free(input);
    g_free(encoded);
    g_free(decoded);
    g_free(dir);
}

static void
test_files_base_errors(void)
{
    GError *error = NULL;
    gchar *dir = g_dir_make_tmp("binark-test-XXXXXX", &error);
    gchar *input = NULL;
    gchar *output = NULL;

    g_assert_no_error(error);
    g_assert_nonnull(dir);

    input = path_join(dir, "bad.base10");
    output = path_join(dir, "result.bin");

    write_bytes(input, (const guint8 *)"000 999", 7);
    g_assert_false(binark_file_decode_base(input, output, 10, NULL, NULL,
                                           &error));
    g_assert_error(error, BINARK_FILE_ERROR,
                   BINARK_FILE_ERROR_INVALID_FORMAT);
    g_clear_error(&error);

    write_bytes(input, (const guint8 *)"000 00A", 7);
    g_assert_false(binark_file_decode_base(input, output, 10, NULL, NULL,
                                           &error));
    g_assert_error(error, BINARK_FILE_ERROR,
                   BINARK_FILE_ERROR_INVALID_FORMAT);
    g_clear_error(&error);

    g_remove(input);
    g_rmdir(dir);
    g_free(input);
    g_free(output);
    g_free(dir);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/files/roundtrip", test_files_roundtrip);
    g_test_add_func("/files/empty-compact", test_files_empty_and_compact);
    g_test_add_func("/files/invalid-preserves-output",
                    test_files_invalid_preserves_output);
    g_test_add_func("/files/cancel", test_files_cancel);
    g_test_add_func("/files/errors", test_files_errors);
    g_test_add_func("/files/large-roundtrip", test_files_large_roundtrip);
    g_test_add_func("/files/base-roundtrip", test_files_base_roundtrip);
    g_test_add_func("/files/base-errors", test_files_base_errors);

    return g_test_run();
}
