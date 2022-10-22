#include <config.h>
#include <iostream>
#include "progress.h"
#include "string-view.h"
#include "test.h"

using namespace pan;

class MyListener : public Progress::Listener
{
   public:
      std::string status;
      std::string error;
      int percentage;
      int finished;
      void on_progress_step (Progress&, int p) override {
         percentage = p;
      }
      void on_progress_status (Progress&, const StringView& s) override {
         status = s.to_string ();
      }
      void on_progress_error (Progress&, const StringView& s) override {
         error = s.to_string ();
      }
      void on_progress_finished (Progress&, int s) override {
         finished = s;
      }
};



int
main (void)
{
   const std::string description ("this is the description");
   Progress p (description);
   MyListener myl;
   p.add_listener (&myl);
   check (p.describe() == description)
   check (p.get_progress_of_100() == 0)

   std::string s = "Hello World";
   p.set_status (s);
   check (myl.status == s)

   s = "This is an error message";
   p.set_error (s);
   check (myl.error == s)

   p.init_steps (100);
   check (p.get_progress_of_100() == 0)
   check (myl.percentage == 0)
   p.increment_step ();
   check (p.get_progress_of_100() == 1)
   check (myl.percentage == 1)
   p.increment_step ();
   check (p.get_progress_of_100() == 2)
   check (myl.percentage == 2)
   p.increment_step ();
   check (p.get_progress_of_100() == 3)
   check (myl.percentage == 3)
   p.set_step (50);
   check (p.get_progress_of_100() == 50)
   check (myl.percentage == 50)

   p.init_steps (200);
   check (p.get_progress_of_100() == 0)
   check (myl.percentage == 0)
   p.set_step (50);
   check (p.get_progress_of_100() == 25)
   check (myl.percentage == 25)

   p.set_finished (2);
   check (myl.finished == 2)

   p.remove_listener (&myl);
   p.init_steps (100);
   check (p.get_progress_of_100() == 0)
   check (myl.percentage == 25) // unchanged -- no listener

   // all clear
   return 0;
}
