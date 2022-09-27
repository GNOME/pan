#include <config.h>
#include <cstdio>
#include <cstdlib>
#include <glib/gi18n.h>
#include <cerrno>
#include "file-util.h"
#include "line-reader.h"
#include "log.h"

#define INITIAL_BUF_SIZE 4096

using namespace pan;

FileLineReader :: FileLineReader (const StringView& filename):
  _buf ((char*) calloc (INITIAL_BUF_SIZE, 1)),
  _bufend (_buf + INITIAL_BUF_SIZE),
  _end (_buf),
  _pos (_buf),
  _alloc_size (INITIAL_BUF_SIZE),
  _fp (fopen (filename.to_string().c_str(), "rb"))
{
}

FileLineReader :: ~FileLineReader ()
{
  if (_fp)
    fclose (_fp);
  free (_buf);
}

bool
FileLineReader :: getline (StringView& setme)
{
  const char * eoln ((const char*) memchr ((const void*)_pos, '\n', _end-_pos));
  if (eoln != nullptr) // found an end of line... easy case.
  {
    setme.assign (_pos, eoln-_pos);
    _pos = eoln + 1;
    return true;
  }
  else // no eoln -- try to read more.
  {
    const size_t remainder (_end - _pos);
    if (remainder != _alloc_size) { // still have room left at the end...
      memmove (_buf, _pos, remainder);
      _pos = _buf;
      _end = _buf + remainder;
    } else { // this line is bigger than our buffer - resize
      char * oldbuf = _buf;
      _alloc_size *= 2u;
      _buf = (char*) calloc (_alloc_size, 1);
      memmove (_buf, _pos, remainder);
      _pos = _buf;
      _end = _buf + remainder;
      _bufend = _buf + _alloc_size;
      free (oldbuf);
    }
    const int readval (_fp!=nullptr ? fread ((char*)_end, 1, _bufend-_end, _fp) : 0);
    _end += readval;
    if (readval > 0) // new content to try
      return getline (setme);
    else {
      setme.assign (_pos, _end-_pos);
      _pos = _end;
      return !setme.empty();
    }
  }
}
