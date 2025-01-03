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

#include <config.h>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <sstream>
extern "C"
{
  #include <unistd.h>
}
#include <gmime/gmime.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/string-view.h>
#include <pan/general/log.h>
#include "mime-utils.h"
#include "gpg.h"

#define is_nonempty_string(a) ((a) && (*a))

using namespace pan;

namespace pan
{

  iconv_t conv(nullptr);
  bool iconv_inited(false);

}

namespace
{
   const char*
   __yenc_extract_tag_val_char (const char * line, const char *tag)
   {
      const char * retval = nullptr;

      const char * tmp = strstr (line, tag);
      if (tmp != nullptr) {
         tmp += strlen (tag);
         if (*tmp != '\0')
            retval = tmp;
      }

      return retval;
   }

   guint
   __yenc_extract_tag_val_int_base (const char * line,
                                    const char * tag,
                                    int          base)
   {
      guint retval = 0;

      const char * tmp = __yenc_extract_tag_val_char (line, tag);
      if (tmp != nullptr) {
         char * tail = nullptr;
         retval = strtoul (tmp, &tail, base);
         if (tmp == tail)
            retval = 0;
      }

      return retval;
   }

   int
   __yenc_extract_tag_val_hex_int (const char * line,
                                   const char * tag)
   {
      return __yenc_extract_tag_val_int_base (line, tag, 16);
   }

   int
   __yenc_extract_tag_val_int (const char *line,
                               const char *tag)
   {
      return __yenc_extract_tag_val_int_base (line, tag, 0);
   }

   int
   yenc_parse_end_line (const char * b,
                        size_t     * size,
                        int        * part,
                        guint      * pcrc,
                        guint      * crc)
   {
      // find size
      const char * pch = __yenc_extract_tag_val_char (b, YENC_TAG_SIZE);
      size_t _size (0);
      if (pch)
         _size = strtoul(pch, nullptr, 10);
      pan_return_val_if_fail (_size!=0, -1);

      // part is optional
      int _part = __yenc_extract_tag_val_int (b, YENC_TAG_PART);
      guint _pcrc = __yenc_extract_tag_val_hex_int (b, YENC_TAG_PCRC32);
      if (part != nullptr)
         pan_return_val_if_fail( _pcrc != 0, -1 );

      guint _crc = __yenc_extract_tag_val_hex_int( b, YENC_TAG_CRC32);

      if (size)
         *size = _size;
      if (part)
         *part = _part;
      if (pcrc)
         *pcrc = _pcrc;
      if (crc)
         *crc = _crc;

      return 0;
   }

   /**
    * yenc_parse_being_line
    * @param line the line to check for "begin [part] line size filename"
    * @param filename if parse is successful, is set with the
    *        starting character of the filename.
    * @param line_len if parse is successful, is set with the line length
    * @param part if parse is successful this is set to the current attachement's
    *       part number
    * @return 0 on success, -1 on failure
    */
   int
   yenc_parse_begin_line (const char  * b,
                          char       ** file,
                          int         * line_len,
                          int         * attach_size,
                          int         * part)
   {
      int ll = __yenc_extract_tag_val_int (b, YENC_TAG_LINE);
      pan_return_val_if_fail (ll != 0, -1);

      // part is optional
      int part_num = __yenc_extract_tag_val_int (b, YENC_TAG_PART);

      int a_sz = __yenc_extract_tag_val_int( b, YENC_TAG_SIZE );
      pan_return_val_if_fail( a_sz != 0, -1 );

      const char * fname = __yenc_extract_tag_val_char (b, YENC_TAG_NAME);
      pan_return_val_if_fail( fname, -1 );

      if (line_len)
         *line_len = ll;
      if (file) {
         const char * pch = strchr (fname, '\n');
         *file = g_strstrip (g_strndup (fname, pch-fname));
      }
      if (part)
         *part = part_num;
      if (attach_size)
         *attach_size = a_sz;

      return 0;
   }

   /*
    * a =ypart line requires both begin & end offsets. These are the location
    * of the part inside the end file
    */
   int
   yenc_parse_part_line (const char * b, guint *begin_offset, guint *end_offset)
   {
      int bg = __yenc_extract_tag_val_int( b, YENC_TAG_BEGIN );
      if (bg == 0)
         return -1;

      int end = __yenc_extract_tag_val_int( b, YENC_TAG_END );
      if (end == 0)
         return -1;

      if (begin_offset)
         *begin_offset = bg;
      if (end_offset)
         *end_offset = end;

      return 0;
   }

   /**
    * yenc_is_beginning
    * @param line line to test & see if it's the beginning of a yenc block
    * @return true if it is, false otherwise
    */
   bool
   yenc_is_beginning_line (const char * line)
   {
      return !strncmp (line, YENC_MARKER_BEGIN, YENC_MARKER_BEGIN_LEN) &&
             !yenc_parse_begin_line (line, nullptr, nullptr, nullptr, nullptr);
   }

   bool
   yenc_is_part_line (const char * line)
   {
      return !strncmp (line, YENC_MARKER_PART, YENC_MARKER_PART_LEN) &&
             !yenc_parse_part_line (line, nullptr, nullptr);
   }

   /**
    * yenc_is_ending_line
    * @param line line to test & see if it's the end of a yenc block
    * @return true if it is, false otherwise
    */
   bool
   yenc_is_ending_line (const char * line)
   {
      return !strncmp (line, YENC_MARKER_END, YENC_MARKER_END_LEN) &&
             !yenc_parse_end_line (line, nullptr, nullptr, nullptr, nullptr);
   }

};


/***
**** UU
***/

namespace
{
   int
   uu_parse_begin_line (const StringView & begin,
                        char            ** setme_filename,
                        gulong           * setme_mode)
   {
      int retval;
      pan_return_val_if_fail (!begin.empty(), -1);

      // skip past the "begin "
      StringView tmp = begin.substr (begin.str+6, nullptr);
      char * end;
      gulong mode = strtoul (tmp.str, &end, 8);
      if (end==nullptr || end-tmp.str<3) /* must have at least 3 octal digits */
         mode = 0ul;

      tmp = tmp.substr (end, nullptr); // skip past the permissions
      tmp = tmp.substr (nullptr, tmp.strchr('\n')); // remove linefeed, if any
      tmp.trim ();

      if (mode==0 || tmp.empty())
         retval = -1;
      else {
         if (setme_mode != nullptr)
            *setme_mode = mode;
         if (setme_filename != nullptr)
            *setme_filename = g_strndup (tmp.str, tmp.len);
         retval = 0;
      }

      return retval;
   }

