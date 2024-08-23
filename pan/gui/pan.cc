/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2006  Charles Kerr <charles@rebelbase.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */


#include <memory>
#include <fstream>
#include <config.h>
#include <signal.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include "pan/gui/load-icon.h"
#include <gmime/gmime.h>
#include <gio/gio.h>

extern "C" {
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <unistd.h>
}

#include <uulib/crc32.h>

#ifdef G_OS_WIN32
  #undef _WIN32_WINNT
  #define _WIN32_WINNT 0x0501
  #include <windows.h>
#endif

#ifdef HAVE_LIBNOTIFY
  #include <libnotify/notify.h>
#endif

#ifdef HAVE_GNUTLS
  #include <pan/tasks/socket-impl-openssl.h>
#endif

#ifdef HAVE_GKR
  #define GCR_API_SUBJECT_TO_CHANGE
  #include <libsecret/secret.h>
  #include <gcr/gcr.h>
  #undef GCR_API_SUBJECT_TO_CHANGE
#endif

#include <config.h>
#include <pan/general/debug.h>
#include <pan/general/log.h>
#include <pan/general/file-util.h>
#include <pan/general/worker-pool.h>
#include <pan/usenet-utils/gpg.h>
#include <pan/data/cert-store.h>
#include <pan/tasks/socket-impl-gio.h>
#include <pan/tasks/socket-impl-main.h>
#include <pan/tasks/task-groups.h>
#include <pan/tasks/task-xover.h>
#include <pan/tasks/nzb.h>
#include <pan/data-impl/data-impl.h>
#include <pan/data-impl/data-io.h>
#include "gui.h"
#include "group-prefs.h"
#include "prefs-file.h"
#include "task-pane.h"
#include "server-ui.h"
#include "pad.h"

//#define DEBUG_LOCALE 1
//#define DEBUG_PARALLEL 1


using namespace pan;

namespace
{
  typedef std::vector<std::string> strings_v;
}

namespace
{
  GMainLoop * nongui_gmainloop (nullptr);

  void mainloop ()
  {
#if 1
    if (nongui_gmainloop)
      g_main_loop_run (nongui_gmainloop);
    else
    {
      gtk_main ();
    }
#else
    while (gtk_events_pending ())
      gtk_main_iteration ();
#endif
  }

  void mainloop_quit ()
  {
    if (nongui_gmainloop)
      g_main_loop_quit (nongui_gmainloop);
    else
      gtk_main_quit ();
  }

  gboolean delete_event_cb (GtkWidget * w, GdkEvent *, gpointer user_data)
  {
    mainloop_quit ();
    return true; // don't invoke the default handler that destroys the widget
  }

#ifndef G_OS_WIN32
  void sighandler (int signum)
  {
    std::cerr << "Shutting down Pan." << std::endl;
    signal (signum, SIG_DFL);

    _dbg_file.close();

    mainloop_quit ();
  }
#endif // G_OS_WIN32

  void register_shutdown_signals ()
  {
#ifndef G_OS_WIN32
    signal (SIGHUP, sighandler);
    signal (SIGINT, sighandler);
    signal (SIGTERM, sighandler);
#endif // G_OS_WIN32
  }

  void destroy_cb (GtkWidget*, gpointer)
  {
    gtk_main_quit ();
  }

  struct DataAndQueue
  {
    Data * data;
    Queue * queue;
  };

  void add_grouplist_task (GtkWidget *, gpointer user_data)
  {
    DataAndQueue * foo (static_cast<DataAndQueue*>(user_data));
    const quarks_t new_servers (foo->data->get_servers());
    foreach_const (quarks_t, new_servers, it)
      if (foo->data->get_server_limits(*it))
        foo->queue->add_task (new TaskGroups (*foo->data, *it));
    g_free (foo);
  }

  gboolean queue_upkeep_timer_cb (gpointer queue_gpointer)
  {
    static_cast<Queue*>(queue_gpointer)->upkeep ();
    return true;
  }

/* ****** Status Icon and Notification *******************************************/

