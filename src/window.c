#include "window.h"

#include <adwaita.h>
#include <string.h>
#include <sys/stat.h>

#include "converter.h"
#include "file_converter.h"
#include "history.h"
#include "number_converter.h"
#include "settings.h"
#include "text_converter.h"

#define TEXT_PLACEHOLDER_ENCODE "Введите текст…"
#define TEXT_PLACEHOLDER_DECODE "Введите последовательность из 0 и 1…"

typedef struct {
    BinarkWindow *window;
    gchar *input_path;
    gchar *output_path;
    gboolean encode;
    gboolean compact;
    guint bytes_per_line;
    guint base;
    guint64 input_size;
    GThread *thread;
    gint ref_count;
    gint cancelled;
    gint finished;
    gint percent;
    gint progress_pending;
    gboolean success;
    GError *error;
} FileJob;

typedef struct {
    BinarkWindow *window;
    guint index;
} ThemeToggle;

typedef struct {
    BinarkWindow *window;
    gchar *kind;
    gboolean text_encode;
} SearchTarget;

typedef struct {
    BinarkWindow *window;
    AdwPreferencesGroup *group;
    GtkWidget *clear_row;
    GPtrArray *rows;
} HistoryPageContext;

typedef struct {
    BinarkWindow *window;
    gchar *content;
} SaveTextContext;

struct _BinarkWindow {
    AdwApplicationWindow parent_instance;

    GSettings *settings;
    BinarkHistory *history;

    AdwToastOverlay *toasts;
    GtkStack *stack;
    GtkStackSwitcher *stack_switcher;

    GtkMenuButton *search_button;
    GtkPopover *search_popover;
    GtkSearchEntry *search_entry;
    GtkListBox *search_list;
    GtkLabel *search_empty;
    GtkMenuButton *theme_button;
    GtkImage *theme_icon;
    GtkCheckButton *theme_checks[3];
    gboolean syncing_theme;

    /* Text mode. */
    gboolean text_encode;
    GtkTextView *text_source;
    GtkTextView *text_result;
    GtkTextBuffer *text_source_buffer;
    GtkTextBuffer *text_result_buffer;
    GtkLabel *text_source_title;
    GtkLabel *text_result_title;
    GtkLabel *text_flow;
    GtkLabel *text_source_stats;
    GtkLabel *text_result_stats;
    GtkLabel *text_error;
    GtkRevealer *text_error_revealer;
    GtkDropDown *text_output_base;

    /* Number mode. */
    GtkEntry *number_source;
    GtkEntry *number_result;
    GtkDropDown *number_source_base;
    GtkDropDown *number_target_base;
    GtkCheckButton *number_uppercase;
    GtkLabel *number_flow;
    GtkLabel *number_error;
    GtkRevealer *number_error_revealer;
    gboolean syncing_number_upper;

    /* File mode. */
    gboolean file_encode;
    gchar *file_input_path;
    gchar *file_output_path;
    guint64 file_input_size;
    GtkButton *file_input_button;
    GtkButton *file_output_button;
    GtkButton *file_convert_button;
    GtkDropDown *file_base;
    GtkButton *file_swap_button;
    GtkButton *file_clear_button;
    GtkButton *file_cancel_button;
    GtkCheckButton *file_compact;
    GtkLabel *file_input_title;
    GtkLabel *file_output_title;
    GtkLabel *file_input_path_label;
    GtkLabel *file_output_path_label;
    GtkLabel *file_input_info;
    GtkLabel *file_flow;
    GtkLabel *file_warning;
    GtkLabel *file_status;
    GtkLabel *file_error;
    GtkRevealer *file_error_revealer;
    GtkRevealer *file_progress_revealer;
    GtkProgressBar *file_progress;
    FileJob *file_job;
    gboolean close_after_job;
};

G_DEFINE_TYPE(BinarkWindow, binark_window, ADW_TYPE_APPLICATION_WINDOW)

static const guint number_bases[] = {
    2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 32, 64
};

static const gchar *
number_base_name(guint base)
{
    switch (base) {
    case 2: return "Двоичная";
    case 3: return "Троичная";
    case 4: return "Четверичная";
    case 5: return "Пятеричная";
    case 6: return "Шестеричная";
    case 7: return "Семеричная";
    case 8: return "Восьмеричная";
    case 9: return "Девятеричная";
    case 10: return "Десятичная";
    case 11: return "Одиннадцатеричная";
    case 12: return "Двенадцатеричная";
    case 13: return "Тринадцатеричная";
    case 14: return "Четырнадцатеричная";
    case 15: return "Пятнадцатеричная";
    case 16: return "Шестнадцатеричная";
    case 20: return "Двадцатеричная";
    case 32: return "Тридцатидвухричная";
    case 36: return "Тридцатишестиричная";
    case 60: return "Шестидесятеричная";
    case 64: return "Шестьдесятчетырёхричная";
    default: return NULL;
    }
}

static gchar *
number_base_label(guint base)
{
    const gchar *name = number_base_name(base);

    if (name != NULL)
        return g_strdup_printf("%s (%u)", name, base);
    return g_strdup_printf("Основание %u", base);
}

static void update_text_direction(BinarkWindow *self);
static void on_text_output_base_changed(GObject *object, GParamSpec *pspec,
                                       gpointer user_data);
static void update_file_direction(BinarkWindow *self);
static void on_file_base_changed(GObject *object, GParamSpec *pspec,
                                 gpointer user_data);
static void update_text_stats(BinarkWindow *self);
static void update_file_warning(BinarkWindow *self);
static void update_number_flow(BinarkWindow *self);
static void update_theme_checks(BinarkWindow *self);
static void text_convert(BinarkWindow *self);
static void number_convert(BinarkWindow *self);
static void file_convert(BinarkWindow *self);
static void file_reset(BinarkWindow *self);
static void text_clear_all(BinarkWindow *self);
static void number_clear_all(BinarkWindow *self);
static void text_swap_direction(BinarkWindow *self);
static void number_swap_direction(BinarkWindow *self);
static void file_swap_direction(BinarkWindow *self);
static void text_copy_result(BinarkWindow *self);
static void number_copy_result(BinarkWindow *self);
static void text_save_result(BinarkWindow *self);
static void number_save_result(BinarkWindow *self);
static void file_choose_input(BinarkWindow *self);
static void file_choose_output(BinarkWindow *self);
static void show_preferences(BinarkWindow *self, const gchar *page);
static void show_about(BinarkWindow *self);
static void search_rebuild(BinarkWindow *self);
static void save_text_async(BinarkWindow *self,
                            const gchar *content,
                            const gchar *title,
                            const gchar *initial_name);
static void show_toast(BinarkWindow *self, const gchar *text);
static void add_history(BinarkWindow *self,
                        BinarkHistoryKind kind,
                        const gchar *direction,
                        const gchar *summary);
static void on_text_convert_clicked(GtkButton *button, gpointer user_data);
static void on_text_paste_clicked(GtkButton *button, gpointer user_data);
static void on_number_convert_clicked(GtkButton *button, gpointer user_data);
static void on_number_entry_activate(GtkEntry *entry, gpointer user_data);

static GtkWidget *
new_label(const gchar *text, const gchar *css_class)
{
    GtkWidget *label = gtk_label_new(text);

    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    if (css_class != NULL)
        gtk_widget_add_css_class(label, css_class);
    return label;
}

static GtkWidget *
new_panel(const gchar *css_class)
{
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    gtk_widget_add_css_class(panel, "card");
    gtk_widget_add_css_class(panel, "binark-panel");
    if (css_class != NULL)
        gtk_widget_add_css_class(panel, css_class);
    return panel;
}

static GtkWidget *
new_scrolled_text_view(GtkTextView *view, int height)
{
    GtkWidget *scrolled = gtk_scrolled_window_new();
    GtkWidget *widget = GTK_WIDGET(view);

    gtk_widget_set_size_request(scrolled, -1, height);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_add_css_class(scrolled, "frame");
    gtk_text_view_set_accepts_tab(view, FALSE);
    gtk_text_view_set_left_margin(view, 10);
    gtk_text_view_set_right_margin(view, 10);
    gtk_text_view_set_top_margin(view, 8);
    gtk_text_view_set_bottom_margin(view, 8);
    gtk_text_view_set_pixels_below_lines(view, 2);
    gtk_widget_add_css_class(widget, "binark-text");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), widget);
    return scrolled;
}

static GtkWidget *
new_button_with_icon(const gchar *label,
                     const gchar *icon_name,
                     const gchar *css_class)
{
    GtkWidget *button = gtk_button_new();
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(content, GTK_ALIGN_CENTER);

    if (icon_name != NULL) {
        GtkWidget *image = gtk_image_new_from_icon_name(icon_name);

        gtk_box_append(GTK_BOX(content), image);
    }

    gtk_box_append(GTK_BOX(content), gtk_label_new(label));
    gtk_button_set_child(GTK_BUTTON(button), content);
    gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   label, -1);
    if (css_class != NULL)
        gtk_widget_add_css_class(button, css_class);
    return button;
}

static gchar *
text_view_get_text(GtkTextView *view)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void
set_text_view_text(GtkTextView *view, const gchar *text)
{
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(view),
                             text != NULL ? text : "",
                             -1);
}

static void
clipboard_copy(BinarkWindow *self, const gchar *text)
{
    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(self));
    GdkClipboard *clipboard = gdk_display_get_clipboard(display);

    gdk_clipboard_set_text(clipboard, text != NULL ? text : "");
}

static void
show_toast(BinarkWindow *self, const gchar *text)
{
    AdwToast *toast = adw_toast_new(text);

    adw_toast_set_timeout(toast, 3);
    adw_toast_overlay_add_toast(self->toasts, toast);
}

static void
show_alert(BinarkWindow *self, const gchar *heading, const gchar *body)
{
    AdwDialog *dialog = adw_alert_dialog_new(heading, body);

    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "close", "ОК");
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "close");
    adw_dialog_present(dialog, GTK_WIDGET(self));
}

static void
add_history(BinarkWindow *self,
            BinarkHistoryKind kind,
            const gchar *direction,
            const gchar *summary)
{
    GError *error = NULL;

    if (!g_settings_get_boolean(self->settings, "history-enabled"))
        return;

    binark_history_add(self->history, kind, direction, summary);
    if (!binark_history_save(self->history, &error)) {
        g_warning("Failed to save history: %s", error->message);
        show_toast(self, "Не удалось сохранить историю.");
        g_clear_error(&error);
    }
}

static void
show_text_error(BinarkWindow *self, const gchar *message)
{
    gtk_label_set_text(self->text_error, message);
    gtk_revealer_set_reveal_child(self->text_error_revealer, TRUE);
}

static void
hide_text_error(BinarkWindow *self)
{
    gtk_revealer_set_reveal_child(self->text_error_revealer, FALSE);
}

static void
show_number_error(BinarkWindow *self, const gchar *message)
{
    gtk_label_set_text(self->number_error, message);
    gtk_revealer_set_reveal_child(self->number_error_revealer, TRUE);
}

static void
hide_number_error(BinarkWindow *self)
{
    gtk_revealer_set_reveal_child(self->number_error_revealer, FALSE);
}

static void
show_file_error(BinarkWindow *self, const gchar *message)
{
    gtk_label_set_text(self->file_error, message);
    gtk_revealer_set_reveal_child(self->file_error_revealer, TRUE);
}

static void
hide_file_error(BinarkWindow *self)
{
    gtk_revealer_set_reveal_child(self->file_error_revealer, FALSE);
}

static void
update_text_stats(BinarkWindow *self)
{
    gchar *source = text_view_get_text(self->text_source);
    gchar *result = text_view_get_text(self->text_result);
    gsize source_len = strlen(source);
    gsize result_len = strlen(result);
    gsize source_chars = binark_utf8_char_count(source);
    gsize result_chars = binark_utf8_char_count(result);
    gchar *source_text;
    gchar *result_text;

    source_text = g_strdup_printf("Символов: %" G_GSIZE_FORMAT
                                  " · Байт: %" G_GSIZE_FORMAT,
                                  source_chars, source_len);
    result_text = g_strdup_printf("Символов: %" G_GSIZE_FORMAT
                                  " · Байт: %" G_GSIZE_FORMAT,
                                  result_chars, result_len);

    gtk_label_set_text(self->text_source_stats, source_text);
    gtk_label_set_text(self->text_result_stats, result_text);

    g_free(source_text);
    g_free(result_text);
    g_free(source);
    g_free(result);
}

static void
on_text_buffer_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    (void)buffer;
    update_text_stats(BINARK_WINDOW(user_data));
}

static guint
text_base_value(GtkDropDown *dropdown)
{
    guint index = gtk_drop_down_get_selected(dropdown);

    if (index == GTK_INVALID_LIST_POSITION || index >= G_N_ELEMENTS(number_bases))
        return 2;
    return number_bases[index];
}

static GtkDropDown *
new_text_base_dropdown(guint selected_base)
{
    GtkStringList *model;
    GtkWidget *dropdown;
    guint i;
    guint selected = 0;

    model = gtk_string_list_new(NULL);
    for (i = 0; i < G_N_ELEMENTS(number_bases); i++) {
        gchar *label = number_base_label(number_bases[i]);

        gtk_string_list_append(model, label);
        g_free(label);

        if (number_bases[i] == selected_base)
            selected = i;
    }

    dropdown = gtk_drop_down_new(G_LIST_MODEL(model), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), selected);
    g_object_unref(model);
    return GTK_DROP_DOWN(dropdown);
}

