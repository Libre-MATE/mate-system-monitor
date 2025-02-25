#ifndef _PROCMAN_DEFAULTTABLE_H_
#define _PROCMAN_DEFAULTTABLE_H_

#include <glibmm/refptr.h>
#include <glibmm/regex.h>

#include <string>

/* This file contains prettynames and icons for well-known applications, that by
 * default has no .desktop entry */

struct PrettyTableItem {
  Glib::RefPtr<Glib::Regex> command;
  std::string icon;

  PrettyTableItem(const std::string& a_command, const std::string& a_icon)
      : command(Glib::Regex::create("^(" + a_command + ")$")), icon(a_icon) {}
};

#define ITEM PrettyTableItem

static const PrettyTableItem default_table[] = {
    ITEM("(ba|z|tc|c|k)?sh", "utilities-terminal"),
    ITEM("(k|sys|u)logd|logger", "internet-news-reader"),
    ITEM("X(org)?", "display"),
    ITEM("apache2?|httpd|lighttpd", "internet-web-browser"),
    ITEM("atd|cron|CRON|ntpd", "date"),
    ITEM("cupsd|lpd?", "printer"),
    ITEM("cvsd|mtn|git|svn", "file-manager"),
    ITEM("evolution.*", "internet-mail"),
    ITEM("famd|gam_server", "file-manager"),
    ITEM("getty", "input-keyboard"),
    ITEM("gdb|((gcc|g\\+\\+)(-.*)?)|ar|ld|make", "applications-development"),
    ITEM("marco|metacity", "gnome-window-manager"),
    ITEM("sendmail|exim\\d?", "internet-mail"),
    ITEM("squid", "proxy"),
    ITEM("ssh(d|-agent)", "ssh-askpass-gnome"),
    ITEM("top|vmstat", "system-monitor"),
    ITEM("vim?", "vim"),
    ITEM("x?inetd", "internet-web-browser"),
    ITEM("vino.*", "gnome-remote-desktop")};

#undef ITEM

#endif /* _PROCMAN_DEFAULTTABLE_H_ */