  enum StatusIcons
  {
    ICON_STATUS_ONLINE,
    ICON_STATUS_OFFLINE,
    ICON_STATUS_ACTIVE,
    ICON_STATUS_QUEUE_EMPTY,
    ICON_STATUS_ERROR,
    ICON_STATUS_IDLE,
    ICON_STATUS_NEW_ARTICLES,
    NUM_STATUS_ICONS
  };

  struct Icon {
      char const *pixbuf_file;
      GdkPixbuf *pixbuf;
  } status_icons[NUM_STATUS_ICONS] = {
    { "icon_status_online.png",          nullptr },
    { "icon_status_offline.png",         nullptr },
    { "icon_status_active.png",          nullptr },
    { "icon_status_queue_empty.png",     nullptr },
    { "icon_status_error.png",           nullptr },
    { "icon_status_idle.png",            nullptr },
    { "icon_status_new_articles.png",    nullptr }
  };

  struct StatusIconListener : public Prefs::Listener,
                              public Queue::Listener,
                              public Data::Listener
  {

    static gboolean status_icon_periodic_refresh (gpointer p)
    {
      return true;
    }

#ifdef HAVE_LIBNOTIFY
    static void notif_maximize_cb(NotifyNotification *notification,
                               char *action,
                               gpointer user_data)
    {
      notify_notification_close (notification, nullptr);

      StatusIconListener* s = static_cast<StatusIconListener*>(user_data);
      gtk_widget_show (GTK_WIDGET(s->root));
      gtk_window_deiconify(GTK_WINDOW(s->root));
    }

    static void notif_close_cb (NotifyNotification *notification,
                                gpointer            user_data)
    {

      StatusIconListener* s = static_cast<StatusIconListener*>(user_data);
      s->notif_shown = false;
      s->_notifs.erase(G_OBJECT(notification));
      g_object_unref (notification);
    }
#endif

    bool n() { return notif_shown; }

    StatusIconListener(GtkWidget* r, Prefs& p, Queue& q, Data& d) :
      queue(q), data(d), tasks_active(0), tasks_total(0),
      notif_shown(false), prefs(p), root(r)
    {
      prefs.add_listener(this);
      queue.add_listener(this);
      data.add_listener(this);
      is_online = q.is_online();

      status_icon_timeout_tag = g_timeout_add (500, status_icon_periodic_refresh, this);
    }

    ~StatusIconListener()
    {
      prefs.remove_listener(this);
      queue.remove_listener(this);
      data.remove_listener(this);
      g_source_remove (status_icon_timeout_tag);
#ifdef HAVE_LIBNOTIFY
      foreach(std::set<GObject*>, _notifs, it)
      {
        notify_notification_close(NOTIFY_NOTIFICATION(*it), nullptr);
        g_object_unref (*it);
      }
#endif
    }

    /* prefs::listener */
    void on_prefs_flag_changed(StringView const &key, bool value) override
    {
    }

    void on_prefs_int_changed(StringView const &key, int color) override
    {
    }

    void on_prefs_string_changed(StringView const &key,
                                 StringView const &value) override
    {
    }

    void on_prefs_color_changed(StringView const &key,
                                GdkRGBA const &color) override
    {
    }

    void notify_of(StatusIcons si, char const *body, char const *summary)
    {
#ifdef HAVE_LIBNOTIFY
      if (!body || !summary) return;
      if (!prefs.get_flag("use-notify", false)) return;

      NotifyNotification *notif(nullptr);
      GError* error(nullptr);

#ifdef NOTIFY_CHECK_VERSION
  #if NOTIFY_CHECK_VERSION (0, 7, 0)
      notif=notify_notification_new(summary, body, nullptr);
  #else
      notif=notify_notification_new(summary, body, nullptr,nullptr);
  #endif
#else
      notif=notify_notification_new(summary, body, nullptr,nullptr);
#endif
      if (!notif) return;

      _notifs.insert(G_OBJECT(notif));

      notify_notification_set_icon_from_pixbuf(notif, status_icons[si].pixbuf);
      notify_notification_set_timeout (notif,5000);
      notify_notification_add_action(notif,"close",_("Maximize"),NOTIFY_ACTION_CALLBACK(notif_maximize_cb),this,nullptr);
      g_signal_connect (G_OBJECT(notif), "closed", G_CALLBACK(notif_close_cb), this);

      notify_notification_show (notif, &error);

      if (error) {
        pan_debug ("Error showing notification: "<<error->message);
        g_error_free (error);
        _notifs.erase(G_OBJECT(notif));
        g_object_unref(G_OBJECT(notif));
      }
#endif
    }

    /* queue::listener */
    void on_queue_task_active_changed (Queue&, Task&, bool active) override {}
    void on_queue_tasks_added (Queue&, int index UNUSED, int count) override
    {
      tasks_total += count;
    }
    void on_queue_task_removed (Queue&, Task&, int pos UNUSED) override
    {
    }
    void on_queue_task_moved (Queue&, Task&, int new_pos UNUSED, int old_pos UNUSED) override {}
    void on_queue_connection_count_changed (Queue&, int count) override {}
    void on_queue_size_changed (Queue&, int active, int total) override
    {
      tasks_total = total;
      tasks_active = active;
    }

    void on_queue_online_changed (Queue&, bool online) override
    {
      is_online = online;
    }

    void on_queue_error(Queue &, StringView const &message) override
    {
      if (n()) return;
      notif_shown = true;
      notify_of(ICON_STATUS_ERROR, message.str, _("An error has occurred!"));
    }

    /* data::listener */
    void on_group_entered(Quark const &group,
                          Article_Count unread,
                          Article_Count total) override
    {

      if (static_cast<uint64_t>(unread) != 0)
      {
        if (n()) return;
        notif_shown = true;
        char const *summary = _("New Articles!");
        char const *body = _("There are new\narticles available.");
        notify_of(ICON_STATUS_NEW_ARTICLES, body, summary);
      }
    }

    private:
      Queue& queue;
      Data& data;
      int tasks_active;
      int tasks_total;
      bool is_online;
      guint status_icon_timeout_tag;
      bool notif_shown;

    public:
      Prefs& prefs;
      GtkStatusIcon *icon;
      GtkWidget* root;
      std::set<GObject*> _notifs;
  };