static void
update_text_direction(BinarkWindow *self)
{
    guint base = text_base_value(self->text_output_base);
    gchar *label = number_base_label(base);
    gchar *flow;

    if (self->text_encode) {
        gtk_label_set_text(self->text_source_title, "Текст");
        gtk_label_set_text(self->text_result_title, label);
        flow = g_strdup_printf("Текст → %s", label);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(self->text_source), FALSE);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(self->text_result), TRUE);
    } else {
        gtk_label_set_text(self->text_source_title, label);
        gtk_label_set_text(self->text_result_title, "Текст");
        flow = g_strdup_printf("%s → текст", label);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(self->text_source), TRUE);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(self->text_result), FALSE);
    }

    gtk_label_set_text(self->text_flow, flow);
    g_free(flow);
    g_free(label);
    hide_text_error(self);
    update_text_stats(self);
}

static gboolean
on_text_source_key_pressed(GtkEventControllerKey *controller,
                           guint keyval,
                           guint keycode,
                           GdkModifierType state,
                           gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);

    (void)controller;
    (void)keycode;
    if ((state & GDK_CONTROL_MASK) != 0 &&
        (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter)) {
        text_convert(self);
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static GtkWidget *
build_text_page(BinarkWindow *self)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *source_panel = new_panel(NULL);
    GtkWidget *source_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *result_panel = new_panel("binark-result");
    GtkWidget *result_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *swap_button;
    GtkWidget *actions;
    GtkWidget *convert_button;
    GtkWidget *paste_button;
    GtkWidget *clear_button;
    GtkWidget *copy_button;
    GtkWidget *save_button;
    GtkEventController *key_controller;
    GtkWidget *format_row;
    GtkWidget *format_label;

    self->text_flow = GTK_LABEL(new_label("Текст → биты", "binark-caption"));
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(self->text_flow));

    self->text_source_title = GTK_LABEL(new_label("Текст",
                                                   "binark-panel-title"));
    gtk_widget_set_hexpand(GTK_WIDGET(self->text_source_title), TRUE);
    gtk_box_append(GTK_BOX(source_header), GTK_WIDGET(self->text_source_title));
    gtk_box_append(GTK_BOX(source_panel), source_header);

    self->text_source = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_wrap_mode(self->text_source, GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_cursor_visible(self->text_source, TRUE);
    gtk_box_append(GTK_BOX(source_panel),
                   new_scrolled_text_view(self->text_source, 170));

    self->text_source_stats = GTK_LABEL(new_label("", "binark-stats"));
    gtk_box_append(GTK_BOX(source_panel),
                   GTK_WIDGET(self->text_source_stats));

    self->text_error = GTK_LABEL(new_label("", "binark-error"));
    gtk_label_set_selectable(self->text_error, TRUE);
    self->text_error_revealer = GTK_REVEALER(gtk_revealer_new());
    gtk_revealer_set_transition_type(self->text_error_revealer,
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_child(self->text_error_revealer,
                           GTK_WIDGET(self->text_error));
    gtk_box_append(GTK_BOX(source_panel),
                   GTK_WIDGET(self->text_error_revealer));
    gtk_box_append(GTK_BOX(page), source_panel);

    format_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    format_label = gtk_label_new("Система счисления");
    self->text_output_base = new_text_base_dropdown(2);
    g_signal_connect(self->text_output_base, "notify::selected",
                     G_CALLBACK(on_text_output_base_changed), self);
    gtk_box_append(GTK_BOX(format_row), GTK_WIDGET(format_label));
    gtk_box_append(GTK_BOX(format_row), GTK_WIDGET(self->text_output_base));
    gtk_box_append(GTK_BOX(page), format_row);

    swap_button = new_button_with_icon("Поменять направление",
                                       "object-flip-horizontal-symbolic",
                                       NULL);
    gtk_widget_set_halign(swap_button, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(swap_button,
                                "Поменять местами исходные данные и результат");
    g_signal_connect_swapped(swap_button, "clicked",
                             G_CALLBACK(text_swap_direction), self);
    gtk_box_append(GTK_BOX(page), swap_button);

    self->text_result_title = GTK_LABEL(new_label("Бинарное представление",
                                                   "binark-panel-title"));
    gtk_widget_set_hexpand(GTK_WIDGET(self->text_result_title), TRUE);
    gtk_box_append(GTK_BOX(result_header), GTK_WIDGET(self->text_result_title));
    gtk_box_append(GTK_BOX(result_panel), result_header);

    self->text_result = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_wrap_mode(self->text_result, GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(self->text_result, FALSE);
    gtk_text_view_set_cursor_visible(self->text_result, FALSE);
    gtk_box_append(GTK_BOX(result_panel),
                   new_scrolled_text_view(self->text_result, 170));

    self->text_result_stats = GTK_LABEL(new_label("", "binark-stats"));
    gtk_box_append(GTK_BOX(result_panel),
                   GTK_WIDGET(self->text_result_stats));
    gtk_box_append(GTK_BOX(page), result_panel);

    actions = adw_wrap_box_new();
    adw_wrap_box_set_child_spacing(ADW_WRAP_BOX(actions), 8);
    adw_wrap_box_set_line_spacing(ADW_WRAP_BOX(actions), 8);

    convert_button = new_button_with_icon("Преобразовать",
                                          "media-playback-start-symbolic",
                                          "suggested-action");
    g_signal_connect(convert_button, "clicked",
                     G_CALLBACK(on_text_convert_clicked), self);
    adw_wrap_box_append(ADW_WRAP_BOX(actions), convert_button);

    copy_button = new_button_with_icon("Копировать результат",
                                       "edit-copy-symbolic", NULL);
    g_signal_connect_swapped(copy_button, "clicked",
                             G_CALLBACK(text_copy_result), self);
    adw_wrap_box_append(ADW_WRAP_BOX(actions), copy_button);

    paste_button = new_button_with_icon("Вставить из буфера",
                                        "edit-paste-symbolic", NULL);
    g_signal_connect(paste_button, "clicked",
                     G_CALLBACK(on_text_paste_clicked), self);
    adw_wrap_box_append(ADW_WRAP_BOX(actions), paste_button);

    clear_button = new_button_with_icon("Очистить",
                                        "edit-clear-symbolic", NULL);
    g_signal_connect_swapped(clear_button, "clicked",
                             G_CALLBACK(text_clear_all), self);
    adw_wrap_box_append(ADW_WRAP_BOX(actions), clear_button);

    save_button = new_button_with_icon("Сохранить в файл",
                                       "document-save-symbolic", NULL);
    g_signal_connect_swapped(save_button, "clicked",
                             G_CALLBACK(text_save_result), self);
    adw_wrap_box_append(ADW_WRAP_BOX(actions), save_button);

    gtk_box_append(GTK_BOX(page), actions);

    self->text_source_buffer = gtk_text_view_get_buffer(self->text_source);
    self->text_result_buffer = gtk_text_view_get_buffer(self->text_result);
    g_signal_connect(self->text_source_buffer, "changed",
                     G_CALLBACK(on_text_buffer_changed), self);
    g_signal_connect(self->text_result_buffer, "changed",
                     G_CALLBACK(on_text_buffer_changed), self);

    key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed",
                     G_CALLBACK(on_text_source_key_pressed), self);
    gtk_widget_add_controller(GTK_WIDGET(self->text_source), key_controller);

    update_text_direction(self);
    return page;
}

static void
on_text_convert_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    text_convert(BINARK_WINDOW(user_data));
}

static void
on_number_convert_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    number_convert(BINARK_WINDOW(user_data));
}

static void
on_number_entry_activate(GtkEntry *entry, gpointer user_data)
{
    (void)entry;
    number_convert(BINARK_WINDOW(user_data));
}

static GtkDropDown *
new_base_dropdown(guint selected_base)
{
    GtkStringList *model;
    GtkWidget *dropdown;
    guint i;
    guint selected = 8; /* Десятичная (10) */

    model = gtk_string_list_new(NULL);
    for (i = 0; i < G_N_ELEMENTS(number_bases); i++) {
        gchar *label = number_base_label(number_bases[i]);

        gtk_string_list_append(model, label);
        g_free(label);

        if (number_bases[i] == selected_base)
            selected = i;
    }

    dropdown = gtk_drop_down_new(G_LIST_MODEL(model), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), selected);
    g_object_unref(model);
    return GTK_DROP_DOWN(dropdown);
}

static guint
number_base_value(GtkDropDown *dropdown)
{
    guint index = gtk_drop_down_get_selected(dropdown);

    if (index == GTK_INVALID_LIST_POSITION || index >= G_N_ELEMENTS(number_bases))
        return 10;
    return number_bases[index];
}

static void
on_number_base_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    update_number_flow(BINARK_WINDOW(user_data));
}

static void
on_number_uppercase_toggled(GtkCheckButton *button, gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);

    if (self->syncing_number_upper)
        return;

    g_settings_set_boolean(self->settings, "hex-uppercase",
                           gtk_check_button_get_active(button));
}

static void
update_number_flow(BinarkWindow *self)
{
    guint source_base = number_base_value(self->number_source_base);
    guint target_base = number_base_value(self->number_target_base);
    gchar *source_label = number_base_label(source_base);
    gchar *target_label = number_base_label(target_base);
    gchar *text;

    text = g_strdup_printf("%s → %s", source_label, target_label);
    gtk_label_set_text(self->number_flow, text);
    g_free(text);
    g_free(source_label);
    g_free(target_label);

    if (self->number_uppercase != NULL)
        gtk_widget_set_sensitive(GTK_WIDGET(self->number_uppercase),
                                 target_base <= 36);
}

static GtkWidget *
build_number_page(BinarkWindow *self)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *source_panel = new_panel(NULL);
    GtkWidget *result_panel = new_panel("binark-result");
    GtkWidget *source_base_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *target_base_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *source_base_label = new_label("Система счисления", NULL);
    GtkWidget *target_base_label = new_label("Система счисления", NULL);
    GtkWidget *hint;
    GtkWidget *swap_button;
    GtkWidget *actions;
    GtkWidget *convert_button;
    GtkWidget *copy_button;
    GtkWidget *clear_button;
    GtkWidget *save_button;

    self->number_flow = GTK_LABEL(new_label("Десятичная (10) → Двоичная (2)",
                                             "binark-caption"));
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(self->number_flow));

    gtk_box_append(GTK_BOX(source_panel),
                   new_label("Исходное число", "binark-panel-title"));

    self->number_source = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(self->number_source,
                                   "Например, 42, -255 или 0x2A");
    gtk_widget_add_css_class(GTK_WIDGET(self->number_source), "binark-mono");
    gtk_widget_set_hexpand(GTK_WIDGET(self->number_source), TRUE);
    g_signal_connect(self->number_source, "activate",
                     G_CALLBACK(on_number_entry_activate), self);
    gtk_box_append(GTK_BOX(source_panel), GTK_WIDGET(self->number_source));

    gtk_widget_set_hexpand(source_base_label, TRUE);
    gtk_widget_set_valign(source_base_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(source_base_row), source_base_label);
    self->number_source_base = new_base_dropdown(10);
    gtk_widget_set_hexpand(GTK_WIDGET(self->number_source_base), FALSE);
    gtk_box_append(GTK_BOX(source_base_row),
                   GTK_WIDGET(self->number_source_base));
    gtk_box_append(GTK_BOX(source_panel), source_base_row);

    hint = new_label("Поддерживаются целые числа произвольной длины, знак "
                     "«−», префиксы 0b, 0o, 0x и основания 2–12, 16, 32 и "
                     "64 (цифры 0–9, A–Z, a–z, а для основания 64 ещё + и /). "
                     "Для основания 64 регистр букв важен.",
                     "binark-caption");
    gtk_box_append(GTK_BOX(source_panel), hint);

    self->number_error = GTK_LABEL(new_label("", "binark-error"));
    gtk_label_set_selectable(self->number_error, TRUE);
    self->number_error_revealer = GTK_REVEALER(gtk_revealer_new());
    gtk_revealer_set_transition_type(self->number_error_revealer,
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_child(self->number_error_revealer,
                           GTK_WIDGET(self->number_error));
    gtk_box_append(GTK_BOX(source_panel),
                   GTK_WIDGET(self->number_error_revealer));
    gtk_box_append(GTK_BOX(page), source_panel);

    swap_button = new_button_with_icon("Поменять направление",
                                       "object-flip-horizontal-symbolic",
                                       NULL);
    gtk_widget_set_halign(swap_button, GTK_ALIGN_CENTER);
    g_signal_connect_swapped(swap_button, "clicked",
                             G_CALLBACK(number_swap_direction), self);
    gtk_box_append(GTK_BOX(page), swap_button);

    gtk_box_append(GTK_BOX(result_panel),
                   new_label("Результат", "binark-panel-title"));

    self->number_result = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(self->number_result,
                                   "Результат появится здесь");
    gtk_editable_set_editable(GTK_EDITABLE(self->number_result), FALSE);
    gtk_widget_add_css_class(GTK_WIDGET(self->number_result), "binark-mono");
    gtk_widget_set_hexpand(GTK_WIDGET(self->number_result), TRUE);
    gtk_box_append(GTK_BOX(result_panel), GTK_WIDGET(self->number_result));

    gtk_widget_set_hexpand(target_base_label, TRUE);
    gtk_widget_set_valign(target_base_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(target_base_row), target_base_label);
    self->number_target_base = new_base_dropdown(2);
    gtk_box_append(GTK_BOX(target_base_row),
                   GTK_WIDGET(self->number_target_base));
    gtk_box_append(GTK_BOX(result_panel), target_base_row);

    self->number_uppercase = GTK_CHECK_BUTTON(
        gtk_check_button_new_with_label("Верхний регистр букв"));
    gtk_check_button_set_active(self->number_uppercase,
                                g_settings_get_boolean(self->settings,
                                                       "hex-uppercase"));
    g_signal_connect(self->number_uppercase, "toggled",
                     G_CALLBACK(on_number_uppercase_toggled), self);
    gtk_box_append(GTK_BOX(result_panel),
                   GTK_WIDGET(self->number_uppercase));
    gtk_box_append(GTK_BOX(page), result_panel);

    actions = adw_wrap_box_new();
    adw_wrap_box_set_child_spacing(ADW_WRAP_BOX(actions), 8);
    adw_wrap_box_set_line_spacing(ADW_WRAP_BOX(actions), 8);

    convert_button = new_button_with_icon("Преобразовать",
                                          "media-playback-start-symbolic",
                                          "suggested-action");
    g_signal_connect(convert_button, "clicked",
                     G_CALLBACK(on_number_convert_clicked), self);
    adw_wrap_box_append(ADW_WRAP_BOX(actions), convert_button);

    copy_button = new_button_with_icon("Копировать результат",
                                       "edit-copy-symbolic", NULL);
    g_signal_connect_swapped(copy_button, "clicked",
                             G_CALLBACK(number_copy_result), self);
    adw_wrap_box_append(ADW_WRAP_BOX(actions), copy_button);

    clear_button = new_button_with_icon("Очистить",
                                        "edit-clear-symbolic", NULL);
    g_signal_connect_swapped(clear_button, "clicked",
                             G_CALLBACK(number_clear_all), self);
    adw_wrap_box_append(ADW_WRAP_BOX(actions), clear_button);

    save_button = new_button_with_icon("Сохранить в файл",
                                       "document-save-symbolic", NULL);
    g_signal_connect_swapped(save_button, "clicked",
                             G_CALLBACK(number_save_result), self);
    adw_wrap_box_append(ADW_WRAP_BOX(actions), save_button);

    gtk_box_append(GTK_BOX(page), actions);

    g_signal_connect(self->number_source_base, "notify::selected",
                     G_CALLBACK(on_number_base_changed), self);
    g_signal_connect(self->number_target_base, "notify::selected",
                     G_CALLBACK(on_number_base_changed), self);

    update_number_flow(self);
    return page;
}