   /**
    * uu_is_beginning
    * @param line line to test & see if it's the beginning of a uu-encoded block
    * @return true if it is, false otherwise
    */
   bool
   uu_is_beginning_line (const StringView& line)
   {
      return !line.empty()
         && line.len>5
         && (!memcmp(line.str, "begin ", 6) || !memcmp(line.str, "BEGIN ", 6))
         && !uu_parse_begin_line (line, nullptr, nullptr);
   }

   /**
    * uu_is_ending
    * @param line line to test & see if it's the end of a uu-encoded block
    * @return true if it is, false otherwise
    */
   bool
   uu_is_ending_line (const char * line)
   {
      return line!=nullptr
          && (line[0]=='e' || line[0]=='E')
          && (line[1]=='n' || line[1]=='N')
          && (line[2]=='d' || line[2]=='D')
          && !strstr(line,"cut") && !strstr(line,"CUT");
   }

   bool
   is_uu_line (const char * line, int len)
   {
      pan_return_val_if_fail (line!=nullptr, FALSE);

      if (*line=='\0' || len<1)
         return false;

      if (len==1 && *line=='`')
         return true;

      // get octet length
      const int octet_len = *line - 0x20;
      if (octet_len > 45)
         return false;

      // get character length
      int char_len = (octet_len / 3) * 4;
      switch (octet_len % 3) {
         case 0: break;
         case 1: char_len += 2; break;
         case 2: char_len += 3; break;
      }
      if (char_len+1 > len)
         return false;

      /* if there's a lot of noise at the end, be suspicious.
         This is to keep out lines of nntp-server-generated
         taglines that have a space as the first character */
      if (char_len+10 < len)
         return false;

      // make sure each character is in the uuencoded range
      for (const char *pch=line+1, *line_end=pch+char_len; pch!=line_end; ++pch)
         if (*pch<0x20 || *pch>0x60)
            return false;

      // looks okay
      return true;
   }
}

namespace
{
  guint
  stream_readln (GMimeStream *stream, GByteArray *line, gint64* startpos)
  {
    char linebuf[1024];
    gssize len;

    /* sanity clause */
    pan_return_val_if_fail (stream!=nullptr, 0u);
    pan_return_val_if_fail (line!=nullptr, 0u);
    pan_return_val_if_fail (startpos!=nullptr, 0u);

    /* where are we now? */
    *startpos = g_mime_stream_tell (stream);

    /* fill the line array */
    g_byte_array_set_size (line, 0);
    if (!g_mime_stream_eos (stream)) do {
      len = g_mime_stream_buffer_gets (stream, linebuf, sizeof(linebuf));
      if (len>0)
        g_byte_array_append (line, (const guint8 *)linebuf, len);
    } while (len>0 && linebuf[len-1]!='\n');

    return line->len;
  }
}

enum EncType
{
	ENC_PLAIN ,
	ENC_YENC,
	ENC_UU
};

namespace pan
{
  struct TempPart
  {
    GMimeStream * stream;
    GMimeFilter * filter;
    GMimeStream * filter_stream;
    char * filename;
    unsigned int valid_lines;
    EncType type;

    int y_line_len;
    int y_attach_size;
    int y_part;
    guint y_offset_begin;
    guint y_offset_end;
    guint y_crc;
    guint y_pcrc;
    size_t y_size;

    TempPart (EncType intype=ENC_UU, char *infilename=nullptr): stream(nullptr), filter(nullptr),
      filter_stream(nullptr), filename(infilename), valid_lines(0), type(intype),
      y_line_len(0), y_attach_size(0), y_part(0),
      y_offset_begin(0), y_offset_end(0),
      y_crc(0), y_pcrc(0), y_size(0)
      {}

    ~TempPart () {
      g_free (filename);
      g_object_unref (stream);
      if (filter)
        g_object_unref (filter);
      if (filter_stream)
        g_object_unref (filter_stream);
    }
  };

  typedef std::vector<TempPart*> temp_parts_t;

  TempPart* find_filename_part (temp_parts_t& parts, const char * filename)
  {
    if (filename && *filename) {
      foreach (temp_parts_t, parts, it) {
        TempPart * part (*it);
        if (part->filename && !strcmp(filename,part->filename))
          return part;
      }
    }
    return nullptr;
  }

  bool append_if_not_present (temp_parts_t& parts, TempPart * part)
  {
    foreach (temp_parts_t, parts, it)
      if (part == *it)
        return false;
    parts.push_back (part);
    return true;
  }


  void apply_source_and_maybe_filter (TempPart * part, GMimeStream * s)
  {

    if (!part->stream) {
      part->stream = g_mime_stream_mem_new ();
      if (part->type != ENC_PLAIN) {
        part->filter_stream = g_mime_stream_filter_new (part->stream);
        switch (part->type)
        {
          case ENC_UU:
            part->filter = g_mime_filter_basic_new (GMIME_CONTENT_ENCODING_UUENCODE, FALSE);
            break;

//          case ENC_BASE64:
//            part->filter = g_mime_filter_basic_new (GMIME_CONTENT_ENCODING_BASE64, FALSE);
//            break;
//
//          case ENC_QP:
//            part->filter = g_mime_filter_basic_new (GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE, FALSE);
//            break;

          case ENC_YENC:
            part->filter = g_mime_filter_yenc_new (FALSE);
            break;
        }

        g_mime_stream_filter_add (GMIME_STREAM_FILTER(part->filter_stream),
                                  part->filter);
      }
    }

    g_mime_stream_write_to_stream (s, (part->type == ENC_PLAIN  ? part->stream : part->filter_stream));

    g_object_unref (s);
  }

  struct sep_state
  {
    temp_parts_t master_list;
    temp_parts_t current_list;
    TempPart *uu_temp;
    sep_state():uu_temp(nullptr) {};
  };