  static StatusIconListener* _status_icon;

  struct QueueAndGui
  {
    Queue& queue;
    GUI& gui;
    QueueAndGui (Queue& q, GUI& g) : queue(q), gui(g) {}
  };

  static void work_online_cb (GtkWidget* w, gpointer data)
  {
    QueueAndGui* d = static_cast<QueueAndGui*>(data);
    d->gui.do_work_online(!d->queue.is_online());
  }

  static QueueAndGui* queue_and_gui(nullptr);

  void run_pan_with_status_icon (GtkWindow * window, GdkPixbuf * pixbuf, Queue& queue, Prefs & prefs, Data& data, GUI* _gui)
  {

    _status_icon = new StatusIconListener(GTK_WIDGET(window), prefs, queue, data);

    // required to show Pan icon in notification
    for (guint i=0; i<NUM_STATUS_ICONS; ++i)
      status_icons[i].pixbuf = load_icon (status_icons[i].pixbuf_file);
  }


  void run_pan_in_window (GUI           * _gui,
                          Data          & data,
                          Queue         & queue,
                          Prefs         & prefs,
                          GroupPrefs    & group_prefs,
                          GtkWindow     * window)
  {

    GUI& gui (*_gui);

    const gulong delete_cb_id =  g_signal_connect (window, "delete-event", G_CALLBACK(delete_event_cb), &prefs);

    gtk_container_add (GTK_CONTAINER(window), gui.root());

    gtk_widget_set_visible (GTK_WIDGET(window), true);

    const quarks_t servers (data.get_servers ());
    if (servers.empty())
    {
      const Quark empty_server;
      GtkWidget * w = server_edit_dialog_new (data, queue, prefs, window, empty_server);
      gtk_widget_show_all (w);
      GtkWidget * msg = gtk_message_dialog_new (GTK_WINDOW(w),
                                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                                GTK_MESSAGE_INFO,
                                                GTK_BUTTONS_CLOSE,
                                              _("Thank you for trying Pan!\n \nTo start newsreading, first Add a Server."));
      g_signal_connect_swapped (msg, "response", G_CALLBACK (gtk_widget_destroy), msg);
      gtk_widget_show_all (msg);

      DataAndQueue * foo = g_new0 (DataAndQueue, 1);
      foo->data = &data;
      foo->queue = &queue;
      g_signal_connect (w, "destroy", G_CALLBACK(add_grouplist_task), foo);
    }

    register_shutdown_signals ();
    mainloop ();
    g_signal_handler_disconnect (window, delete_cb_id);

    delete _gui;

    gtk_widget_destroy (GTK_WIDGET(window));
  }