static void
on_file_compact_toggled(GtkCheckButton *button, gpointer user_data)
{
    (void)button;
    update_file_warning(BINARK_WINDOW(user_data));
}

static void
on_file_base_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    update_file_direction(BINARK_WINDOW(user_data));
}

static void
on_file_choose_input_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    file_choose_input(BINARK_WINDOW(user_data));
}

static void
on_file_choose_output_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    file_choose_output(BINARK_WINDOW(user_data));
}

static void
on_file_convert_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    file_convert(BINARK_WINDOW(user_data));
}

static void
on_file_cancel_clicked(GtkButton *button, gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);

    (void)button;
    if (self->file_job != NULL) {
        g_atomic_int_set(&self->file_job->cancelled, 1);
        gtk_widget_set_sensitive(GTK_WIDGET(self->file_cancel_button), FALSE);
        gtk_label_set_text(self->file_status, "Отмена операции…");
    }
}

static void
on_file_swap_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    file_swap_direction(BINARK_WINDOW(user_data));
}

static void
on_file_clear_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    file_reset(BINARK_WINDOW(user_data));
}

static GtkWidget *
build_file_page(BinarkWindow *self)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *source_panel = new_panel(NULL);
    GtkWidget *result_panel = new_panel("binark-result");
    GtkWidget *source_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *output_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *base_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *base_label = gtk_label_new("Система счисления");
    GtkWidget *progress_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *progress_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *actions;
    GtkWidget *open_button;

    self->file_flow = GTK_LABEL(new_label("Файл → биты", "binark-caption"));
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(self->file_flow));

    self->file_input_title = GTK_LABEL(new_label("Исходный файл",
                                                  "binark-panel-title"));
    gtk_box_append(GTK_BOX(source_panel), GTK_WIDGET(self->file_input_title));

    self->file_input_button = GTK_BUTTON(
        new_button_with_icon("Выбрать файл…",
                             "document-open-symbolic", NULL));
    g_signal_connect(self->file_input_button, "clicked",
                     G_CALLBACK(on_file_choose_input_clicked), self);
    gtk_box_append(GTK_BOX(source_row), GTK_WIDGET(self->file_input_button));

    self->file_input_path_label = GTK_LABEL(new_label("Файл не выбран",
                                                       "binark-file-path"));
    gtk_label_set_ellipsize(self->file_input_path_label, PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(GTK_WIDGET(self->file_input_path_label), TRUE);
    gtk_label_set_selectable(self->file_input_path_label, TRUE);
    gtk_box_append(GTK_BOX(source_row),
                   GTK_WIDGET(self->file_input_path_label));
    gtk_box_append(GTK_BOX(source_panel), source_row);

    self->file_input_info = GTK_LABEL(new_label("Имя, размер и тип появятся "
                                                 "после выбора файла.",
                                                 "binark-caption"));
    gtk_label_set_selectable(self->file_input_info, TRUE);
    gtk_box_append(GTK_BOX(source_panel),
                   GTK_WIDGET(self->file_input_info));

    self->file_warning = GTK_LABEL(new_label("", "binark-warning"));
    gtk_box_append(GTK_BOX(source_panel), GTK_WIDGET(self->file_warning));

    self->file_error = GTK_LABEL(new_label("", "binark-error"));
    gtk_label_set_selectable(self->file_error, TRUE);
    self->file_error_revealer = GTK_REVEALER(gtk_revealer_new());
    gtk_revealer_set_transition_type(self->file_error_revealer,
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_child(self->file_error_revealer,
                           GTK_WIDGET(self->file_error));
    gtk_box_append(GTK_BOX(source_panel),
                   GTK_WIDGET(self->file_error_revealer));
    gtk_box_append(GTK_BOX(page), source_panel);

    gtk_widget_set_hexpand(base_label, TRUE);
    gtk_widget_set_valign(base_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(base_row), base_label);
    self->file_base = new_text_base_dropdown(2);
    g_signal_connect(self->file_base, "notify::selected",
                     G_CALLBACK(on_file_base_changed), self);
    gtk_box_append(GTK_BOX(base_row), GTK_WIDGET(self->file_base));
    gtk_box_append(GTK_BOX(page), base_row);

    gtk_box_append(GTK_BOX(result_panel),
                   new_label("Куда сохранить", "binark-panel-title"));

    self->file_output_title = GTK_LABEL(new_label("Результат",
                                                   "binark-caption"));
    gtk_box_append(GTK_BOX(result_panel), GTK_WIDGET(self->file_output_title));

    self->file_output_button = GTK_BUTTON(
        new_button_with_icon("Выбрать путь…",
                             "document-save-symbolic", NULL));
    g_signal_connect(self->file_output_button, "clicked",
                     G_CALLBACK(on_file_choose_output_clicked), self);
    gtk_box_append(GTK_BOX(output_row), GTK_WIDGET(self->file_output_button));

    self->file_output_path_label = GTK_LABEL(new_label("Путь не выбран",
                                                        "binark-file-path"));
    gtk_label_set_ellipsize(self->file_output_path_label,
                            PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(GTK_WIDGET(self->file_output_path_label), TRUE);
    gtk_label_set_selectable(self->file_output_path_label, TRUE);
    gtk_box_append(GTK_BOX(output_row),
                   GTK_WIDGET(self->file_output_path_label));
    gtk_box_append(GTK_BOX(result_panel), output_row);

    self->file_compact = GTK_CHECK_BUTTON(
        gtk_check_button_new_with_label("Компактный формат без разделителей"));
    gtk_check_button_set_active(self->file_compact,
                                g_settings_get_boolean(self->settings,
                                                       "compact-file-format"));
    g_signal_connect(self->file_compact, "toggled",
                     G_CALLBACK(on_file_compact_toggled), self);
    gtk_box_append(GTK_BOX(result_panel), GTK_WIDGET(self->file_compact));

    self->file_status = GTK_LABEL(new_label("Готово к преобразованию",
                                             "binark-stats"));
    gtk_box_append(GTK_BOX(result_panel), GTK_WIDGET(self->file_status));

    self->file_progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_progress_bar_set_show_text(self->file_progress, TRUE);
    gtk_progress_bar_set_text(self->file_progress, "0 %");
    gtk_box_append(GTK_BOX(progress_row), GTK_WIDGET(self->file_progress));
    gtk_widget_set_hexpand(GTK_WIDGET(self->file_progress), TRUE);
    self->file_cancel_button = GTK_BUTTON(
        new_button_with_icon("Отменить", "process-stop-symbolic",
                             "destructive-action"));
    g_signal_connect(self->file_cancel_button, "clicked",
                     G_CALLBACK(on_file_cancel_clicked), self);
    gtk_box_append(GTK_BOX(progress_row),
                   GTK_WIDGET(self->file_cancel_button));
    gtk_box_append(GTK_BOX(progress_box), progress_row);

    self->file_progress_revealer = GTK_REVEALER(gtk_revealer_new());
    gtk_revealer_set_transition_type(self->file_progress_revealer,
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_child(self->file_progress_revealer, progress_box);
    gtk_revealer_set_reveal_child(self->file_progress_revealer, FALSE);
    gtk_box_append(GTK_BOX(result_panel),
                   GTK_WIDGET(self->file_progress_revealer));
    gtk_box_append(GTK_BOX(page), result_panel);

    actions = adw_wrap_box_new();
    adw_wrap_box_set_child_spacing(ADW_WRAP_BOX(actions), 8);
    adw_wrap_box_set_line_spacing(ADW_WRAP_BOX(actions), 8);

    self->file_convert_button = GTK_BUTTON(
        new_button_with_icon("Преобразовать",
                             "media-playback-start-symbolic",
                             "suggested-action"));
    g_signal_connect(self->file_convert_button, "clicked",
                     G_CALLBACK(on_file_convert_clicked), self);
    adw_wrap_box_append(ADW_WRAP_BOX(actions),
                        GTK_WIDGET(self->file_convert_button));

    self->file_swap_button = GTK_BUTTON(
        new_button_with_icon("Поменять направление",
                             "object-flip-horizontal-symbolic", NULL));
    g_signal_connect(self->file_swap_button, "clicked",
                     G_CALLBACK(on_file_swap_clicked), self);
    adw_wrap_box_append(ADW_WRAP_BOX(actions),
                        GTK_WIDGET(self->file_swap_button));

    open_button = new_button_with_icon("Открыть…",
                                       "document-open-symbolic", NULL);
    g_signal_connect_swapped(open_button, "clicked",
                             G_CALLBACK(file_choose_input), self);
    adw_wrap_box_append(ADW_WRAP_BOX(actions), open_button);

    self->file_clear_button = GTK_BUTTON(
        new_button_with_icon("Очистить", "edit-clear-symbolic", NULL));
    g_signal_connect(self->file_clear_button, "clicked",
                     G_CALLBACK(on_file_clear_clicked), self);
    adw_wrap_box_append(ADW_WRAP_BOX(actions),
                        GTK_WIDGET(self->file_clear_button));

    gtk_box_append(GTK_BOX(page), actions);

    update_file_direction(self);
    return page;
}

static void
on_theme_toggle(GtkCheckButton *button, gpointer user_data)
{
    ThemeToggle *toggle = user_data;
    const gchar *value;

    if (!gtk_check_button_get_active(button) || toggle->window->syncing_theme)
        return;

    switch (toggle->index) {
    case 1:
        value = "light";
        break;
    case 2:
        value = "dark";
        break;
    default:
        value = "system";
        break;
    }
    g_settings_set_string(toggle->window->settings, "theme", value);
}

static void
update_theme_checks(BinarkWindow *self)
{
    gchar *theme = g_settings_get_string(self->settings, "theme");
    guint selected = 0;

    if (g_strcmp0(theme, "light") == 0)
        selected = 1;
    else if (g_strcmp0(theme, "dark") == 0)
        selected = 2;

    self->syncing_theme = TRUE;
    for (guint i = 0; i < 3; i++) {
        if (self->theme_checks[i] != NULL)
            gtk_check_button_set_active(self->theme_checks[i], i == selected);
    }
    self->syncing_theme = FALSE;

    if (selected == 1)
        gtk_image_set_from_icon_name(self->theme_icon,
                                     "weather-clear-symbolic");
    else if (selected == 2)
        gtk_image_set_from_icon_name(self->theme_icon,
                                     "weather-clear-night-symbolic");
    else
        gtk_image_set_from_icon_name(self->theme_icon,
                                     "display-brightness-symbolic");

    g_free(theme);
}

static void
save_text_finish(GObject *source_object,
                 GAsyncResult *result,
                 gpointer user_data)
{
    SaveTextContext *context = user_data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    GFile *file;
    GError *error = NULL;
    gchar *path = NULL;

    file = gtk_file_dialog_save_finish(dialog, result, &error);
    if (file == NULL) {
        if (!g_error_matches(error, GTK_DIALOG_ERROR,
                             GTK_DIALOG_ERROR_DISMISSED) &&
            !g_error_matches(error, GTK_DIALOG_ERROR,
                             GTK_DIALOG_ERROR_CANCELLED)) {
            show_alert(context->window, "Не удалось сохранить файл",
                       error->message);
        }
        g_clear_error(&error);
        goto out;
    }

    path = g_file_get_path(file);
    if (path == NULL) {
        show_alert(context->window, "Не удалось сохранить файл",
                   "Выбранный файл не является локальным.");
        g_object_unref(file);
        goto out;
    }

    if (!g_file_replace_contents(file,
                                 context->content,
                                 strlen(context->content),
                                 NULL,
                                 FALSE,
                                 G_FILE_CREATE_REPLACE_DESTINATION,
                                 NULL,
                                 NULL,
                                 &error)) {
        show_alert(context->window, "Не удалось сохранить файл",
                   error->message);
        g_clear_error(&error);
    } else {
        show_toast(context->window, "Результат сохранён.");
    }

    g_object_unref(file);
    g_free(path);

out:
    g_clear_object(&context->window);
    g_free(context->content);
    g_free(context);
}

static void
save_text_async(BinarkWindow *self,
                const gchar *content,
                const gchar *title,
                const gchar *initial_name)
{
    GtkFileDialog *dialog;
    SaveTextContext *context;

    if (content == NULL || *content == '\0') {
        show_toast(self, "Нет результата для сохранения.");
        return;
    }

    dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, title);
    gtk_file_dialog_set_initial_name(dialog, initial_name);
    gtk_file_dialog_set_accept_label(dialog, "Сохранить");

    context = g_new0(SaveTextContext, 1);
    context->window = g_object_ref(self);
    context->content = g_strdup(content);

    gtk_file_dialog_save(dialog, GTK_WINDOW(self), NULL,
                         save_text_finish, context);
    g_object_unref(dialog);
}

static void
on_text_output_base_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    update_text_direction(BINARK_WINDOW(user_data));
}

static void
text_convert(BinarkWindow *self)
{
    gchar *source = text_view_get_text(self->text_source);
    gsize source_len = strlen(source);
    guint base = text_base_value(self->text_output_base);
    gchar *label = number_base_label(base);
    GError *error = NULL;

    hide_text_error(self);

    if (self->text_encode) {
        gboolean separated = g_settings_get_boolean(self->settings,
                                                     "separated-bits");
        guint bytes_per_line = (guint)g_settings_get_int(self->settings,
                                                          "bytes-per-line");
        gsize result_len = 0;
        gchar *result;
        gchar *summary;

        result = binark_text_encode_base(source, source_len, base, separated,
                                         bytes_per_line, &result_len);

        set_text_view_text(self->text_result, result);
        if (base == 2) {
            summary = g_strdup_printf("%" G_GSIZE_FORMAT " симв. → "
                                      "%" G_GSIZE_FORMAT " байт → "
                                      "%" G_GSIZE_FORMAT " бит",
                                      binark_utf8_char_count(source),
                                      source_len,
                                      result_len);
        } else {
            summary = g_strdup_printf("%" G_GSIZE_FORMAT " байт → %s",
                                      source_len, label);
        }
        {
            gchar *history = g_strdup_printf("Текст → %s", label);

            add_history(self, BINARK_HISTORY_TEXT, history, summary);
            g_free(history);
        }
        g_free(summary);
        if (g_settings_get_boolean(self->settings, "copy-after-convert") &&
            result_len > 0) {
            clipboard_copy(self, result);
            show_toast(self, "Результат скопирован в буфер обмена.");
        }
        g_free(result);
    } else {
        gchar *decoded = NULL;
        gsize decoded_len = 0;
        gchar *summary;
        gchar *history;

        if (!binark_text_decode_base(source, source_len, base, &decoded,
                                     &decoded_len, &error)) {
            set_text_view_text(self->text_result, "");
            show_text_error(self, error->message);
            g_clear_error(&error);
            update_text_stats(self);
            g_free(label);
            g_free(source);
            return;
        }

        set_text_view_text(self->text_result, decoded);
        summary = g_strdup_printf("%" G_GSIZE_FORMAT " байт → "
                                  "%" G_GSIZE_FORMAT " симв.",
                                  decoded_len,
                                  binark_utf8_char_count(decoded));
        history = g_strdup_printf("%s → текст", label);
        add_history(self, BINARK_HISTORY_TEXT, history, summary);
        g_free(history);
        g_free(summary);
        if (g_settings_get_boolean(self->settings, "copy-after-convert") &&
            decoded_len > 0) {
            clipboard_copy(self, decoded);
            show_toast(self, "Результат скопирован в буфер обмена.");
        }
        g_free(decoded);
    }

    update_text_stats(self);
    g_free(label);
    g_free(source);
}

static void
text_swap_direction(BinarkWindow *self)
{
    gchar *source = text_view_get_text(self->text_source);
    gchar *result = text_view_get_text(self->text_result);

    if (*result != '\0') {
        gchar *old_source = source;

        source = NULL;
        set_text_view_text(self->text_source, result);
        set_text_view_text(self->text_result, old_source);
        g_free(old_source);
    }

    self->text_encode = !self->text_encode;
    update_text_direction(self);
    g_free(source);
    g_free(result);
}

static void
text_clear_all(BinarkWindow *self)
{
    set_text_view_text(self->text_source, "");
    set_text_view_text(self->text_result, "");
    hide_text_error(self);
    update_text_stats(self);
    gtk_widget_grab_focus(GTK_WIDGET(self->text_source));
}

static void
text_copy_result(BinarkWindow *self)
{
    gchar *result = text_view_get_text(self->text_result);

    if (*result == '\0') {
        show_toast(self, "Сначала выполните преобразование.");
    } else {
        clipboard_copy(self, result);
        show_toast(self, "Результат скопирован в буфер обмена.");
    }
    g_free(result);
}

static void
text_save_result(BinarkWindow *self)
{
    gchar *result = text_view_get_text(self->text_result);

    save_text_async(self, result, "Сохранить результат",
                    self->text_encode ? "binark-result.bits.txt"
                                      : "binark-result.txt");
    g_free(result);
}

typedef struct {
    BinarkWindow *window;
} PasteContext;

static void
text_paste_finish(GObject *source_object,
                  GAsyncResult *result,
                  gpointer user_data)
{
    PasteContext *context = user_data;
    GdkClipboard *clipboard = GDK_CLIPBOARD(source_object);
    GError *error = NULL;
    gchar *text;

    text = gdk_clipboard_read_text_finish(clipboard, result, &error);
    if (text == NULL) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            show_toast(context->window, "Не удалось вставить текст из буфера.");
        g_clear_error(&error);
    } else {
        set_text_view_text(context->window->text_source, text);
        g_free(text);
    }

    g_object_unref(context->window);
    g_free(context);
}

