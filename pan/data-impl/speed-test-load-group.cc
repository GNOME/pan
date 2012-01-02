#include <config.h>
#include <pan/data/data.h>
#include "data-impl.h"

using namespace pan;

int main (int argc, char *argv[])
{
  if (argc < 2)
    fprintf (stderr, "Usage: %s groupname\n", argv[0]);
  else {
    const Quark group (argv[1]);
    for (int i=0; i<8; ++i) {
      DataImpl data;
      Data::ArticleTree * tree (data.group_get_articles (group,Quark("")));
      delete tree;
    }
  }
  return 0;
}
