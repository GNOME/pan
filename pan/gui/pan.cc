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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include <memory>
#include <fstream>
#include <config.h>
#include <signal.h>

extern "C" {
  #include <glib/gi18n.h>
  #include "gtk-compat.h"
  #include <gmime/gmime.h>
  #include <gio/gio.h>
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <unistd.h>
  #include <uulib/crc32.h>
}

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
#if GTK_CHECK_VERSION(3,0,0)
  #define GCR_API_SUBJECT_TO_CHANGE
  #include <libsecret/secret.h>
  #include <gcr/gcr.h>
  #undef GCR_API_SUBJECT_TO_CHANGE
#endif /* GTK_CHECK_VERSION(3,0,0) */
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
#include <pan/icons/pan-pixbufs.h>
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
  GMainLoop * nongui_gmainloop (0);

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
    if (static_cast<Prefs*>(user_data)->get_flag("status-icon", false))
    {
      gtk_widget_hide(w);
    }
    else
    {
      mainloop_quit ();
    }
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
  const guint8 * pixbuf_txt;
  GdkPixbuf * pixbuf;
  } status_icons[NUM_STATUS_ICONS] = {
    { icon_status_online,          0 },
    { icon_status_offline,         0 },
    { icon_status_active,          0 },
    { icon_status_queue_empty,     0 },
    { icon_status_error,           0 },
    { icon_status_idle,            0 },
    { icon_status_new_articles,    0 }
  };


  void status_icon_activate (GtkStatusIcon *icon, gpointer data)
  {

    GtkWindow * window = GTK_WINDOW(data);

    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET(window));
    GdkWindow *gdkwindow = gtk_widget_get_window (toplevel);
    GdkWindowState state = gdk_window_get_state (gdkwindow);
    gboolean maximized = state & GDK_WINDOW_STATE_MAXIMIZED;
    gboolean iconified = state & GDK_WINDOW_STATE_ICONIFIED;