  /** Queue:::Listener that quits Pan via mainloop_exit()
      when on_queue_size_changed() say the queue is empty.
      See https://bugzilla.gnome.org/show_bug.cgi?id=424248. */
  struct PanKiller : public Queue::Listener
  {
    PanKiller(Queue & q) : q(q) { q.add_listener(this); }
    ~PanKiller() { q.remove_listener(this); }

    /** Method from Queue::Listener interface: quits program on zero sized Q*/
    void on_queue_size_changed (Queue&, int active, int total) override
      {  if (!active && !total) mainloop_quit();  }

    // all below methods from Queue::Listener interface are noops
    void on_queue_task_active_changed (Queue&, Task&, bool) override {}
    void on_queue_tasks_added (Queue&, int , int ) override {}
    void on_queue_task_removed (Queue&, Task&, int) override {}
    void on_queue_task_moved (Queue&, Task&, int, int) override {}
    void on_queue_connection_count_changed (Queue&, int) override {}
    void on_queue_online_changed (Queue&, bool) override {}
    void on_queue_error (Queue&, const StringView&) override {}

  private:
    Queue & q;
  };

#ifdef G_OS_WIN32
  void console()
  {
    using namespace std;
    static bool done = false;
    if ( done ) return;
    done = true;

    //AllocConsole();
    if ( !AttachConsole( -1 ) ) return;
    static ofstream out("CONOUT$");
    static ofstream err("CONOUT$");
    streambuf *tmp, *t2;

    tmp = cout.rdbuf();
    t2 = out.rdbuf();
    cout.ios::rdbuf( t2 );
    out.ios::rdbuf( tmp );

    tmp = cerr.rdbuf();
    t2 = err.rdbuf();
    cerr.ios::rdbuf( t2 );
    err.ios::rdbuf( tmp );

    (void)freopen( "CONOUT$", "w", stdout );
    (void)freopen( "CONOUT$", "w", stderr );
  }
#else
  void console()
  {
    return;
  }
#endif

  void usage ()
  {
    console();
    std::cerr << "Pan " << PAN_VERSION << "\n\n" <<
_("General Options\n"
"  -h, --help               Show this usage information and exit.\n"
"  -v, --version            Print release version and exit.\n"
"  --verbose                Be verbose (in non-GUI mode).\n"
"  --debug                  Run in debug mode. Use --debug twice for verbose debug.\n"
"  --debug-ssl              Run in TLS (aka SSL) debug mode.\n"
"\n"
"URL Options\n"
/** NOT IMPLEMENTED
"  news:message-id          Show the specified article.\n"
"  news:group.name          Show the specified newsgroup.\n"
*/
"  headers:group.name       Download new headers for the specified newsgroup.\n"
"  news:message-id          When specified together with --no-gui, dump\n"
"                           the message-id article to standard output.\n"
"\n"
"NZB Batch Options\n"
"  --nzb file1 file2 ...    Process NZB files in non-GUI mode.\n"
"  -o path, --output=path   Path to save attachments listed in the NZB file(s).\n"
"  --no-gui                 Only show console output, not the download queue.\n") << std::endl;
  }

#ifdef HAVE_DBUS
  /***
   ** DBUS
   ***/

