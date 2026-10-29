#ifndef BINARK_WINDOW_H
#define BINARK_WINDOW_H

#include <adwaita.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define BINARK_TYPE_WINDOW (binark_window_get_type())
G_DECLARE_FINAL_TYPE(BinarkWindow, binark_window, BINARK, WINDOW,
                     AdwApplicationWindow)

GtkWidget *binark_window_new(GtkApplication *application);

G_END_DECLS

#endif /* BINARK_WINDOW_H */