  bool
  separate_encoded_parts (GMimeStream  * istream, sep_state &state, EncType et)
  {
    temp_parts_t& master(state.master_list);
    temp_parts_t& appendme(state.current_list);
    TempPart * cur = nullptr;
    EncType enc_type = et;
    GByteArray * line;
    gboolean yenc_looking_for_part_line = FALSE;
    gint64 linestart_pos = 0;
    gint64 sub_begin = 0;
    guint line_len;
    bool found = false;

    /* sanity clause */
    pan_return_val_if_fail (istream!=nullptr,false);

    sub_begin = 0;
    line = g_byte_array_sized_new (4096);

    while ((line_len = stream_readln (istream, line, &linestart_pos)))
    {
      char * line_str = (char*) line->data;
      char * pch = strchr (line_str, '\n');
      if (pch != nullptr) {
        pch[1] = '\0';
        line_len = pch - line_str;
      }

      switch (enc_type)
      {

//        case ENC_QP:
//          sub_begin = linestart_pos;
//          cur = new TempPart(type = ENC_QP);
//          break;
//
//        case ENC_BASE64:
//          sub_begin = linestart_pos;
//          cur = new TempPart(type = ENC_BASE64);
//          break;

        case ENC_PLAIN:
        {
          const StringView line_pstr (line_str, line_len);

          if (uu_is_beginning_line (line_pstr))
          {
            gulong mode = 0ul;
            char * filename = nullptr;

            found=true;
            // flush the current entry
            if (cur != nullptr) {
              GMimeStream * s = g_mime_stream_substream (istream, sub_begin, linestart_pos);
              apply_source_and_maybe_filter (cur, s);

              if ( append_if_not_present (master, cur) )
                append_if_not_present (appendme, cur);
              cur = nullptr;
            }

            // start a new section (or, if filename matches, continue an existing one)
            sub_begin = linestart_pos;
            uu_parse_begin_line (line_pstr, &filename, &mode);
            cur = find_filename_part (master, filename);
            if (cur)
              g_free (filename);
            else
              cur = new TempPart (enc_type=ENC_UU, filename);
            state.uu_temp = cur;
          }
          else if (yenc_is_beginning_line (line_str))
          {
            found = true;
            // flush the current entry
            if (cur != nullptr) {
              GMimeStream * s = g_mime_stream_substream (istream, sub_begin, linestart_pos);
              apply_source_and_maybe_filter (cur, s);

              if ( append_if_not_present (master, cur) )
                append_if_not_present (appendme, cur);
              cur = nullptr;
            }
            sub_begin = linestart_pos;

            // start a new section (or, if filename matches, continue an existing one)
            char * fname;
            int line_len, attach_size, part;
            yenc_parse_begin_line (line_str, &fname, &line_len, &attach_size, &part);
            cur = find_filename_part (master, fname);
            if (cur) {
              g_free (fname);
              g_mime_filter_yenc_set_state (GMIME_FILTER_YENC (cur->filter),
                                            GMIME_YDECODE_STATE_INIT);
            }
            else
            {
              cur = new TempPart (enc_type=ENC_YENC, fname);
              cur->y_line_len = line_len;
              cur->y_attach_size = attach_size;
              cur->y_part = part;
              yenc_looking_for_part_line = cur->y_part!=0;
            }
          }
          else if (state.uu_temp != nullptr && is_uu_line(line_str, line_len) )
          {
            // continue an incomplete uu decode
            found = true;
            // flush the current entry
            if (cur != nullptr) {
              GMimeStream * s = g_mime_stream_substream (istream, sub_begin, linestart_pos);
              apply_source_and_maybe_filter (cur, s);

              if ( append_if_not_present (master, cur) )
                append_if_not_present (appendme, cur);
              cur = nullptr;
            }
            sub_begin = linestart_pos;
            cur = state.uu_temp;
            ++cur->valid_lines;
            enc_type = ENC_UU;
          }
          else if (cur == nullptr)
          {
            sub_begin = linestart_pos;
            cur = new TempPart(enc_type);

          }
          break;
        }
        case ENC_UU:
        {
          if (uu_is_ending_line(line_str))
          {
            GMimeStream * stream;
            if (sub_begin < 0)
              sub_begin = linestart_pos;
            stream = g_mime_stream_substream (istream, sub_begin, linestart_pos+line_len);
            apply_source_and_maybe_filter (cur, stream);
            if( append_if_not_present (master, cur) )
              append_if_not_present (appendme, cur);

            cur = nullptr;
            enc_type = ENC_PLAIN;
            state.uu_temp = nullptr;
          }
          else if (!is_uu_line(line_str, line_len))
          {
            /* hm, this isn't a uenc line, so ending the cat and setting sub_begin to -1 */
            if (sub_begin >= 0)
            {
              GMimeStream * stream = g_mime_stream_substream (istream, sub_begin, linestart_pos);
                apply_source_and_maybe_filter (cur, stream);
            }

            sub_begin = -1;
          }
          else if (sub_begin == -1)
          {
            /* looks like they decided to start using uu lines again. */
            ++cur->valid_lines;
            sub_begin = linestart_pos;
          }
          else
          {
            ++cur->valid_lines;
          }
          break;
        }
        case ENC_YENC:
        {
          if (yenc_is_ending_line (line_str))
          {
            GMimeStream * stream = g_mime_stream_substream (istream, sub_begin, linestart_pos+line_len);
            apply_source_and_maybe_filter (cur, stream);
            yenc_parse_end_line (line_str, &cur->y_size, nullptr, &cur->y_pcrc, &cur->y_crc);
            if( append_if_not_present (master, cur) )
              append_if_not_present (appendme, cur);

            cur = nullptr;
            enc_type = ENC_PLAIN;
          }
          else if (yenc_looking_for_part_line && yenc_is_part_line(line_str))
          {
            yenc_parse_part_line (line_str, &cur->y_offset_begin, &cur->y_offset_end);
            yenc_looking_for_part_line = FALSE;
            ++cur->valid_lines;
          }
          else
          {
            ++cur->valid_lines;
          }
          break;
        }
      }
    }

    /* close last entry */
    if (cur != nullptr)
    {
      if (sub_begin >= 0)
      {
        GMimeStream * stream = g_mime_stream_substream (istream, sub_begin, linestart_pos);
        apply_source_and_maybe_filter (cur, stream);
      }

      /* just in case someone started with "yenc" or "begin 644 asf" in a text message to fuck with unwary newsreaders */
      if (cur->valid_lines < 10u)
        cur->type = ENC_PLAIN;

      if( append_if_not_present (master, cur) )
        append_if_not_present (appendme, cur);
      cur = nullptr;
      enc_type = ENC_PLAIN;

    }

    g_byte_array_free (line, TRUE);
    return found;
  }

}

