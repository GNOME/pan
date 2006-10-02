#include <config.h>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pan/general/test.h>
#include "numbers.h"

using namespace pan;

int
main (void)
{
	Numbers n;

	n.mark_str ("0-1641");
	check (n.to_string() == "0-1641")
        n.mark_range (0, 1586, false);
std::cerr << "n.to_string() == " << n.to_string() << std::endl;
        check (n.to_string() == "1587-1641")

	/* simple test */
        n.clear ();
	n.mark_str ("1-20,100,102,200-300");
	check (n.is_marked (1))

	/* testing boundaries */
	check (n.is_marked (19))
	check (n.is_marked (20))
	check (!n.is_marked (21))

	/* more boundary testing */
	check ( n.is_marked (100))
	check (!n.is_marked (101))
	check ( n.is_marked (102))

	/* make sure marking read doesn't corrupt boundaries */
	bool b = n.mark_one (101);
	check (!b)
	check (!n.is_marked ( 99))
	check ( n.is_marked (100))
	check ( n.is_marked (101))
	check ( n.is_marked (102))
	check (!n.is_marked (103))

	/* make sure marking unread doesn't corrupt boundaries */
	b = n.mark_one (101, false);
	check (b)
	check (!n.is_marked (99))
	check ( n.is_marked (100))
	check (!n.is_marked (101))
	check ( n.is_marked (102))
	check (!n.is_marked (103))

	/* newsrc_get_read_str */
        n.clear ();
	n.mark_str ("1-20,100,102,200-300");
	check (n.to_string() == "1-20,100,102,200-300")
	b = n.mark_one (15, false);
	check (b!=false)
	check (n.to_string() == "1-14,16-20,100,102,200-300")
	b = n.mark_one (101);
	check (b==false)
	check (n.to_string() == "1-14,16-20,100-102,200-300")
	b = n.mark_one (103);
	check (b==false)
	check (n.to_string() == "1-14,16-20,100-103,200-300")
	b = n.mark_one (105);
	check (b==false)
	check (n.to_string() == "1-14,16-20,100-103,105,200-300")

	/* newsrc_mark_range */
	n.clear ();
	n.mark_str ("1-1000");
	int i = n.mark_range (100, 199, false);
	check (i==100);
	check (n.to_string() == "1-99,200-1000")
	i = n.mark_range (100, 199);
	check (i==100)
	check (n.to_string() == "1-1000")

	n.clear ();
        n.mark_str ("1-500");
	i = n.mark_range (400, 800);
	check (i==300)
	check (n.to_string() == "1-800")

	n.clear ();
        n.mark_str ("250-500");
	i = n.mark_range (100, 400);
	check (i==150)
	check (n.to_string() == "100-500")

	n.clear ();
        n.mark_str ("250-500");
	i = n.mark_range (100, 600);
	check (i==250)
	check (n.to_string() == "100-600")

	n.clear ();
        n.mark_str ("100-500");
	i = n.mark_range (50, 600, false);
	check (i==401)
	check (n.to_string().empty())

	n.clear ();
        n.mark_str ("100-500");
	i = n.mark_range (50, 200, false);
	check (i==101)
	check (n.to_string() == "201-500")

	n.clear ();
        n.mark_str ("100-500");
	i = n.mark_range (400, 600, false);
	check (i==101)
	check (n.to_string() == "100-399")

	n.clear ();
        n.mark_str ("1-50,100-150,200-250,300-350");
	i = n.mark_range (75, 275, false);
	check (i==102)
	check (n.to_string() == "1-50,300-350")

	n.clear ();
        n.mark_str ("1-50,100-150,200-250,300-350");
	i = n.mark_range (75, 300, false);
	check (i==103)
	check (n.to_string() == "1-50,301-350")

	n.clear ();
        n.mark_str ("250-500");
	i = n.mark_range (600, 700, false);
	check (i==0)
	check (n.to_string() == "250-500")
	i = n.mark_range (50, 200, false);
	check (i==0)
	check (n.to_string() == "250-500")

	n.clear ();
        n.mark_str ("1-498,500-599,601-1000");
	i = n.mark_range (500, 599, false);
	check (i==100)
	check (n.to_string() == "1-498,601-1000")

	n.clear ();
        n.mark_str ("1-498,500-599,601-1000");
	i = n.mark_range (498, 601, false);
	check (i==102)
	check (n.to_string() == "1-497,602-1000")

	n.clear ();
        n.mark_str ("1-498,500-599,601-1000");
	i = n.mark_range (499, 601, false);
	check (i==101)
	check (n.to_string() == "1-498,602-1000")

	// Janus Sandsgaard reported this one against 0.9.7pre5
	n.clear ();
        n.mark_str ("17577-17578,17581-17582,17589-17590");
	i = n.mark_range (17578, 17578);
	check (i==0)
	check (n.to_string() == "17577-17578,17581-17582,17589-17590")

	// Jiri Kubicek reported this one against 0.11.2.90
	n.clear ();
	n.mark_range (1, 4);
	n.mark_range (7, 9);
	n.mark_range (3, 4, false);
	check (n.to_string() == "1-2,7-9")

	// found this during a code review for 0.12.90
	n.clear ();
	n.mark_str ("1-75, 100-120, 150-200");
	i = n.mark_range (50, 175);
	check (i==53)
	check (n.to_string() == "1-200")

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

	// http://bugzilla.gnome.org/show_bug.cgi?id=77878
	n.clear ();
	n.mark_range (1, 4);
	n.mark_range (7, 9);
	n.mark_range (3, 4, false);
	check (n.to_string() == "1-2,7-9")

	// success
	return 0;
}