  /** Struct for dbus handling */
  struct Pan
  {
    Data& data;
    Queue& queue;
    ArticleCache& cache;
    EncodeCache& encode_cache;
    Prefs& prefs;
    GroupPrefs& group_prefs;
    GDBusNodeInfo * busnodeinfo;
    int dbus_id;
    bool lost_name;
    bool name_valid;

    GDBusInterfaceVTable ifacetable;

    Pan(Data& d, Queue& q, ArticleCache& c, EncodeCache& ec, Prefs& p, GroupPrefs& gp) :
      data(d), queue(q),
      cache(c), encode_cache(ec), prefs(p), group_prefs(gp), busnodeinfo(nullptr), dbus_id(-1),
      lost_name(false), name_valid(false)
      {}
  };

  static void nzb_method_call(GDBusConnection *connection,
                              gchar const *sender,
                              gchar const *object_path,
                              gchar const *interface_name,
                              gchar const *method_name,
                              GVariant *parameters,
                              GDBusMethodInvocation *invocation,
                              gpointer user_data)
  {

    Pan* pan(static_cast<Pan*>(user_data));
    if (!pan) return;

    gboolean nzb(false);
    gchar* groups;
    gchar* nzb_output_path;
    gchar* nzbs;
    strings_v nzb_files;

    if (g_strcmp0 (method_name, "NZBEnqueue") == 0)
    {
      g_variant_get (parameters, "(sssb)", &groups, &nzb_output_path, &nzbs, &nzb);

      if (groups && strlen(groups)!=0)
      {
        StringView tok, v(groups);
        while (v.pop_token(tok,','))
          pan->queue.add_task (new TaskXOver (pan->data, tok, TaskXOver::NEW), Queue::BOTTOM);
      }

      if (nzb && nzbs)
      {
        //parse the file list
        StringView tok, nzb(nzbs);
        while (nzb.pop_token(tok))
          nzb_files.push_back(tok);

        // load the nzb files...
        std::vector<Task*> tasks;
        foreach_const (strings_v, nzb_files, it)
          NZB :: tasks_from_nzb_file (*it, nzb_output_path, pan->cache, pan->encode_cache, pan->data, pan->data, pan->data, tasks);
        pan->queue.add_tasks (tasks, Queue::BOTTOM);
      }
    }

    g_dbus_method_invocation_return_value (invocation, nullptr);
  }

  static GDBusConnection *dbus_connection(nullptr);

  static const gchar xml[] =
    "<node>"
    "  <interface name='news.pan.NZB'>"
    "    <method name='NZBEnqueue'>"
    "      <arg type='s' name='groups'    direction='in'/>"
    "      <arg type='s' name='nzb_files' direction='in'/>"
    "      <arg type='s' name='nzb_path'  direction='in'/>"
    "      <arg type='b' name='nzb'       direction='in'/>"
    "    </method>"
    "  </interface>"
    "</node>";

  static void on_bus_acquired(GDBusConnection *connection,
                              gchar const *name,
                              gpointer user_data)
  {
    Pan* pan (static_cast<Pan*>(user_data));
    g_return_if_fail (pan);

    pan->busnodeinfo = g_dbus_node_info_new_for_xml (xml, nullptr);

   /* init iface table */
    GDBusInterfaceVTable tmp =
    {
      nzb_method_call ,
      nullptr, nullptr
    };
    pan->ifacetable = tmp;

    g_dbus_connection_register_object(
      connection,
      "/news/pan/NZB",
      pan->busnodeinfo->interfaces[0],
      &pan->ifacetable,
      pan,
      nullptr,
      nullptr
    );

  }

  static void on_name_acquired(GDBusConnection *connection,
                               gchar const *name,
                               gpointer user_data)
  {

    Pan* pan(static_cast<Pan*>(user_data));
    g_return_if_fail (pan);

    pan->name_valid = true;
    pan->lost_name = false;
  }

  static void on_name_lost(GDBusConnection *connection,
                           gchar const *name,
                           gpointer user_data)
  {
    Pan* pan(static_cast<Pan*>(user_data));
    g_return_if_fail (pan);

    pan->name_valid = false;
    pan->lost_name = true;
    pan->dbus_id= -1;
  }


