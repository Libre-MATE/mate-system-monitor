#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <glibtop/mountlist.h>
#include <glibtop/procmap.h>
#include <sys/stat.h>

#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

using std::string;

#include "memmaps.h"
#include "procman.h"
#include "proctable.h"
#include "util.h"

/* be careful with this enum, you could break the column names */
enum {
  MMAP_COL_FILENAME,
  MMAP_COL_VMSTART,
  MMAP_COL_VMEND,
  MMAP_COL_VMSZ,
  MMAP_COL_FLAGS,
  MMAP_COL_VMOFFSET,
  MMAP_COL_PRIVATE_CLEAN,
  MMAP_COL_PRIVATE_DIRTY,
  MMAP_COL_SHARED_CLEAN,
  MMAP_COL_SHARED_DIRTY,
  MMAP_COL_DEVICE,
  MMAP_COL_INODE,
  MMAP_COL_MAX
};

namespace {
class OffsetFormater {
  string format;

 public:
  void set(const glibtop_map_entry &last_map) {
    this->format = (last_map.end <= G_MAXUINT32) ? "%08" G_GINT64_MODIFIER "x"
                                                 : "%016" G_GINT64_MODIFIER "x";
  }

  string operator()(guint64 v) const {
    char buffer[17];
    g_snprintf(buffer, sizeof buffer, this->format.c_str(), v);
    return buffer;
  }
};

class InodeDevices {
  typedef std::map<guint16, string> Map;
  Map devices;

 public:
  void update() {
    this->devices.clear();

    glibtop_mountlist list;
    glibtop_mountentry *entries = glibtop_get_mountlist(&list, 1);

    for (unsigned i = 0; i != list.number; ++i) {
      struct stat buf;

      if (stat(entries[i].devname, &buf) != -1)
        this->devices[buf.st_rdev] = entries[i].devname;
    }

    g_free(entries);
  }

  string get(guint64 dev64) {
    if (dev64 == 0) return "";

    guint16 dev = dev64 & 0xffff;

    if (dev != dev64) g_warning("weird device %" G_GINT64_MODIFIER "x", dev64);

    Map::iterator it(this->devices.find(dev));

    if (it != this->devices.end()) return it->second;

    guint8 major, minor;
    major = dev >> 8;
    minor = dev;

    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(2) << unsigned(major)
        << ':' << std::setw(2) << unsigned(minor);

    this->devices[dev] = out.str();
    return out.str();
  }
};

class MemMapsData {
 public:
  guint timer;
  GtkWidget *tree;
  GSettings *settings;
  ProcInfo *info;
  OffsetFormater format;
  mutable InodeDevices devices;
  const char *const schema;

  MemMapsData(GtkWidget *a_tree, GSettings *a_settings)
      : tree(a_tree), settings(a_settings), schema("memmapstree") {
    procman_get_tree_state(this->settings, this->tree, this->schema);
  }

  ~MemMapsData() {
    procman_save_tree_state(this->settings, this->tree, this->schema);
  }
};
}  // namespace

struct glibtop_map_entry_cmp {
  bool operator()(const glibtop_map_entry &a, const guint64 start) const {
    return a.start < start;
  }

  bool operator()(const guint64 &start, const glibtop_map_entry &a) const {
    return start < a.start;
  }
};

static void update_row(GtkTreeModel *model, GtkTreeIter &row,
                       const MemMapsData &mm,
                       const glibtop_map_entry *memmaps) {
  guint64 size;
  string filename, device;
  string vmstart, vmend, vmoffset;
  char flags[5] = "----";

  size = memmaps->end - memmaps->start;

  if (memmaps->perm & GLIBTOP_MAP_PERM_READ) flags[0] = 'r';
  if (memmaps->perm & GLIBTOP_MAP_PERM_WRITE) flags[1] = 'w';
  if (memmaps->perm & GLIBTOP_MAP_PERM_EXECUTE) flags[2] = 'x';
  if (memmaps->perm & GLIBTOP_MAP_PERM_SHARED) flags[3] = 's';
  if (memmaps->perm & GLIBTOP_MAP_PERM_PRIVATE) flags[3] = 'p';

  if (memmaps->flags & (1 << GLIBTOP_MAP_ENTRY_FILENAME))
    filename = memmaps->filename;

  vmstart = mm.format(memmaps->start);
  vmend = mm.format(memmaps->end);
  vmoffset = mm.format(memmaps->offset);
  device = mm.devices.get(memmaps->device);

  gtk_list_store_set(
      GTK_LIST_STORE(model), &row, MMAP_COL_FILENAME, filename.c_str(),
      MMAP_COL_VMSTART, vmstart.c_str(), MMAP_COL_VMEND, vmend.c_str(),
      MMAP_COL_VMSZ, size, MMAP_COL_FLAGS, flags, MMAP_COL_VMOFFSET,
      vmoffset.c_str(), MMAP_COL_PRIVATE_CLEAN, memmaps->private_clean,
      MMAP_COL_PRIVATE_DIRTY, memmaps->private_dirty, MMAP_COL_SHARED_CLEAN,
      memmaps->shared_clean, MMAP_COL_SHARED_DIRTY, memmaps->shared_dirty,
      MMAP_COL_DEVICE, device.c_str(), MMAP_COL_INODE, memmaps->inode, -1);
}

