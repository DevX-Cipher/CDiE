#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "global.h"
#include "theme/theme.h"
#include "controls/sidebar.h"
#include "panels/header.h"
#include "panels/infobar.h"
#include "controls/tabbar.h"
#include "controls/bottombar.h"

static GtkWidget* g_win;
static GtkWidget* g_sw;
static GtkWidget* g_fixed;
static Sidebar* g_sidebar;
static Header* g_header;
static Infobar* g_infobar;
static Tabbar* g_tabbar;
static Bottombar* g_bottombar;

static void on_browse(void* user)
{
  GtkWidget* dlg;
  (void)user;

  dlg = gtk_file_chooser_dialog_new("Select a file to scan",
    GTK_WINDOW(g_win), GTK_FILE_CHOOSER_ACTION_OPEN,
    "_Cancel", GTK_RESPONSE_CANCEL,
    "_Open", GTK_RESPONSE_ACCEPT, NULL);

  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    if (path) {
      char* dir = g_path_get_dirname(path);
      char* base = g_path_get_basename(path);
      char* dir_u = g_filename_display_name(dir);
      char* base_u = g_filename_display_name(base);
      char* crumb = g_str_has_suffix(dir_u, "/")
        ? g_strdup(dir_u) : g_strconcat(dir_u, "/", NULL);

      if (g_header) header_set_path(g_header, crumb, base_u);

      if (g_infobar) {
        GStatBuf st;
        char* size = NULL, * size_sub = NULL;
        InfoCard cards[4];

        if (g_stat(path, &st) == 0) {
          size = g_format_size_full((guint64)st.st_size,
            G_FORMAT_SIZE_IEC_UNITS);
          size_sub = g_strdup_printf("%" G_GUINT64_FORMAT " bytes",
            (guint64)st.st_size);
        }
        cards[0].label = "FILE TYPE";    cards[0].value = "Unknown";
        cards[0].subvalue = "";
        cards[1].label = "FILE SIZE";    cards[1].value = size ? size : "N/A";
        cards[1].subvalue = size_sub ? size_sub : "";
        cards[2].label = "ENTRY POINT";  cards[2].value = "N/A";
        cards[2].subvalue = "";
        cards[3].label = "BASE ADDRESS"; cards[3].value = "N/A";
        cards[3].subvalue = "";
        infobar_set_cards(g_infobar, cards, 4);

        g_free(size);
        g_free(size_sub);
      }

      g_free(crumb); g_free(base_u); g_free(dir_u);
      g_free(base);  g_free(dir);    g_free(path);
    }
  }
  gtk_widget_destroy(dlg);
}

static void on_nav(void* user, int nav_id)
{
  (void)user;
  g_print("nav: %d\n", nav_id);
}

static void on_tab(void* user, int tab_id)
{
  (void)user;
  if (g_tabbar) tabbar_set_active(g_tabbar, tab_id);
}

static void on_bottom(void* user, int button_id)
{
  (void)user;
  g_print("bottom button: %d\n", button_id);
}

static void layout(void)
{
  int W, H, cx, cy;
  if (!g_sw || !g_fixed || !g_sidebar || !g_header || !g_infobar ||
    !g_tabbar || !g_bottombar) return;
  W = gtk_widget_get_allocated_width(g_sw);
  H = gtk_widget_get_allocated_height(g_sw);
  if (W <= SIDEBAR_W || H <= 0) return;
  cx = SIDEBAR_W;
  cy = 0;

  pl_widget_set_rect(sidebar_widget(g_sidebar), 0, 0, SIDEBAR_W, H);
  pl_widget_set_rect(header_widget(g_header), cx, cy, W - cx, HEADER_H);
  cy += HEADER_H;
  pl_widget_set_rect(infobar_widget(g_infobar), cx, cy, W - cx, INFOBAR_H);
  cy += INFOBAR_H;
  pl_widget_set_rect(tabbar_widget(g_tabbar), cx, cy, W - cx, TABBAR_H);
  pl_widget_set_rect(bottombar_widget(g_bottombar),
    cx, H - BOTTOMBAR_H, W - cx, BOTTOMBAR_H);
}

static void on_size_allocate(GtkWidget* w, GdkRectangle* a, gpointer user)
{
  (void)w; (void)a; (void)user;
  layout();
}

int gui_run(void)
{
  GtkWidget* win;

  gtk_init(NULL, NULL);

  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_win = win;
  gtk_window_set_title(GTK_WINDOW(win), "Detect It Easy v" X_APPLICATIONVERSION);
  gtk_window_set_default_size(GTK_WINDOW(win), 1150, 700);
  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

  {
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
      "window, scrolledwindow, viewport { background-color: #1F212E; }",
      -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
      GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    g_object_set(gtk_settings_get_default(),
      "gtk-application-prefer-dark-theme", TRUE, NULL);
  }

  {
    GtkWidget* sw = gtk_scrolled_window_new(NULL, NULL);
    g_sw = sw;
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
      GTK_POLICY_EXTERNAL, GTK_POLICY_EXTERNAL);
    g_fixed = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(sw), g_fixed);
    gtk_container_add(GTK_CONTAINER(win), sw);
  }

  g_sidebar = sidebar_create(g_fixed, 0, 0, SIDEBAR_W, 700, on_nav, NULL);
  g_header = header_create(g_fixed, SIDEBAR_W, 0, 800, HEADER_H,
    on_browse, NULL);
  g_infobar = infobar_create(g_fixed, SIDEBAR_W, HEADER_H, 800, INFOBAR_H);
  g_tabbar = tabbar_create(g_fixed, SIDEBAR_W, HEADER_H + INFOBAR_H,
    800, TABBAR_H, on_tab, NULL);
  g_bottombar = bottombar_create(g_fixed, SIDEBAR_W, 644,
    800, BOTTOMBAR_H, on_bottom, NULL);
  if (!g_sidebar || !g_header || !g_infobar || !g_tabbar || !g_bottombar) {
    g_printerr("cdie: failed to create the chrome controls\n");
    return 1;
  }

  g_signal_connect(g_sw, "size-allocate", G_CALLBACK(on_size_allocate), NULL);
  gtk_widget_show_all(win);
  gtk_main();
  return 0;
}