static void
on_text_paste_clicked(GtkButton *button, gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);
    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(self));
    GdkClipboard *clipboard = gdk_display_get_clipboard(display);
    PasteContext *context = g_new0(PasteContext, 1);

    (void)button;
    context->window = g_object_ref(self);
    gdk_clipboard_read_text_async(clipboard, NULL, text_paste_finish,
                                  context);
}

static void
number_convert(BinarkWindow *self)
{
    guint source_base = number_base_value(self->number_source_base);
    guint target_base = number_base_value(self->number_target_base);
    const gchar *source = gtk_editable_get_text(GTK_EDITABLE(self->number_source));
    gchar *result = NULL;
    GError *error = NULL;
    gboolean uppercase = gtk_check_button_get_active(self->number_uppercase);

    hide_number_error(self);

    if (!binark_number_convert(source, source_base, target_base, uppercase,
                               &result, &error)) {
        gtk_editable_set_text(GTK_EDITABLE(self->number_result), "");
        show_number_error(self, error->message);
        g_clear_error(&error);
        return;
    }

    gtk_editable_set_text(GTK_EDITABLE(self->number_result), result);
    {
        gchar *source_label = number_base_label(source_base);
        gchar *target_label = number_base_label(target_base);
        gchar *direction = g_strdup_printf("%s → %s", source_label,
                                           target_label);
        gchar *summary = g_strdup_printf("Результат: %" G_GSIZE_FORMAT
                                         " цифр.",
                                         g_utf8_strlen(result, -1));

        add_history(self, BINARK_HISTORY_NUMBER, direction, summary);
        g_free(summary);
        g_free(direction);
        g_free(source_label);
        g_free(target_label);
    }

    if (g_settings_get_boolean(self->settings, "copy-after-convert") &&
        *result != '\0') {
        clipboard_copy(self, result);
        show_toast(self, "Результат скопирован в буфер обмена.");
    }

    g_free(result);
}

static void
number_swap_direction(BinarkWindow *self)
{
    guint source_index = gtk_drop_down_get_selected(self->number_source_base);
    guint target_index = gtk_drop_down_get_selected(self->number_target_base);
    const gchar *result = gtk_editable_get_text(GTK_EDITABLE(self->number_result));

    if (*result != '\0') {
        gchar *copy = g_strdup(result);

        gtk_editable_set_text(GTK_EDITABLE(self->number_source), copy);
        gtk_editable_set_text(GTK_EDITABLE(self->number_result), "");
        g_free(copy);
    }

    gtk_drop_down_set_selected(self->number_source_base, target_index);
    gtk_drop_down_set_selected(self->number_target_base, source_index);
    hide_number_error(self);
    update_number_flow(self);
    gtk_widget_grab_focus(GTK_WIDGET(self->number_source));
}

static void
number_clear_all(BinarkWindow *self)
{
    gtk_editable_set_text(GTK_EDITABLE(self->number_source), "");
    gtk_editable_set_text(GTK_EDITABLE(self->number_result), "");
    hide_number_error(self);
    gtk_widget_grab_focus(GTK_WIDGET(self->number_source));
}

static void
number_copy_result(BinarkWindow *self)
{
    const gchar *result = gtk_editable_get_text(GTK_EDITABLE(self->number_result));

    if (*result == '\0') {
        show_toast(self, "Сначала выполните преобразование.");
        return;
    }

    clipboard_copy(self, result);
    show_toast(self, "Результат скопирован в буфер обмена.");
}

static void
number_save_result(BinarkWindow *self)
{
    const gchar *result = gtk_editable_get_text(GTK_EDITABLE(self->number_result));

    save_text_async(self, result, "Сохранить результат",
                    "binark-number.txt");
}

static void
file_job_ref(FileJob *job)
{
    g_atomic_int_inc(&job->ref_count);
}

static void
file_job_unref(FileJob *job)
{
    if (!g_atomic_int_dec_and_test(&job->ref_count))
        return;

    g_clear_error(&job->error);
    g_free(job->input_path);
    g_free(job->output_path);
    if (job->window != NULL)
        g_object_unref(job->window);
    g_free(job);
}

static const gchar *
digits_instrumental(guint count)
{
    return (count % 10 == 1 && count % 100 != 11) ? "цифрой" : "цифрами";
}

static const gchar *
digits_genitive(guint count)
{
    if (count % 10 == 1 && count % 100 != 11)
        return "цифра";
    if (count % 10 >= 2 && count % 10 <= 4 &&
        (count % 100 < 12 || count % 100 > 14))
        return "цифры";
    return "цифр";
}

static void
update_file_warning(BinarkWindow *self)
{
    guint base = text_base_value(self->file_base);
    guint width = binark_text_base_width(base);

    if (width == 0)
        width = 8;

    if (self->file_encode) {
        gboolean compact = gtk_check_button_get_active(self->file_compact) ||
                           !g_settings_get_boolean(self->settings,
                                                   "separated-bits");
        guint bytes_per_line = (guint)g_settings_get_int(self->settings,
                                                         "bytes-per-line");
        guint64 estimate;
        gchar *estimate_text;
        gchar *text;
        gchar *label = number_base_label(base);

        gtk_widget_add_css_class(GTK_WIDGET(self->file_warning),
                                 "binark-warning");
        if (self->file_input_path == NULL) {
            text = g_strdup_printf("Каждый байт будет записан %u %s "
                                   "в системе «%s». Для больших файлов "
                                   "выбирайте компактный формат.",
                                   width, digits_instrumental(width), label);
            gtk_label_set_text(self->file_warning, text);
            g_free(text);
            g_free(label);
            return;
        }

        estimate = binark_file_encoded_size_base(self->file_input_size, base,
                                                 compact, bytes_per_line);
        estimate_text = binark_format_size(estimate);
        text = g_strdup_printf("Текстовый результат займёт около %s: "
                               "%u %s на байт в системе «%s».",
                               estimate_text, width, digits_genitive(width),
                               label);
        gtk_label_set_text(self->file_warning, text);
        g_free(text);
        g_free(estimate_text);
        g_free(label);
    } else {
        gchar *text;

        gtk_widget_remove_css_class(GTK_WIDGET(self->file_warning),
                                    "binark-warning");
        text = g_strdup_printf("При восстановлении каждая группа из %u "
                               "цифр проверяется как значение байта (0–255). "
                               "Повреждённый файл не заменит существующий "
                               "результат.", width);
        gtk_label_set_text(self->file_warning, text);
        g_free(text);
    }
}