//    std::cerr<<(state & GDK_WINDOW_STATE_MAXIMIZED)<<" "
//    <<(state & GDK_WINDOW_STATE_ICONIFIED)<<" "
//    <<(state & GDK_WINDOW_STATE_WITHDRAWN)<<" "
//    <<(state & GDK_WINDOW_STATE_FULLSCREEN)<<"\n" ;

    if (maximized || (maximized && iconified))
    {
      gtk_window_iconify (window);
      gtk_widget_hide (GTK_WIDGET(window));
    }
    else if (!maximized || iconified)
    {
      gtk_widget_show (GTK_WIDGET(window));
      gtk_window_deiconify(window);
    }

  }

  struct StatusIconListener : public Prefs::Listener,
                              public Queue::Listener,
                              public Data::Listener
  {

    static gboolean status_icon_periodic_refresh (gpointer p)
    {
      static_cast<StatusIconListener*>(p)->update_status_tooltip();
      return true;
    }

#ifdef HAVE_LIBNOTIFY
    static void notif_maximize_cb(NotifyNotification *notification,
                               char *action,
                               gpointer user_data)
    {
      notify_notification_close (notification, NULL);

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

    StatusIconListener(GtkStatusIcon * i, GtkWidget* r, Prefs& p, Queue& q, Data& d, bool v) :
      icon(i), root(r), prefs(p), queue(q), data(d), minimized(v),
      tasks_active(0), tasks_total(0), notif_shown(false)
    {
      prefs.add_listener(this);
      queue.add_listener(this);
      data.add_listener(this);
      is_online = q.is_online();

      update_status_tooltip();
      status_icon_timeout_tag = g_timeout_add (500, status_icon_periodic_refresh, this);

      update_status_icon(ICON_STATUS_IDLE);

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
        notify_notification_close(NOTIFY_NOTIFICATION(*it), NULL);
        g_object_unref (*it);
      }
#endif
    }

    /* prefs::listener */
    virtual void on_prefs_flag_changed (const StringView &key, bool value)
    {
       if(key == "status-icon")
         gtk_status_icon_set_visible(icon, value);
    }

    virtual void on_prefs_int_changed (const StringView& key, int color) {}
    virtual void on_prefs_string_changed (const StringView& key, const StringView& value) {}
    virtual void on_prefs_color_changed (const StringView& key, const GdkColor& color) {}

    void update_status_tooltip()
    {
      char buf[512];
      g_snprintf(buf,sizeof(buf), "<i>Pan %s : %s</i> \n"
                                  "<b>Tasks running:</b> %d\n"
                                  "<b>Total queued:</b> %d\n"
                                  "<b>Speed:</b> %.1f KiBps\n",
                                  PACKAGE_VERSION,
                                  is_online ? "online" : "offline",
                                  tasks_active,
                                  tasks_total,
                                  queue.get_speed_KiBps());

      gtk_status_icon_set_tooltip_markup(icon, buf);

    }

    void update_status_icon(StatusIcons si)
    {
      if (si==ICON_STATUS_IDLE)
      {
        if (is_online)
          gtk_status_icon_set_from_pixbuf(icon, status_icons[ICON_STATUS_ONLINE].pixbuf);
        else
          gtk_status_icon_set_from_pixbuf(icon, status_icons[ICON_STATUS_OFFLINE].pixbuf);
      } else
        gtk_status_icon_set_from_pixbuf(icon, status_icons[si].pixbuf);
    }

    void notify_of(StatusIcons si, const char* body, const char* summary)
    {
#ifdef HAVE_LIBNOTIFY
      if (!body || !summary) return;
      if (!prefs.get_flag("use-notify", false)) return;

      NotifyNotification *notif(0);
      GError* error(0);

#ifdef NOTIFY_CHECK_VERSION
  #if NOTIFY_CHECK_VERSION (0, 7, 0)
      notif=notify_notification_new(summary, body, NULL);
  #else
      notif=notify_notification_new(summary, body, NULL,NULL);
  #endif
#else
      notif=notify_notification_new(summary, body, NULL,NULL);
#endif
      if (!notif) return;

      _notifs.insert(G_OBJECT(notif));

      notify_notification_set_icon_from_pixbuf(notif, status_icons[si].pixbuf);
      notify_notification_set_timeout (notif,5000);
      notify_notification_add_action(notif,"close",_("Maximize"),NOTIFY_ACTION_CALLBACK(notif_maximize_cb),this,NULL);
      g_signal_connect (G_OBJECT(notif), "closed", G_CALLBACK(notif_close_cb), this);

      notify_notification_show (notif, &error);

      if (error) {
        debug ("Error showing notification: "<<error->message);
        g_error_free (error);
        _notifs.erase(G_OBJECT(notif));
        g_object_unref(G_OBJECT(notif));
      }
#endif
    }

    /* queue::listener */
    virtual void on_queue_task_active_changed (Queue&, Task&, bool active) {}
    virtual void on_queue_tasks_added (Queue&, int index UNUSED, int count)
    {
      tasks_total += count;
      update_status_tooltip();
      update_status_icon(ICON_STATUS_ACTIVE);
    }
    virtual void on_queue_task_removed (Queue&, Task&, int pos UNUSED)
    {
      update_status_icon(ICON_STATUS_ACTIVE);
    }
    virtual void on_queue_task_moved (Queue&, Task&, int new_pos UNUSED, int old_pos UNUSED) {}
    virtual void on_queue_connection_count_changed (Queue&, int count) {}
    virtual void on_queue_size_changed (Queue&, int active, int total)
    {
      tasks_total = total;
      tasks_active = active;
      if (tasks_total == 0 || tasks_active == 0)
        update_status_icon(ICON_STATUS_IDLE);
      update_status_tooltip();
    }

    virtual void on_queue_online_changed (Queue&, bool online)
    {
      is_online = online;
      if (tasks_total)
        update_status_icon(ICON_STATUS_ACTIVE);
      else
        update_status_icon(ICON_STATUS_IDLE);

      update_status_tooltip();
    }

    virtual void on_queue_error (Queue&, const StringView& message)
    {
      update_status_icon(ICON_STATUS_ERROR);
      if (n()) return;
      notif_shown = true;
      notify_of(ICON_STATUS_ERROR, message.str, _("An error has occurred!"));
    }

    virtual void on_queue_size_changed (Queue&, unsigned long, unsigned long) {}

    /* data::listener */
    virtual void on_group_entered (const Quark& group, unsigned long unread, unsigned long total)
    {

      if (unread)
      {
        update_status_icon(ICON_STATUS_NEW_ARTICLES);
        if (n()) return;
        notif_shown = true;
        const char* summary = _("New Articles!");
        const char* body = _("There are new\narticles available.");
        notify_of(ICON_STATUS_NEW_ARTICLES, body, summary);
      }
    }

    private:
      Queue& queue;
      Data& data;
      int tasks_active;
      int tasks_total;
      bool is_online;
      bool minimized;
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

  void status_icon_popup_menu (GtkStatusIcon *icon,
                               guint button,
                               guint activation_time,
                               gpointer data)
  {
    GtkMenu * menu = GTK_MENU(data);
    gtk_menu_popup(menu, NULL, NULL, NULL, NULL, button, activation_time);
  }

  static QueueAndGui* queue_and_gui(0);

  void run_pan_with_status_icon (GtkWindow * window, GdkPixbuf * pixbuf, Queue& queue, Prefs & prefs, Data& data, GUI* _gui)
  {

    GUI& gui (*_gui);

    for (guint i=0; i<NUM_STATUS_ICONS; ++i)
      status_icons[i].pixbuf = gdk_pixbuf_new_from_inline (-1, status_icons[i].pixbuf_txt, FALSE, 0);

    GtkStatusIcon * icon = gtk_status_icon_new_from_pixbuf (status_icons[ICON_STATUS_IDLE].pixbuf);
    GtkWidget * menu = gtk_menu_new ();

    GtkWidget * menu_quit = gtk_image_menu_item_new_from_stock ( GTK_STOCK_QUIT, NULL);
    g_signal_connect(menu_quit, "activate", G_CALLBACK(mainloop_quit), NULL);

    gtk_status_icon_set_visible(icon, prefs.get_flag("status-icon", false));
    StatusIconListener* pl = _status_icon = new StatusIconListener(icon, GTK_WIDGET(window), prefs, queue, data, prefs.get_flag("start-minimized", false));

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_quit);

    const char* label_names[] = {_("Toggle on/offline")};

    GtkWidget* labels[G_N_ELEMENTS(label_names)];

    labels[0] = gtk_menu_item_new_with_label(label_names[0]);
    queue_and_gui = new QueueAndGui(queue, gui);
    g_signal_connect(labels[0], "activate", G_CALLBACK(work_online_cb), queue_and_gui);

    for (int i=0; i < G_N_ELEMENTS(label_names); ++i)
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), labels[i]);

    gtk_widget_show_all(menu);
    g_signal_connect(icon, "activate", G_CALLBACK(status_icon_activate), window);
    g_signal_connect(icon, "popup-menu", G_CALLBACK(status_icon_popup_menu), menu);

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
    const bool minimized(prefs.get_flag("start-minimized", false));

    if (minimized)
      gtk_window_iconify (window);

    gtk_widget_set_visible (GTK_WIDGET(window), !minimized);

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
      See bugzilla bug #424248. */
  struct PanKiller : public Queue::Listener
  {
    PanKiller(Queue & q) : q(q) { q.add_listener(this); }
    ~PanKiller() { q.remove_listener(this); }

    /** Method from Queue::Listener interface: quits program on zero sized Q*/
    void on_queue_size_changed (Queue&, int active, int total)
      {  if (!active && !total) mainloop_quit();  }

    // all below methods from Queue::Listener interface are noops
    void on_queue_task_active_changed (Queue&, Task&, bool) {}
    void on_queue_tasks_added (Queue&, int , int ) {}
    void on_queue_task_removed (Queue&, Task&, int) {}
    void on_queue_task_moved (Queue&, Task&, int, int) {}
    void on_queue_connection_count_changed (Queue&, int) {}
    void on_queue_online_changed (Queue&, bool) {}
    void on_queue_error (Queue&, const StringView&) {}

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

    freopen( "CONOUT$", "w", stdout );
    freopen( "CONOUT$", "w", stderr );
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
    std::cerr << "Pan " << VERSION << "\n\n" <<
_("General Options\n"
"  -h, --help               Show this usage page.\n"
"  --verbose                Be verbose (in non-GUI mode).\n"
"\n"
"URL Options\n"

// NOT IMPLEMENTED
//"  news:message-id          Show the specified article.\n"
//"  news:group.name          Show the specified newsgroup.\n"
"  headers:group.name       Download new headers for the specified newsgroup.\n"
"  --no-gui                 On news:message-id, dump the article to stdout.\n"
"\n"
"NZB Batch Options\n"
"  --nzb file1 file2 ...    Process nzb files without launching all of Pan.\n"
"  -o path, --output=path   Path to save attachments listed in the nzb files.\n"
"  --no-gui                 Only show console output, not the download queue.\n") << std::endl;
  }

