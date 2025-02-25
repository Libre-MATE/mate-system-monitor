#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lsof.h"

#include <glib/gi18n.h>
#include <glibmm/regex.h>
#include <glibtop/procopenfiles.h>
#include <gtkmm/messagedialog.h>
#include <sys/wait.h>

#include <iterator>
#include <set>
#include <sstream>
#include <string>

#include "procman.h"
#include "util.h"

using std::string;

namespace {

class Lsof {
  Glib::RefPtr<Glib::Regex> re;

  bool matches(const string &filename) const {
    return this->re->match(filename);
  }

 public:
  Lsof(const string &pattern, bool caseless) {
    Glib::RegexCompileFlags flags = static_cast<Glib::RegexCompileFlags>(0);

    if (caseless) flags |= Glib::REGEX_CASELESS;

    this->re = Glib::Regex::create(pattern, flags);
  }

  template <typename OutputIterator>
  void search(const ProcInfo &info, OutputIterator out) const {
    glibtop_open_files_entry *entries;
    glibtop_proc_open_files buf;

    entries = glibtop_get_proc_open_files(&buf, info.pid);

    for (unsigned i = 0; i != buf.number; ++i) {
      if (entries[i].type & GLIBTOP_FILE_TYPE_FILE) {
        const string filename(entries[i].info.file.name);
        if (this->matches(filename)) *out++ = filename;
      }
    }

    g_free(entries);
  }
};

// GUI Stuff

enum ProcmanLsof {
  PROCMAN_LSOF_COL_SURFACE,
  PROCMAN_LSOF_COL_PROCESS,
  PROCMAN_LSOF_COL_PID,
  PROCMAN_LSOF_COL_FILENAME,
  PROCMAN_LSOF_NCOLS
};

struct GUI {
  GtkListStore *model;
  GtkEntry *entry;
  GtkWindow *window;
  GtkLabel *count;
  ProcData *procdata;
  bool case_insensitive;

  GUI() { procman_debug("New Lsof GUI %p", this); }

  ~GUI() { procman_debug("Destroying Lsof GUI %p", this); }

  void clear_results() {
    gtk_list_store_clear(this->model);
    gtk_label_set_text(this->count, "");
  }

  void clear() {
    this->clear_results();
    gtk_entry_set_text(this->entry, "");
  }

  void display_regex_error(const Glib::RegexError &error) {
    char *msg = g_strdup_printf(
        "<b>%s</b>\n%s\n%s", _("Error"),
        _("'%s' is not a valid Perl regular expression."), "%s");
    std::string message = make_string(
        g_strdup_printf(msg, this->pattern().c_str(), error.what().c_str()));
    g_free(msg);

    Gtk::MessageDialog dialog(message,
                              true,  // use markup
                              Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK,
                              true);  // modal
    dialog.run();
  }

  void update_count(unsigned count) {
    std::ostringstream ss;
    ss << count;
    string s = ss.str();
    gtk_label_set_text(this->count, s.c_str());
  }

  string pattern() const { return gtk_entry_get_text(this->entry); }

  void search() {
    typedef std::set<string> MatchSet;
    typedef MatchSet::const_iterator iterator;

    this->clear_results();

    try {
      Lsof lsof(this->pattern(), this->case_insensitive);

      unsigned count = 0;

      for (ProcInfo::Iterator it(ProcInfo::begin()); it != ProcInfo::end();
           ++it) {
        const ProcInfo &info(*it->second);

        MatchSet matches;
        lsof.search(info, std::inserter(matches, matches.begin()));
        count += matches.size();

        for (iterator it(matches.begin()), end(matches.end()); it != end;
             ++it) {
          GtkTreeIter file;
          gtk_list_store_append(this->model, &file);
          gtk_list_store_set(this->model, &file, PROCMAN_LSOF_COL_SURFACE,
                             info.surface, PROCMAN_LSOF_COL_PROCESS, info.name,
                             PROCMAN_LSOF_COL_PID, info.pid,
                             PROCMAN_LSOF_COL_FILENAME, it->c_str(), -1);
        }
      }

      this->update_count(count);
    } catch (Glib::RegexError &error) {
      this->display_regex_error(error);
    }
  }