static void
update_file_direction(BinarkWindow *self)
{
    guint base = text_base_value(self->file_base);
    gchar *label = number_base_label(base);
    gchar *flow;
    gchar *title;

    if (self->file_encode) {
        flow = g_strdup_printf("Файл → %s", label);
        gtk_label_set_text(self->file_flow, flow);
        gtk_label_set_text(self->file_input_title, "Исходный файл");
        gtk_label_set_text(self->file_output_title,
                           "Сохранить текст в системе счисления в");
        gtk_widget_set_sensitive(GTK_WIDGET(self->file_compact), TRUE);
    } else {
        flow = g_strdup_printf("%s → файл", label);
        gtk_label_set_text(self->file_flow, flow);
        title = g_strdup_printf("Файл с текстом в системе «%s»", label);
        gtk_label_set_text(self->file_input_title, title);
        g_free(title);
        gtk_label_set_text(self->file_output_title,
                           "Сохранить восстановленный файл в");
        gtk_widget_set_sensitive(GTK_WIDGET(self->file_compact), FALSE);
    }

    gtk_widget_set_sensitive(GTK_WIDGET(self->file_base),
                             self->file_job == NULL);
    g_free(flow);
    g_free(label);
    hide_file_error(self);
    update_file_warning(self);
}

static gchar *
file_suffix_for_base(guint base)
{
    if (base == 2)
        return g_strdup(".bits.txt");
    return g_strdup_printf(".base%u.txt", base);
}

static gchar *
suggested_output_name(const gchar *input_path, gboolean encode, guint base)
{
    gchar *suffix = file_suffix_for_base(base);
    gchar *basename;
    gchar *result;

    if (input_path == NULL) {
        if (encode)
            result = g_strconcat("binark-output", suffix, NULL);
        else
            result = g_strdup("binark-output.bin");
        g_free(suffix);
        return result;
    }

    basename = g_path_get_basename(input_path);
    if (encode) {
        result = g_strconcat(basename, suffix, NULL);
    } else if (g_str_has_suffix(basename, suffix)) {
        result = g_strndup(basename, strlen(basename) - strlen(suffix));
        if (*result == '\0') {
            g_free(result);
            result = g_strdup("binark-output.bin");
        }
    } else if (g_str_has_suffix(basename, ".bits.txt")) {
        result = g_strndup(basename, strlen(basename) - strlen(".bits.txt"));
        if (*result == '\0') {
            g_free(result);
            result = g_strdup("binark-output.bin");
        }
    } else {
        result = g_strconcat(basename, ".decoded", NULL);
    }

    g_free(basename);
    g_free(suffix);
    return result;
}

static void
update_file_info(BinarkWindow *self, GFile *file)
{
    GFileInfo *info = NULL;
    GError *error = NULL;
    const gchar *display_name = NULL;
    const gchar *content_type;
    gchar *type_description = NULL;
    gchar *size_text;
    gchar *info_text;
    guint64 size = 0;

    self->file_input_size = 0;

    info = g_file_query_info(file,
                             G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
                             G_FILE_ATTRIBUTE_STANDARD_SIZE ","
                             G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                             G_FILE_QUERY_INFO_NONE, NULL, &error);
    if (info == NULL) {
        gtk_label_set_text(self->file_input_info, error->message);
        gtk_widget_add_css_class(GTK_WIDGET(self->file_input_info),
                                 "binark-error");
        g_clear_error(&error);
        update_file_warning(self);
        return;
    }

    gtk_widget_remove_css_class(GTK_WIDGET(self->file_input_info),
                                "binark-error");
    display_name = g_file_info_get_display_name(info);
    size = g_file_info_get_size(info);
    content_type = g_file_info_get_content_type(info);
    if (content_type != NULL)
        type_description = g_content_type_get_description(content_type);
    if (type_description == NULL)
        type_description = g_strdup("Неизвестный тип");

    self->file_input_size = size;
    size_text = binark_format_size(size);
    info_text = g_strdup_printf("Имя: %s\nРазмер: %s\nТип: %s",
                                display_name != NULL ? display_name : "—",
                                size_text,
                                type_description);
    gtk_label_set_text(self->file_input_info, info_text);

    g_free(info_text);
    g_free(size_text);
    g_free(type_description);
    g_object_unref(info);
    update_file_warning(self);
}

static void
file_input_finish(GObject *source_object,
                  GAsyncResult *result,
                  gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    GFile *file;
    GError *error = NULL;
    gchar *path;

    file = gtk_file_dialog_open_finish(dialog, result, &error);
    if (file == NULL) {
        if (!g_error_matches(error, GTK_DIALOG_ERROR,
                             GTK_DIALOG_ERROR_DISMISSED) &&
            !g_error_matches(error, GTK_DIALOG_ERROR,
                             GTK_DIALOG_ERROR_CANCELLED)) {
            show_file_error(self, error->message);
        }
        g_clear_error(&error);
        g_object_unref(self);
        return;
    }

    path = g_file_get_path(file);
    if (path == NULL) {
        show_file_error(self, "Выбранный файл не является локальным.");
        g_object_unref(file);
        g_object_unref(self);
        return;
    }

    g_free(self->file_input_path);
    self->file_input_path = path;
    gtk_label_set_text(self->file_input_path_label, path);
    update_file_info(self, file);
    hide_file_error(self);
    g_object_unref(file);
    g_object_unref(self);
}

static void
file_choose_input(BinarkWindow *self)
{
    GtkFileDialog *dialog;

    if (self->file_job != NULL)
        return;

    dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog,
                              self->file_encode ? "Выбрать файл"
                                                : "Выбрать файл с текстом");
    gtk_file_dialog_set_accept_label(dialog, "Открыть");
    gtk_file_dialog_open(dialog, GTK_WINDOW(self), NULL,
                         file_input_finish, g_object_ref(self));
    g_object_unref(dialog);
}

static void
file_output_finish(GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    GFile *file;
    GError *error = NULL;
    gchar *path;

    file = gtk_file_dialog_save_finish(dialog, result, &error);
    if (file == NULL) {
        if (!g_error_matches(error, GTK_DIALOG_ERROR,
                             GTK_DIALOG_ERROR_DISMISSED) &&
            !g_error_matches(error, GTK_DIALOG_ERROR,
                             GTK_DIALOG_ERROR_CANCELLED)) {
            show_file_error(self, error->message);
        }
        g_clear_error(&error);
        g_object_unref(self);
        return;
    }

    path = g_file_get_path(file);
    if (path == NULL) {
        show_file_error(self, "Выбранный файл не является локальным.");
        g_object_unref(file);
        g_object_unref(self);
        return;
    }

    g_free(self->file_output_path);
    self->file_output_path = path;
    gtk_label_set_text(self->file_output_path_label, path);
    hide_file_error(self);
    g_object_unref(file);
    g_object_unref(self);
}

static void
file_choose_output(BinarkWindow *self)
{
    GtkFileDialog *dialog;
    gchar *suggested;
    GtkFileFilter *filter;
    GListStore *filters;

    if (self->file_job != NULL)
        return;

    dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog,
                              self->file_encode
                                  ? "Сохранить текст в системе счисления"
                                  : "Сохранить восстановленный файл");
    gtk_file_dialog_set_accept_label(dialog, "Сохранить");

    suggested = suggested_output_name(self->file_input_path,
                                      self->file_encode,
                                      text_base_value(self->file_base));
    gtk_file_dialog_set_initial_name(dialog, suggested);
    g_free(suggested);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Все файлы");
    gtk_file_filter_add_pattern(filter, "*");
    filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, filter);
    g_object_unref(filter);
    g_object_unref(filters);

    if (self->file_input_path != NULL) {
        GFile *input = g_file_new_for_path(self->file_input_path);
        GFile *parent = g_file_get_parent(input);

        if (parent != NULL)
            gtk_file_dialog_set_initial_folder(dialog, parent);
        g_clear_object(&parent);
        g_object_unref(input);
    }

    gtk_file_dialog_save(dialog, GTK_WINDOW(self), NULL,
                         file_output_finish, g_object_ref(self));
    g_object_unref(dialog);
}

static gboolean
on_file_progress_idle(gpointer user_data)
{
    FileJob *job = user_data;
    BinarkWindow *self = job->window;
    gint percent;

    g_atomic_int_set(&job->progress_pending, 0);
    if (g_atomic_int_get(&job->finished))
        return G_SOURCE_REMOVE;

    percent = g_atomic_int_get(&job->percent);
    gtk_progress_bar_set_fraction(self->file_progress, percent / 100.0);
    {
        gchar *text = g_strdup_printf("%d %%", percent);

        gtk_progress_bar_set_text(self->file_progress, text);
        g_free(text);
    }
    return G_SOURCE_REMOVE;
}

static gboolean
on_file_job_done(gpointer user_data)
{
    FileJob *job = user_data;
    BinarkWindow *self = job->window;
    gboolean close_window;

    g_atomic_int_set(&job->finished, 1);

    if (job->thread != NULL) {
        g_thread_join(job->thread);
        job->thread = NULL;
    }

    gtk_widget_set_sensitive(GTK_WIDGET(self->file_input_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->file_output_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->file_convert_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->file_swap_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->file_clear_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->file_compact), self->file_encode);
    gtk_widget_set_sensitive(GTK_WIDGET(self->file_base), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->file_cancel_button), FALSE);

    if (job->success) {
        gchar *basename = g_path_get_basename(job->output_path);
        gchar *message = g_strdup_printf("Файл сохранён: %s", basename);
        gchar *base_label = number_base_label(job->base);
        gchar *direction = job->encode
                               ? g_strdup_printf("Файл → %s", base_label)
                               : g_strdup_printf("%s → файл", base_label);
        gchar *summary;
        struct stat output_info;

        gtk_progress_bar_set_fraction(self->file_progress, 1.0);
        gtk_progress_bar_set_text(self->file_progress, "100 %");
        gtk_label_set_text(self->file_status, message);
        gtk_widget_remove_css_class(GTK_WIDGET(self->file_status),
                                    "binark-error");
        gtk_widget_add_css_class(GTK_WIDGET(self->file_status),
                                 "binark-success");
        show_toast(self, message);

        if (stat(job->output_path, &output_info) == 0 && output_info.st_size >= 0) {
            gchar *input_text = binark_format_size(job->input_size);
            gchar *output_text = binark_format_size((guint64)output_info.st_size);

            summary = g_strdup_printf("%s → %s", input_text, output_text);
            g_free(input_text);
            g_free(output_text);
        } else {
            summary = g_strdup("Файл успешно сохранён.");
        }
        add_history(self, BINARK_HISTORY_FILE, direction, summary);
        g_free(summary);
        g_free(direction);
        g_free(base_label);
        g_free(message);
        g_free(basename);
        hide_file_error(self);
    } else if (job->error != NULL &&
               g_error_matches(job->error, BINARK_FILE_ERROR,
                               BINARK_FILE_ERROR_CANCELLED)) {
        gtk_label_set_text(self->file_status, "Операция отменена.");
        gtk_widget_remove_css_class(GTK_WIDGET(self->file_status),
                                    "binark-success");
        gtk_widget_add_css_class(GTK_WIDGET(self->file_status),
                                 "binark-error");
        gtk_progress_bar_set_text(self->file_progress, "Отменено");
        hide_file_error(self);
    } else {
        gtk_label_set_text(self->file_status, "Не удалось преобразовать файл.");
        gtk_widget_remove_css_class(GTK_WIDGET(self->file_status),
                                    "binark-success");
        show_file_error(self, job->error != NULL
                                  ? job->error->message
                                  : "Неизвестная ошибка.");
        gtk_progress_bar_set_text(self->file_progress, "Ошибка");
    }

    close_window = self->close_after_job;
    self->close_after_job = FALSE;
    if (self->file_job == job) {
        self->file_job = NULL;
        file_job_unref(job);
    }

    if (close_window)
        gtk_window_close(GTK_WINDOW(self));

    return G_SOURCE_REMOVE;
}

static gboolean
file_progress_cb(guint64 done, guint64 total, gpointer user_data)
{
    FileJob *job = user_data;
    gint percent;

    if (g_atomic_int_get(&job->cancelled))
        return FALSE;

    if (total == 0 || done >= total)
        percent = 100;
    else if (done > G_MAXUINT64 / 100)
        percent = (gint)(done / (total / 100 + 1));
    else
        percent = (gint)((done * 100) / total);

    if (percent != g_atomic_int_get(&job->percent)) {
        g_atomic_int_set(&job->percent, percent);
        if (g_atomic_int_compare_and_exchange(&job->progress_pending, 0, 1)) {
            file_job_ref(job);
            g_idle_add_full(G_PRIORITY_DEFAULT,
                            on_file_progress_idle,
                            job,
                            (GDestroyNotify)file_job_unref);
        }
    }
    return TRUE;
}

static gpointer
file_job_worker(gpointer user_data)
{
    FileJob *job = user_data;
    GError *error = NULL;
    gboolean ok;

    if (job->encode) {
        ok = binark_file_encode_base(job->input_path, job->output_path,
                                     job->base, job->compact,
                                     job->bytes_per_line,
                                     file_progress_cb, job, &error);
    } else {
        ok = binark_file_decode_base(job->input_path, job->output_path,
                                     job->base, file_progress_cb, job,
                                     &error);
    }

    job->success = ok;
    job->error = error;

    file_job_ref(job);
    g_idle_add_full(G_PRIORITY_DEFAULT,
                    on_file_job_done,
                    job,
                    (GDestroyNotify)file_job_unref);
    return NULL;
}