void
mime :: guess_part_type_from_filename (const char   * filename,
                                       const char  ** setme_type,
                                       const char  ** setme_subtype)
{
	static const struct {
		const char * suffix;
		const char * type;
		const char * subtype;
	} suffixes[] = {
	  { ".asc",   "text",         "plain" }, // plain-encoded signature
		{ ".avi",   "video",        "vnd.msvideo" },
		{ ".dtd",   "text",         "xml-dtd" },
		{ ".flac",  "audio",        "ogg" },
		{ ".gif",   "image",        "gif" },
		{ ".htm",   "text",         "html" },
		{ ".html",  "text",         "html" },
		{ ".jpg",   "image",        "jpeg" },
		{ ".jpeg",  "image",        "jpeg" },
		{ ".md5",   "image",        "tiff" },
		{ ".mp3",   "audio",        "mpeg" },
		{ ".mpeg",  "video",        "mpeg" },
		{ ".mpg",   "video",        "mpeg" },
		{ ".mov",   "video",        "quicktime" },
		{ ".nfo",   "text",         "plain" },
		{ ".oga",   "audio",        "x-vorbis" },
		{ ".ogg",   "audio",        "ogg" },
		{ ".ogv",   "video",        "ogg" },
		{ ".ogx",   "application",  "ogg" },
		{ ".png",   "image",        "png" },
		{ ".qt",    "video",        "quicktime" },
		{ ".rar",   "application",  "x-rar" },
		{ ".rv",    "video",        "vnd.rn-realvideo" },
		{ ".scr",   "application",  "octet-stream" },
		{ ".spx",   "audio",        "ogg" },
		{ ".svg",   "image",        "svg+xml" },
		{ ".tar",   "application",  "x-tar" },
		{ ".tbz2",  "application",  "x-tar" },
		{ ".tgz",   "application",  "x-tar" },
		{ ".tiff",  "image",        "tiff" },
		{ ".tif",   "image",        "tiff" },
		{ ".txt",   "text",         "plain" },
		{ ".uu",    "text",         "x-uuencode" },
		{ ".uue",   "text",         "x-uuencode" },
		{ ".xml",   "text",         "xml" },
		{ ".xsl",   "text",         "xml" },
		{ ".zip",   "application",  "zip" }
	};
	static const int suffix_qty = G_N_ELEMENTS (suffixes);
	const char * suffix;

	/* zero out the return values */
	pan_return_if_fail (setme_type!=nullptr);
	pan_return_if_fail (setme_subtype!=nullptr);
	*setme_type = *setme_subtype = nullptr;

	/* sanity clause */
	pan_return_if_fail (is_nonempty_string(filename));

       	suffix = strrchr (filename, '.');
	if (suffix != nullptr) {
		int i;
		for (i=0; i<suffix_qty; ++i) {
			if (!g_ascii_strcasecmp (suffix, suffixes[i].suffix)) {
				*setme_type = suffixes[i].type;
				*setme_subtype = suffixes[i].subtype;
				break;
			}
		}
	}

	if (*setme_type == nullptr) {
		*setme_type = "application";
		*setme_subtype = "octet-stream";
	}
}

namespace
{
  void
  ptr_array_insert (GPtrArray *array, guint index, gpointer object)
  {
    unsigned char *dest, *src;
    guint n;

    g_ptr_array_set_size (array, array->len + 1);

    if (index != array->len) {
      /* need to move items down */
      dest = ((unsigned char *) array->pdata) + (sizeof (void *) * (index + 1));
      src = ((unsigned char *) array->pdata) + (sizeof (void *) * index);
      n = array->len - index - 1;

      g_memmove (dest, src, (sizeof (void *) * n));
    }

    array->pdata[index] = object;
  }


  void handle_encoded_in_text_plain_cb (GMimeObject *parent, GMimeObject *part, gpointer data)
  {

    if (!part) return;

    // we assume that inlined yenc and uu are only in text/plain blocks
    GMimeContentType * content_type = g_mime_object_get_content_type (part);
    if (!g_mime_content_type_is_type (content_type, "text", "plain"))
      return;

    // get this part's content
    GMimeDataWrapper * content = g_mime_part_get_content (GMIME_PART (part));

    if (!content)
      return;

    // wrap a buffer stream around it for faster reading -- it could be a file stream
    GMimeStream * stream = g_mime_data_wrapper_get_stream (content);
    g_mime_stream_reset (stream);
    GMimeStream * istream = g_mime_stream_buffer_new (stream, GMIME_STREAM_BUFFER_BLOCK_READ);

    // break it into separate parts for text, uu, and yenc pieces.
    sep_state &state(*(sep_state*)data);
    temp_parts_t &parts(state.current_list);
    bool split = separate_encoded_parts (istream, state, ENC_PLAIN);
    g_mime_stream_reset (istream);

    // split?
    if(split)
    {
      //this part was completely folded into a previous part
      //so delete it
      if(parts.size()==0) {
        GMimeMultipart *mp = GMIME_MULTIPART (parent);
        int index = g_mime_multipart_index_of (mp,part);
        if(index>0)
          g_mime_multipart_remove_at (mp,index);
        g_object_unref(part);
      }
      else
      {
        GMimeMultipart * multipart = g_mime_multipart_new_with_subtype ("mixed");

        const TempPart *tmp_part;
        const char *filename;
        GMimePart *subpart;
        GMimeStream *subpart_stream;
        foreach (temp_parts_t, parts, it)
        {
          // reset these for each part
          const char * type = "text";
          const char * subtype = "plain";

          tmp_part = *it;
          filename = tmp_part->filename;

          if (filename && *filename)
            mime::guess_part_type_from_filename (filename, &type, &subtype);

          subpart = g_mime_part_new_with_type (type, subtype);
          if (filename && *filename)
            g_mime_part_set_filename (subpart, filename);

          subpart_stream = tmp_part->stream;
          content = g_mime_data_wrapper_new_with_stream (subpart_stream, GMIME_CONTENT_ENCODING_DEFAULT);
          g_mime_part_set_content (subpart, content);
          g_mime_multipart_add (GMIME_MULTIPART (multipart), GMIME_OBJECT (subpart));

          g_object_unref (content);
          g_object_unref (subpart);
        }

        // replace the old part with the new multipart
        GMimeObject *newpart = GMIME_OBJECT(multipart);
        if(parts.size()==1)
        {
          //only one part so no need for multipart
          newpart = g_mime_multipart_remove_at(multipart,0);
          g_object_unref(multipart);
        }
        if(GMIME_IS_MULTIPART(parent))
        {
          GMimeMultipart *mp = GMIME_MULTIPART (parent);
          int index = g_mime_multipart_index_of (mp, part);
          g_mime_multipart_remove_at (mp, index);
          g_object_unref (part);

          //workaround gmime insert bug
          //g_mime_multipart_insert (mp,index,newpart);
          {
            ptr_array_insert(mp->children, index, newpart);
            g_object_ref(newpart);
          }
        }
        else if(GMIME_IS_MESSAGE(parent))
        {
          g_mime_message_set_mime_part((GMimeMessage*)parent, newpart);
        }
        g_object_unref(newpart);
      }
    }

    parts.clear();
    g_object_unref (istream);
  }
}

