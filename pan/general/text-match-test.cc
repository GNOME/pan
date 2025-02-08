#include <config.h>
#include <iostream>
#include <map>
#include "text-match.h"
#include "test.h"

using namespace pan;

int
main (void)
{
   TextMatch m;
   std::string fill_sql, fill_param;

   m.set ("fillyjonk", m.CONTAINS, false);
   m.create_sql_search(fill_sql, fill_param);
   check_eq(fill_sql, "lower(grp.name) like ?");
   check_eq(fill_param, "%fillyjonk%");

   check (m.test ("Can we find fillyjonk when it's by itself?"));
   check (m.test ("Can we find fillyJonk when its case is wrong?"));
   check (m.test ("Can we find FiLlYJonK when its case is more wrong?"));
   check (m.test ("Can we find FILLYJONK when it's in all uppercase?"));
   check (m.test ("Can we findfillyjonkwhen it's inside a string?"));
   check (m.test ("Can we findFillyJonKwhen it's inside a string and its case is wrong?"));
   check (m.test ("WTF is a fillyjonk?"));
   check (!m.test ("WTF is a illyjonk?"));
   check (!m.test ("WTF is a fillyonk?"));
   check (!m.test ("WTF is a asf?"));
   check (!m.test ("WTF is a fillyjon?"));


   m.set ("fillyjonk", m.CONTAINS, true);
   m.create_sql_search(fill_sql, fill_param);
   check_eq(fill_sql, "grp.name like ?");
   check_eq(fill_param, "%fillyjonk%");
   check (m.test ("Can we find fillyjonk when it's by itself?"));
   check (!m.test ("Can we find fillyJonk when its case is wrong?"));
   check (!m.test ("Can we find FiLlYJonK when its case is more wrong?"));
   check (!m.test ("Can we find FILLYJONK when it's in all uppercase?"));
   check (m.test ("Can we findfillyjonkwhen it's inside a string?"));
   check (!m.test ("Can we findFillyJonKwhen it's inside a string and its case is wrong?"));
   check (m.test ("WTF is a fillyjonk?"));
   check (!m.test ("WTF is a illyjonk?"));
   check (!m.test ("WTF is a fillyonk?"));
   check (!m.test ("WTF is a asf?"));
   check (!m.test ("WTF is a fillyjon?"));
   check (!m.test ("WTF is a Fillyjonk?"));

   m.set ("fillyjonk", m.BEGINS_WITH, false);
   m.create_sql_search(fill_sql, fill_param);
   check_eq(fill_sql, "lower(grp.name) like ?");
   check_eq(fill_param, "fillyjonk%");
   check (m.test ("fillyjonk at the front"));
   check (m.test ("Fillyjonk at the front, in Caps"));
   check (!m.test ("at the end comes the fillyjonk"));
   check (!m.test ("the fillyjonk comes before the mymble"));

   m.set ("^fillyjonk", m.REGEX, false);
   // "simple" regexp are downgraded to begins_with or ends_with
   check_eq(fill_sql, "lower(grp.name) like ?");
   check_eq(fill_param, "fillyjonk%");
   check (m.test ("fillyjonk at the front"));
   check (m.test ("Fillyjonk at the front, in Caps"));
   check (!m.test ("at the end comes the fillyjonk"));
   check (!m.test ("the fillyjonk comes before the mymble"));

   m.set ("fillyjonk", m.ENDS_WITH, false);
   m.create_sql_search(fill_sql, fill_param);
   check_eq(fill_sql, "lower(grp.name) like ?");
   check_eq(fill_param, "%fillyjonk");
   check (!m.test ("fillyjonk at the front"));
   check (m.test ("at the end comes the fillyjonk"));
   check (m.test ("at the end, in caps, comes the Fillyjonk"));
   check (!m.test ("the fillyjonk comes before the mymble"));

   m.set ("fillyjonk$", m.REGEX, false);
   m.create_sql_search(fill_sql, fill_param);
   check_eq(fill_sql, "lower(grp.name) like ?");
   check_eq(fill_param, "%fillyjonk");
   check (!m.test ("fillyjonk at the front"));
   check (m.test ("at the end comes the fillyjonk"));
   check (m.test ("at the end, in caps, comes the Fillyjonk"));
   check (!m.test ("the fillyjonk comes before the mymble"));

   m.set ("fillyjonk", m.IS, false);
   m.create_sql_search(fill_sql, fill_param);
   check_eq(fill_sql, "grp.name == ? collate nocase");
   check_eq(fill_param, "fillyjonk");
   check (!m.test ("fillyjonk at the front"));
   check (!m.test ("at the end comes the fillyjonk"));
   check (!m.test ("at the end, in caps, comes the Fillyjonk"));
   check (!m.test ("the fillyjonk comes before the mymble"));
   check (!m.test (" fillyjonk "));
   check (!m.test ("fillyjonk "));
   check ( m.test ("fillyjonk"));
   check (!m.test ("illyjonk"));
   check (!m.test ("fillyjonk "));

   m.set ("^fillyjonk$", m.REGEX, false);
   check (!m.test ("fillyjonk at the front"));
   check (!m.test ("at the end comes the fillyjonk"));
   check (!m.test ("at the end, in caps, comes the Fillyjonk"));
   check (!m.test ("the fillyjonk comes before the mymble"));
   check (!m.test (" fillyjonk "));
   check (!m.test ("fillyjonk "));
   check ( m.test ("fillyjonk"));
   check (!m.test ("illyjonk"));
   check (!m.test ("fillyjonk "));

   m.set ("(filly|jonk)", m.REGEX, false);
   m.create_sql_search(fill_sql, fill_param);
   check_eq(fill_sql, "grp.name regexp ?");
   check_eq(fill_param, "(filly|jonk)");
   check (!m.test ("illyonking"));
   check ( m.test ("filly at the front"));
   check ( m.test ("Filly at the front, in caps"));
   check ( m.test ("at the end, filly"));
   check ( m.test ("in the middle fillyjonk without caps"));
   check ( m.test ("in the middle Fillyjonk with caps"));

   m.set ("\\bfillyjonk\\b", m.REGEX, false);
   check (!m.test ("fillyyonking at the front"));
   check ( m.test ("fillyjonk at the front"));
   check ( m.test ("at the end, fillyjonk"));
   check ( m.test ("in the middle fillyjonk without caps"));
   check ( m.test ("in the middle Fillyjonk with caps"));

   return 0;
}
