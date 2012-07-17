
/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file
 * Copyright (C) 2007 Calin Culianu <calin@ajvar.org>
 * Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#ifndef _XZVERDecoder_H_
#define _XZVERDecoder_H_

#include <list>
#include <string>
#include <sstream>
#include <vector>

#include <fstream>

#include <pan/general/locking.h>
#include <pan/general/worker-pool.h>
#include <pan/tasks/task-xover.h>
#include <pan/tasks/decoder.h>
extern "C" {
#  define PROTOTYPES
#  include <uulib/uudeview.h>
};

namespace pan
{
  #define CHUNK 16384

  class Decoder;

  /**
   * Decodes XZVER yenc-encoded and zlib-deflated
   * headers to process with TaskXOver
   * @author Heinrich Mueller <heinrich.mueller82@gmail.com>
   * @author Calin Culianu <calin@ajvar.org>
   * @author Charles Kerr <charles@rebelbase.com>
   * @ingroup tasks
   * @see Queue
   * @see TaskXOver
   */
  class XZVERDecoder: public Decoder
  {
    public:

      struct InflateChunk
      {
        int ret;
        unsigned char tmpbuf[CHUNK]; // dbg
      };

      XZVERDecoder (WorkerPool&);

      ~XZVERDecoder ();

      typedef std::vector<std::string> strings_t;

      void enqueue (TaskXOver * task, TaskXOver::DataStream*, Data*);

    protected: // inherited from WorkerPool::Worker

      void do_work();
      TaskXOver * xtask;
      TaskXOver::DataStream* stream;

      z_stream _strm;
      int _zret;
      NNTP* nntp;
      int _cnt;
      Data* data;
      uint64_t high;
      GString * out;
      unsigned char outbuf[4096];

      int inflate_xzver (size_t len, char* buf);
      void on_nntp_batch_process (StringView&);
      void find_lines();

      std::stringstream s_stream; // output for inflate

  };
}

#endif
