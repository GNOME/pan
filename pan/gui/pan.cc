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
  #include <gtk/gtk.h>
  #include <gmime/gmime.h>
}
#ifdef G_OS_WIN32
#define _WIN32_WINNT 0x0501
#include <windows.h>
#endif
#include <pan/general/debug.h>
#include <pan/general/log.h>
#include <pan/general/file-util.h>
#include <pan/general/worker-pool.h>
#include <pan/tasks/socket-impl-gio.h>
#include <pan/tasks/task-groups.h>
#include <pan/tasks/task-xover.h>
#include <pan/tasks/task-xzver-test.h>
#include <pan/tasks/nzb.h>
#include <pan/data-impl/data-impl.h>
#include <pan/icons/pan-pixbufs.h>
#include "gui.h"
#include "group-prefs.h"
#include "prefs-file.h"
#include "task-pane.h"
#include "server-ui.h"
#include "pad.h"

using namespace pan;

namespace
{
  GMainLoop * nongui_gmainloop (0);

  void mainloop ()
  {
#if 1
    if (nongui_gmainloop)
      g_main_loop_run (nongui_gmainloop);
    else
      gtk_main ();
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

  gboolean delete_event_cb (GtkWidget *, GdkEvent *, gpointer )
  {
    mainloop_quit ();
    return true; // don't invoke the default handler that destroys the widget
  }

#ifndef G_OS_WIN32
  void sighandler (int signum)
  {
    std::cerr << "shutting down pan." << std::endl;
    signal (signum, SIG_DFL);
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
      if (foo->data->get_server_limits(*it)) {
        foo->queue->add_task (new TaskXZVerTest(*foo->data, *it));
        foo->queue->add_task (new TaskGroups (*foo->data, *it));
      }
    g_free (foo);
  }

  gboolean queue_upkeep_timer_cb (gpointer queue_gpointer)
  {
    static_cast<Queue*>(queue_gpointer)->upkeep ();
    return true;
  }

  void run_pan_in_window (ArticleCache  & cache,
                          EncodeCache   & encode_cache,
                          Data          & data,
                          Queue         & queue,
                          Prefs         & prefs,
                          GroupPrefs    & group_prefs,
                          GtkWindow     * window)
  {
    {
      const gulong delete_cb_id =  g_signal_connect (window, "delete-event", G_CALLBACK(delete_event_cb), 0);

      GUI gui (data, queue, cache, encode_cache, prefs, group_prefs);
      gtk_container_add (GTK_CONTAINER(window), gui.root());
      gtk_widget_show (GTK_WIDGET(window));

      const quarks_t servers (data.get_servers ());
      if (servers.empty())
      {
        const Quark empty_server;
        GtkWidget * w = server_edit_dialog_new (data, queue, window, empty_server);
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
    }

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
"\n"
"URL Options\n"
"  news:message-id          Show the specified article.\n"
"  news:group.name          Show the specified newsgroup.\n"
"  headers:group.name       Download new headers for the specified newsgroup.\n"
"  --no-gui                 On news:message-id, dump the article to stdout.\n"
"\n"
"NZB Batch Options\n"
"  --nzb file1 file2 ...    Process nzb files without launching all of Pan.\n"
"  -o path, --output=path   Path to save attachments listed in the nzb files.\n"
"  --no-gui                 Only show console output, not the download queue.\n") << std::endl;
  }
}

int
main (int argc, char *argv[])
{
  bindtextdomain (GETTEXT_PACKAGE, PANLOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  g_thread_init (0);
  g_mime_init (GMIME_ENABLE_RFC2047_WORKAROUNDS);

  bool gui(true), nzb(false);
  std::string url;
  std::string groups;
  std::string nzb_output_path;
  typedef std::vector<std::string> strings_t;
  strings_t nzb_files;

  for (int i=1; i<argc; ++i)
  {
    const char * tok (argv[i]);
    if (!memcmp(tok,"headers:", 8))
      groups = tok+8;
    else if (!memcmp(tok,"news:", 5))
      url = tok;
    else if (!strcmp(tok,"--no-gui") || !strcmp(tok,"--nogui"))
      gui = false;
    else if (!strcmp (tok, "--debug")) { // do --debug --debug for verbose debug
      console();
      if (_debug_flag) _debug_verbose_flag = true;
      else _debug_flag = true;
    } else if (!strcmp (tok, "--nzb"))
      nzb = true;
    else if (!strcmp (tok, "--version"))
      { std::cerr << "Pan " << VERSION << '\n'; return 0; }
    else if (!strcmp (tok, "-o") && i<argc-1)
      nzb_output_path = argv[++i];
    else if (!memcmp (tok, "--output=", 9))
      nzb_output_path = tok+9;
    else if (!strcmp(tok,"-h") || !strcmp(tok,"--help"))
      { usage (); return 0; }
    else {
      nzb = true;
      nzb_files.push_back (tok);
    }
  }

  if (gui)
    gtk_init (&argc, &argv);

  if (!gui && nzb_files.empty() && url.empty() && groups.empty()) {
    std::cerr << _("Error: --no-gui used without nzb files or news:message-id.") << std::endl;
    return 0;
  }

  Log::add_info_va (_("Pan %s started"), VERSION);

  {
    // load the preferences...
    char * filename = g_build_filename (file::get_pan_home().c_str(), "preferences.xml", NULL);
    PrefsFile prefs (filename);
    g_free (filename);
    filename = g_build_filename (file::get_pan_home().c_str(), "group-preferences.xml", NULL);
    GroupPrefs group_prefs (filename);
    g_free (filename);

    // instantiate the backend...
    const int cache_megs = prefs.get_int ("cache-size-megs", 10);
    DataImpl data (false, cache_megs);
    ArticleCache& cache (data.get_cache ());
    EncodeCache& encode_cache (data.get_encode_cache());

    if (nzb && data.get_servers().empty()) {
      std::cerr << _("Please configure Pan's news servers before using it as an nzb client.") << std::endl;
       return 0;
    }
    data.set_newsrc_autosave_timeout( prefs.get_int("newsrc-autosave-timeout-min", 10 ));

    // instantiate the queue...
    WorkerPool worker_pool (4, true);
    GIOChannelSocket::Creator socket_creator;
    Queue queue (data, data, &socket_creator, worker_pool,
                 prefs.get_flag ("work-online", true),
                 prefs.get_int ("task-save-delay-secs", 10));
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
          return 0;

        // load the nzb files...
        std::vector<Task*> tasks;
        foreach_const (strings_t, nzb_files, it)
          NZB :: tasks_from_nzb_file (*it, nzb_output_path, cache, encode_cache, data, data, data, tasks);
        queue.add_tasks (tasks, Queue::BOTTOM);
      }

      // iff non-gui mode, contains a PanKiller ptr to quit pan on queue empty
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
      g_object_unref (pixbuf);
      run_pan_in_window (cache, encode_cache, data, queue, prefs, group_prefs, GTK_WINDOW(window));
    }

    worker_pool.cancel_all_silently ();

    if (prefs.get_flag("clear-article-cache-on-shutdown", false)) {
      cache.clear ();
      encode_cache.clear();
    }
  }

  g_mime_shutdown ();
  Quark::dump (std::cerr);
  return 0;
}
