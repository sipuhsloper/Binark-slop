#include "file_converter.h"
#include "converter.h"
#include "text_converter.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

G_DEFINE_QUARK(binark-file-error-quark, binark_file_error)

#define BINARK_FILE_BUFFER_SIZE (64 * 1024)
#define BINARK_OUTPUT_BUFFER_SIZE (64 * 1024)

typedef struct {
    FILE *fp;
    gchar *path;
    int fd;
} AtomicOutput;

static gboolean
progress_step(BinarkFileProgressFunc progress,
              gpointer user_data,
              guint64 done,
              guint64 total,
              GError **error)
{
    if (progress != NULL && !progress(done, total, user_data)) {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_CANCELLED,
                    "Операция отменена.");
        return FALSE;
    }
    return TRUE;
}

static gboolean
same_file_path(const gchar *a, const gchar *b)
{
    gchar *ca = g_canonicalize_filename(a, NULL);
    gchar *cb = g_canonicalize_filename(b, NULL);
    gboolean same = g_strcmp0(ca, cb) == 0;

    g_free(ca);
    g_free(cb);
    return same;
}

static gboolean
atomic_output_open(AtomicOutput *out,
                   const gchar *output_path,
                   GError **error)
{
    gchar *canonical = NULL;
    gchar *directory = NULL;
    gchar *template = NULL;
    int fd = -1;

    memset(out, 0, sizeof(*out));
    out->fd = -1;

    canonical = g_canonicalize_filename(output_path, NULL);
    directory = g_path_get_dirname(canonical);

    if (!g_file_test(directory, G_FILE_TEST_IS_DIR)) {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_INVALID_PATH,
                    "Каталог для сохранения не существует: %s",
                    directory);
        goto fail;
    }

    template = g_build_filename(directory, ".binark-XXXXXX", NULL);
    fd = g_mkstemp(template);
    if (fd < 0) {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_WRITE,
                    "Не удалось создать временный файл в каталоге %s: %s",
                    directory,
                    g_strerror(errno));
        goto fail;
    }

    out->fp = fdopen(fd, "wb");
    if (out->fp == NULL) {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_WRITE,
                    "Не удалось открыть временный файл для записи: %s",
                    g_strerror(errno));
        close(fd);
        unlink(template);
        goto fail;
    }

    /* Respect the user's umask while dropping mkstemp's restrictive mode. */
    {
        mode_t mask = umask(0);

        umask(mask);
        (void)fchmod(fd, 0666 & ~mask);
    }

    out->path = g_steal_pointer(&template);
    out->fd = fd;
    g_free(canonical);
    g_free(directory);
    return TRUE;

fail:
    g_free(template);
    g_free(canonical);
    g_free(directory);
    return FALSE;
}

static gboolean
atomic_output_commit(AtomicOutput *out,
                     const gchar *output_path,
                     GError **error)
{
    gchar *canonical = NULL;
    int saved_errno;

    if (out->fp == NULL)
        return FALSE;

    if (fflush(out->fp) != 0 || ferror(out->fp)) {
        saved_errno = errno;
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_WRITE,
                    "Ошибка записи результата: %s",
                    g_strerror(saved_errno != 0 ? saved_errno : EIO));
        goto fail;
    }

    if (fsync(out->fd) != 0) {
        saved_errno = errno;
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_WRITE,
                    "Не удалось сбросить данные на диск: %s",
                    g_strerror(saved_errno));
        goto fail;
    }

    if (fclose(out->fp) != 0) {
        out->fp = NULL;
        saved_errno = errno;
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_WRITE,
                    "Не удалось закрыть результат: %s",
                    g_strerror(saved_errno));
        goto fail;
    }
    out->fp = NULL;

    canonical = g_canonicalize_filename(output_path, NULL);
    if (g_rename(out->path, canonical) != 0) {
        saved_errno = errno;
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_WRITE,
                    "Не удалось сохранить файл %s: %s",
                    canonical,
                    g_strerror(saved_errno));
        g_free(canonical);
        goto fail;
    }

    g_free(canonical);
    g_clear_pointer(&out->path, g_free);
    return TRUE;

fail:
    if (out->fp != NULL) {
        fclose(out->fp);
        out->fp = NULL;
    }
    if (out->path != NULL) {
        unlink(out->path);
        g_clear_pointer(&out->path, g_free);
    }
    return FALSE;
}