  static void search_button_clicked(GtkButton *, gpointer data) {
    static_cast<GUI *>(data)->search();
  }

  static void search_entry_activate(GtkEntry *, gpointer data) {
    static_cast<GUI *>(data)->search();
  }

  static void clear_button_clicked(GtkButton *, gpointer data) {
    static_cast<GUI *>(data)->clear();
  }

  static void close_button_clicked(GtkButton *, gpointer data) {
    GUI *gui = static_cast<GUI *>(data);
    gtk_widget_destroy(GTK_WIDGET(gui->window));
    delete gui;
  }

  static void case_button_toggled(GtkToggleButton *button, gpointer data) {
    bool state = gtk_toggle_button_get_active(button);
    static_cast<GUI *>(data)->case_insensitive = state;
  }

  static gboolean window_delete_event(GtkWidget *, GdkEvent *, gpointer data) {
    delete static_cast<GUI *>(data);
    return FALSE;
  }
};
}  // namespace

void procman_lsof(ProcData *procdata) {
  GtkListStore *model = gtk_list_store_new(
      PROCMAN_LSOF_NCOLS,
      CAIRO_GOBJECT_TYPE_SURFACE,  // PROCMAN_LSOF_COL_SURFACE
      G_TYPE_STRING,               // PROCMAN_LSOF_COL_PROCESS
      G_TYPE_UINT,                 // PROCMAN_LSOF_COL_PID
      G_TYPE_STRING                // PROCMAN_LSOF_COL_FILENAME
  );

  GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
  g_object_unref(model);

  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;

  // SURFACE / PROCESS

  column = gtk_tree_view_column_new();

  renderer = gtk_cell_renderer_pixbuf_new();
  gtk_tree_view_column_pack_start(column, renderer, FALSE);
  gtk_tree_view_column_set_attributes(column, renderer, "surface",
                                      PROCMAN_LSOF_COL_SURFACE, NULL);

  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_column_pack_start(column, renderer, FALSE);
  gtk_tree_view_column_set_attributes(column, renderer, "text",
                                      PROCMAN_LSOF_COL_PROCESS, NULL);

  gtk_tree_view_column_set_title(column, _("Process"));
  gtk_tree_view_column_set_sort_column_id(column, PROCMAN_LSOF_COL_PROCESS);
  gtk_tree_view_column_set_resizable(column, TRUE);
  gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
  gtk_tree_view_column_set_min_width(column, 10);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
  gtk_tree_sortable_set_sort_column_id(
      GTK_TREE_SORTABLE(model), PROCMAN_LSOF_COL_PROCESS, GTK_SORT_ASCENDING);

  // PID
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(_("PID"), renderer, "text",
                                                    PROCMAN_LSOF_COL_PID, NULL);
  gtk_tree_view_column_set_sort_column_id(column, PROCMAN_LSOF_COL_PID);
  gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  // FILENAME
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(
      _("Filename"), renderer, "text", PROCMAN_LSOF_COL_FILENAME, NULL);
  gtk_tree_view_column_set_sort_column_id(column, PROCMAN_LSOF_COL_FILENAME);
  gtk_tree_view_column_set_resizable(column, TRUE);
  gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  GtkWidget *dialog; /* = gtk_dialog_new_with_buttons(_("Search for Open
                        Files"), NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
                                                      GTK_STOCK_CLOSE,
                        GTK_RESPONSE_CLOSE, NULL); */
  dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(procdata->app));
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
  // gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_title(GTK_WINDOW(dialog), _("Search for Open Files"));

  // g_signal_connect(dialog, "response",
  //                           G_CALLBACK(close_dialog), NULL);
  gtk_window_set_resizable(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 575, 400);
  gtk_container_set_border_width(GTK_CONTAINER(dialog), 12);
  GtkWidget *mainbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_add(GTK_CONTAINER(dialog), mainbox);
  gtk_box_set_spacing(GTK_BOX(mainbox), 6);

  // Label, entry and search button

  GtkWidget *hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start(GTK_BOX(mainbox), hbox1, FALSE, FALSE, 0);

  GtkWidget *image =
      gtk_image_new_from_icon_name("edit-find", GTK_ICON_SIZE_DIALOG);
  gtk_box_pack_start(GTK_BOX(hbox1), image, FALSE, FALSE, 0);

  GtkWidget *vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_box_pack_start(GTK_BOX(hbox1), vbox2, TRUE, TRUE, 0);

  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start(GTK_BOX(vbox2), hbox, TRUE, TRUE, 0);
  GtkWidget *label = gtk_label_new_with_mnemonic(_("_Name contains:"));
  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
  GtkWidget *entry = gtk_entry_new();

  gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);

  GtkWidget *search_button =
      GTK_WIDGET(g_object_new(GTK_TYPE_BUTTON, "label", "gtk-find", "use-stock",
                              TRUE, "use-underline", TRUE, NULL));

  gtk_box_pack_start(GTK_BOX(hbox), search_button, FALSE, FALSE, 0);

  GtkWidget *clear_button =
      GTK_WIDGET(g_object_new(GTK_TYPE_BUTTON, "label", "gtk-clear",
                              "use-stock", TRUE, "use-underline", TRUE, NULL));

  /* The default accelerator collides with the default close accelerator. */
  gtk_button_set_label(GTK_BUTTON(clear_button), _("C_lear"));
  gtk_box_pack_start(GTK_BOX(hbox), clear_button, FALSE, FALSE, 0);

  GtkWidget *case_button =
      gtk_check_button_new_with_mnemonic(_("Case insensitive matching"));
  GtkWidget *hbox3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start(GTK_BOX(hbox3), case_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox2), hbox3, FALSE, FALSE, 0);

  GtkWidget *results_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start(GTK_BOX(mainbox), results_box, FALSE, FALSE, 0);
  GtkWidget *results_label = gtk_label_new_with_mnemonic(_("S_earch results:"));
  gtk_box_pack_start(GTK_BOX(results_box), results_label, FALSE, FALSE, 0);
  GtkWidget *count_label = gtk_label_new(NULL);
  gtk_box_pack_end(GTK_BOX(results_box), count_label, FALSE, FALSE, 0);

  // Scrolled TreeView
  GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled),
                                      GTK_SHADOW_IN);
  gtk_container_add(GTK_CONTAINER(scrolled), tree);
  gtk_box_pack_start(GTK_BOX(mainbox), scrolled, TRUE, TRUE, 0);

  GtkWidget *bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

  GtkWidget *close_button =
      GTK_WIDGET(g_object_new(GTK_TYPE_BUTTON, "label", "gtk-close",
                              "use-stock", TRUE, "use-underline", TRUE, NULL));

  gtk_box_pack_start(GTK_BOX(mainbox), bottom_box, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(bottom_box), close_button, FALSE, FALSE, 0);

  GUI *gui = new GUI;  // wil be deleted by the close button or delete-event
  gui->procdata = procdata;
  gui->model = model;
  gui->window = GTK_WINDOW(dialog);
  gui->entry = GTK_ENTRY(entry);
  gui->count = GTK_LABEL(count_label);

  g_signal_connect(entry, "activate",
                   G_CALLBACK(GUI::search_entry_activate), gui);
  g_signal_connect(clear_button, "clicked",
                   G_CALLBACK(GUI::clear_button_clicked), gui);
  g_signal_connect(search_button, "clicked",
                   G_CALLBACK(GUI::search_button_clicked), gui);
  g_signal_connect(close_button, "clicked",
                   G_CALLBACK(GUI::close_button_clicked), gui);
  g_signal_connect(case_button, "toggled",
                   G_CALLBACK(GUI::case_button_toggled), gui);
  g_signal_connect(dialog, "delete-event",
                   G_CALLBACK(GUI::window_delete_event), gui);

  gtk_widget_show_all(dialog);
}
