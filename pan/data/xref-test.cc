#include <config.h>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pan/general/string-view.h>
#include <pan/general/test.h>
#include "xref.h"
#include "article.h"

using namespace pan;

int
main (void)
{
   const Quark s1 ("server1");
   const Quark s2 ("server2");
   const Quark s3 ("server3");
   const Quark s4 ("server4");
   const Quark g1 ("group1");
   const Quark g2 ("group2");
   const Quark g3 ("group3");

   /**
   ***  Add
   **/

   Xref s;
   s.insert (s1, g1, 100ul);
   check (s.size() == 1)
   s.insert (s2, g1, 200ul);
   check (s.size() == 2)
   s.insert (s3, g1, 300ul);
   check (s.size() == 3)
   s.insert (s1, g1, 300); // should fail -- already have server+group
   check (s.size() == 3)
   s.insert (s1, g2, 300);
   check (s.size() == 4)

   /**
   ***  Find Number
   **/

#if 0
   uint64_t number;
   check (s.find (s1, g1, number))
   check (number == 100)
   check (s.find (s2, g1, number))
   check (number == 200)
   check (s.find (s3, g1, number))
   check (number == 300)
   check (s.find (s1, g2, number))
   check (number == 300)
   check (!s.find (s1, g3, number))
   check (!s.find (s2, g3, number))
   check (!s.find (s4, g1, number))
#endif

   /**
   *** Remove Server
   **/

   s.remove_server (s1);
   check (s.size() == 2)
   for (Xref::targets_t::const_iterator it=s.begin(), end=s.end(); it!=end; ++it)
      check (it->server != s1)

   /**
   *** Clear
   **/

   s.clear ();
   check (s.empty())

   /**
   *** Xref: header
   **/

   StringView v ("news.netfront.net alt.zen:175094 alt.religion.buddhism:10033 talk.religion.buddhism:170169");
   s.insert ("netfront", v);
   check (s.size() == 3)
   v = "news.froody.org alt.zen:666 alt.religion.buddhism:999 talk.religion.buddhism:333";
   s.insert ("froody", v);
   check (s.size() == 6)

  quarks_t servers;
  s.get_servers (servers);
  check (servers.size()==2)
  check (servers.count("froody"))
  check (servers.count("netfront"))

  check (s.has_server ("froody"))
  check (s.has_server ("netfront"))
  check (!s.has_server ("foo"))

//for (Xref::const_iterator it(s.begin()), end(s.end()); it!=end; ++it) std::cerr << "server [" << it->server << "], group [" << it->group << "], number [" << it->number << "]" << std::endl;
  s.remove_server ("froody");
  check (s.size() == 3);
  check (!s.has_server ("froody"));
  check (s.has_server ("netfront"));

  s.remove_targets_less_than ("netfront", "alt.zen", 100000ul);
  check (s.size() == 3);
  s.remove_targets_less_than ("netfront", "alt.zen", 200000ul);
  check (s.size() == 2);

  Quark group;
  uint64_t number (0ul);
  check (s.find ("netfront", group, number))
  check (number!=175094)
  check (number==10033 || number==107169)
  check (number==10033 ? group=="alt.religion.buddhism" : group=="talk.religion.buddhism")

  Xref x;
  x.insert (s);
  x.insert (s);
  check (x.size() == 2)

  return 0;
}