namespace pan
{
  struct temp_p {
    GMimeObject *parent,*part;

    temp_p(GMimeObject *p, GMimeObject *par):parent(p),part(par) {};
  };

#ifdef HAVE_GMIME_CRYPTO
  struct QueryMPType
  {
    GMimeObject* obj;
    GPGDecType type;
    std::string algo;

    QueryMPType() : obj(nullptr), type(GPG_DECODE) {}
  };
#else
   struct QueryMPType
  {
    GMimeObject* obj;
    std::string algo;

    QueryMPType() : obj(nullptr) {}
  };
#endif
}

namespace
{

  typedef std::vector<temp_p> temp_p_t;

  void find_text_cb(GMimeObject *parent, GMimeObject *part, gpointer data)
  {
    if(!GMIME_IS_PART(part))
      return;

    temp_p_t *v( (temp_p_t *) data);
    // we assume that inlined yenc and uu are only in text/plain blocks
    GMimeContentType * content_type = g_mime_object_get_content_type (part);
    if (!g_mime_content_type_is_type (content_type, "text", "plain"))
      return;
    v->push_back(temp_p(parent,part) );
  }


  void mixed_mp_cb(GMimeObject *parent, GMimeObject *part, gpointer data)
  {
    if(!GMIME_IS_PART(part))
      return;

    QueryMPType * type = static_cast<QueryMPType*>(data);

    GMimeContentType * content_type = g_mime_object_get_content_type (part);
//    if (!g_mime_content_type_is_type (content_type, "text", "plain") &&
//        !g_mime_content_type_is_type (content_type, "application", "pgp-signature"))
//      return;

//    if (g_mime_content_type_is_type (content_type, "application", "pgp-signature"))
//    {
//      g_mime_object_set_content_type_parameter(part, "micalg", "pgp-sha1");//type->algo.c_str());
//      std::cerr<<g_mime_object_to_string(part)<<"\n";
//    }

    g_mime_multipart_add (GMIME_MULTIPART(type->obj), part);
  }


  void mixed_mp_find_gpg_params_cb (GMimeObject *parent, GMimeObject *part, gpointer data)
  {

    if(!GMIME_IS_PART(part))
      return;

    QueryMPType * type = static_cast<QueryMPType*>(data);

//    const char* micalg = g_mime_object_get_content_type_parameter (GMIME_OBJECT (part), "micalg");
//    if (micalg)
//    {
//      std::cerr<<"micalg found : "<<micalg<<"\n";
//      type->algo = micalg;
//    }

    GMimeContentType * content_type = g_mime_object_get_content_type (part);

    if (!content_type) return;
#ifdef HAVE_GMIME_CRYPTO
    if (g_mime_content_type_is_type (content_type, "application", "pgp-signature"))
      type->type = GPG_VERIFY;
    else if (g_mime_content_type_is_type (content_type, "application", "pgp-encrypted"))
      type->type = GPG_DECODE;
#endif

  }

}

/***
****
***/

#ifdef HAVE_GMIME_CRYPTO
GMimeMessage*
mime :: construct_message (GMimeStream    ** istreams,
                           unsigned int      qty,
                           GPGDecErr      &  err)
#else
GMimeMessage*
mime :: construct_message (GMimeStream    ** istreams,
                           unsigned int      qty)