#ifdef HAVE_DBUS
  /***
   ** DBUS
   ***/

  #define PAN_DBUS_SERVICE_NAME      "news.pan.NZB"
  #define PAN_DBUS_SERVICE_PATH      "/news/pan/NZB"

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
      dbus_id(-1), busnodeinfo(0),
      data(d), queue(q), cache(c), encode_cache(ec), prefs(p), group_prefs(gp),
      lost_name(false), name_valid(false)
      {}
  };

  static void
  nzb_method_call  (GDBusConnection      *connection,
                   const gchar           *sender,
                   const gchar           *object_path,
                   const gchar           *interface_name,
                   const gchar           *method_name,
                   GVariant              *parameters,
                   GDBusMethodInvocation *invocation,
                   gpointer               user_data)
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

    g_dbus_method_invocation_return_value (invocation, NULL);
  }

  static GDBusConnection *dbus_connection(NULL);

  static const gchar xml[]=
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


  static void
  on_bus_acquired (GDBusConnection *connection,
                   const gchar     *name,
                   gpointer         user_data)
  {
    Pan* pan (static_cast<Pan*>(user_data));
    g_return_if_fail (pan);

    pan->busnodeinfo = g_dbus_node_info_new_for_xml (xml, NULL);

   /* init iface table */
    GDBusInterfaceVTable tmp =
    {
      nzb_method_call ,
      NULL, NULL
    };
    pan->ifacetable = tmp;

    g_dbus_connection_register_object(
      connection,
      "/news/pan/NZB",
      pan->busnodeinfo->interfaces[0],
      &pan->ifacetable,
      pan,
      NULL,
      NULL
    );

  }

  static void
  on_name_acquired (GDBusConnection *connection,
                    const gchar     *name,
                    gpointer         user_data)
  {

    Pan* pan(static_cast<Pan*>(user_data));
    g_return_if_fail (pan);

    pan->name_valid = true;
    pan->lost_name = false;
  }

  static void
  on_name_lost (GDBusConnection *connection,
                const gchar     *name,
                gpointer         user_data)
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
        pan,NULL);

    dbus_connection = g_bus_get_sync  (G_BUS_TYPE_SESSION , NULL, NULL);
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
  GUI * gui_ptr (0);
}

