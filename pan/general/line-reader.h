#ifndef _Line_Reader_h_
#define _Line_Reader_h_

#include <cstdio>
#include <list>
#include <string>
#include <vector>
#include <pan/general/string-view.h>

namespace pan
{
  /**
   * Interface for objects that read data line-by-line.
   *
   * These hold live data when running Pan and test data
   * when we're running unit tests.
   *
   * Maybe this should be replaced with good ol' std::getline(istream).
   *
   * @ingroup general
   */
  class LineReader
  {
    public:
      virtual ~LineReader () {}
      virtual bool getline (StringView& setme) = 0;
      virtual bool fail () const = 0;
  };

  /**
   * A LineReader that reads from a local file.
   * @ingroup general
   */
  class FileLineReader final: public LineReader
  {
    public:
      FileLineReader (const StringView& filename);
      virtual ~FileLineReader ();
      bool getline (StringView& setme) override;
      bool fail () const override { return !_fp || ferror(_fp); }

      FileLineReader(FileLineReader const &) = delete;
      FileLineReader operator=(FileLineReader const &) = delete;

    private:
      char * _buf;
      const char * _bufend;
      const char * _end;
      const char * _pos;
      size_t _alloc_size;
      FILE * _fp;
  };

  /**
   * Used for feeding mock data to objects in unit tests.
   *
   * @ingroup general
   */
  class ScriptedLineReader final: public LineReader
  {
    public:
      ScriptedLineReader () {}
      ScriptedLineReader (const StringView& in) { _buf.assign (in.str, in.len); }
      virtual ~ScriptedLineReader () {}
      bool fail () const override { return false; }
      bool getline (StringView& setme) override {
        if (_buf.empty())
          return false;
        std::string::size_type pos = _buf.find ('\n');
        if (pos != std::string::npos) {
          _line.assign (_buf, 0, pos);
          _buf.erase (0, pos+1);
        } else {
          _line.clear ();
          _line.swap (_buf);
        }
        setme = _line;
        return true;
      }
      std::string _line;
      std::string _buf;
  };
}

#endif
