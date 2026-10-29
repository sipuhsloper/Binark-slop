#ifndef BINARK_HISTORY_H
#define BINARK_HISTORY_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    BINARK_HISTORY_TEXT,
    BINARK_HISTORY_NUMBER,
    BINARK_HISTORY_FILE
} BinarkHistoryKind;

typedef struct {
    gint64 timestamp;
    BinarkHistoryKind kind;
    gchar *direction;
    gchar *summary;
} BinarkHistoryEntry;

typedef struct {
    GPtrArray *entries;
} BinarkHistory;

BinarkHistory *binark_history_new(void);
void binark_history_free(BinarkHistory *history);

gboolean binark_history_load(BinarkHistory *history, GError **error);
gboolean binark_history_save(BinarkHistory *history, GError **error);

void binark_history_add(BinarkHistory *history,
                        BinarkHistoryKind kind,
                        const gchar *direction,
                        const gchar *summary);
void binark_history_clear(BinarkHistory *history);

guint binark_history_get_length(BinarkHistory *history);
const BinarkHistoryEntry *binark_history_get(BinarkHistory *history, guint index);

const gchar *binark_history_kind_name(BinarkHistoryKind kind);
const gchar *binark_history_kind_label(BinarkHistoryKind kind);
gchar *binark_history_format_time(gint64 timestamp);

G_END_DECLS

#endif /* BINARK_HISTORY_H */
