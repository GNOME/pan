#include <config.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <gmime/gmime.h>
#include <pan/general/debug.h>
#include <pan/general/string-view.h>
#include <pan/general/quark.h>
#include <pan/general/test.h>
#include "gnksa.h"
#include "message-check.h"

using namespace pan;

#define PRINT_ERRORS \
  if (1) { \
    int i = 0; \
    for (MessageCheck::unique_strings_t::const_iterator it(errors.begin()), end(errors.end()); it!=end; ++it, ++i) \
      std::cerr << LINE_ID << " [" << i << "][" << *it << ']' << std::endl; \
  }

#define CHARSET "UTF-8"

static void
mime_part_set_content (GMimePart *part, const char *str)
{
	GMimeDataWrapper *content;
	GMimeStream *stream;
	
	stream = g_mime_stream_mem_new_with_buffer (str, strlen (str));
	content = g_mime_data_wrapper_new_with_stream (stream, GMIME_CONTENT_ENCODING_DEFAULT);
	g_object_unref (stream);
	
	g_mime_part_set_content (part, content);
	g_object_unref (content);
}

int main ()
{
  g_mime_init ();

  MessageCheck::unique_strings_t errors;
  MessageCheck::Goodness goodness;

  quarks_t groups_our_server_has;
  groups_our_server_has.insert ("alt.test");
  groups_our_server_has.insert ("alt.religion.kibology");
  groups_our_server_has.insert ("alt.binaries.sounds.mp3.indie");

  // populate a simple article
  std::string attribution ("Someone wrote");
  GMimeMessage * msg = g_mime_message_new (FALSE);
  //g_mime_message_set_sender (msg, "\"Charles Kerr\" <charles@rebelbase.com>");
  g_mime_message_add_mailbox(msg,GMIME_ADDRESS_TYPE_SENDER, NULL, "\"Charles Kerr\" <charles@rebelbase.com>" );
  std::string message_id = GNKSA :: generate_message_id ("rebelbase.com");
  g_mime_message_set_message_id (msg, message_id.c_str());
  g_mime_message_set_subject (msg, "MAKE MONEY FAST", CHARSET);
  g_mime_object_set_header ((GMimeObject *) msg, "Organization", "Lazars Android Works", CHARSET);
  g_mime_object_set_header ((GMimeObject *) msg, "Newsgroups", "alt.test", CHARSET);
  GMimePart * part = g_mime_part_new_with_type ("text", "plain");
  const char * cpch = "Hello World!";
  mime_part_set_content (part, cpch);
  g_mime_message_set_mime_part (msg, GMIME_OBJECT(part));
  // this should pass the tests
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  printf("errors: «%s»", errors);
  check (errors.empty())
  check (goodness.is_ok())

  // all quoted
  cpch = "> Hello World!\n> All quoted text.";
  mime_part_set_content (part, cpch);
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  std::vector<std::string> e (errors.begin(), errors.end());
  check (errors.size() == 2)
  check (goodness.is_refuse())
  check (e[0] == "Error: Message appears to have no new content.");
  check (e[1] == "Warning: The message is entirely quoted text!");

  // mostly quoted
  cpch = "> Hello World!\n> quoted\n> text\n> foo\n> bar\nnew text";
  mime_part_set_content (part, cpch);
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  e.assign (errors.begin(), errors.end());
  check (errors.size() == 1)
  check (goodness.is_warn())
  check (e[0] == "Warning: The message is mostly quoted text.")

  // mostly quoted border condition: 20% of message is new content (should pass)
  cpch = "> Hello World!\n> quoted\n> text\n> foo\nnew text";
  mime_part_set_content (part, cpch);
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  check (errors.empty())
  check (goodness.is_ok())

  // sig check: too long
  cpch = "Hello!\n\n-- \nThis\nSig\nIs\nToo\nLong\n";
  mime_part_set_content (part, cpch);
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  e.assign (errors.begin(), errors.end());
  check (errors.size() == 1)
  check (goodness.is_warn())
  check (e[0] == "Warning: Signature is more than 4 lines long.");

  // sig check: too wide
  cpch = "Hello!\n"
         "\n"
         "-- \n"
         "This sig line is exactly 80 characters wide.  I'll keep typing until I reach 80.\n"
         "This sig line is greater than 80 characters wide.  In fact, it's 84 characters wide.\n"
         "This sig line is greater than 80 characters wide.  In fact, it measures 95 characters in width!\n"
         "This sig line is less than 80 characters wide.";
  mime_part_set_content (part, cpch);
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  e.assign (errors.begin(), errors.end());
  check (errors.size() == 1)
  check (goodness.is_warn())
  check (e[0] == "Warning: Signature is more than 80 characters wide.");

  // sig check: sig marker, no sig
  cpch = "Hello!\n\n-- \n";
  mime_part_set_content (part, cpch);
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  e.assign (errors.begin(), errors.end());
  check (errors.size() == 1)
  check (goodness.is_warn ())
  check (e[0] == "Warning: Signature prefix with no signature.");

  // sig check: okay sig
  cpch = "Hello!\n\n-- \nThis is a short, narrow sig.\nIt should pass.\n";
  mime_part_set_content (part, cpch);
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  check (errors.empty())
  check (goodness.is_ok())

  // adrian's fake followup
  cpch = ">>>>>>>>>>>> I think A\n"
         ">>>>>>>>>>> No, it's not\n"
         ">>>>>>>>>> But B => C\n"
         ">>>>>>>>> What's that got to do with A?\n"
         ">>>>>>>> I still think B => C\n"
         ">>>>>>> It's not even B => C. But Still waiting for proof for A\n"
         ">>>>>> You don't prove !A, either.\n"
         ">>>>> There's the FAQ: X => !A and Y => !A\n"
         ">>>> But there in the text it sais T' => A\n"
         ">>> But T' is only a subset of T. T => !A.\n"
         ">> Moron\n"
         "> Jackass.\n"
         "\n"
         "I don't know wether I am amused or annoyed. Apparently the funny side\n"
         "prevailed so far, as I'm still reading.\n"
         "\n"
         "-- vbi";
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  check (errors.empty())
  check (goodness.is_ok())

  // body too wide
  cpch = "Hello!\n"
         "This sig line is exactly 80 characters wide.  I'll keep typing until I reach 80.\n"
         "This sig line is greater than 80 characters wide.  In fact, it's 84 characters wide.\n"
         "This sig line is greater than 80 characters wide.  In fact, it measures 95 characters in width!\n"
         "This sig line is less than 80 characters wide.";
  mime_part_set_content (part, cpch);
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  e.assign (errors.begin(), errors.end());
  check (errors.size() == 1)
  check (goodness.is_warn())
  check (e[0] == "Warning: 2 lines are more than 80 characters wide.");

  // body empty
  cpch = "\n\t\n   \n-- \nThis is the sig.";
  mime_part_set_content (part, cpch);
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  e.assign (errors.begin(), errors.end());
  check (errors.size() == 2)
  check (goodness.is_refuse())
  check (e[0] == "Error: Message appears to have no new content.");
  check (e[1] == "Error: Message is empty.");
  cpch = "Some valid message.";
  mime_part_set_content (part, cpch);

  // empty subject
  g_mime_message_set_subject (msg, "", CHARSET);
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  e.assign (errors.begin(), errors.end());
  check (errors.size() == 1)
  check (goodness.is_refuse())
  check (e[0] == "Error: No Subject specified.");
  g_mime_message_set_subject (msg, "Happy Lucky Feeling", CHARSET);

  // newsgroups
 g_mime_object_set_header ((GMimeObject *) msg, "Newsgroups", "alt.test,unknown.group", CHARSET);
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  e.assign (errors.begin(), errors.end());
  check (errors.size() == 1)
  check (goodness.is_warn())
  check (e[0] == "Warning: The posting profile's server doesn't carry newsgroup\n\t\"unknown.group\".\n\tIf the group name is correct, switch profiles in the \"From:\"\n\tline or edit the profile with \"Edit|Manage Posting Profiles\".")
	  g_mime_object_set_header ((GMimeObject *) msg, "Newsgroups", "alt.test", CHARSET);

  // newsgroups w/o followup
  g_mime_object_set_header ((GMimeObject *) msg, "Newsgroups", "alt.test,alt.religion.kibology,alt.binaries.sounds.mp3.indie", CHARSET);
  g_mime_header_list_remove (GMIME_OBJECT(msg)->headers, "Followup-To");
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  e.assign (errors.begin(), errors.end());
  check (errors.size() == 1)
  check (goodness.is_warn())
  check (e[0] == "Warning: Crossposting without setting Followup-To header.")

  // unknown follow-up
    g_mime_object_set_header ((GMimeObject *) msg, "Newsgroups", "alt.test", CHARSET);
 g_mime_object_set_header ((GMimeObject *) msg, "Followup-To", "alt.test,unknown.group", CHARSET);
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  e.assign (errors.begin(), errors.end());
  check (errors.size() == 1)
  check (goodness.is_warn ())
  check (e[0] == "Warning: The posting profile's server doesn't carry newsgroup\n\t\"unknown.group\".\n\tIf the group name is correct, switch profiles in the \"From:\"\n\tline or edit the profile with \"Edit|Manage Posting Profiles\".")
  g_mime_object_remove_header (GMIME_OBJECT(msg), "Followup-To");

  // top posting
 g_mime_object_set_header ((GMimeObject *) msg, "References", "<asdf@foo.com>", CHARSET);
  cpch = "How Fascinating!\n"
         "\n"
         "> Blah blah blah.\n";
  mime_part_set_content (part, cpch);
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  e.assign (errors.begin(), errors.end());
  check (errors.size() == 1)
  check (goodness.is_warn ())
  check (e[0] == "Warning: Reply seems to be top-posted.")
  g_mime_object_remove_header (GMIME_OBJECT(msg), "References");

  // top posting
 g_mime_object_set_header ((GMimeObject *) msg, "References", "<asdf@foo.com>", CHARSET);
  cpch = "How Fascinating!\n"
         "\n"
         "> Blah blah blah.\n"
         "\n"
         "-- \n"
         "Pan shouldn't mistake this signature for\n"
         "original content in the top-posting check.\n";
  mime_part_set_content (part, cpch);
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  e.assign (errors.begin(), errors.end());
  check (errors.size() == 1)
  check (goodness.is_warn ())
  check (e[0] == "Warning: Reply seems to be top-posted.")
  g_mime_object_remove_header (GMIME_OBJECT(msg), "References");

  // bad signature
  cpch = "Testing to see what happens if the signature is malformed.\n"
         "It *should* warn us about it.\n"
         "\n"
         "--\n"
         "This is my signature.\n";
  mime_part_set_content (part, cpch);
  MessageCheck :: message_check (msg, attribution, groups_our_server_has, errors, goodness);
  e.assign (errors.begin(), errors.end());
  check (errors.size() == 1)
  check (goodness.is_warn())
  check (e[0] == "Warning: The signature marker should be \"-- \", not \"--\".");

  // success
  return 0;
}