static void update_memmaps_dialog(MemMapsData *mmdata) {
  GtkTreeModel *model;
  glibtop_map_entry *memmaps;
  glibtop_proc_map procmap;

  memmaps = glibtop_get_proc_map(&procmap, mmdata->info->pid);
  /* process has disappeared */
  if (!memmaps or procmap.number == 0) return;

  mmdata->format.set(memmaps[procmap.number - 1]);

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(mmdata->tree));

  GtkTreeIter iter;

  typedef std::map<guint64, GtkTreeIter> IterCache;
  IterCache iter_cache;

  /*
    removes the old maps and
    also fills a cache of start -> iter in order to speed
    up add
  */

  if (gtk_tree_model_get_iter_first(model, &iter)) {
    while (true) {
      char *vmstart = 0;
      guint64 start;
      gtk_tree_model_get(model, &iter, MMAP_COL_VMSTART, &vmstart, -1);

      try {
        std::istringstream(vmstart) >> std::hex >> start;
      } catch (std::logic_error &e) {
        g_warning("Could not parse %s", vmstart);
        start = 0;
      }

      g_free(vmstart);

      bool found = std::binary_search(memmaps, memmaps + procmap.number, start,
                                      glibtop_map_entry_cmp());

      if (found) {
        iter_cache[start] = iter;
        if (!gtk_tree_model_iter_next(model, &iter)) break;
      } else {
        if (!gtk_list_store_remove(GTK_LIST_STORE(model), &iter)) break;
      }
    }
  }

  mmdata->devices.update();

  /*
    add the new maps
  */

  for (guint i = 0; i != procmap.number; i++) {
    GtkTreeIter iter;
    IterCache::iterator it(iter_cache.find(memmaps[i].start));

    if (it != iter_cache.end())
      iter = it->second;
    else
      gtk_list_store_prepend(GTK_LIST_STORE(model), &iter);

    update_row(model, iter, *mmdata, &memmaps[i]);
  }

  g_free(memmaps);
}

static void dialog_response(GtkDialog *dialog, gint response_id,
                            gpointer data) {
  MemMapsData *const mmdata = static_cast<MemMapsData *>(data);

  g_source_remove(mmdata->timer);

  delete mmdata;
  gtk_widget_destroy(GTK_WIDGET(dialog));
}

