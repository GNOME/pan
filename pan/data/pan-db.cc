#include "pan-db.h"
#include <pan/general/file-util.h>
#include <sstream>

using std::ostringstream;

SQLiteDb get_sqlite_db()
{
  ostringstream db_file;
  db_file << pan::file::get_pan_home() << G_DIR_SEPARATOR << "pan.db";
  SQLiteDb my_db(db_file.str(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
  return my_db;
}

SQLiteDb pan_db = get_sqlite_db();


