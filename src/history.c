#include "history.h"

#include <errno.h>
#include <glib/gstdio.h>

#define BINARK_HISTORY_LIMIT 100
#define BINARK_HISTORY_GROUP "entry%u"

static void
history_entry_free(gpointer data)
{
    BinarkHistoryEntry *entry = data;

    if (entry == NULL)
        return;
    g_free(entry->direction);
    g_free(entry->summary);
    g_free(entry);
}

static gchar *
history_file_path(void)
{
    gchar *directory = g_build_filename(g_get_user_data_dir(),
                                        "binark",
                                        NULL);
    gchar *path = g_build_filename(directory, "history.ini", NULL);

    g_free(directory);
    return path;
}

BinarkHistory *
binark_history_new(void)
{
    BinarkHistory *history = g_new0(BinarkHistory, 1);

    history->entries = g_ptr_array_new_with_free_func(history_entry_free);
    return history;
}

void
binark_history_free(BinarkHistory *history)
{
    if (history == NULL)
        return;
    g_ptr_array_unref(history->entries);
    g_free(history);
}

const gchar *
binark_history_kind_name(BinarkHistoryKind kind)
{
    switch (kind) {
    case BINARK_HISTORY_TEXT:
        return "text";
    case BINARK_HISTORY_NUMBER:
        return "number";
    case BINARK_HISTORY_FILE:
        return "file";
    default:
        return "text";
    }
}

const gchar *
binark_history_kind_label(BinarkHistoryKind kind)
{
    switch (kind) {
    case BINARK_HISTORY_TEXT:
        return "Текст";
    case BINARK_HISTORY_NUMBER:
        return "Числа";
    case BINARK_HISTORY_FILE:
        return "Файлы";
    default:
        return "Операция";
    }
}

static BinarkHistoryKind
kind_from_name(const gchar *name)
{
    if (g_strcmp0(name, "number") == 0)
        return BINARK_HISTORY_NUMBER;
    if (g_strcmp0(name, "file") == 0)
        return BINARK_HISTORY_FILE;
    return BINARK_HISTORY_TEXT;
}

gboolean
binark_history_load(BinarkHistory *history, GError **error)
{
    GKeyFile *key_file;
    gchar *path;
    gchar **groups;
    gsize group_count;
    gsize i;

    g_return_val_if_fail(history != NULL, FALSE);

    g_ptr_array_set_size(history->entries, 0);
    path = history_file_path();

    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        g_free(path);
        return TRUE;
    }

    key_file = g_key_file_new();
    if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, error)) {
        g_key_file_unref(key_file);
        g_free(path);
        return FALSE;
    }

    groups = g_key_file_get_groups(key_file, &group_count);
    for (i = 0; i < group_count && history->entries->len < BINARK_HISTORY_LIMIT;
         i++) {
        BinarkHistoryEntry *entry;
        gchar *kind;
        gchar *direction;
        gchar *summary;
        gint64 timestamp;
        GError *local_error = NULL;

        if (!g_str_has_prefix(groups[i], "entry"))
            continue;

        timestamp = g_key_file_get_int64(key_file, groups[i], "timestamp",
                                         &local_error);
        if (local_error != NULL) {
            g_clear_error(&local_error);
            continue;
        }

        kind = g_key_file_get_string(key_file, groups[i], "kind", NULL);
        direction = g_key_file_get_string(key_file, groups[i], "direction", NULL);
        summary = g_key_file_get_string(key_file, groups[i], "summary", NULL);

        if (direction == NULL || summary == NULL) {
            g_free(kind);
            g_free(direction);
            g_free(summary);
            continue;
        }

        entry = g_new0(BinarkHistoryEntry, 1);
        entry->timestamp = timestamp;
        entry->kind = kind_from_name(kind);
        entry->direction = direction;
        entry->summary = summary;
        g_ptr_array_add(history->entries, entry);
        g_free(kind);
    }
    g_strfreev(groups);
    g_key_file_unref(key_file);
    g_free(path);
    return TRUE;
}

gboolean
binark_history_save(BinarkHistory *history, GError **error)
{
    GKeyFile *key_file;
    gchar *path;
    gchar *directory;
    gchar *data;
    gsize length = 0;
    gboolean ok;
    guint i;

    g_return_val_if_fail(history != NULL, FALSE);

    path = history_file_path();
    directory = g_path_get_dirname(path);
    if (g_mkdir_with_parents(directory, 0700) != 0) {
        g_set_error(error,
                    G_FILE_ERROR,
                    g_file_error_from_errno(errno),
                    "Не удалось создать каталог истории %s: %s",
                    directory,
                    g_strerror(errno));
        g_free(directory);
        g_free(path);
        return FALSE;
    }
    g_free(directory);

    key_file = g_key_file_new();
    for (i = 0; i < history->entries->len; i++) {
        const BinarkHistoryEntry *entry = g_ptr_array_index(history->entries, i);
        gchar *group = g_strdup_printf(BINARK_HISTORY_GROUP, i);

        g_key_file_set_int64(key_file, group, "timestamp", entry->timestamp);
        g_key_file_set_string(key_file, group, "kind",
                              binark_history_kind_name(entry->kind));
        g_key_file_set_string(key_file, group, "direction", entry->direction);
        g_key_file_set_string(key_file, group, "summary", entry->summary);
        g_free(group);
    }

    data = g_key_file_to_data(key_file, &length, error);
    g_key_file_unref(key_file);
    if (data == NULL) {
        g_free(path);
        return FALSE;
    }

    ok = g_file_set_contents_full(path, data, (gssize)length,
                                  G_FILE_SET_CONTENTS_CONSISTENT, 0600, error);
    g_free(data);
    g_free(path);
    return ok;
}

void
binark_history_add(BinarkHistory *history,
                   BinarkHistoryKind kind,
                   const gchar *direction,
                   const gchar *summary)
{
    BinarkHistoryEntry *entry;

    g_return_if_fail(history != NULL);

    entry = g_new0(BinarkHistoryEntry, 1);
    entry->timestamp = g_get_real_time() / G_USEC_PER_SEC;
    entry->kind = kind;
    entry->direction = g_strdup(direction != NULL ? direction : "");
    entry->summary = g_strdup(summary != NULL ? summary : "");
    g_ptr_array_insert(history->entries, 0, entry);

    while (history->entries->len > BINARK_HISTORY_LIMIT)
        g_ptr_array_remove_index(history->entries,
                                 history->entries->len - 1);
}

void
binark_history_clear(BinarkHistory *history)
{
    g_return_if_fail(history != NULL);
    g_ptr_array_set_size(history->entries, 0);
}

guint
binark_history_get_length(BinarkHistory *history)
{
    g_return_val_if_fail(history != NULL, 0);
    return history->entries->len;
}

const BinarkHistoryEntry *
binark_history_get(BinarkHistory *history, guint index)
{
    g_return_val_if_fail(history != NULL, NULL);

    if (index >= history->entries->len)
        return NULL;
    return g_ptr_array_index(history->entries, index);
}

gchar *
binark_history_format_time(gint64 timestamp)
{
    GDateTime *date_time = g_date_time_new_from_unix_local(timestamp);
    gchar *formatted;

    if (date_time == NULL)
        return g_strdup("");

    formatted = g_date_time_format(date_time, "%d.%m.%Y, %H:%M");
    g_date_time_unref(date_time);
    return formatted;
}
