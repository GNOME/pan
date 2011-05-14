#include <config.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "string-view.h"
#include "test.h"

using namespace pan;

int
main (void)
{
  const char * cpch;
  StringView a, b;

  // set str
  cpch = "Hello World!";
  a.assign (cpch);
  check (a.str == cpch)
  check (a.len == strlen(cpch))

  // set str + len
  a.assign (cpch, 4);
  check (a.str == cpch)
  check (a.len == 4)

  /**
  ***  strstr
  **/

  a = "Grand Canyon Sunset.jpg (1/2)";
  check (a.strstr("Grand") == a.str)
  check (a.strstr("Canyon") == a.str+6)
  check (a.strstr("anyon Sun") == a.str+7)
  check (a.strstr("anyon sun") == NULL)

  a.assign ("Looking for overruns", 5);
  check (a.strstr("Looking") == NULL)
  check (a.strstr("for overruns") == NULL)
  check (a.strstr("Look") == a.str)
  check (a.strstr("ook") == a.str+1)

  /**
  ***  pop_token
  **/

  a = "Grand Canyon Sunset.jpg (1/2)";
  check (a.pop_token (b, ' '))
  check (b == "Grand")
  check (a == "Canyon Sunset.jpg (1/2)")
  check (a.pop_token (b, ' '))
  check (b == "Canyon")
  check (a == "Sunset.jpg (1/2)")
  check (a.pop_token (b, ' '))
  check (b == "Sunset.jpg")
  check (a == "(1/2)")
  check (a.pop_token (b, ' '))
  check (b == "(1/2)")
  check (a.empty())
  check (!a.pop_token (b, ' '))

        /**
        ***  pop_last_token
        **/

  a = "<1109881740.966093.167850@l41g2000cwc.googlegroups.com> <38psfvF5oka9bU3@individual.net> <mbPVd.53488$uc.51147@trnddc08> <38q14bF5trum0U1@individual.net> <N6idne16pKkb2rTfRVn-oA@megapath.net> <38tobgF5ra6jaU1@individual.net>";
  check (a.pop_last_token (b))
  check (b == "<38tobgF5ra6jaU1@individual.net>")
  check (a.pop_last_token (b))
  check (b == "<N6idne16pKkb2rTfRVn-oA@megapath.net>")
  check (a.pop_last_token (b))
  check (b == "<38q14bF5trum0U1@individual.net>")
  check (a.pop_last_token (b))
  check (b == "<mbPVd.53488$uc.51147@trnddc08>")
  check (a.pop_last_token (b))
  check (b == "<38psfvF5oka9bU3@individual.net>")
  check (a.pop_last_token (b))
  check (b == "<1109881740.966093.167850@l41g2000cwc.googlegroups.com>")
  check (!a.pop_last_token (b))

  a = "asfd";
  check (a.pop_last_token(b));
  check (a.empty())
  check (b == "asfd");
  a.clear ();
  check (a.empty())
  check (!a.pop_last_token(b));

  /**
  ***  Trim
  **/

  a = "  Hello World!!   ";
  a.trim ();
  check (a == "Hello World!!")
  a = "       (*#()#*&#* Hello #$ *#($(# ??     ";
  a.trim ();
  check (a == "(*#()#*&#* Hello #$ *#($(# ??")
  a = "      ";
  a.trim ();
  check (a.empty());
  a = "no-spaces";
  a.trim ();
  check (a == "no-spaces");
  a = "middle space";
  a.trim ();
  check (a == "middle space");

  /**
  ***  Clear
  **/

  a.clear ();
  check (!a.str)
  check (!a.len)
  check (a.empty())

  /**
  ***  strchr
  **/

  std::string s ("abcdefghijklmnopqrstuvwxyz");
  a.assign (s.c_str(), s.size());
  char * p = a.strchr ('a');
  check (p)
  check (*p == 'a')
  check (a.strchr ('e', 4))
  check (!a.strchr ('e', 5))
  p = a.strchr ('e');
  check (p)
  check (*p == 'e')
  a.assign (s.c_str(), 4);
  check (!a.strchr ('e'))
  check (!a.strchr ('e', 2))

  return 0;
}
