#ifndef H_MATE_SYSTEM_MONITOR_SELECTION_H_1183113337
#define H_MATE_SYSTEM_MONITOR_SELECTION_H_1183113337

#include <gtk/gtk.h>
#include <sys/types.h>

#include <vector>

namespace procman {
class SelectionMemento {
  std::vector<pid_t> pids;
  static void add_to_selected(GtkTreeModel* model, GtkTreePath* path,
                              GtkTreeIter* iter, gpointer data);

 public:
  void save(GtkWidget* tree);
  void restore(GtkWidget* tree);
};
}  // namespace procman

#endif /* H_MATE_SYSTEM_MONITOR_SELECTION_H_1183113337 */