static void
file_start_job(BinarkWindow *self)
{
    FileJob *job;
    gboolean separated = g_settings_get_boolean(self->settings,
                                                 "separated-bits");
    gboolean compact = gtk_check_button_get_active(self->file_compact) ||
                       !separated;

    job = g_new0(FileJob, 1);
    job->window = g_object_ref(self);
    job->input_path = g_strdup(self->file_input_path);
    job->output_path = g_strdup(self->file_output_path);
    job->encode = self->file_encode;
    job->compact = compact;
    job->bytes_per_line = (guint)g_settings_get_int(self->settings,
                                                    "bytes-per-line");
    job->base = text_base_value(self->file_base);
    job->input_size = self->file_input_size;
    job->ref_count = 1;

    self->file_job = job;
    hide_file_error(self);
    gtk_revealer_set_reveal_child(self->file_progress_revealer, TRUE);
    gtk_progress_bar_set_fraction(self->file_progress, 0.0);
    gtk_progress_bar_set_text(self->file_progress, "0 %");
    gtk_label_set_text(self->file_status,
                       job->encode ? "Кодирование файла…"
                                   : "Восстановление файла…");
    gtk_widget_remove_css_class(GTK_WIDGET(self->file_status),
                                "binark-success");
    gtk_widget_remove_css_class(GTK_WIDGET(self->file_status),
                                "binark-error");
    gtk_widget_set_sensitive(GTK_WIDGET(self->file_cancel_button), TRUE);

    gtk_widget_set_sensitive(GTK_WIDGET(self->file_input_button), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->file_output_button), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->file_convert_button), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->file_swap_button), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->file_clear_button), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->file_compact), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->file_base), FALSE);

    job->thread = g_thread_new("binark-file", file_job_worker, job);
}

typedef struct {
    BinarkWindow *window;
} OverwriteContext;

static void
on_overwrite_choice(GObject *source_object,
                    GAsyncResult *result,
                    gpointer user_data)
{
    OverwriteContext *context = user_data;
    const gchar *response;

    response = adw_alert_dialog_choose_finish(
        ADW_ALERT_DIALOG(source_object), result);
    if (g_strcmp0(response, "replace") == 0)
        file_start_job(context->window);

    g_object_unref(context->window);
    g_free(context);
}

static void
file_convert(BinarkWindow *self)
{
    if (self->file_job != NULL) {
        show_toast(self, "Дождитесь завершения текущей операции.");
        return;
    }

    if (self->file_input_path == NULL) {
        show_file_error(self, "Сначала выберите исходный файл.");
        return;
    }
    if (self->file_output_path == NULL) {
        show_file_error(self, "Сначала выберите путь для сохранения.");
        return;
    }
    if (!g_file_test(self->file_input_path, G_FILE_TEST_IS_REGULAR)) {
        show_file_error(self, "Исходный файл не существует или недоступен.");
        return;
    }

    hide_file_error(self);

    if (g_file_test(self->file_output_path, G_FILE_TEST_EXISTS)) {
        AdwDialog *dialog = adw_alert_dialog_new(
            "Заменить существующий файл?",
            "Файл по выбранному пути уже существует. Он будет заменён "
            "только после успешного завершения преобразования.");
        OverwriteContext *context = g_new0(OverwriteContext, 1);

        adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog),
                                       "cancel", "Отмена",
                                       "replace", "Заменить",
                                       NULL);
        adw_alert_dialog_set_response_appearance(
            ADW_ALERT_DIALOG(dialog), "replace",
            ADW_RESPONSE_DESTRUCTIVE);
        adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog),
                                              "cancel");
        adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog),
                                            "cancel");
        context->window = g_object_ref(self);
        adw_alert_dialog_choose(ADW_ALERT_DIALOG(dialog), GTK_WIDGET(self),
                                NULL, on_overwrite_choice, context);
        return;
    }

    file_start_job(self);
}

static void
file_reset(BinarkWindow *self)
{
    if (self->file_job != NULL) {
        g_atomic_int_set(&self->file_job->cancelled, 1);
        show_toast(self, "Сначала дождитесь отмены операции.");
        return;
    }

    g_clear_pointer(&self->file_input_path, g_free);
    g_clear_pointer(&self->file_output_path, g_free);
    self->file_input_size = 0;
    gtk_label_set_text(self->file_input_path_label, "Файл не выбран");
    gtk_label_set_text(self->file_output_path_label, "Путь не выбран");
    gtk_label_set_text(self->file_input_info,
                       "Имя, размер и тип появятся после выбора файла.");
    gtk_widget_remove_css_class(GTK_WIDGET(self->file_input_info),
                                "binark-error");
    gtk_label_set_text(self->file_status, "Готово к преобразованию");
    gtk_widget_remove_css_class(GTK_WIDGET(self->file_status),
                                "binark-success");
    gtk_widget_remove_css_class(GTK_WIDGET(self->file_status),
                                "binark-error");
    gtk_revealer_set_reveal_child(self->file_progress_revealer, FALSE);
    hide_file_error(self);
    update_file_warning(self);
}

static void
file_swap_direction(BinarkWindow *self)
{
    if (self->file_job != NULL) {
        show_toast(self, "Дождитесь завершения операции.");
        return;
    }

    self->file_encode = !self->file_encode;
    if (self->file_output_path != NULL) {
        g_clear_pointer(&self->file_output_path, g_free);
        gtk_label_set_text(self->file_output_path_label, "Путь не выбран");
    }
    gtk_revealer_set_reveal_child(self->file_progress_revealer, FALSE);
    gtk_label_set_text(self->file_status, "Готово к преобразованию");
    update_file_direction(self);
}

static void
history_context_free(gpointer data)
{
    HistoryPageContext *context = data;

    g_ptr_array_unref(context->rows);
    g_free(context);
}

static void
history_page_refresh(HistoryPageContext *context)
{
    BinarkWindow *self = context->window;
    guint i;

    for (i = 0; i < context->rows->len; i++) {
        GtkWidget *row = g_ptr_array_index(context->rows, i);

        if (gtk_widget_get_parent(row) == GTK_WIDGET(context->group))
            adw_preferences_group_remove(context->group, row);
    }
    g_ptr_array_set_size(context->rows, 0);

    if (gtk_widget_get_parent(context->clear_row) ==
        GTK_WIDGET(context->group))
        adw_preferences_group_remove(context->group, context->clear_row);

    if (binark_history_get_length(self->history) == 0) {
        AdwPreferencesRow *row = ADW_PREFERENCES_ROW(
            adw_action_row_new());

        adw_preferences_row_set_title(row, "Операций пока нет");
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row),
                                    "Здесь появятся последние преобразования.");
        adw_preferences_group_add(context->group, GTK_WIDGET(row));
        g_ptr_array_add(context->rows, GTK_WIDGET(row));
    } else {
        for (i = 0; i < binark_history_get_length(self->history); i++) {
            const BinarkHistoryEntry *entry =
                binark_history_get(self->history, i);
            gchar *time = binark_history_format_time(entry->timestamp);
            gchar *subtitle;
            AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());

            subtitle = g_strdup_printf("%s · %s · %s",
                                       time,
                                       binark_history_kind_label(entry->kind),
                                       entry->summary);
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),
                                          entry->direction);
            adw_action_row_set_subtitle(row, subtitle);
            adw_action_row_set_subtitle_lines(row, 2);
            adw_preferences_group_add(context->group, GTK_WIDGET(row));
            g_ptr_array_add(context->rows, GTK_WIDGET(row));
            g_free(subtitle);
            g_free(time);
        }
    }

    adw_preferences_group_add(context->group, context->clear_row);
    gtk_widget_set_sensitive(context->clear_row,
                             binark_history_get_length(self->history) > 0);
}

static void
on_clear_history_clicked(GtkWidget *widget, gpointer user_data)
{
    HistoryPageContext *context = user_data;
    GError *error = NULL;

    (void)widget;
    binark_history_clear(context->window->history);
    if (!binark_history_save(context->window->history, &error)) {
        show_alert(context->window, "Не удалось очистить историю",
                   error->message);
        g_clear_error(&error);
    } else {
        show_toast(context->window, "История очищена.");
    }
    history_page_refresh(context);
}

static void
on_settings_theme_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);
    guint selected = adw_combo_row_get_selected(ADW_COMBO_ROW(object));
    const gchar *theme;

    (void)pspec;
    theme = selected == 1 ? "light" : (selected == 2 ? "dark" : "system");
    g_settings_set_string(self->settings, "theme", theme);
}

static void
on_settings_bytes_per_line_changed(GObject *object,
                                   GParamSpec *pspec,
                                   gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);
    gint value = (gint)adw_spin_row_get_value(ADW_SPIN_ROW(object));

    (void)pspec;
    g_settings_set_int(self->settings, "bytes-per-line", value);
}

static void
on_settings_separator_changed(GObject *object,
                              GParamSpec *pspec,
                              gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);
    guint selected = adw_combo_row_get_selected(ADW_COMBO_ROW(object));

    (void)pspec;
    g_settings_set_boolean(self->settings, "separated-bits", selected == 0);
}

static void
on_preferences_closed(AdwDialog *dialog, gpointer user_data)
{
    (void)dialog;
    history_context_free(user_data);
}

static void
show_preferences(BinarkWindow *self, const gchar *page)
{
    AdwPreferencesDialog *dialog = ADW_PREFERENCES_DIALOG(
        adw_preferences_dialog_new());
    AdwPreferencesPage *page_appearance;
    AdwPreferencesPage *page_format;
    AdwPreferencesPage *page_behavior;
    AdwPreferencesPage *page_history;
    AdwPreferencesGroup *group;
    AdwComboRow *theme_row;
    GtkStringList *themes;
    GtkAdjustment *adjustment;
    AdwSpinRow *spin;
    AdwComboRow *separator;
    GtkStringList *separators;
    AdwSwitchRow *switch_row;
    HistoryPageContext *history_context;
    gchar *theme;
    guint selected = 0;
    GtkWidget *clear_row;

    adw_preferences_dialog_set_search_enabled(dialog, TRUE);

    page_appearance = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page_appearance, "Внешний вид");
    adw_preferences_page_set_name(page_appearance, "appearance");
    adw_preferences_page_set_icon_name(page_appearance,
                                       "applications-graphics-symbolic");

    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, "Тема");
    adw_preferences_page_add(page_appearance, group);

    theme_row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(theme_row), "Оформление");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(theme_row),
                                "Системная тема, светлая или тёмная");
    themes = gtk_string_list_new(NULL);
    gtk_string_list_append(themes, "Системная");
    gtk_string_list_append(themes, "Светлая");
    gtk_string_list_append(themes, "Тёмная");
    adw_combo_row_set_model(theme_row, G_LIST_MODEL(themes));
    theme = g_settings_get_string(self->settings, "theme");
    if (g_strcmp0(theme, "light") == 0)
        selected = 1;
    else if (g_strcmp0(theme, "dark") == 0)
        selected = 2;
    adw_combo_row_set_selected(theme_row, selected);
    g_free(theme);
    g_object_unref(themes);
    g_signal_connect(theme_row, "notify::selected",
                     G_CALLBACK(on_settings_theme_changed), self);
    adw_preferences_group_add(group, GTK_WIDGET(theme_row));
    adw_preferences_dialog_add(dialog, page_appearance);

    page_format = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page_format, "Форматирование");
    adw_preferences_page_set_name(page_format, "format");
    adw_preferences_page_set_icon_name(page_format, "preferences-system-symbolic");

    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, "Двоичное представление");
    adw_preferences_group_set_description(
        group, "Эти настройки применяются к тексту и кодированию файлов.");
    adw_preferences_page_add(page_format, group);

    separator = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(separator),
                                   "Разделитель байтов");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(separator),
                                "Пробелы и переносы строк или компактная запись");
    separators = gtk_string_list_new(NULL);
    gtk_string_list_append(separators, "Пробелы и переносы");
    gtk_string_list_append(separators, "Без разделителей");
    adw_combo_row_set_model(separator, G_LIST_MODEL(separators));
    adw_combo_row_set_selected(separator,
                               g_settings_get_boolean(self->settings,
                                                      "separated-bits")
                                   ? 0
                                   : 1);
    g_object_unref(separators);
    g_signal_connect(separator, "notify::selected",
                     G_CALLBACK(on_settings_separator_changed), self);
    adw_preferences_group_add(group, GTK_WIDGET(separator));

    adjustment = gtk_adjustment_new(
        g_settings_get_int(self->settings, "bytes-per-line"),
        1, 64, 1, 8, 0);
    spin = ADW_SPIN_ROW(adw_spin_row_new(adjustment, 1, 0));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(spin),
                                   "Байтов в строке");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(spin),
                                "От 1 до 64 байт в одной строке");
    g_signal_connect(spin, "notify::value",
                     G_CALLBACK(on_settings_bytes_per_line_changed), self);
    adw_preferences_group_add(group, GTK_WIDGET(spin));

    switch_row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(switch_row),
                                   "Верхний регистр букв");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(switch_row),
                                "Использовать A–Z в системах счисления больше 10");
    g_settings_bind(self->settings, "hex-uppercase", switch_row, "active",
                    G_SETTINGS_BIND_DEFAULT);
    adw_preferences_group_add(group, GTK_WIDGET(switch_row));

    switch_row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(switch_row),
                                   "Компактный формат файлов");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(switch_row),
                                "По умолчанию записывать биты файлов без разделителей");
    g_settings_bind(self->settings, "compact-file-format", switch_row,
                    "active", G_SETTINGS_BIND_DEFAULT);
    adw_preferences_group_add(group, GTK_WIDGET(switch_row));
    adw_preferences_dialog_add(dialog, page_format);

    page_behavior = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page_behavior, "Поведение");
    adw_preferences_page_set_name(page_behavior, "behavior");
    adw_preferences_page_set_icon_name(page_behavior, "emblem-system-symbolic");

    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, "Результат");
    adw_preferences_page_add(page_behavior, group);

    switch_row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(switch_row),
                                   "Копировать результат автоматически");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(switch_row),
                                "После успешного преобразования текста или числа");
    g_settings_bind(self->settings, "copy-after-convert", switch_row,
                    "active", G_SETTINGS_BIND_DEFAULT);
    adw_preferences_group_add(group, GTK_WIDGET(switch_row));

    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, "История");
    adw_preferences_page_add(page_behavior, group);

    switch_row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(switch_row),
                                   "Сохранять историю");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(switch_row),
                                "Только тип, время, направление и краткое описание");
    g_settings_bind(self->settings, "history-enabled", switch_row, "active",
                    G_SETTINGS_BIND_DEFAULT);
    adw_preferences_group_add(group, GTK_WIDGET(switch_row));
    adw_preferences_dialog_add(dialog, page_behavior);

    page_history = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page_history, "История");
    adw_preferences_page_set_name(page_history, "history");
    adw_preferences_page_set_icon_name(page_history, "document-open-recent-symbolic");
    adw_preferences_page_set_description(
        page_history,
        "Содержимое текста и файлов не сохраняется. Хранятся только тип "
        "операции, время, направление и краткое описание.");
    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, "Последние операции");
    adw_preferences_page_add(page_history, group);

    history_context = g_new0(HistoryPageContext, 1);
    history_context->window = self;
    history_context->group = group;
    history_context->rows = g_ptr_array_new();
    clear_row = GTK_WIDGET(adw_button_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(clear_row),
                                   "Очистить историю");
    adw_button_row_set_start_icon_name(ADW_BUTTON_ROW(clear_row),
                                       "edit-clear-all-symbolic");
    gtk_widget_add_css_class(clear_row, "destructive-action");
    g_signal_connect(clear_row, "activated",
                     G_CALLBACK(on_clear_history_clicked), history_context);
    history_context->clear_row = clear_row;
    history_page_refresh(history_context);
    adw_preferences_dialog_add(dialog, page_history);

    g_signal_connect(dialog, "closed", G_CALLBACK(on_preferences_closed),
                     history_context);

    if (page != NULL)
        adw_preferences_dialog_set_visible_page_name(dialog, page);

    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(self));
}

