#include <glib.h>
#include <string.h>

#include "converter.h"
#include "text_converter.h"

static void
test_text_empty(void)
{
    gchar *encoded;
    gchar *decoded = NULL;
    gsize encoded_len = 0;
    gsize decoded_len = 0;
    GError *error = NULL;

    encoded = binark_text_encode("", 0, TRUE, 8, &encoded_len);
    g_assert_nonnull(encoded);
    g_assert_cmpuint(encoded_len, ==, 0);
    g_assert_cmpstr(encoded, ==, "");
    g_free(encoded);

    g_assert_true(binark_text_decode("", 0, &decoded, &decoded_len, &error));
    g_assert_no_error(error);
    g_assert_nonnull(decoded);
    g_assert_cmpstr(decoded, ==, "");
    g_assert_cmpuint(decoded_len, ==, 0);
    g_free(decoded);

    g_assert_true(binark_text_decode(" \n\t\r", 4, &decoded, &decoded_len,
                                     &error));
    g_assert_no_error(error);
    g_assert_cmpstr(decoded, ==, "");
    g_free(decoded);
}

static void
test_text_ascii(void)
{
    gchar *encoded;
    gchar *decoded = NULL;
    gsize len;
    GError *error = NULL;

    encoded = binark_text_encode("A", 1, TRUE, 8, &len);
    g_assert_cmpstr(encoded, ==, "01000001");
    g_free(encoded);

    encoded = binark_text_encode("Hi", 2, TRUE, 8, &len);
    g_assert_cmpstr(encoded, ==, "01001000 01101001");
    g_free(encoded);

    g_assert_true(binark_text_decode("01001000 01101001", 17, &decoded,
                                     &len, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(decoded, ==, "Hi");
    g_assert_cmpuint(len, ==, 2);
    g_free(decoded);
}

static void
test_text_unicode(void)
{
    const gchar *samples[] = {
        "Привет, мир!",
        "Ёжик",
        "😀🚀✨",
        "e\u0301",
        "日本語"
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS(samples); i++) {
        gchar *encoded;
        gchar *decoded = NULL;
        gsize encoded_len;
        gsize decoded_len;
        GError *error = NULL;

        encoded = binark_text_encode(samples[i], strlen(samples[i]), TRUE, 4,
                                     &encoded_len);
        g_assert_nonnull(encoded);
        g_assert_cmpuint(encoded_len, >, 0);

        g_assert_true(binark_text_decode(encoded, encoded_len, &decoded,
                                         &decoded_len, &error));
        g_assert_no_error(error);
        g_assert_cmpstr(decoded, ==, samples[i]);
        g_assert_cmpuint(decoded_len, ==, strlen(samples[i]));
        g_assert_cmpuint(binark_utf8_char_count(decoded), ==,
                         g_utf8_strlen(decoded, -1));

        g_free(decoded);
        g_free(encoded);
    }
}

static void
test_text_known_utf8(void)
{
    gchar *encoded;
    gsize len;

    encoded = binark_text_encode("Ё", strlen("Ё"), TRUE, 8, &len);
    g_assert_cmpstr(encoded, ==, "11010000 10000001");
    g_free(encoded);
}

static void
test_text_formatting(void)
{
    gchar *encoded;
    gsize len;

    encoded = binark_text_encode("ABCD", 4, TRUE, 2, &len);
    g_assert_cmpstr(encoded,
                    ==,
                    "01000001 01000010\n01000011 01000100");
    g_free(encoded);

    encoded = binark_text_encode("AB", 2, FALSE, 1, &len);
    g_assert_cmpstr(encoded, ==, "0100000101000010");
    g_free(encoded);
}

static void
test_text_decode_whitespace(void)
{
    gchar *decoded = NULL;
    GError *error = NULL;

    g_assert_true(binark_text_decode("0100 0001\n\t0100\r\n0010",
                                     strlen("0100 0001\n\t0100\r\n0010"),
                                     &decoded, NULL, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(decoded, ==, "AB");
    g_free(decoded);
}

static void
test_text_incomplete(void)
{
    gchar *decoded = NULL;
    GError *error = NULL;

    g_assert_false(binark_text_decode("0100000", 7, &decoded, NULL, &error));
    g_assert_error(error, BINARK_TEXT_ERROR,
                   BINARK_TEXT_ERROR_INCOMPLETE_BYTE);
    g_assert_null(decoded);
    g_clear_error(&error);

    g_assert_false(binark_text_decode("01000001 111", 12, &decoded, NULL,
                                      &error));
    g_assert_error(error, BINARK_TEXT_ERROR,
                   BINARK_TEXT_ERROR_INCOMPLETE_BYTE);
    g_clear_error(&error);
}

static void
test_text_invalid(void)
{
    gchar *decoded = NULL;
    GError *error = NULL;

    g_assert_false(binark_text_decode("01000001 2", 11, &decoded, NULL,
                                      &error));
    g_assert_error(error, BINARK_TEXT_ERROR,
                   BINARK_TEXT_ERROR_INVALID_CHARACTER);
    g_clear_error(&error);

    /* 0xFF is not valid UTF-8. */
    g_assert_false(binark_text_decode("11111111", 8, &decoded, NULL, &error));
    g_assert_error(error, BINARK_TEXT_ERROR, BINARK_TEXT_ERROR_INVALID_UTF8);
    g_clear_error(&error);

    /* Embedded NUL is rejected by the text converter. */
    g_assert_false(binark_text_decode("00000000", 8, &decoded, NULL, &error));
    g_assert_error(error, BINARK_TEXT_ERROR, BINARK_TEXT_ERROR_NUL_BYTE);
    g_clear_error(&error);
}

static void
test_text_base64(void)
{
    gchar *encoded;
    gchar *decoded = NULL;
    gsize encoded_len = 0;
    gsize decoded_len = 0;
    GError *error = NULL;

    encoded = binark_text_encode_base64("Hi", 2, &encoded_len);
    g_assert_cmpstr(encoded, ==, "SGk=");
    g_assert_cmpuint(encoded_len, ==, 4);
    g_free(encoded);

    encoded = binark_text_encode_base64("", 0, &encoded_len);
    g_assert_cmpstr(encoded, ==, "");
    g_assert_cmpuint(encoded_len, ==, 0);
    g_free(encoded);

    g_assert_true(binark_text_decode_base64("SGk=", 4, &decoded, &decoded_len,
                                            &error));
    g_assert_no_error(error);
    g_assert_cmpstr(decoded, ==, "Hi");
    g_assert_cmpuint(decoded_len, ==, 2);
    g_free(decoded);

    g_assert_false(binark_text_decode_base64("SGk", 3, &decoded, &decoded_len,
                                             &error));
    g_assert_error(error, BINARK_TEXT_ERROR, BINARK_TEXT_ERROR_INVALID_CHARACTER);
    g_clear_error(&error);
}

static void
test_text_base32(void)
{
    gchar *encoded;
    gchar *decoded = NULL;
    gsize encoded_len = 0;
    gsize decoded_len = 0;
    GError *error = NULL;

    encoded = binark_text_encode_base32("Hi", 2, &encoded_len);
    g_assert_cmpstr(encoded, ==, "JBUQ====");
    g_assert_cmpuint(encoded_len, ==, 8);
    g_free(encoded);

    encoded = binark_text_encode_base32("", 0, &encoded_len);
    g_assert_cmpstr(encoded, ==, "");
    g_assert_cmpuint(encoded_len, ==, 0);
    g_free(encoded);

    g_assert_true(binark_text_decode_base32("JBUQ====", 8, &decoded, &decoded_len,
                                            &error));
    g_assert_no_error(error);
    g_assert_cmpstr(decoded, ==, "Hi");
    g_assert_cmpuint(decoded_len, ==, 2);
    g_free(decoded);

    g_assert_false(binark_text_decode_base32("JBUQ", 4, &decoded, &decoded_len,
                                             &error));
    g_assert_error(error, BINARK_TEXT_ERROR, BINARK_TEXT_ERROR_INVALID_CHARACTER);
    g_clear_error(&error);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/text/empty", test_text_empty);
    g_test_add_func("/text/ascii", test_text_ascii);
    g_test_add_func("/text/unicode", test_text_unicode);
    g_test_add_func("/text/known-utf8", test_text_known_utf8);
    g_test_add_func("/text/formatting", test_text_formatting);
    g_test_add_func("/text/decode-whitespace", test_text_decode_whitespace);
    g_test_add_func("/text/incomplete", test_text_incomplete);
    g_test_add_func("/text/invalid", test_text_invalid);
    g_test_add_func("/text/base64", test_text_base64);
    g_test_add_func("/text/base32", test_text_base32);

    return g_test_run();
}