namespace
{
  void init_colors()
  {

    GtkWidget* r = gtk_label_new("bla");

    GdkColor def_fg, def_bg;
    std::string fg_col, bg_col;

    // init colors of PanColors
#if GTK_CHECK_VERSION(3,0,0)
    GdkRGBA fg_color, bg_color;
    GtkStyleContext* ctx = gtk_widget_get_style_context(r);
    if(!ctx || !gtk_style_context_lookup_color(ctx, "color", &fg_color))
      gdk_color_parse("black", &def_fg);
    else
    {
      def_fg.red = fg_color.red;
      def_fg.green = fg_color.red;
      def_fg.blue = fg_color.blue;
    }
    if(!ctx || !gtk_style_context_lookup_color(ctx, "background-color", &bg_color))
      gdk_color_parse("white", &def_bg);
    else
    {
      def_bg.red = bg_color.red;
      def_bg.green = bg_color.red;
      def_bg.blue = bg_color.blue;
    }
#else
    GtkStyle *style = gtk_rc_get_style(r);
    if(!style || !gtk_style_lookup_color(style, "text_color", &def_fg))
      gdk_color_parse("black", &def_fg);
    if(!style || !gtk_style_lookup_color(style, "bg_color", &def_bg))
      gdk_color_parse("white", &def_bg);

#endif

    //todo move to pancolors
    fg_col = GroupPrefs::color_to_string(def_fg);
    bg_col = GroupPrefs::color_to_string(def_bg);

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
  g_mime_init (GMIME_ENABLE_RFC2047_WORKAROUNDS);

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
    const char * tok (argv[i]);
    if (!memcmp(tok,"headers:", 8))
      groups = tok+8;
    else if (!memcmp(tok,"news:", 5))
      url = tok;
    else if (!strcmp(tok,"--no-gui") || !strcmp(tok,"--nogui"))
      gui = false;
    else if (!strcmp (tok, "--debug")) { // use --debug --debug for verbose debug
      if (!console_active) { console_active = true; console(); }
      if (_debug_flag) _debug_verbose_flag = true;
      else _debug_flag = true;
    } else if (!strcmp (tok, "--nzb"))
      nzb = true;

    // undocumented, internal(!) debug flag for ssl problems (after 0.136)
    else if (!strcmp (tok, "--debug-ssl")) {
      _dbg_ssl = true;
      _dbg_file.open("ssl.debug");
    }

    else if (!strcmp (tok, "--version") || !strcmp (tok, "-v"))
      { std::cerr << "Pan " << VERSION << '\n'; return EXIT_SUCCESS; }
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

  Log::add_info_va (_("Pan %s started"), VERSION);

  if (gui)
  {
    // load the preferences...
    char * filename = g_build_filename (file::get_pan_home().c_str(), "preferences.xml", NULL);
    PrefsFile prefs (filename); // dummy is used to get fg/bg colors from UI
    g_free (filename);
    filename = g_build_filename (file::get_pan_home().c_str(), "group-preferences.xml", NULL);
    GroupPrefs group_prefs (filename);
    g_free (filename);

    // instantiate the backend...
    const int cache_megs = prefs.get_int ("cache-size-megs", 10);
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

      GError* error(NULL);
      GVariant* var;

      if (!dbus_connection) goto _fail;
      {
        g_dbus_connection_call_sync (dbus_connection,
                               PAN_DBUS_SERVICE_NAME,
                               PAN_DBUS_SERVICE_PATH,
                               "news.pan.NZB",
                               "NZBEnqueue",
                               g_variant_new ("(sssb)",
                                  groups.c_str(), nzb_output_path.c_str(), nzb_str.c_str(),  gui, nzb),
                               NULL,
                               G_DBUS_CALL_FLAGS_NONE,
                               -1,
                               NULL,
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
          nzb_output_path = GUI::prompt_user_for_save_path (NULL, prefs);
        if (nzb_output_path.empty()) // user pressed `cancel' when prompted
          return EXIT_FAILURE;

        // load the nzb files...
        std::vector<Task*> tasks;
        foreach_const (strings_v, nzb_files, it)
          NZB :: tasks_from_nzb_file (*it, nzb_output_path, cache, encode_cache, data, data, data, tasks);
        queue.add_tasks (tasks, Queue::BOTTOM);
      }

      // if in non-gui mode, contains a PanKiller ptr to quit pan on queue empty
      std::auto_ptr<PanKiller> killer;

      // don't open the full-blown Pan, just act as a nzb client,
      // with a gui or without.
      if (gui) {
        TaskPane * pane = new TaskPane (queue, prefs);
        GtkWidget * w (pane->root());
        GdkPixbuf * pixbuf = gdk_pixbuf_new_from_inline (-1, icon_pan, FALSE, 0);
        gtk_window_set_default_icon (pixbuf);
        gtk_widget_show_all (w);
        g_signal_connect (w, "destroy", G_CALLBACK(destroy_cb), 0);
        g_signal_connect (G_OBJECT(w), "delete-event", G_CALLBACK(delete_event_cb), 0);
      } else {
        nongui_gmainloop = g_main_loop_new (NULL, false);
        // create a PanKiller object -- which quits pan when the queue is done
        killer.reset(new PanKiller(queue));
      }
      register_shutdown_signals ();
      mainloop ();
    }
    else
    {
      GdkPixbuf * pixbuf = gdk_pixbuf_new_from_inline (-1, icon_pan, FALSE, 0);
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
    foreach(quarks_t, data.get_servers(), it)
    {
      Data::Server* s(data.find_server(*it));
      if (s && s->gkr_pw)
      {
#if GTK_CHECK_VERSION(3,0,0)
        gcr_secure_memory_free(s->gkr_pw);
#else
        gnome_keyring_memory_free(s->gkr_pw);
#endif /* GTK_CHECK_VERSION(3,0,0) */
      }
    }
  }
#endif
  }

  g_mime_shutdown ();
  Quark::dump (std::cerr);
  return EXIT_SUCCESS;
}
