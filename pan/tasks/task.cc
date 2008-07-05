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

#include <config.h>
#include <pan/general/debug.h>
#include <pan/general/messages.h>
#include "task.h"

using namespace pan;

Task :: Task (const Quark& type, const StringView& description): Progress(description), _type(type)
{
}

Task :: ~Task ()
{
}

/***
****  Socket Handling
***/

void
Task :: give_nntp (NNTP::Source * source, NNTP* nntp)
{
  _nntp_to_source[nntp] = source;
  debug ("gave nntp " << nntp->_server << " (" << nntp << ") to task " << this << ", which now has " << _nntp_to_source.size() << " nntps");
  use_nntp (nntp);
}

void
Task :: check_in (NNTP * nntp, Health health)
{
   debug ("task " << this << " returning nntp " << nntp);

   nntp_to_source_t::iterator it = _nntp_to_source.find (nntp);
   if (it != _nntp_to_source.end())
   {
      NNTP::Source * source = it->second;
      _nntp_to_source.erase (nntp);
      debug ("returned nntp " << nntp << " OK; task " << this << " now has " << _nntp_to_source.size() << " nntps");

      source->check_in (nntp, health);
   }
}

/***
****
***/

void
Task :: give_decoder (DecoderSource* s, Decoder* d)
{
  _decoder_to_source[d] = s;
  use_decoder (d);
}
void
Task :: use_decoder (Decoder * d UNUSED)
{
  abort ();
}
void
Task :: check_in (Decoder * d)
{
  decoder_to_source_t::iterator it (_decoder_to_source.find (d));

  if (it != _decoder_to_source.end())
  {
    DecoderSource * s (it->second);
    _decoder_to_source.erase (d);
    s->check_in (d, this);
  }
}
