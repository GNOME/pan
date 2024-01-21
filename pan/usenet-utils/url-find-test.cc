#include <config.h>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <pan/general/test.h>
#include "url-find.h"

using namespace pan;

int
main (void)
{
  const char * in;
  StringView out;

  in = "and this is a string with http://www.gtk.org/ i wonder if it will work.";
  check (url_find (in, out))
  check (out == "http://www.gtk.org/")

  in = "I am going to end this sentence with a URL: http://www.gtk.org/.";
  check (url_find (in, out))
  check (out == "http://www.gtk.org/")

  in = "Have you ever visited https://www.google.com/?";
  check (url_find (in, out))
  check (out == "https://www.google.com/")

  in = "la la lawww.google.com sfadf";
  check (url_find (in, out))
  check (out == "www.google.com")

  in = "My email address is still charles@rebelbase.com.";
  check (url_find (in, out))
  check (out == "charles@rebelbase.com")

  in = "Go visit ftps://ftp.gnome.org and get cool software!";
  check (url_find (in, out))
  check (out == "ftps://ftp.gnome.org")

  in = "Go visit ftp.gnome.org and get cool software!";
  check (url_find (in, out))
  check (out == "ftp.gnome.org")

  in = "I am going to run this to the end of the string http://www.goo";
  check (url_find (in, out))
  check (out == "http://www.goo")

  in = "but, there is no url here.";
  check (!url_find (in, out))

  in = "All the instant reviews are at news:alt.religion.kibology!  Read 'em today!";
  check (url_find (in, out))
  check (out == "news:alt.religion.kibology")

  in = "Here is a link sent in by Henri Naccache: http://cgi.ebay.com/ws/eBayISAPI.dll?ViewItem&rd=1,1&item=6270812078 and that is the string.";
  check (url_find (in, out))
  check (out == "http://cgi.ebay.com/ws/eBayISAPI.dll?ViewItem&rd=1,1&item=6270812078")

  in = "Here is my email address: charles@rebelbase.com.  Did you get it?";
  check (url_find (in, out))
  check (out == "charles@rebelbase.com")

  in = "Here is my email address: charles@rebelbase.com  Did you get it?";
  check (url_find (in, out))
  check (out == "charles@rebelbase.com")

  in = "blah blah <http://www.dobreprogramy.pl/pirat/dobreprogramy_pl(piratXXX).jpg#frag>, only the";
  check (url_find (in, out))
  check (out == "http://www.dobreprogramy.pl/pirat/dobreprogramy_pl(piratXXX).jpg#frag")

  in = "Here is my email address: lost.foo_bar@rebelbase.com  Did you get it?";
  check (url_find (in, out))
  check (out == "lost.foo_bar@rebelbase.com")

  in = "A URl 'http://www.www.com'?";
  check (url_find (in, out))
  check (out == "http://www.www.com")

  in = "Sample gemini link: gemini://example.com";
  check (url_find (in, out))
  check (out == "gemini://example.com")

  in = "Second gemini link: gemini://example.com/test/foo";
  check (url_find (in, out))
  check (out == "gemini://example.com/test/foo")

  in = "Third gemini link: gemini://example.com/test/foo.gmi";
  check (url_find (in, out))
  check (out == "gemini://example.com/test/foo.gmi")

  in = "Fourth gemini link: gemini://example.com/test/foo.gmi and the last one";
  check (url_find (in, out))
  check (out == "gemini://example.com/test/foo.gmi")

  // success
  return 0;
}
