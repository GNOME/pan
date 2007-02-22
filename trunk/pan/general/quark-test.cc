#include <config.h>
#include <iostream>
#include <map>
#include "quark.h"
#include "test.h"

using namespace pan;

int
main (void)
{
	{
		Quark o ("foo");
		{
			Quark a (o);
			Quark b (o);
			check (a == b)
			check (a == a)
			check (a == "foo")
			check (Quark::size() == 1)
			Quark c ("bar");
			check (a != c)
			check (b != c)
			check (Quark::size() == 2)
			a = c;
			check (Quark::size() == 2)
			check (a != b)
			check (a == c)
			c = "mum";
			check (Quark::size() == 3)
			o = c;

			std::map<Quark,std::string> mymap;
			mymap[o] = o;
			check (mymap[c] == o.to_string())
			check (o.to_string() == mymap[c])
			check (c.to_string() == o.to_string())
                        check (Quark::size() == 3)
                        c = "gronk";
                        check (Quark::size() == 4)
		}
        	check (Quark::size() == 1)
		check (o == "mum");
	}
        check (Quark::size() == 0)

	return 0;
}