static void
atomic_output_abort(AtomicOutput *out)
{
    if (out->fp != NULL) {
        fclose(out->fp);
        out->fp = NULL;
    }
    if (out->path != NULL) {
        unlink(out->path);
        g_clear_pointer(&out->path, g_free);
    }
}

static guint64
available_space(const gchar *output_path)
{
    gchar *canonical = g_canonicalize_filename(output_path, NULL);
    gchar *directory = g_path_get_dirname(canonical);
    struct statvfs info;
    guint64 available = G_MAXUINT64;

    if (statvfs(directory, &info) == 0) {
        guint64 block_size = (guint64)info.f_frsize;
        guint64 blocks = (guint64)info.f_bavail;

        if (block_size == 0)
            block_size = (guint64)info.f_bsize;
        if (block_size != 0 && blocks <= G_MAXUINT64 / block_size)
            available = block_size * blocks;
    }

    g_free(canonical);
    g_free(directory);
    return available;
}

gboolean
binark_file_check_space(const gchar *output_path,
                        guint64 needed,
                        GError **error)
{
    guint64 available;

    if (output_path == NULL)
        return TRUE;

    available = available_space(output_path);
    if (available == G_MAXUINT64)
        return TRUE;

    if (needed > available) {
        gchar *needed_text = binark_format_size(needed);
        gchar *available_text = binark_format_size(available);

        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_NO_SPACE,
                    "Недостаточно свободного места: требуется около %s, "
                    "доступно %s.",
                    needed_text,
                    available_text);
        g_free(needed_text);
        g_free(available_text);
        return FALSE;
    }

    return TRUE;
}

guint64
binark_file_encoded_size(guint64 input_size,
                         gboolean compact,
                         guint bytes_per_line)
{
    return binark_file_encoded_size_base(input_size, 2, compact,
                                         bytes_per_line);
}

guint64
binark_file_encoded_size_base(guint64 input_size,
                              guint base,
                              gboolean compact,
                              guint bytes_per_line)
{
    guint width = binark_text_base_width(base);
    guint64 per_byte;
    guint64 result;

    if (width == 0)
        width = 8;

    per_byte = width + (compact ? 0 : 1);
    if (input_size > G_MAXUINT64 / per_byte)
        return G_MAXUINT64;
    result = input_size * per_byte;

    if (!compact && bytes_per_line > 0) {
        guint64 lines = input_size / bytes_per_line;

        if (result > G_MAXUINT64 - lines)
            return G_MAXUINT64;
        result += lines;
    }

    return result;
}

guint64
binark_file_decoded_size(guint64 input_size)
{
    return binark_file_decoded_size_base(input_size, 2);
}

guint64
binark_file_decoded_size_base(guint64 input_size, guint base)
{
    guint width = binark_text_base_width(base);

    if (width == 0)
        width = 8;
    return input_size / width + (input_size % width != 0 ? 1 : 0);
}

static gboolean
flush_bytes(FILE *fp,
            const guchar *buffer,
            gsize size,
            GError **error)
{
    if (size == 0)
        return TRUE;

    if (fwrite(buffer, 1, size, fp) != size) {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_WRITE,
                    "Ошибка записи файла: %s",
                    g_strerror(errno));
        return FALSE;
    }
    return TRUE;
}

gboolean
binark_file_encode(const gchar *input_path,
                   const gchar *output_path,
                   gboolean compact,
                   guint bytes_per_line,
                   BinarkFileProgressFunc progress,
                   gpointer user_data,
                   GError **error)
{
    return binark_file_encode_base(input_path, output_path, 2, compact,
                                   bytes_per_line, progress, user_data, error);
}