static void
show_about(BinarkWindow *self)
{
    static const char *developers[] = {
        "SipuhSloper",
        NULL
    };
    static const char *technologies[] = {
        "Язык C17",
        "GTK 4",
        "libadwaita",
        "GLib и GIO",
        NULL
    };
    AdwDialog *dialog = adw_about_dialog_new();

    adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(dialog), "Binark");
    adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(dialog),
                                           "com.binark.Binark");
    adw_about_dialog_set_developer_name(ADW_ABOUT_DIALOG(dialog),
                                         "SipuhSloper");
    adw_about_dialog_set_developers(ADW_ABOUT_DIALOG(dialog), developers);
    adw_about_dialog_set_version(ADW_ABOUT_DIALOG(dialog), BINARK_VERSION);
    adw_about_dialog_set_comments(
        ADW_ABOUT_DIALOG(dialog),
        "Универсальный конвертер текста, целых чисел и файлов в бинарное "
        "представление и обратно. Работает полностью локально и не требует "
        "подключения к интернету.");
    adw_about_dialog_add_acknowledgement_section(
        ADW_ABOUT_DIALOG(dialog), "Используемые технологии", technologies);
    adw_about_dialog_set_license_type(ADW_ABOUT_DIALOG(dialog),
                                      GTK_LICENSE_MIT_X11);
    adw_about_dialog_set_copyright(ADW_ABOUT_DIALOG(dialog),
                                    "© 2026 SipuhSloper");
    adw_dialog_present(dialog, GTK_WIDGET(self));
}

static void
search_target_free(gpointer data)
{
    SearchTarget *target = data;

    g_free(target->kind);
    g_free(target);
}

static void
search_add_result(BinarkWindow *self,
                  const gchar *title,
                  const gchar *subtitle,
                  const gchar *kind,
                  gboolean text_encode)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title_label = new_label(title, NULL);
    GtkWidget *subtitle_label = new_label(subtitle, "binark-caption");
    SearchTarget *target = g_new0(SearchTarget, 1);

    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_label_set_ellipsize(GTK_LABEL(subtitle_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(box), title_label);
    gtk_box_append(GTK_BOX(box), subtitle_label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);

    target->window = self;
    target->kind = g_strdup(kind);
    target->text_encode = text_encode;
    g_object_set_data_full(G_OBJECT(row), "binark-search-target", target,
                           search_target_free);
    gtk_list_box_append(self->search_list, row);
}

static gboolean
search_matches(const gchar *query, const gchar *text)
{
    gchar *folded_text;
    gchar *folded_query;
    gboolean result;

    if (query == NULL || *query == '\0')
        return TRUE;

    folded_text = g_utf8_casefold(text != NULL ? text : "", -1);
    folded_query = g_utf8_casefold(query, -1);
    result = g_strstr_len(folded_text, -1, folded_query) != NULL;
    g_free(folded_text);
    g_free(folded_query);
    return result;
}

static void
search_rebuild(BinarkWindow *self)
{
    const gchar *query = gtk_editable_get_text(GTK_EDITABLE(self->search_entry));
    guint added = 0;
    guint history_limit;
    guint i;

    gtk_list_box_remove_all(self->search_list);

    struct {
        const gchar *title;
        const gchar *subtitle;
        const gchar *kind;
        gboolean encode;
    } functions[] = {
        {"Текст → система счисления", "Кодировать UTF-8 текст", "text-encode", TRUE},
        {"Система счисления → текст", "Декодировать текст", "text-decode", FALSE},
        {"Числа", "Основания 2–12, 16, 32 и 64", "numbers", FALSE},
        {"Файлы", "Кодировать или восстановить файл потоком в выбранной системе", "files", FALSE},
        {"Настройки", "Тема, форматирование, история и поведение", "preferences", FALSE},
        {"История", "Последние операции без содержимого", "history", FALSE},
        {"О программе", "Версия, технологии и лицензия", "about", FALSE}
    };

    for (i = 0; i < G_N_ELEMENTS(functions); i++) {
        if (search_matches(query, functions[i].title) ||
            search_matches(query, functions[i].subtitle)) {
            search_add_result(self, functions[i].title,
                              functions[i].subtitle,
                              functions[i].kind,
                              functions[i].encode);
            added++;
        }
    }

    history_limit = *query == '\0' ? 8 : binark_history_get_length(self->history);
    for (i = 0; i < history_limit; i++) {
        const BinarkHistoryEntry *entry = binark_history_get(self->history, i);
        gchar *time;
        gchar *subtitle;

        if (entry == NULL)
            break;

        time = binark_history_format_time(entry->timestamp);
        subtitle = g_strdup_printf("История · %s · %s · %s",
                                   time,
                                   binark_history_kind_label(entry->kind),
                                   entry->summary);
        if (search_matches(query, entry->direction) ||
            search_matches(query, entry->summary) ||
            search_matches(query, binark_history_kind_label(entry->kind)) ||
            search_matches(query, time)) {
            search_add_result(self, entry->direction, subtitle,
                              "history", FALSE);
            added++;
        }
        g_free(subtitle);
        g_free(time);
    }

    gtk_widget_set_visible(GTK_WIDGET(self->search_empty), added == 0);
    gtk_label_set_text(self->search_empty,
                       added == 0 ? "Ничего не найдено." : "");
}

static void
on_search_entry_changed(GtkSearchEntry *entry, gpointer user_data)
{
    (void)entry;
    search_rebuild(BINARK_WINDOW(user_data));
}

static void
on_search_popover_map(GtkWidget *widget, gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);

    (void)widget;
    search_rebuild(self);
    gtk_widget_grab_focus(GTK_WIDGET(self->search_entry));
}

static void
on_search_row_activated(GtkListBox *list, GtkListBoxRow *row, gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);
    SearchTarget *target = g_object_get_data(G_OBJECT(row),
                                             "binark-search-target");

    (void)list;
    if (target == NULL)
        return;

    gtk_popover_popdown(self->search_popover);

    if (g_strcmp0(target->kind, "text-encode") == 0) {
        gtk_stack_set_visible_child_name(self->stack, "text");
        self->text_encode = TRUE;
        update_text_direction(self);
        gtk_widget_grab_focus(GTK_WIDGET(self->text_source));
    } else if (g_strcmp0(target->kind, "text-decode") == 0) {
        gtk_stack_set_visible_child_name(self->stack, "text");
        self->text_encode = FALSE;
        update_text_direction(self);
        gtk_widget_grab_focus(GTK_WIDGET(self->text_source));
    } else if (g_strcmp0(target->kind, "numbers") == 0) {
        gtk_stack_set_visible_child_name(self->stack, "numbers");
        gtk_widget_grab_focus(GTK_WIDGET(self->number_source));
    } else if (g_strcmp0(target->kind, "files") == 0) {
        gtk_stack_set_visible_child_name(self->stack, "files");
    } else if (g_strcmp0(target->kind, "preferences") == 0) {
        show_preferences(self, NULL);
    } else if (g_strcmp0(target->kind, "history") == 0) {
        show_preferences(self, "history");
    } else if (g_strcmp0(target->kind, "about") == 0) {
        show_about(self);
    }
}

static GtkWidget *
build_search_button(BinarkWindow *self)
{
    GtkWidget *button = gtk_menu_button_new();
    GtkWidget *popover = gtk_popover_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *scrolled;
    GtkWidget *title;

    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button),
                                  "system-search-symbolic");
    gtk_widget_set_tooltip_text(button, "Поиск функций и истории");
    gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Поиск", -1);
    gtk_widget_set_size_request(box, 370, -1);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);

    title = new_label("Поиск", "title-4");
    gtk_box_append(GTK_BOX(box), title);

    self->search_entry = GTK_SEARCH_ENTRY(gtk_search_entry_new());
    gtk_search_entry_set_placeholder_text(self->search_entry,
                                          "Функции и история…");
    g_signal_connect(self->search_entry, "search-changed",
                     G_CALLBACK(on_search_entry_changed), self);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(self->search_entry));

    self->search_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(self->search_list, GTK_SELECTION_NONE);
    gtk_list_box_set_activate_on_single_click(self->search_list, TRUE);
    g_signal_connect(self->search_list, "row-activated",
                     G_CALLBACK(on_search_row_activated), self);

    scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, -1, 340);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled),
                                  GTK_WIDGET(self->search_list));
    gtk_box_append(GTK_BOX(box), scrolled);

    self->search_empty = GTK_LABEL(new_label("Ничего не найдено.",
                                             "binark-caption"));
    gtk_label_set_xalign(self->search_empty, 0.5f);
    gtk_widget_set_margin_top(GTK_WIDGET(self->search_empty), 8);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(self->search_empty));

    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect(popover, "map",
                     G_CALLBACK(on_search_popover_map), self);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);

    self->search_button = GTK_MENU_BUTTON(button);
    self->search_popover = GTK_POPOVER(popover);
    return button;
}

static GtkWidget *
build_theme_button(BinarkWindow *self)
{
    static const gchar *labels[] = {
        "Системная тема",
        "Светлая тема",
        "Тёмная тема"
    };
    GtkWidget *button = gtk_menu_button_new();
    GtkWidget *popover = gtk_popover_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *title = new_label("Оформление", "title-4");
    GtkCheckButton *previous = NULL;

    gtk_widget_set_size_request(box, 220, -1);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_box_append(GTK_BOX(box), title);

    for (guint i = 0; i < 3; i++) {
        GtkCheckButton *check = GTK_CHECK_BUTTON(
            gtk_check_button_new_with_label(labels[i]));
        ThemeToggle *toggle = g_new0(ThemeToggle, 1);

        toggle->window = self;
        toggle->index = i;
        if (previous != NULL)
            gtk_check_button_set_group(check, previous);
        previous = check;
        g_object_set_data_full(G_OBJECT(check), "binark-theme-toggle", toggle,
                               g_free);
        g_signal_connect(check, "toggled", G_CALLBACK(on_theme_toggle),
                         toggle);
        gtk_box_append(GTK_BOX(box), GTK_WIDGET(check));
        self->theme_checks[i] = check;
    }

    self->theme_icon = GTK_IMAGE(gtk_image_new_from_icon_name(
        "display-brightness-symbolic"));
    gtk_menu_button_set_child(GTK_MENU_BUTTON(button),
                              GTK_WIDGET(self->theme_icon));
    gtk_widget_set_tooltip_text(button, "Тема оформления");
    gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Тема оформления", -1);
    gtk_popover_set_child(GTK_POPOVER(popover), box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);
    self->theme_button = GTK_MENU_BUTTON(button);
    return button;
}