#endif
{
  const char * message_id = "Foo <bar@mum>";
  GMimeMessage * retval = nullptr;

  // sanity clause
  pan_return_val_if_fail (is_nonempty_string(message_id), nullptr);
  pan_return_val_if_fail (istreams!=nullptr, nullptr);
  pan_return_val_if_fail (qty != 0, nullptr);
  for (int i=0; i<qty; ++i)
    pan_return_val_if_fail (GMIME_IS_STREAM(istreams[i]), nullptr);

  // build the GMimeMessages
  GMimeMessage ** messages = g_new (GMimeMessage*, qty);

  for (int i=0; i<qty; ++i) {
    GMimeParser* parser = g_mime_parser_new_with_stream (istreams[i]);
    messages[i] = g_mime_parser_construct_message(parser, nullptr);
    g_object_unref(parser);
    g_mime_stream_reset(istreams[i]);
    parser = g_mime_parser_new_with_stream (istreams[i]);
    GMimeObject* part = g_mime_parser_construct_part(parser, nullptr);
    g_object_unref (parser);
    parser = nullptr;
    GMimeContentType * type = g_mime_object_get_content_type (part);
    const bool multipart (GMIME_IS_MULTIPART_SIGNED(part) || GMIME_IS_MULTIPART_ENCRYPTED(part));
#ifdef HAVE_GMIME_CRYPTO
    if (GMIME_IS_MULTIPART_SIGNED(part))
    {
      err.type = GPG_VERIFY;
      err.verify_ok = gpg_verify_mps(part, err);
    }
    else if (GMIME_IS_MULTIPART_ENCRYPTED(part))
    {
      err.type = GPG_DECODE;
      err.verify_ok =  gpg_verify_mps(part, err);
    }
#endif
    if (type)
    {
      if (g_mime_content_type_is_type (type, "multipart", "mixed"))
      {
        QueryMPType qtype;
        g_mime_multipart_foreach (GMIME_MULTIPART(part), mixed_mp_find_gpg_params_cb, &qtype);
        GMimeObject* o(nullptr);
#ifdef HAVE_GMIME_CRYPTO
        if (qtype.type == GPG_VERIFY)
        {
          GMimeMultipartSigned* new_mp = g_mime_multipart_signed_new();
          err.type = GPG_VERIFY;
          qtype.obj = o = GMIME_OBJECT(new_mp);
          g_mime_multipart_foreach (GMIME_MULTIPART(part), mixed_mp_cb, &qtype);
          err.verify_ok = gpg_verify_mps(GMIME_OBJECT(new_mp), err);
          g_mime_message_set_mime_part(messages[i], GMIME_OBJECT(new_mp));
        }

        if (qtype.type == GPG_DECODE)
        {
          GMimeMultipartEncrypted* new_mp = g_mime_multipart_encrypted_new();
          err.type = GPG_DECODE;
          qtype.obj = o = GMIME_OBJECT(new_mp);
          g_mime_multipart_foreach (GMIME_MULTIPART(part), mixed_mp_cb, &qtype);
          err.verify_ok =  gpg_verify_mps(GMIME_OBJECT(new_mp), err);
          g_mime_message_set_mime_part(messages[i], GMIME_OBJECT(new_mp));
        }
#endif
        if (o != nullptr)
          g_object_unref(o);
      }
      else if (multipart)
      {
#ifdef HAVE_GMIME_CRYPTO
        if (err.decrypted)
          g_mime_message_set_mime_part(messages[i], err.decrypted);
      } else
      {
#endif
          g_mime_message_set_mime_part(messages[i], part);
      }
    }

    g_object_unref (part);
  }

  if (qty > 1) // fold multiparts together
  {
    GMimeMultipart * mp = g_mime_multipart_new ();

    for (int i=0; i<qty; ++i) // should this 0 be 1?
    {
      g_mime_multipart_add(mp,g_mime_message_get_mime_part(messages[i]) );
    }

    g_mime_message_set_mime_part(messages[0],GMIME_OBJECT(mp));
    g_object_unref(mp);
  }

  retval = messages[0];
  for (int i=1; i<qty; ++i)
    g_object_unref (messages[i]);

  // pick out yenc and uuenc data in text/plain messages
  temp_p_t partslist;
  sep_state state;
  if (retval != nullptr)
    g_mime_message_foreach(retval, find_text_cb, &partslist);


  foreach(temp_p_t, partslist, it)
  {
    temp_p &data(*it);
    handle_encoded_in_text_plain_cb(data.parent, data.part, &state);
  }


  // cleanup
  foreach (temp_parts_t, state.master_list, it)
  {
    delete *it;
  }
  g_free (messages);

  return retval;
}

/***
****
***/

/**
 * Retrieve the charset from a mime message
 */

#if 0 // unused?
static void
get_charset_partfunc (GMimeObject * obj, gpointer charset_gpointer)
{
	GMimePart * part;
	const GMimeContentType * type;
	const char ** charset;

	if (!GMIME_IS_PART (obj))
		return;

	part = GMIME_PART (obj);
	type = g_mime_object_get_content_type (GMIME_OBJECT (part));
	charset = (const char **) charset_gpointer;
	if (g_mime_content_type_is_type (type, "text", "*"))
		*charset = g_mime_object_get_content_type_parameter (GMIME_OBJECT (part), "charset");
}

const char *
mime :: get_charset (GMimeMessage * message)
{
	const char * retval = nullptr;
	pan_return_val_if_fail (message!=nullptr, nullptr);

	g_mime_message_foreach_part (message, get_charset_partfunc, &retval);

	return retval;
}
#endif

/**
***
**/

namespace
{
   enum StripFlags
   {
      STRIP_CASE                    = (1<<0),
      STRIP_MULTIPART_NUMERATOR     = (1<<1),
      STRIP_MULTIPART               = (1<<2)
   };

   /**
    * Normalizing a subject header involves:
    *
    * 1. tearing out the numerator from multipart indicators
    *    (i.e., remove "21" from (21/42))
    *    for threading.
    * 2. convert it to lowercase so that, when sorting, we can
    *    have case-insensitive sorting without having to use
    *    a slower case-insensitive compare function.
    * 3. strip out all the leading noise that breaks sorting.
    *
    * When we're threading articles, it's much faster to
    * normalize the * subjects at the outset instead of
    * normalizing them for each comparison.
    */
   void
   normalize_subject (const StringView   & subject,
                      StripFlags           strip,
                      std::string        & setme)
   {
#if 0
      static bool _keep_chars[UCHAR_MAX+1];
      static bool _inited (false);
      if (!_inited) {
         _inited = true;
         for (int i=0; i<=UCHAR_MAX; ++i) {
            const unsigned char uch ((unsigned char)i);
            _keep_chars[i] = isalnum(uch) || isdigit(uch) || isspace(uch);
         }
      }
#endif
      setme.reserve (subject.len + 1);
      const unsigned char * in ((const unsigned char*) subject.begin());
      const unsigned char * end ((const unsigned char*) subject.end());
      const bool strip_case (strip & STRIP_CASE);
      const bool strip_numerator (strip & STRIP_MULTIPART_NUMERATOR);
      const bool strip_multipart (strip & STRIP_MULTIPART);

      // skip the leading noise
      while (in!=end && isspace(*in))
         ++in;

      while (in!=end)
      {
         // strip numerator out of multipart information
         if ((strip_multipart||strip_numerator)
             && (*in=='('||*in=='[')
             && isdigit(in[1]))
         {
            const unsigned char * start (++in);

            if (strip_multipart || strip_numerator)
            {
               while (in!=end && isdigit(*in))
                  ++in;

               if (*in!='/' && *in!='|') // oops, not multipart information
                  in = start;

               else if (strip_multipart)
               {
                  for (++in; in!=end && *in!=']' && *in!=')'; ++in)
                  {
                     if (isalpha(*in)) { // oops, not multipart information
                        in = ++start;
                        break;
                     }
                  }

                  if (in!=end && (*in==']' || *in==')'))
                    ++in;
               }
            }
            continue;
         }

#if 0
         // strip out junk that breaks sorting
         if (_keep_chars[*in])
#endif
            setme += (char) (strip_case ? tolower(*in) : *in);

         ++in;
      }
   }
}