static MemMapsData *create_memmapsdata(ProcData *procdata) {
  GtkWidget *tree;
  GtkListStore *model;
  guint i;

  const gchar *const titles[] = {N_("Filename"),
                                 // xgettext: virtual memory start
                                 N_("VM Start"),
                                 // xgettext: virtual memory end
                                 N_("VM End"),
                                 // xgettext: virtual memory syze
                                 N_("VM Size"), N_("Flags"),
                                 // xgettext: virtual memory offset
                                 N_("VM Offset"),
                                 // xgettext: memory that has not been modified
                                 // since it has been allocated
                                 N_("Private clean"),
                                 // xgettext: memory that has been modified
                                 // since it has been allocated
                                 N_("Private dirty"),
                                 // xgettext: shared memory that has not been
                                 // modified since it has been allocated
                                 N_("Shared clean"),
                                 // xgettext: shared memory that has been
                                 // modified since it has been allocated
                                 N_("Shared dirty"), N_("Device"), N_("Inode")};

  model =
      gtk_list_store_new(MMAP_COL_MAX, G_TYPE_STRING, /* MMAP_COL_FILENAME  */
                         G_TYPE_STRING,               /* MMAP_COL_VMSTART     */
                         G_TYPE_STRING,               /* MMAP_COL_VMEND     */
                         G_TYPE_UINT64,               /* MMAP_COL_VMSZ     */
                         G_TYPE_STRING,               /* MMAP_COL_FLAGS     */
                         G_TYPE_STRING,               /* MMAP_COL_VMOFFSET  */
                         G_TYPE_UINT64, /* MMAP_COL_PRIVATE_CLEAN */
                         G_TYPE_UINT64, /* MMAP_COL_PRIVATE_DIRTY */
                         G_TYPE_UINT64, /* MMAP_COL_SHARED_CLEAN */
                         G_TYPE_UINT64, /* MMAP_COL_SHARED_DIRTY */
                         G_TYPE_STRING, /* MMAP_COL_DEVICE     */
                         G_TYPE_UINT64  /* MMAP_COL_INODE     */
      );

  tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
  g_object_unref(G_OBJECT(model));

  for (i = 0; i < MMAP_COL_MAX; i++) {
    GtkCellRenderer *cell;
    GtkTreeViewColumn *col;

    cell = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new();
    gtk_tree_view_column_pack_start(col, cell, TRUE);
    gtk_tree_view_column_set_title(col, _(titles[i]));
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_column_set_sort_column_id(col, i);
    gtk_tree_view_column_set_reorderable(col, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);

    switch (i) {
      case MMAP_COL_PRIVATE_CLEAN:
      case MMAP_COL_PRIVATE_DIRTY:
      case MMAP_COL_SHARED_CLEAN:
      case MMAP_COL_SHARED_DIRTY:
      case MMAP_COL_VMSZ:
        gtk_tree_view_column_set_cell_data_func(
            col, cell, &procman::memory_size_cell_data_func,
            GUINT_TO_POINTER(i), NULL);

        g_object_set(cell, "xalign", 1.0f, NULL);
        break;

      default:
        gtk_tree_view_column_set_attributes(col, cell, "text", i, NULL);
        break;
    }

    switch (i) {
      case MMAP_COL_VMSTART:
      case MMAP_COL_VMEND:
      case MMAP_COL_FLAGS:
      case MMAP_COL_VMOFFSET:
      case MMAP_COL_DEVICE:
        g_object_set(cell, "family", "monospace", NULL);
        break;
    }
  }

  return new MemMapsData(tree, procdata->settings);
}

static gboolean memmaps_timer(gpointer data) {
  MemMapsData *const mmdata = static_cast<MemMapsData *>(data);
  GtkTreeModel *model;

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(mmdata->tree));
  g_assert(model);

  update_memmaps_dialog(mmdata);

  return TRUE;
}

static void create_single_memmaps_dialog(GtkTreeModel *model, GtkTreePath *path,
                                         GtkTreeIter *iter, gpointer data) {
  ProcData *const procdata = static_cast<ProcData *>(data);
  MemMapsData *mmdata;
  GtkWidget *memmapsdialog;
  GtkWidget *dialog_vbox;
  GtkWidget *label;
  GtkWidget *scrolled;
  ProcInfo *info;

  gtk_tree_model_get(model, iter, COL_POINTER, &info, -1);

  if (!info) return;

  mmdata = create_memmapsdata(procdata);
  mmdata->info = info;

  memmapsdialog = gtk_dialog_new_with_buttons(
      _("Memory Maps"), GTK_WINDOW(procdata->app),
      GTK_DIALOG_DESTROY_WITH_PARENT, "gtk-close", GTK_RESPONSE_CLOSE, NULL);
  gtk_window_set_resizable(GTK_WINDOW(memmapsdialog), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(memmapsdialog), 620, 400);
  gtk_container_set_border_width(GTK_CONTAINER(memmapsdialog), 5);

  dialog_vbox = gtk_dialog_get_content_area(GTK_DIALOG(memmapsdialog));
  gtk_container_set_border_width(GTK_CONTAINER(dialog_vbox), 5);

  label = procman_make_label_for_mmaps_or_ofiles(
      _("_Memory maps for process \"%s\" (PID %u):"), info->name, info->pid);

  gtk_box_pack_start(GTK_BOX(dialog_vbox), label, FALSE, TRUE, 0);

  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled),
                                      GTK_SHADOW_IN);

  gtk_container_add(GTK_CONTAINER(scrolled), mmdata->tree);
  gtk_label_set_mnemonic_widget(GTK_LABEL(label), mmdata->tree);

  gtk_box_pack_start(GTK_BOX(dialog_vbox), scrolled, TRUE, TRUE, 0);

  g_signal_connect(memmapsdialog, "response",
                   G_CALLBACK(dialog_response), mmdata);

  gtk_widget_show_all(memmapsdialog);

  mmdata->timer = g_timeout_add_seconds(5, memmaps_timer, mmdata);

  update_memmaps_dialog(mmdata);
}

void create_memmaps_dialog(ProcData *procdata) {
  /* TODO: do we really want to open multiple dialogs ? */
  gtk_tree_selection_selected_foreach(procdata->selection,
                                      create_single_memmaps_dialog, procdata);
}
