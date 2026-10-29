#include <glib.h>

#include "number_converter.h"

static gchar *
convert_ok(const gchar *input, guint source, guint target, gboolean upper)
{
    gchar *result = NULL;
    GError *error = NULL;

    g_assert_true(binark_number_convert(input, source, target, upper,
                                        &result, &error));
    g_assert_no_error(error);
    g_assert_nonnull(result);
    return result;
}

static void
test_numbers_examples(void)
{
    gchar *result;

    result = convert_ok("42", 10, 2, TRUE);
    g_assert_cmpstr(result, ==, "101010");
    g_free(result);

    result = convert_ok("255", 10, 16, TRUE);
    g_assert_cmpstr(result, ==, "FF");
    g_free(result);

    result = convert_ok("255", 10, 16, FALSE);
    g_assert_cmpstr(result, ==, "ff");
    g_free(result);

    result = convert_ok("101010", 2, 10, TRUE);
    g_assert_cmpstr(result, ==, "42");
    g_free(result);

    result = convert_ok("777", 8, 10, TRUE);
    g_assert_cmpstr(result, ==, "511");
    g_free(result);

    result = convert_ok("ff", 16, 2, TRUE);
    g_assert_cmpstr(result, ==, "11111111");
    g_free(result);
}

static void
test_numbers_zero_and_sign(void)
{
    gchar *result;

    result = convert_ok("0", 10, 2, TRUE);
    g_assert_cmpstr(result, ==, "0");
    g_free(result);

    result = convert_ok("-0", 10, 16, TRUE);
    g_assert_cmpstr(result, ==, "0");
    g_free(result);

    result = convert_ok("-42", 10, 2, TRUE);
    g_assert_cmpstr(result, ==, "-101010");
    g_free(result);

    result = convert_ok("+42", 10, 16, TRUE);
    g_assert_cmpstr(result, ==, "2A");
    g_free(result);

    result = convert_ok("-0x2a", 16, 10, TRUE);
    g_assert_cmpstr(result, ==, "-42");
    g_free(result);
}

static void
test_numbers_prefixes_and_whitespace(void)
{
    gchar *result;

    result = convert_ok("  0b1010  ", 2, 10, TRUE);
    g_assert_cmpstr(result, ==, "10");
    g_free(result);

    result = convert_ok("0o755", 8, 16, TRUE);
    g_assert_cmpstr(result, ==, "1ED");
    g_free(result);

    result = convert_ok("0XDeadBeef", 16, 10, TRUE);
    g_assert_cmpstr(result, ==, "3735928559");
    g_free(result);

    result = convert_ok("00000042", 10, 2, TRUE);
    g_assert_cmpstr(result, ==, "101010");
    g_free(result);
}

static void
test_numbers_huge(void)
{
    const gchar *decimal = "1234567890123456789012345678901234567890";
    const gchar *hex = "3A0C92075C0DBF3B8ACBC5F96CE3F0AD2";
    gchar *result;

    result = convert_ok(decimal, 10, 16, TRUE);
    g_assert_cmpstr(result, ==, hex);
    g_free(result);

    result = convert_ok(hex, 16, 10, TRUE);
    g_assert_cmpstr(result, ==, decimal);
    g_free(result);

    /* 2^128 - 1 */
    result = convert_ok("340282366920938463463374607431768211455", 10, 16,
                        TRUE);
    g_assert_cmpstr(result, ==, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
    g_free(result);
}

static void
test_numbers_errors(void)
{
    gchar *result = NULL;
    GError *error = NULL;

    g_assert_false(binark_number_convert("", 10, 2, TRUE, &result, &error));
    g_assert_error(error, BINARK_NUMBER_ERROR, BINARK_NUMBER_ERROR_EMPTY);
    g_assert_null(result);
    g_clear_error(&error);

    g_assert_false(binark_number_convert("   ", 10, 2, TRUE, &result, &error));
    g_assert_error(error, BINARK_NUMBER_ERROR, BINARK_NUMBER_ERROR_EMPTY);
    g_clear_error(&error);

    g_assert_false(binark_number_convert("12A", 10, 2, TRUE, &result, &error));
    g_assert_error(error, BINARK_NUMBER_ERROR,
                   BINARK_NUMBER_ERROR_INVALID_DIGIT);
    g_clear_error(&error);

    g_assert_false(binark_number_convert("102", 2, 10, TRUE, &result, &error));
    g_assert_error(error, BINARK_NUMBER_ERROR,
                   BINARK_NUMBER_ERROR_INVALID_DIGIT);
    g_clear_error(&error);

    g_assert_false(binark_number_convert("8", 8, 10, TRUE, &result, &error));
    g_assert_error(error, BINARK_NUMBER_ERROR,
                   BINARK_NUMBER_ERROR_INVALID_DIGIT);
    g_clear_error(&error);

    g_assert_false(binark_number_convert("0x10", 10, 2, TRUE, &result,
                                         &error));
    g_assert_error(error, BINARK_NUMBER_ERROR,
                   BINARK_NUMBER_ERROR_PREFIX_MISMATCH);
    g_clear_error(&error);

    g_assert_false(binark_number_convert("0x", 16, 10, TRUE, &result, &error));
    g_assert_error(error, BINARK_NUMBER_ERROR, BINARK_NUMBER_ERROR_EMPTY);
    g_clear_error(&error);

    g_assert_false(binark_number_convert("10", 3, 10, TRUE, &result, &error));
    g_assert_error(error, BINARK_NUMBER_ERROR,
                   BINARK_NUMBER_ERROR_INVALID_BASE);
    g_clear_error(&error);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/numbers/examples", test_numbers_examples);
    g_test_add_func("/numbers/zero-sign", test_numbers_zero_and_sign);
    g_test_add_func("/numbers/prefixes", test_numbers_prefixes_and_whitespace);
    g_test_add_func("/numbers/huge", test_numbers_huge);
    g_test_add_func("/numbers/errors", test_numbers_errors);

    return g_test_run();
}