  static void
  pan_dbus_init (Pan* pan)
  {
    pan->dbus_id = g_bus_own_name(
        G_BUS_TYPE_SESSION,
        PAN_DBUS_SERVICE_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        pan,nullptr);

    dbus_connection = g_bus_get_sync  (G_BUS_TYPE_SESSION , nullptr, nullptr);
  }

  static void
  pan_dbus_deinit (Pan* pan)
  {
    if (pan->dbus_id != -1) g_bus_unown_name(pan->dbus_id);
  }

  /***
   ***
   ***/
#endif
}

namespace
{
  GUI * gui_ptr (nullptr);
}

namespace
{
  void init_colors()
  {

    GtkWidget* r = gtk_label_new("bla");

    GdkRGBA def_fg, def_bg;
    std::string fg_col, bg_col;

    // init colors of PanColors
    GdkRGBA fg_color, bg_color;
    GtkStyleContext* ctx = gtk_widget_get_style_context(r);
    if(!ctx || !gtk_style_context_lookup_color(ctx, "color", &fg_color))
      gdk_rgba_parse(&def_fg, "black");
    else
    {
      def_fg.red = fg_color.red;
      def_fg.green = fg_color.red;
      def_fg.blue = fg_color.blue;
      def_fg.alpha = fg_color.alpha;
    }
    if(!ctx || !gtk_style_context_lookup_color(ctx, "background-color", &bg_color))
      gdk_rgba_parse(&def_bg, "white");
    else
    {
      def_bg.red = bg_color.red;
      def_bg.green = bg_color.red;
      def_bg.blue = bg_color.blue;
      def_bg.alpha = bg_color.alpha;
    }

    //todo move to pancolors
    fg_col = gdk_rgba_to_string(&def_fg);
    bg_col = gdk_rgba_to_string(&def_bg);

    g_object_ref_sink (r);
    gtk_widget_destroy (r);
    g_object_unref (r);

    PanColors& c (PanColors::get());
    c.def_fg = fg_col;
    c.def_bg = bg_col;
    c.def_fg_col = def_fg;
    c.def_bg_col = def_bg;
  }

}