void
mime :: remove_multipart_from_subject (const StringView    & subject,
                                       std::string         & setme)
{
  normalize_subject (subject, STRIP_MULTIPART, setme);
}

void
mime :: remove_multipart_part_from_subject (const StringView    & subject,
                                            std::string         & setme)
{
  normalize_subject (subject, STRIP_MULTIPART_NUMERATOR, setme);
}

namespace
{
  GMimeObject *
  handle_multipart_mixed (GMimeMultipart *multipart, gboolean *is_html);

  GMimeObject *
  handle_multipart_alternative (GMimeMultipart *multipart, gboolean *is_html)
  {
    GMimeObject *mime_part, *text_part = nullptr;
    GMimeContentType *type;
    int count = g_mime_multipart_get_count (multipart);

    for (int i = 0; i < count; ++i) {
      mime_part = g_mime_multipart_get_part (multipart, i);

      type = g_mime_object_get_content_type (mime_part);
      if (g_mime_content_type_is_type (type, "text", "*")) {
        if (!text_part || !g_ascii_strcasecmp (type->subtype, "plain")) {
          *is_html = !g_ascii_strcasecmp (type->subtype, "html");
          text_part = mime_part;
        }
      }
    }

    return text_part;
  }

  GMimeObject *
  handle_multipart_mixed (GMimeMultipart *multipart, gboolean *is_html)
  {

    GMimeObject *mime_part, *text_part = nullptr;
    GMimeContentType *type, *first_type = nullptr;
    int count = g_mime_multipart_get_count (multipart);

    for (int i = 0; i < count; ++i) {
      mime_part = g_mime_multipart_get_part (multipart, i);

      type = g_mime_object_get_content_type (mime_part);
      if (GMIME_IS_MULTIPART (mime_part)) {
        multipart = GMIME_MULTIPART (mime_part);
        if (g_mime_content_type_is_type (type, "multipart", "alternative")) {
          mime_part = handle_multipart_alternative (multipart, is_html);
          if (mime_part)
            return mime_part;
        } else {
          mime_part = handle_multipart_mixed (multipart, is_html);
          if (mime_part && !text_part)
            text_part = mime_part;
        }
      } else if (g_mime_content_type_is_type (type, "text", "*")) {
        if (!g_ascii_strcasecmp (type->subtype, "plain")) {
          /* we got what we came for */
          *is_html = !g_ascii_strcasecmp (type->subtype, "html");
          return mime_part;
        }

        /* if we haven't yet found a text part or if it is a type we can
         * understand and it is the first of that type, save it */
        if (!text_part || (!g_ascii_strcasecmp (type->subtype, "plain") && (first_type &&
                           g_ascii_strcasecmp (type->subtype, first_type->subtype) != 0))) {
          *is_html = !g_ascii_strcasecmp (type->subtype, "html");
          text_part = mime_part;
          first_type = type;
        }
      }
    }

    return text_part;
  }

}

namespace
{
  char *
  pan_g_mime_part_get_content (GMimePart *mime_part, size_t *len)
  {
    char *retval = nullptr;

    g_return_val_if_fail (GMIME_IS_PART (mime_part), nullptr);

    if (!mime_part->content || !mime_part->content->stream) {
//      g_warning ("no content set on this mime part");  // dbg
      return nullptr;
    }

    GMimeDataWrapper *wrapper = g_mime_part_get_content(mime_part);
    GMimeStream *stream = g_mime_stream_mem_new();
    g_mime_data_wrapper_write_to_stream (wrapper, stream);
    GByteArray *bytes = g_mime_stream_mem_get_byte_array((GMimeStreamMem*)stream);
    *len = bytes->len + 1;
    if (bytes->len)
    {
      retval = (char*)g_malloc0(bytes->len + 1);
      memcpy(retval, bytes->data, bytes->len);
    }
    g_object_unref(stream);

    return retval;
  }
}

char *pan::pan_g_mime_message_get_body (GMimeMessage *message, gboolean *is_html)
{
  GMimeObject *mime_part = nullptr;
  GMimeContentType *type;
  GMimeMultipart *multipart;
  char *body = nullptr;
  size_t len = 0;

  g_return_val_if_fail (GMIME_IS_MESSAGE (message), nullptr);
//  g_return_val_if_fail (is_html != nullptr, nullptr);

  type = g_mime_object_get_content_type (message->mime_part);
  if (GMIME_IS_MULTIPART (message->mime_part)) {
    /* let's see if we can find a body in the multipart */
    multipart = GMIME_MULTIPART (message->mime_part);
    if (g_mime_content_type_is_type (type, "multipart", "alternative"))
      mime_part = handle_multipart_alternative (multipart, is_html);
    else
      mime_part = handle_multipart_mixed (multipart, is_html);
  } else if (g_mime_content_type_is_type (type, "text", "*")) {
    /* this *has* to be the message body */
    if (g_mime_content_type_is_type (type, "text", "html"))
      *is_html = TRUE;
    else
      *is_html = FALSE;
    mime_part = message->mime_part;
  }

  if (mime_part != nullptr) {
    body = pan_g_mime_part_get_content (GMIME_PART (mime_part), &len);
  }
  return body;
}

void pan::pan_g_mime_message_add_recipients_from_string (GMimeMessage *message, GMimeAddressType type, const char *string)
{
  InternetAddressList *addrlist;
  if ((addrlist = internet_address_list_parse (nullptr, string))) {
    for (int i = 0; i < internet_address_list_length (addrlist); ++i) {
      InternetAddress *ia = internet_address_list_get_address (addrlist, i);
      if (INTERNET_ADDRESS_IS_MAILBOX(ia))
        g_mime_message_add_mailbox (message, type, internet_address_get_name(ia), internet_address_mailbox_get_addr(INTERNET_ADDRESS_MAILBOX(ia)));
    }
  }
}

