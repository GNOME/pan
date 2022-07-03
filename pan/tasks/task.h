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

#ifndef __TASK_H__
#define __TASK_H__

#include <pan/general/map-vector.h>
#include <pan/general/progress.h>
#include <pan/general/quark.h>
#include <pan/general/sorted-vector.h>
#include <pan/tasks/health.h>
#include <pan/tasks/nntp.h>

namespace pan
{
   class Decoder;
   class Encoder;

   /**
    * Base class for jobs that require NNTP interaction to be completed.
    * These wait their turn in the queue until an NNTP connection is available.
    *
    * @see NNTP
    * @see Queue
    * @ingroup tasks
    */
   class Task: public Progress
   {
      public:

         /**
          * Possible work states of a Task.
          */
         enum Work
         {
            /** Task finished successfully */
            COMPLETED = 0,
            /** Task is waiting on an nntp connection */
            NEED_NNTP = 1,
            /** Task waiting for a decoder/encoder */
            NEED_DECODER = 2,
            NEED_ENCODER = 3,
            /** Task is running */
            WORKING = 5,
            INITIAL = 6
         };

         /**
          * Work and Health states of a particular task.
          */
         struct State
         {
            public:

               /** What does this task need to do next? */
               Work _work;

               /** What is this task's health? */
               Health _health;

               /** when _work is NEED_NNTP, we can use any of these servers */
               typedef sorted_vector<Quark,true> unique_servers_t;
               unique_servers_t _servers;

            public:

               void set_completed () {
                   _work = COMPLETED; _servers.clear(); }

               void set_initial () {
                   _work = INITIAL; _servers.clear(); }

               void set_working () {
                  _work = WORKING; _servers.clear(); }

               void set_need_nntp (const quarks_t& servers) {
                  _work=NEED_NNTP; _servers.get_container().assign(servers.begin(),servers.end()); }

               void set_need_nntp (const Quark& server) {
                  _work=NEED_NNTP; _servers.clear(); _servers.insert(server); }

               void set_need_decoder () {
                   _work = NEED_DECODER; _servers.clear(); }

               void set_need_encoder () {
                   _work = NEED_ENCODER; _servers.clear(); }

               void set_health (Health h) {
                  _health = h; }

            public:

               //Note: The _work member used to be uninitialised, so depending
               //on pretty much anything, it could start in ANY of the states,
               //or none. For now I'm going to assume it starts in completed
               //state, but it's possible that if the value isn't immediately
               //set, the task could be reaped early in Queue::process_task
               State():
                _work(COMPLETED), //? guesswork
                _health(OK)
               {}
         };

      public:

         /** Loan the task a NNTP connection */
         void give_nntp (NNTP::Source*, NNTP* nntp);

         struct DecoderSource {
           virtual ~DecoderSource() {}
           virtual void check_in (Decoder*, Task*) = 0;
         };
         struct EncoderSource {
           virtual ~EncoderSource() {}
           virtual void check_in (Encoder*, Task*) = 0;
         };

         /** Loan the task a Decoder */
         void give_decoder (DecoderSource*, Decoder*);

         void give_encoder (EncoderSource*, Encoder*);

      public:

         Task (const Quark& type, const StringView& description);
         virtual ~Task ();

         const State& get_state () const { return _state; }

         State& get_state () { return _state; }

         const Quark& get_type () const { return _type; }

         virtual unsigned long get_bytes_remaining () const = 0;

         /// stop a running task
         virtual void stop () { }

         /// wakeup a sleeping task
         virtual void wakeup() {}

      protected:

         State _state;

         virtual void use_nntp (NNTP*) = 0;

         void check_in (NNTP*, Health);

         int get_nntp_count () const { return _nntp_to_source.size(); }

         virtual void use_decoder (Decoder*);
         virtual void use_encoder (Encoder*);

         void check_in (Decoder*);
         void check_in (Encoder*);

      private:

         /** What type this task is ("XOVER", "POST", "SAVE", "BODIES", etc...) */
         const Quark _type;

         /** typedef for _nntp_to_source */
         typedef Loki::AssocVector<NNTP*,NNTP::Source*> nntp_to_source_t;
         /** used in check_in() to remember where the nntp is to be returned */
         nntp_to_source_t _nntp_to_source;

         typedef Loki::AssocVector<Decoder*,DecoderSource*> decoder_to_source_t;
         typedef Loki::AssocVector<Encoder*,EncoderSource*> encoder_to_source_t;
         /** used in check_in() to remember where the decoder/encoder is to be returned */
         decoder_to_source_t _decoder_to_source;
         encoder_to_source_t _encoder_to_source;
   };
}

#endif