int
main (int argc, char *argv[])
{
  bindtextdomain (GETTEXT_PACKAGE, PANLOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

#if !GLIB_CHECK_VERSION(2,36,0)
  g_type_init();
#endif

#if !GLIB_CHECK_VERSION(2,32,0)
  g_thread_init (0);
#endif
  g_mime_init ();

  bool gui(true), nzb(false), verbosed(false);
  std::string url;
  std::string groups;
  std::string nzb_output_path;
  strings_v nzb_files;
  std::string nzb_str;
  bool fatal_dbg(true);
  bool console_active(false);

  for (int i=1; i<argc; ++i)
  {
    char const *tok(argv[i]);
    if (! memcmp(tok, "headers:", 8))
      groups = tok + 8;
    else if (! memcmp(tok, "news:", 5))
      url = tok;
    else if (! strcmp(tok, "--no-gui") || ! strcmp(tok, "--nogui"))
      gui = false;
    else if (! strcmp(tok, "--debug"))
      {
        // use --debug --debug for verbose debug
        if (! console_active)
        {
          console_active = true;
          console();
        }
        if (_debug_flag)
          _debug_verbose_flag = true;
        else
          _debug_flag = true;
      }
    else if (!strcmp (tok, "--nzb"))
      nzb = true;
    else if (!strcmp (tok, "--debug-ssl")) {
      // undocumented, internal(!) debug flag for ssl problems (after 0.136)
      _dbg_ssl = true;
      _dbg_file.open("ssl.debug");
    }
    else if (!strcmp (tok, "--version") || !strcmp (tok, "-v"))
      { std::cerr << "Pan " << PAN_VERSION << '\n'; return EXIT_SUCCESS; }
    else if (!strcmp (tok, "-o") && i<argc-1)
      nzb_output_path = argv[++i];
    else if (!memcmp (tok, "--output=", 9))
      nzb_output_path = tok+9;
    else if (!strcmp(tok,"-h") || !strcmp(tok,"--help"))
      { usage (); return EXIT_SUCCESS; }
    else if (!strcmp(tok, "--verbose") )
    {
      if (!console_active) { console_active = true; console(); }
      verbosed = true;
    } else {
      nzb = true;
      nzb_files.push_back (tok);
      if (nzb_files.size() > 1) nzb_str +=" ";
      nzb_str += tok;
    }
  }

#ifdef DEBUG_LOCALE
  setlocale(LC_ALL,"C");
#endif

  g_set_prgname("org.gnome.pan");

  if (verbosed && !gui)
    _verbose_flag = true;

  if (gui)
  {
    gtk_init (&argc, &argv);
    init_colors();
  }

  if (!gui && nzb_files.empty() && url.empty() && groups.empty()) {
    std::cerr << _("Error: --no-gui used without nzb files or news:message-id.") << std::endl;
    return EXIT_FAILURE;
  }

  Log::add_info_va (_("Pan %s started"), PAN_VERSION);

  if (gui)
  {
    // load the preferences...
    char * filename = g_build_filename (file::get_pan_home().c_str(), "preferences.xml", nullptr);
    PrefsFile prefs (filename); // dummy is used to get fg/bg colors from UI
    g_free (filename);
    filename = g_build_filename (file::get_pan_home().c_str(), "group-preferences.xml", nullptr);
    GroupPrefs group_prefs (filename);
    g_free (filename);

    // instantiate the backend...
    int const cache_megs = prefs.get_int("cache-size-megs", 10);
    DataImpl data (prefs.get_string("cache-file-extension","msg"), prefs, false, cache_megs);
    ArticleCache& cache (data.get_cache ());
    EncodeCache& encode_cache (data.get_encode_cache());
    CertStore& certstore (data.get_certstore());

    if (nzb && data.get_servers().empty()) {
      std::cerr << _("Please configure Pan's news servers before using it as an nzb client.") << std::endl;
       return EXIT_FAILURE;
    }
    data.set_newsrc_autosave_timeout( prefs.get_int("newsrc-autosave-timeout-min", 10 ));

    // instantiate the queue...
    WorkerPool worker_pool (4, true);
    SocketCreator socket_creator(data, certstore);
    Queue queue (data, data, data, &socket_creator, certstore, prefs, worker_pool, false, 32768);

#ifdef HAVE_DBUS
    Pan pan(data, queue, cache, encode_cache, prefs, group_prefs);
  #ifndef DEBUG_PARALLEL
    if (!prefs.get_flag("allow-multiple-instances", false))
    {

      pan_dbus_init(&pan);

      GError* error(nullptr);
      GVariant* var;

      if (!dbus_connection) goto _fail;

      if (nzb)
      {
        g_dbus_connection_call_sync (dbus_connection,
                               PAN_DBUS_SERVICE_NAME,
                               PAN_DBUS_SERVICE_PATH,
                               "news.pan.NZB",
                               "NZBEnqueue",
                               g_variant_new ("(sssb)",
                                  groups.c_str(), nzb_output_path.c_str(), nzb_str.c_str(),  gui, nzb),
                               nullptr,
                               G_DBUS_CALL_FLAGS_NONE,
                               -1,
                               nullptr,
                               &error);

        if (!error)
        {
          std::cout<<"Added "<<nzb_files.size()<<" files to the queue. Exiting.\n";
          exit(EXIT_SUCCESS);
        } else
        {
          std::cerr<<error->message<<"\n";
          g_error_free(error);
        }
      }
    }
  #endif
    _fail:
#endif

    // set queue prefs _after_ initializing dbus
    queue.set_online(true);
    queue.set_task_save_delay(prefs.get_int ("task-save-delay-secs", 10));

    g_timeout_add (5000, queue_upkeep_timer_cb, &queue);

    if (nzb || !groups.empty())
    {
      StringView tok, v(groups);
      while (v.pop_token(tok,','))
        queue.add_task (new TaskXOver (data, tok, TaskXOver::NEW), Queue::BOTTOM);

      if (nzb)
      {
        // if no save path was specified, either prompt for one or
        // use the user's home directory as a fallback.
        if (nzb_output_path.empty() && gui)
          nzb_output_path = GUI::prompt_user_for_save_path (nullptr, prefs);
        if (nzb_output_path.empty()) // user pressed `cancel' when prompted
          return EXIT_FAILURE;

        // load the nzb files...
        std::vector<Task*> tasks;
        foreach_const (strings_v, nzb_files, it)
          NZB :: tasks_from_nzb_file (*it, nzb_output_path, cache, encode_cache, data, data, data, tasks);
        queue.add_tasks (tasks, Queue::BOTTOM);
      }

      // if in non-gui mode, contains a PanKiller ptr to quit pan on queue empty
      std::unique_ptr<PanKiller> killer;

      // don't open the full-blown Pan, just act as a nzb client,
      // with a gui or without.
      if (gui) {
        TaskPane * pane = new TaskPane (queue, prefs);
        GtkWidget * w (pane->root());
        GdkPixbuf * pixbuf = load_icon("icon_pan.png");
        gtk_window_set_default_icon (pixbuf);
        gtk_widget_show_all (w);
        g_signal_connect (w, "destroy", G_CALLBACK(destroy_cb), nullptr);
        g_signal_connect (G_OBJECT(w), "delete-event", G_CALLBACK(delete_event_cb), nullptr);
      } else {
        nongui_gmainloop = g_main_loop_new (nullptr, false);
        // create a PanKiller object -- which quits pan when the queue is done
        killer.reset(new PanKiller(queue));
      }
      register_shutdown_signals ();
      mainloop ();
    }
    else
    {
      GdkPixbuf* pixbuf = load_icon("icon_pan.png");
      GtkWidget * window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
      if (prefs.get_flag ("main-window-is-maximized", false))
        gtk_window_maximize (GTK_WINDOW (window));
      else
        gtk_window_unmaximize (GTK_WINDOW (window));
      gtk_window_set_role (GTK_WINDOW(window), "pan-main-window");
      prefs.set_window ("main-window", GTK_WINDOW(window), 100, 100, 900, 700);
      gtk_window_set_title (GTK_WINDOW(window), "Pan");
      gtk_window_set_resizable (GTK_WINDOW(window), true);
      gtk_window_set_default_icon (pixbuf);

      gui_ptr = new GUI (data, queue, prefs, group_prefs);

#ifdef HAVE_LIBNOTIFY
      if (!notify_is_initted ())
        notify_init (_("Pan notification"));
#endif

      run_pan_with_status_icon(GTK_WINDOW(window), pixbuf, queue, prefs, data, gui_ptr);

      g_object_unref (pixbuf);

      run_pan_in_window (gui_ptr, data, queue, prefs, group_prefs, GTK_WINDOW(window));
    }

    // free status icons
    for (guint i=0; i<NUM_STATUS_ICONS; ++i)
      g_object_unref(status_icons[i].pixbuf);
    delete _status_icon;

    worker_pool.cancel_all_silently ();

    if (prefs.get_flag("clear-article-cache-on-shutdown", false)) {
      cache.clear ();
      encode_cache.clear();
    }

    delete queue_and_gui;

  // free dbus pan struct and handle
#ifdef HAVE_DBUS
  #ifndef DEBUG_PARALLEL
    pan_dbus_deinit(&pan);
  #endif
#endif

#ifdef HAVE_GKR
  if (!data.get_servers().empty())
  {
    // free secure passwords
    quarks_t srv_list = data.get_servers();
    foreach(quarks_t, srv_list, it)
    {
      Data::Server* s(data.find_server(*it));
      if (s && s->gkr_pw)
      {
        gcr_secure_memory_free(s->gkr_pw);
      }
    }
  }
#endif
  }

  g_mime_shutdown ();
  Quark::dump (std::cerr);
  return EXIT_SUCCESS;
}