gboolean
binark_file_encode_base(const gchar *input_path,
                        const gchar *output_path,
                        guint base,
                        gboolean compact,
                        guint bytes_per_line,
                        BinarkFileProgressFunc progress,
                        gpointer user_data,
                        GError **error)
{
    guint width = binark_text_base_width(base);
    FILE *input = NULL;
    AtomicOutput output;
    struct stat info;
    guint64 input_size = 0;
    guint64 done = 0;
    guchar in_buffer[BINARK_FILE_BUFFER_SIZE];
    guchar out_buffer[BINARK_OUTPUT_BUFFER_SIZE];
    gsize out_pos = 0;
    guint column = 0;
    gsize bytes_read;
    gboolean ok = FALSE;

    memset(&output, 0, sizeof(output));
    output.fd = -1;

    if (width == 0) {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_INVALID_FORMAT,
                    "Поддерживаются системы счисления с основаниями "
                    "от 2 до 64.");
        return FALSE;
    }

    if (input_path == NULL || output_path == NULL || *input_path == '\0' ||
        *output_path == '\0') {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_INVALID_PATH,
                    "Не выбраны исходный или итоговый файл.");
        return FALSE;
    }

    if (same_file_path(input_path, output_path)) {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_SAME_FILE,
                    "Исходный и итоговый файлы не должны совпадать.");
        return FALSE;
    }

    input = g_fopen(input_path, "rb");
    if (input == NULL) {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_READ,
                    "Не удалось открыть файл %s: %s",
                    input_path,
                    g_strerror(errno));
        return FALSE;
    }

    if (fstat(fileno(input), &info) == 0 && info.st_size > 0)
        input_size = (guint64)info.st_size;

    if (!binark_file_check_space(output_path,
                                 binark_file_encoded_size_base(input_size,
                                                               base, compact,
                                                               bytes_per_line) +
                                     1024,
                                 error))
        goto out;

    if (!atomic_output_open(&output, output_path, error))
        goto out;

    if (!progress_step(progress, user_data, 0, input_size, error))
        goto out;

    while ((bytes_read = fread(in_buffer, 1, sizeof(in_buffer), input)) > 0) {
        gsize i;

        for (i = 0; i < bytes_read; i++) {
            guint8 byte = in_buffer[i];
            gchar digits[8];

            if (out_pos + 10 > sizeof(out_buffer)) {
                if (!flush_bytes(output.fp, out_buffer, out_pos, error))
                    goto out;
                out_pos = 0;
            }

            if (!compact) {
                if (column != 0)
                    out_buffer[out_pos++] = ' ';
            }

            binark_text_format_byte(byte, base, digits);
            memcpy(out_buffer + out_pos, digits, width);
            out_pos += width;

            column++;
            if (!compact && bytes_per_line > 0 &&
                column >= bytes_per_line) {
                out_buffer[out_pos++] = '\n';
                column = 0;
            }
        }

        done += bytes_read;
        if (ferror(input)) {
            g_set_error(error,
                        BINARK_FILE_ERROR,
                        BINARK_FILE_ERROR_READ,
                        "Ошибка чтения файла %s: %s",
                        input_path,
                        g_strerror(errno));
            goto out;
        }

        if (!progress_step(progress, user_data, done, input_size, error))
            goto out;
    }

    if (ferror(input)) {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_READ,
                    "Ошибка чтения файла %s: %s",
                    input_path,
                    g_strerror(errno));
        goto out;
    }

    if (!flush_bytes(output.fp, out_buffer, out_pos, error))
        goto out;
    out_pos = 0;

    if (!atomic_output_commit(&output, output_path, error))
        goto out;

    ok = TRUE;

out:
    if (!ok)
        atomic_output_abort(&output);
    if (input != NULL)
        fclose(input);
    return ok;
}

static gboolean
is_bit_separator(guchar c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\v' || c == '\f';
}

gboolean
binark_file_decode(const gchar *input_path,
                   const gchar *output_path,
                   BinarkFileProgressFunc progress,
                   gpointer user_data,
                   GError **error)
{
    return binark_file_decode_base(input_path, output_path, 2, progress,
                                   user_data, error);
}

