#include <config.h>
#include <iostream>
#include <glib.h>
#include <pan/general/string-view.h>
#include <pan/tasks/queue.h>
#include <pan/tasks/socket-impl-gio.h>
#include <pan/tasks/task-groups.h>
#include <pan/tasks/socket.h>
#include "data-impl.h"


//FIXME adapt to SocketCreator & SSL-Support!

using namespace pan;

namespace
{
  GMainLoop * main_loop;

  gboolean
  check_for_tasks_done (gpointer queue_gpointer)
  {
    Queue * queue (static_cast<Queue*>(queue_gpointer));

    queue->upkeep ();

    Queue::task_states_t tasks;
    queue->get_all_task_states (tasks);
    if (tasks.tasks.empty())
      g_main_loop_quit (main_loop);
    return true;
  }
};

int main (int argc, char *argv[])
{
  // check the command line args
  if (argc<3 || !atoi(argv[2])) {
    std::cerr << "Usage: add-server hostname port [username password]" << std::endl;
    return 0;
  }

  DataImpl data;

  // ensure backend knows about this server
  const Quark servername (data.add_new_server ());
  const char * host (argv[1]);
  const int port (atoi (argv[2]));
  data.set_server_addr (servername, host, port);

  const bool have_username (argc > 3);
  const bool have_password (argc > 4);
  StringView username, password;
  if (have_username) username = argv[3];
  if (have_password) password = argv[4];
  if (have_username || have_password) {
    std::cerr << "Username [" << username << "], password [" << password << "]\n";
    data.set_server_auth (servername, username, password);
  }

  // initialize the queue
  TaskArchive null_task_archive;
  WorkerPool pool;
  CertStore cs;
  // FIXME : adapt!
  SocketCreator _socket_creator(cs);
  Queue queue (data, null_task_archive, &_socket_creator, cs, pool, true, 10);
  queue.add_task (new TaskGroups (data, servername));

  // start the event loop...
  main_loop = g_main_loop_new (NULL, false);
  g_timeout_add (2*1000, check_for_tasks_done, &queue);
  g_main_loop_run (main_loop);
  return 0;
}