/**
* Works around a GMime bug that uses `Message-Id' rather than `Message-ID'
*/

std::string pan::pan_g_mime_message_set_message_id (GMimeMessage *msg, const char *mid)
{
    const char * charset = nullptr; // "ISO-8859-1";  // FIXME
    g_mime_object_append_header ((GMimeObject *) msg, "Message-ID", mid, charset);
    char * bracketed = g_strdup_printf ("<%s>", mid);
    g_mime_header_list_set (GMIME_OBJECT(msg)->headers, "Message-ID", bracketed, charset);
    std::string ret (bracketed);
    g_free (bracketed);
    return ret;
}

namespace pan
{

  void
  mime_part_set_content (GMimePart *part, const char *str)
  {
    GMimeDataWrapper *content;
    GMimeStream *stream;

    stream = g_mime_stream_mem_new_with_buffer (str, strlen (str));
    content = g_mime_data_wrapper_new_with_stream (stream, GMIME_CONTENT_ENCODING_DEFAULT);
    g_object_unref (stream);

    g_mime_part_set_content (part, content);
    g_object_unref (content);
  }
#ifdef HAVE_GMIME_CRYPTO
  GMimeSignatureStatus
  get_sig_status (GMimeSignatureList *signatures)
  {
	GMimeSignatureStatus status = GMIME_SIGNATURE_STATUS_VALID;
    GMimeSignature *sig;
    int i;

    if (!signatures || signatures->array->len == 0)
      return GMIME_SIGNATURE_STATUS_SYS_ERROR;

    for (i = 0; i < g_mime_signature_list_length (signatures); i++) {
      sig = g_mime_signature_list_get_signature (signatures, i);
      status = MAX (status, sig->status);
    }

    return status;
  }

  bool
  gpg_verify_mps (GMimeObject* obj, GPGDecErr& info)
  {

    if (!gpg_inited) return false;

    GMimeMultipartSigned * mps(nullptr);
    GMimeMultipartEncrypted * mpe(nullptr);

    switch (info.type)
    {
      case GPG_VERIFY:
        mps = (GMimeMultipartSigned *)obj;
        break;

      case GPG_DECODE:
        mpe = (GMimeMultipartEncrypted *)obj;
        break;
    }

    info.clear();

    if (info.type == GPG_VERIFY)
    {
      GMimeSignatureList * sigs = g_mime_multipart_signed_verify (mps, GMIME_VERIFY_NONE, &info.err);
      if (info.err || !sigs) return false;
      if (sigs) info.no_sigs = false;
      fill_signer_info(info.signers, sigs);
      bool status = get_sig_status(sigs) == GMIME_SIGNATURE_STATUS_VALID;
      g_object_unref(sigs);
      return status;
    }

    if (info.type == GPG_DECODE)
    {
      info.decrypted = g_mime_multipart_encrypted_decrypt (mpe, GMIME_DECRYPT_NONE, nullptr, &info.result, &info.err);
      if (!info.decrypted)
        if (info.err) return false;

      GMimeSignatureList * sigs = g_mime_decrypt_result_get_signatures (info.result);
      if (sigs)
      {
        info.no_sigs = false;
        fill_signer_info(info.signers, sigs);
        bool status = get_sig_status(info.result->signatures) == GMIME_SIGNATURE_STATUS_VALID;
        g_object_unref(sigs);
        return status;
      }
      return !info.err;
    }

    return true;
  }

  enum
  {
    GMIME_MULTIPART_SIGNED_CONTENT,
    GMIME_MULTIPART_SIGNED_SIGNATURE
  };

  bool message_add_signed_part (const std::string& uid, const std::string& body_str,
                                GMimeMessage* body)
  {
    if (uid.empty()) return false;

    GMimeMultipartSigned *mps;
    GError* err(nullptr);
    GMimePart* part = g_mime_part_new_with_type("text", "plain");
    mime_part_set_content (part, body_str.c_str());

    mps = g_mime_multipart_signed_new ();

    /* sign the part */
    GMimeObject *gmo;
    gmo = g_mime_message_get_mime_part (body);
    mps = g_mime_multipart_signed_sign (gpg_ctx, gmo, uid.c_str(), &err);
    if (mps == nullptr)
    {
      g_object_unref(mps);
      g_object_unref(G_OBJECT(part));
      return false;
    }

    /* GMIME _dirty_ hack : set filename for signature attachment */
    /// FIXME : gets scrambled somehow, but _atm_ i don't care ....
    GMimeObject * signature = g_mime_multipart_get_part (GMIME_MULTIPART (mps), GMIME_MULTIPART_SIGNED_SIGNATURE);
    g_mime_part_set_filename (GMIME_PART(signature), "signature.asc");

    g_mime_message_set_mime_part(body,GMIME_OBJECT(mps));
    g_object_unref(G_OBJECT(part));
    g_object_unref(mps);
    return true;
  }

  bool
  gpg_encrypt (const std::string& uid, const std::string& body_str,
               GMimeMessage* body, GPtrArray* rcp, bool sign)
  {

    GError* err(nullptr);
    GMimePart* part = g_mime_part_new_with_type("text", "plain");
    mime_part_set_content (part, body_str.c_str());

    GMimeMultipartEncrypted * mpe = g_mime_multipart_encrypted_new();

	GMimeMultipartEncrypted *gmme;
	gmme = g_mime_multipart_encrypted_encrypt(gpg_ctx, GMIME_OBJECT (part), sign, uid.c_str(),
                                           GMIME_ENCRYPT_NONE, rcp, &err);
    if (gmme == nullptr)
    {
      g_object_unref(mpe);
      g_object_unref(G_OBJECT(part));
      g_object_unref(gmme);
      return false;
    }

    g_mime_message_set_mime_part(body,GMIME_OBJECT(mpe));
    g_object_unref(G_OBJECT(part));
    g_object_unref(mpe);
    g_object_unref(gmme);

    return true;
  }
#endif
}