gboolean
binark_file_decode_base(const gchar *input_path,
                        const gchar *output_path,
                        guint base,
                        BinarkFileProgressFunc progress,
                        gpointer user_data,
                        GError **error)
{
    guint width = binark_text_base_width(base);
    FILE *input = NULL;
    AtomicOutput output;
    struct stat info;
    guint64 input_size = 0;
    guint64 done = 0;
    guchar in_buffer[BINARK_FILE_BUFFER_SIZE];
    guchar out_buffer[BINARK_OUTPUT_BUFFER_SIZE];
    gsize out_pos = 0;
    guint current = 0;
    guint digit_count = 0;
    gsize line = 1;
    gsize column = 1;
    gsize bytes_read;
    gboolean ok = FALSE;

    memset(&output, 0, sizeof(output));
    output.fd = -1;

    if (width == 0) {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_INVALID_FORMAT,
                    "Поддерживаются системы счисления с основаниями "
                    "от 2 до 64.");
        return FALSE;
    }

    if (input_path == NULL || output_path == NULL || *input_path == '\0' ||
        *output_path == '\0') {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_INVALID_PATH,
                    "Не выбраны исходный или итоговый файл.");
        return FALSE;
    }

    if (same_file_path(input_path, output_path)) {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_SAME_FILE,
                    "Исходный и итоговый файлы не должны совпадать.");
        return FALSE;
    }

    input = g_fopen(input_path, "rb");
    if (input == NULL) {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_READ,
                    "Не удалось открыть файл %s: %s",
                    input_path,
                    g_strerror(errno));
        return FALSE;
    }

    if (fstat(fileno(input), &info) == 0 && info.st_size > 0)
        input_size = (guint64)info.st_size;

    if (!binark_file_check_space(output_path,
                                 binark_file_decoded_size_base(input_size,
                                                               base) + 1024,
                                 error))
        goto out;

    if (!atomic_output_open(&output, output_path, error))
        goto out;

    if (!progress_step(progress, user_data, 0, input_size, error))
        goto out;

    while ((bytes_read = fread(in_buffer, 1, sizeof(in_buffer), input)) > 0) {
        gsize i;

        for (i = 0; i < bytes_read; i++) {
            guchar c = in_buffer[i];

            if (is_bit_separator(c)) {
                if (c == '\n') {
                    line++;
                    column = 1;
                } else {
                    column++;
                }
                continue;
            }

            {
                gint digit = binark_text_digit_value(c, base);

                if (digit < 0) {
                    gchar printable[64];

                    if (c >= 0x20 && c <= 0x7e)
                        g_snprintf(printable, sizeof(printable), "«%c»", c);
                    else
                        g_snprintf(printable, sizeof(printable),
                                   "байт 0x%02X", c);

                    g_set_error(error,
                                BINARK_FILE_ERROR,
                                BINARK_FILE_ERROR_INVALID_FORMAT,
                                "Недопустимый символ %s в строке "
                                "%" G_GSIZE_FORMAT ", столбце "
                                "%" G_GSIZE_FORMAT ". Ожидаются цифры "
                                "системы счисления с основанием %u "
                                "и пробельные символы.",
                                printable, line, column, base);
                    goto out;
                }

                current = current * base + (guint)digit;
            }
            digit_count++;
            column++;

            if (digit_count == width) {
                if (current > 255) {
                    g_set_error(error,
                                BINARK_FILE_ERROR,
                                BINARK_FILE_ERROR_INVALID_FORMAT,
                                "Значение %u в строке %" G_GSIZE_FORMAT
                                " больше 255 и не является байтом. "
                                "Итоговый файл не создан.",
                                current, line);
                    goto out;
                }

                {
                    guint8 byte = (guint8)current;
                    out_buffer[out_pos++] = byte;
                }
                current = 0;
                digit_count = 0;

                if (out_pos == sizeof(out_buffer)) {
                    if (!flush_bytes(output.fp, out_buffer, out_pos, error))
                        goto out;
                    out_pos = 0;
                }
            }
        }

        done += bytes_read;
        if (ferror(input)) {
            g_set_error(error,
                        BINARK_FILE_ERROR,
                        BINARK_FILE_ERROR_READ,
                        "Ошибка чтения файла %s: %s",
                        input_path,
                        g_strerror(errno));
            goto out;
        }

        if (!progress_step(progress, user_data, done, input_size, error))
            goto out;
    }

    if (ferror(input)) {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_READ,
                    "Ошибка чтения файла %s: %s",
                    input_path,
                    g_strerror(errno));
        goto out;
    }

    if (digit_count != 0) {
        g_set_error(error,
                    BINARK_FILE_ERROR,
                    BINARK_FILE_ERROR_INVALID_FORMAT,
                    "Неполная группа: в файле осталось цифр: %u, а нужно %u. "
                    "Итоговый файл не создан.",
                    digit_count, width);
        goto out;
    }

    if (!flush_bytes(output.fp, out_buffer, out_pos, error))
        goto out;
    out_pos = 0;

    if (!atomic_output_commit(&output, output_path, error))
        goto out;

    ok = TRUE;

out:
    if (!ok)
        atomic_output_abort(&output);
    if (input != NULL)
        fclose(input);
    return ok;
}