static GtkWidget *
build_app_menu_button(BinarkWindow *self)
{
    GtkWidget *button = gtk_menu_button_new();
    GMenu *menu = g_menu_new();
    GMenu *section = g_menu_new();

    (void)self;
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button),
                                  "open-menu-symbolic");
    gtk_widget_set_tooltip_text(button, "Главное меню");
    gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Главное меню", -1);

    g_menu_append(section, "История", "win.history");
    g_menu_append(section, "Настройки", "win.preferences");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    section = g_menu_new();
    g_menu_append(section, "О программе", "win.about");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    section = g_menu_new();
    g_menu_append(section, "Выход", "win.quit");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), G_MENU_MODEL(menu));
    g_object_unref(menu);
    return button;
}

static void
on_settings_changed(GSettings *settings,
                    const gchar *key,
                    gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);

    if (g_strcmp0(key, "theme") == 0) {
        binark_settings_apply_theme(settings);
        update_theme_checks(self);
    } else if (g_strcmp0(key, "hex-uppercase") == 0) {
        self->syncing_number_upper = TRUE;
        gtk_check_button_set_active(
            self->number_uppercase,
            g_settings_get_boolean(settings, "hex-uppercase"));
        self->syncing_number_upper = FALSE;
    } else if (g_strcmp0(key, "bytes-per-line") == 0 ||
               g_strcmp0(key, "separated-bits") == 0 ||
               g_strcmp0(key, "compact-file-format") == 0) {
        update_file_warning(self);
    }
}

static gboolean
on_close_request(GtkWindow *window, gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);

    (void)window;
    if (self->file_job != NULL) {
        g_atomic_int_set(&self->file_job->cancelled, 1);
        self->close_after_job = TRUE;
        gtk_label_set_text(self->file_status, "Отмена и закрытие…");
        show_toast(self, "Операция будет отменена перед закрытием.");
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static void
action_convert(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);
    const gchar *visible = gtk_stack_get_visible_child_name(self->stack);

    (void)action;
    (void)parameter;
    if (g_strcmp0(visible, "numbers") == 0)
        number_convert(self);
    else if (g_strcmp0(visible, "files") == 0)
        file_convert(self);
    else
        text_convert(self);
}

static void
action_clear(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);
    const gchar *visible = gtk_stack_get_visible_child_name(self->stack);
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(self));

    (void)action;
    (void)parameter;
    if (g_strcmp0(visible, "files") == 0) {
        file_reset(self);
    } else if (g_strcmp0(visible, "numbers") == 0) {
        if (focus == GTK_WIDGET(self->number_result))
            gtk_editable_set_text(GTK_EDITABLE(self->number_result), "");
        else
            gtk_editable_set_text(GTK_EDITABLE(self->number_source), "");
        hide_number_error(self);
    } else {
        if (focus == GTK_WIDGET(self->text_result))
            set_text_view_text(self->text_result, "");
        else
            set_text_view_text(self->text_source, "");
        hide_text_error(self);
        update_text_stats(self);
    }
}

static void
action_copy(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);
    const gchar *visible = gtk_stack_get_visible_child_name(self->stack);

    (void)action;
    (void)parameter;
    if (g_strcmp0(visible, "numbers") == 0)
        number_copy_result(self);
    else if (g_strcmp0(visible, "files") == 0)
        show_toast(self, "Используйте сохранённый файл результата.");
    else
        text_copy_result(self);
}

static void
action_swap(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);
    const gchar *visible = gtk_stack_get_visible_child_name(self->stack);

    (void)action;
    (void)parameter;
    if (g_strcmp0(visible, "numbers") == 0)
        number_swap_direction(self);
    else if (g_strcmp0(visible, "files") == 0)
        file_swap_direction(self);
    else
        text_swap_direction(self);
}

static void
action_save(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);
    const gchar *visible = gtk_stack_get_visible_child_name(self->stack);

    (void)action;
    (void)parameter;
    if (g_strcmp0(visible, "numbers") == 0)
        number_save_result(self);
    else if (g_strcmp0(visible, "files") == 0)
        file_choose_output(self);
    else
        text_save_result(self);
}

static void
action_open(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);

    (void)action;
    (void)parameter;
    gtk_stack_set_visible_child_name(self->stack, "files");
    file_choose_input(self);
}

static void
action_preferences(GSimpleAction *action,
                    GVariant *parameter,
                    gpointer user_data)
{
    (void)action;
    (void)parameter;
    show_preferences(BINARK_WINDOW(user_data), NULL);
}

static void
action_history(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    show_preferences(BINARK_WINDOW(user_data), "history");
}

static void
action_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    show_about(BINARK_WINDOW(user_data));
}

static void
action_quit(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    gtk_window_close(GTK_WINDOW(user_data));
}

static void
action_search(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);

    (void)action;
    (void)parameter;
    gtk_popover_popup(self->search_popover);
    gtk_widget_grab_focus(GTK_WIDGET(self->search_entry));
}

static void
action_theme(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    BinarkWindow *self = BINARK_WINDOW(user_data);
    const gchar *theme = g_variant_get_string(parameter, NULL);

    (void)action;
    if (g_strcmp0(theme, "light") != 0 && g_strcmp0(theme, "dark") != 0)
        theme = "system";
    g_settings_set_string(self->settings, "theme", theme);
}

static const GActionEntry window_actions[] = {
    {"convert", action_convert, NULL, NULL, NULL, {0, 0, 0}},
    {"clear", action_clear, NULL, NULL, NULL, {0, 0, 0}},
    {"copy", action_copy, NULL, NULL, NULL, {0, 0, 0}},
    {"swap", action_swap, NULL, NULL, NULL, {0, 0, 0}},
    {"save", action_save, NULL, NULL, NULL, {0, 0, 0}},
    {"open", action_open, NULL, NULL, NULL, {0, 0, 0}},
    {"preferences", action_preferences, NULL, NULL, NULL, {0, 0, 0}},
    {"history", action_history, NULL, NULL, NULL, {0, 0, 0}},
    {"about", action_about, NULL, NULL, NULL, {0, 0, 0}},
    {"quit", action_quit, NULL, NULL, NULL, {0, 0, 0}},
    {"search", action_search, NULL, NULL, NULL, {0, 0, 0}},
    {"theme", action_theme, "s", NULL, NULL, {0, 0, 0}}
};

static void
binark_window_setup(BinarkWindow *self)
{
    GtkApplication *application;
    AdwToolbarView *toolbar;
    AdwHeaderBar *header;
    GtkWidget *title_box;
    GtkWidget *title_icon;
    GtkWidget *title_label;
    GtkWidget *search_button;
    GtkWidget *theme_button;
    GtkWidget *menu_button;
    GtkWidget *scrolled;
    GtkWidget *clamp;
    GtkWidget *main_box;
    GtkWidget *heading;
    GtkWidget *description;
    GtkWidget *text_page;
    GtkWidget *number_page;
    GtkWidget *file_page;
    GError *error = NULL;

    self->settings = binark_settings_new();
    binark_settings_apply_theme(self->settings);
    self->history = binark_history_new();
    if (!binark_history_load(self->history, &error)) {
        g_warning("Failed to load history: %s", error->message);
        g_clear_error(&error);
    }

    self->text_encode = TRUE;
    self->file_encode = TRUE;

    gtk_window_set_title(GTK_WINDOW(self), "Binark");
    gtk_window_set_default_size(GTK_WINDOW(self), 980, 800);
    gtk_window_set_icon_name(GTK_WINDOW(self), "com.binark.Binark");
    gtk_widget_set_size_request(GTK_WIDGET(self), 430, 560);

    self->toasts = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    adw_toast_overlay_set_child(self->toasts, GTK_WIDGET(toolbar));

    header = ADW_HEADER_BAR(adw_header_bar_new());
    title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    title_icon = gtk_image_new_from_icon_name("com.binark.Binark");
    gtk_image_set_pixel_size(GTK_IMAGE(title_icon), 24);
    title_label = gtk_label_new("Binark");
    gtk_widget_add_css_class(title_label, "title-4");
    gtk_box_append(GTK_BOX(title_box), title_icon);
    gtk_box_append(GTK_BOX(title_box), title_label);
    adw_header_bar_set_title_widget(header, title_box);

    menu_button = build_app_menu_button(self);
    theme_button = build_theme_button(self);
    search_button = build_search_button(self);
    adw_header_bar_pack_end(header, menu_button);
    adw_header_bar_pack_end(header, theme_button);
    adw_header_bar_pack_end(header, search_button);

    scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled, TRUE);

    clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 1040);
    adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 760);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), clamp);

    main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top(main_box, 24);
    gtk_widget_set_margin_bottom(main_box, 32);
    gtk_widget_set_margin_start(main_box, 24);
    gtk_widget_set_margin_end(main_box, 24);
    adw_clamp_set_child(ADW_CLAMP(clamp), main_box);

    heading = new_label("Конвертер", "binark-heading");
    gtk_box_append(GTK_BOX(main_box), heading);
    description = new_label("Преобразуйте текст, целые числа и файлы в "
                            "двоичное представление и обратно. Всё "
                            "выполняется локально.",
                            "binark-subtitle");
    gtk_box_append(GTK_BOX(main_box), description);

    self->stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(self->stack,
                                  GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(self->stack, 150);
    gtk_widget_set_vexpand(GTK_WIDGET(self->stack), TRUE);

    self->stack_switcher = GTK_STACK_SWITCHER(gtk_stack_switcher_new());
    gtk_stack_switcher_set_stack(self->stack_switcher, self->stack);
    gtk_widget_set_halign(GTK_WIDGET(self->stack_switcher), GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(main_box), GTK_WIDGET(self->stack_switcher));

    text_page = build_text_page(self);
    number_page = build_number_page(self);
    file_page = build_file_page(self);
    gtk_stack_add_titled(self->stack, text_page, "text", "Текст");
    gtk_stack_add_titled(self->stack, number_page, "numbers", "Числа");
    gtk_stack_add_titled(self->stack, file_page, "files", "Файлы");
    gtk_box_append(GTK_BOX(main_box), GTK_WIDGET(self->stack));

    adw_toolbar_view_add_top_bar(toolbar, GTK_WIDGET(header));
    adw_toolbar_view_set_content(toolbar, scrolled);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(self),
                                       GTK_WIDGET(self->toasts));

    g_settings_bind(self->settings, "compact-file-format", self->file_compact,
                    "active", G_SETTINGS_BIND_DEFAULT);
    g_signal_connect(self->settings, "changed",
                     G_CALLBACK(on_settings_changed), self);
    g_signal_connect(self, "close-request",
                     G_CALLBACK(on_close_request), self);

    g_action_map_add_action_entries(G_ACTION_MAP(self), window_actions,
                                    G_N_ELEMENTS(window_actions), self);

    application = gtk_window_get_application(GTK_WINDOW(self));
    if (application != NULL) {
        static const char *convert_accels[] = {
            "<Control>Return", "<Control>KP_Enter", NULL
        };
        static const char *clear_accels[] = {"<Control>l", NULL};
        static const char *save_accels[] = {"<Control><Shift>s", NULL};
        static const char *open_accels[] = {"<Control>o", NULL};
        static const char *preferences_accels[] = {"<Control>comma", NULL};
        static const char *quit_accels[] = {"<Control>q", NULL};
        static const char *search_accels[] = {"<Control>f", NULL};

        gtk_application_set_accels_for_action(application, "win.convert",
                                              convert_accels);
        gtk_application_set_accels_for_action(application, "win.clear",
                                              clear_accels);
        gtk_application_set_accels_for_action(application, "win.save",
                                              save_accels);
        gtk_application_set_accels_for_action(application, "win.open",
                                              open_accels);
        gtk_application_set_accels_for_action(application, "win.preferences",
                                              preferences_accels);
        gtk_application_set_accels_for_action(application, "win.quit",
                                              quit_accels);
        gtk_application_set_accels_for_action(application, "win.search",
                                              search_accels);
    }

    update_theme_checks(self);
    update_text_direction(self);
    update_file_direction(self);
    update_number_flow(self);
}

GtkWidget *
binark_window_new(GtkApplication *application)
{
    BinarkWindow *self = g_object_new(BINARK_TYPE_WINDOW,
                                      "application", application,
                                      NULL);

    binark_window_setup(self);
    return GTK_WIDGET(self);
}

static void
binark_window_dispose(GObject *object)
{
    BinarkWindow *self = BINARK_WINDOW(object);

    if (self->file_job != NULL) {
        g_atomic_int_set(&self->file_job->cancelled, 1);
        self->file_job = NULL;
    }

    g_clear_object(&self->settings);
    g_clear_pointer(&self->history, binark_history_free);
    g_clear_pointer(&self->file_input_path, g_free);
    g_clear_pointer(&self->file_output_path, g_free);

    G_OBJECT_CLASS(binark_window_parent_class)->dispose(object);
}

static void
binark_window_class_init(BinarkWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = binark_window_dispose;
}

static void
binark_window_init(BinarkWindow *self)
{
    (void)self;
}
