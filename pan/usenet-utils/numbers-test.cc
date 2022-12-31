#include "numbers.h"
#include <config.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <pan/general/test.h>

using namespace pan;

int main(void)
{
  Numbers n;

  n.mark_str("0-1641");
  check(n.to_string() == "0-1641")
  n.mark_range(
    static_cast<Article_Number>(0), static_cast<Article_Number>(1586), false);
  std::cerr << "n.to_string() == " << n.to_string() << std::endl;
  check(n.to_string() == "1587-1641")

  /* simple test */
  n.clear();
  n.mark_str("1-20,100,102,200-300");
  check(n.is_marked(static_cast<Article_Number>(1)))

  /* testing boundaries */
  check(n.is_marked(static_cast<Article_Number>(19)))
  check(n.is_marked(static_cast<Article_Number>(20)))
  check(! n.is_marked(static_cast<Article_Number>(21)))

  /* more boundary testing */
  check(n.is_marked(static_cast<Article_Number>(100)))
  check(! n.is_marked(static_cast<Article_Number>(101)))
  check(n.is_marked(static_cast<Article_Number>(102)))

  /* make sure marking read doesn't corrupt boundaries */
  bool b = n.mark_one(static_cast<Article_Number>(101));
  check(! b)
  check(! n.is_marked(static_cast<Article_Number>(99)))
  check(n.is_marked(static_cast<Article_Number>(100)))
  check(n.is_marked(static_cast<Article_Number>(101)))
  check(n.is_marked(static_cast<Article_Number>(102)))
  check(! n.is_marked(static_cast<Article_Number>(103)))

  /* make sure marking unread doesn't corrupt boundaries */
  b = n.mark_one(static_cast<Article_Number>(101), false);
  check(b)
  check(! n.is_marked(static_cast<Article_Number>(99)))
  check(n.is_marked(static_cast<Article_Number>(100)))
  check(! n.is_marked(static_cast<Article_Number>(101)))
  check(n.is_marked(static_cast<Article_Number>(102)))
  check(! n.is_marked(static_cast<Article_Number>(103)))

  /* newsrc_get_read_str */
  n.clear();
  n.mark_str("1-20,100,102,200-300");
  check(n.to_string() == "1-20,100,102,200-300")
  b = n.mark_one(static_cast<Article_Number>(15), false);
  check(b != false)
  check(n.to_string() == "1-14,16-20,100,102,200-300")
  b = n.mark_one(static_cast<Article_Number>(101));
  check(b == false)
  check(n.to_string() == "1-14,16-20,100-102,200-300")
  b = n.mark_one(static_cast<Article_Number>(103));
  check(b == false)
  check(n.to_string() == "1-14,16-20,100-103,200-300")
  b = n.mark_one(static_cast<Article_Number>(105));
  check(b == false)
  check(n.to_string() == "1-14,16-20,100-103,105,200-300")

  /* newsrc_mark_range */
  n.clear();
  n.mark_str("1-1000");
  Article_Count i = n.mark_range(
    static_cast<Article_Number>(100), static_cast<Article_Number>(199), false);
  check(i == static_cast<Article_Count>(100));
  check(n.to_string() == "1-99,200-1000")
  i = n.mark_range(static_cast<Article_Number>(100),
                   static_cast<Article_Number>(199));
  check(i == static_cast<Article_Count>(100))
  check(n.to_string() == "1-1000")

  n.clear();
  n.mark_str("1-500");
  i = n.mark_range(static_cast<Article_Number>(400),
                   static_cast<Article_Number>(800));
  check(i == static_cast<Article_Count>(300))
  check(n.to_string() == "1-800")

  n.clear();
  n.mark_str("250-500");
  i = n.mark_range(static_cast<Article_Number>(100),
                   static_cast<Article_Number>(400));
  check(i == static_cast<Article_Count>(150))
  check(n.to_string() == "100-500")

  n.clear();
  n.mark_str("250-500");
  i = n.mark_range(static_cast<Article_Number>(100),
                   static_cast<Article_Number>(600));
  check(i == static_cast<Article_Count>(250))
  check(n.to_string() == "100-600")

  n.clear();
  n.mark_str("100-500");
  i = n.mark_range(
    static_cast<Article_Number>(50), static_cast<Article_Number>(600), false);
  check(i == static_cast<Article_Count>(401))
  check(n.to_string().empty())

  n.clear();
  n.mark_str("100-500");
  i = n.mark_range(
    static_cast<Article_Number>(50), static_cast<Article_Number>(200), false);
  check(i == static_cast<Article_Count>(101))
  check(n.to_string() == "201-500")

  n.clear();
  n.mark_str("100-500");
  i = n.mark_range(
    static_cast<Article_Number>(400), static_cast<Article_Number>(600), false);
  check(i == static_cast<Article_Count>(101))
  check(n.to_string() == "100-399")

  n.clear();
  n.mark_str("1-50,100-150,200-250,300-350");
  i = n.mark_range(
    static_cast<Article_Number>(75), static_cast<Article_Number>(275), false);
  check(i == static_cast<Article_Count>(102))
  check(n.to_string() == "1-50,300-350")

  n.clear();
  n.mark_str("1-50,100-150,200-250,300-350");
  i = n.mark_range(
    static_cast<Article_Number>(75), static_cast<Article_Number>(300), false);
  check(i == static_cast<Article_Count>(103))
  check(n.to_string() == "1-50,301-350")

  n.clear();
  n.mark_str("250-500");
  i = n.mark_range(
    static_cast<Article_Number>(600), static_cast<Article_Number>(700), false);
  check(i == static_cast<Article_Count>(0))
  check(n.to_string() == "250-500")
  i = n.mark_range(
    static_cast<Article_Number>(50), static_cast<Article_Number>(200), false);
  check(i == static_cast<Article_Count>(0))
  check(n.to_string() == "250-500")

  n.clear();
  n.mark_str("1-498,500-599,601-1000");
  i = n.mark_range(
    static_cast<Article_Number>(500), static_cast<Article_Number>(599), false);
  check(i == static_cast<Article_Count>(100))
  check(n.to_string() == "1-498,601-1000")

  n.clear();
  n.mark_str("1-498,500-599,601-1000");
  i = n.mark_range(
    static_cast<Article_Number>(498), static_cast<Article_Number>(601), false);
  check(i == static_cast<Article_Count>(102))
  check(n.to_string() == "1-497,602-1000")

  n.clear();
  n.mark_str("1-498,500-599,601-1000");
  i = n.mark_range(
    static_cast<Article_Number>(499), static_cast<Article_Number>(601), false);
  check(i == static_cast<Article_Count>(101))
  check(n.to_string() == "1-498,602-1000")

  // Janus Sandsgaard reported this one against 0.9.7pre5
  n.clear();
  n.mark_str("17577-17578,17581-17582,17589-17590");
  i = n.mark_range(static_cast<Article_Number>(17578),
                   static_cast<Article_Number>(17578));
  check(i == static_cast<Article_Count>(0))
  check(n.to_string() == "17577-17578,17581-17582,17589-17590")

  // Jiri Kubicek reported this one against 0.11.2.90
  n.clear();
  n.mark_range(static_cast<Article_Number>(1), static_cast<Article_Number>(4));
  n.mark_range(static_cast<Article_Number>(7), static_cast<Article_Number>(9));
  n.mark_range(
    static_cast<Article_Number>(3), static_cast<Article_Number>(4), false);
  check(n.to_string() == "1-2,7-9")

  // found this during a code review for 0.12.90
  n.clear();
  n.mark_str("1-75, 100-120, 150-200");
  i = n.mark_range(static_cast<Article_Number>(50),
                   static_cast<Article_Number>(175));
  check(i == static_cast<Article_Count>(53))
  check(n.to_string() == "1-200")

#if 0
	// newsrc_set_group_range
	// note that after a mark all, the lower bound is 0.
	// this follows newsrc conventions.
	n.clear ();
	n.mark_str ("1-20,100,102,200-300");
	n.set_range (15, 220);
	check (n.to_string() == "15-20,100,102,200-220")
	n.set_range (50, 219);
	check (n.to_string() == "100,102,200-219")
	n.set_range (10, 1000);
	check (n.to_string() == "100,102,200-219")
	n.mark_all (true);
	check (n.to_string() == "0-1000")
#endif

  // https://bugzilla.gnome.org/show_bug.cgi?id=77878
  n.clear();
  n.mark_range(static_cast<Article_Number>(1), static_cast<Article_Number>(4));
  n.mark_range(static_cast<Article_Number>(7), static_cast<Article_Number>(9));
  n.mark_range(
    static_cast<Article_Number>(3), static_cast<Article_Number>(4), false);
  check(n.to_string() == "1-2,7-9")

  // success
  return 0;
}
