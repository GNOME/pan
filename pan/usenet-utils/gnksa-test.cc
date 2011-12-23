#include <config.h>
#include <climits>
#include <iostream>
#include <string>
#include <pan/general/string-view.h>
#include <pan/general/test.h>
#include "gnksa.h"

using namespace pan;

typedef struct
{
	int expected_retval;
	const char * address;
}
AddressCheck;

typedef struct
{
	int len;
	const char * in;
	const char * expected_out;
}
ReferencesCheck;

static int
test_generate_references (void)
{
	const char * refs;
	const char * msg_id;
	std::string expected;
	std::string out;

	refs =  "<9uh0rq$8g9ua$1@ID-41667.news.dfncis.de> <9596705.7QZiUf9aFP@aschiller.easynet.de> <Xns916D78745865Dvg@newsf1.volker-gringmuth.de> <3C111F64.D2AABC41@babsi.de> <pan.2001.12.07.21.44.36.620796.4981@gmx.de> <pan.2001.12.08.21.19.07.420400.7547@babsi.de> <pan.2001.12.08.21.30.14.714578.7547@babsi.de>";
	msg_id = "<pan.2001.12.08.22.06.46.245566.1891@gmx.de>";
	out = GNKSA::generate_references (refs, msg_id);
	expected = "<9uh0rq$8g9ua$1@ID-41667.news.dfncis.de> <9596705.7QZiUf9aFP@aschiller.easynet.de> <Xns916D78745865Dvg@newsf1.volker-gringmuth.de> <3C111F64.D2AABC41@babsi.de> <pan.2001.12.07.21.44.36.620796.4981@gmx.de> <pan.2001.12.08.21.19.07.420400.7547@babsi.de> <pan.2001.12.08.21.30.14.714578.7547@babsi.de> <pan.2001.12.08.22.06.46.245566.1891@gmx.de>";
  check (expected == out)

	refs = "<qm2vdvs9kmd9nualp7gpcuqp02ommq191p@4ax.com> <3EE5A096.98029307@hotmail.com> <bc4sb4$fbmlp$1@ID-191099.news.dfncis.de> <3EE6DE5C.A8D18620@hotmail.com> <bc6qeb$g02ia$9@ID-191099.news.dfncis.de> <3EE70552.FFBAD8F1@hotmail.com> <bc80jt$gjfab$9@ID-191099.news.dfncis.de> <3EE7B742.CB99E2A9@hotmail.com> <bc8esv$god63$2@ID-191099.news.dfncis.de> <3EE83795.BAB1A91B@hotmail.com> <bc9o49$glr0p$1@ID-191099.news.dfncis.de> <3EE8F9DD.18250DED@hotmail.com> <1231781.L8vHGuMyzo@cleeson.com> <3EEB7C07.4752E42A@hotmail.com> <1296817.URW5Hf7Ksp@pedro-loves-maide.cleeson.com> <3EEEF80E.861F9856@hotmail.com> <28546748.CjhBzqWdrr@pedro-loves-maide.cleeson.com> <3EF04A70.BCC38BF9@hotmail.com> <3009961.k4EQsI0dRT@pedro-loves-maide.cleeson.com> <3EF2E85E.904F3FBA@hotmail.com> <352040964.KGvfqQSRi4@pedro-loves-maide.cleeson.com> <3EFC7072.C6E6D00E@hotmail.com> <1622701.ZSR7dHnD1g@pedro.loves.maide> <3F13C513.6CDFABEB@hotmail.com> <3F13DBF6.8000505@gmx.net>";
	msg_id = "<3f13ffeb$0$301$ba620e4c@reader1.news.skynet.be>";
	out = GNKSA::generate_references (refs, msg_id);
	expected = "<qm2vdvs9kmd9nualp7gpcuqp02ommq191p@4ax.com> <bc4sb4$fbmlp$1@ID-191099.news.dfncis.de> <3EE6DE5C.A8D18620@hotmail.com> <bc6qeb$g02ia$9@ID-191099.news.dfncis.de> <3EE70552.FFBAD8F1@hotmail.com> <bc80jt$gjfab$9@ID-191099.news.dfncis.de> <3EE7B742.CB99E2A9@hotmail.com> <bc8esv$god63$2@ID-191099.news.dfncis.de> <3EE83795.BAB1A91B@hotmail.com> <bc9o49$glr0p$1@ID-191099.news.dfncis.de> <3EE8F9DD.18250DED@hotmail.com> <1231781.L8vHGuMyzo@cleeson.com> <3EEB7C07.4752E42A@hotmail.com> <1296817.URW5Hf7Ksp@pedro-loves-maide.cleeson.com> <3EEEF80E.861F9856@hotmail.com> <28546748.CjhBzqWdrr@pedro-loves-maide.cleeson.com> <3EF04A70.BCC38BF9@hotmail.com> <3009961.k4EQsI0dRT@pedro-loves-maide.cleeson.com> <3EF2E85E.904F3FBA@hotmail.com> <352040964.KGvfqQSRi4@pedro-loves-maide.cleeson.com> <3EFC7072.C6E6D00E@hotmail.com> <1622701.ZSR7dHnD1g@pedro.loves.maide> <3F13C513.6CDFABEB@hotmail.com> <3F13DBF6.8000505@gmx.net> <3f13ffeb$0$301$ba620e4c@reader1.news.skynet.be>";
	check (expected == out)

  refs = "as.com> <qm2vdvs9kmd9nualp7gpcuqp02ommq191p@4ax.com> <3EE5A096.98029307@hotmail.com> ";
  expected = "<qm2vdvs9kmd9nualp7gpcuqp02ommq191p@4ax.com> <3EE5A096.98029307@hotmail.com>";
  out = GNKSA::remove_broken_message_ids_from_references (refs);
  check (expected == out)

  refs = "as.com> <asdf <qm2vdvs9kmd9nualp7gpcuqp02ommq191p@4ax.com> <3EE5A096.98029307@hotmail.com> <as";
  expected = "<qm2vdvs9kmd9nualp7gpcuqp02ommq191p@4ax.com> <3EE5A096.98029307@hotmail.com>";
  out = GNKSA::remove_broken_message_ids_from_references (refs);
  check (expected == out)

  refs = "as.com> <asdf <qm2vdvs9kmd9nualp7gpcuqp02ommq191p_!dfdsfsd@4ax.com> <3EE5A096.98029307@hotmail.com> <as";
  expected = "<qm2vdvs9kmd9nualp7gpcuqp02ommq191p_!dfdsfsd@4ax.com> <3EE5A096.98029307@hotmail.com>";
  out = GNKSA::remove_broken_message_ids_from_references (refs);
  check (expected == out)

  return 0;
}

static ReferencesCheck references [] =
{
	{
		998, /*  gnksa cutoff */

		"<gnksa_pan-0.8.0_001@lull.org>"
		" <gnksa_pan-0.8.0_002@lull.org> <gnksa_pan-0.8.0_003@lull.org>"
		" <gnksa_pan-0.8.0_004@lull.org> <gnksa_pan-0.8.0_005@lull.org>"
		" <gnksa_pan-0.8.0_006@lull.org> <gnksa_pan-0.8.0_007@lull.org>"
		" <gnksa_pan-0.8.0_008@lull.org> <gnksa_pan-0.8.0_009@lull.org>"
		" <gnksa_pan-0.8.0_010@lull.org> <gnksa_pan-0.8.0_011@lull.org>"
		" <gnksa_pan-0.8.0_012@lull.org> <gnksa_pan-0.8.0_013@lull.org>"
		" <gnksa_pan-0.8.0_014@lull.org> <gnksa_pan-0.8.0_015@lull.org>"
		" <gnksa_pan-0.8.0_016@lull.org> <gnksa_pan-0.8.0_017@lull.org>"
		" <gnksa_pan-0.8.0_018@lull.org> <gnksa_pan-0.8.0_019@lull.org>"
		" <gnksa_pan-0.8.0_020@lull.org> <gnksa_pan-0.8.0_021@lull.org>"
		" <gnksa_pan-0.8.0_022@lull.org> <gnksa_pan-0.8.0_023@lull.org>"
		" <gnksa_pan-0.8.0_024@lull.org> <gnksa_pan-0.8.0_025@lull.org>"
		" <gnksa_pan-0.8.0_026@lull.org> <gnksa_pan-0.8.0_027@lull.org>"
		" <gnksa_pan-0.8.0_028@lull.org> <gnksa_pan-0.8.0_029@lull.org>"
		" <gnksa_pan-0.8.0_030@lull.org> <gnksa_pan-0.8.0_031@lull.org>"
  		" <gnksa_pan-0.8.0_035.12345@lull.org>",

		"<gnksa_pan-0.8.0_001@lull.org>"
		" <gnksa_pan-0.8.0_002@lull.org> <gnksa_pan-0.8.0_003@lull.org>"
		" <gnksa_pan-0.8.0_004@lull.org> <gnksa_pan-0.8.0_005@lull.org>"
		" <gnksa_pan-0.8.0_006@lull.org> <gnksa_pan-0.8.0_007@lull.org>"
		" <gnksa_pan-0.8.0_008@lull.org> <gnksa_pan-0.8.0_009@lull.org>"
		" <gnksa_pan-0.8.0_010@lull.org> <gnksa_pan-0.8.0_011@lull.org>"
		" <gnksa_pan-0.8.0_012@lull.org> <gnksa_pan-0.8.0_013@lull.org>"
		" <gnksa_pan-0.8.0_014@lull.org> <gnksa_pan-0.8.0_015@lull.org>"
		" <gnksa_pan-0.8.0_016@lull.org> <gnksa_pan-0.8.0_017@lull.org>"
		" <gnksa_pan-0.8.0_018@lull.org> <gnksa_pan-0.8.0_019@lull.org>"
		" <gnksa_pan-0.8.0_020@lull.org> <gnksa_pan-0.8.0_021@lull.org>"
		" <gnksa_pan-0.8.0_022@lull.org> <gnksa_pan-0.8.0_023@lull.org>"
		" <gnksa_pan-0.8.0_024@lull.org> <gnksa_pan-0.8.0_025@lull.org>"
		" <gnksa_pan-0.8.0_026@lull.org> <gnksa_pan-0.8.0_027@lull.org>"
		" <gnksa_pan-0.8.0_028@lull.org> <gnksa_pan-0.8.0_029@lull.org>"
		" <gnksa_pan-0.8.0_030@lull.org> <gnksa_pan-0.8.0_031@lull.org>"
  		" <gnksa_pan-0.8.0_035.12345@lull.org>"
	},
	{
		998, /*  gnksa cutoff */

		"<gnksa_pan-0.8.0_001@lull.org>"
		" <gnksa_pan-0.8.1_001@lull.org>"
		" <gnksa_pan-0.8.0_002@lull.org> <gnksa_pan-0.8.0_003@lull.org>"
		" <gnksa_pan-0.8.0_004@lull.org> <gnksa_pan-0.8.0_005@lull.org>"
		" <gnksa_pan-0.8.0_006@lull.org> <gnksa_pan-0.8.0_007@lull.org>"
		" <gnksa_pan-0.8.0_008@lull.org> <gnksa_pan-0.8.0_009@lull.org>"
		" <gnksa_pan-0.8.0_010@lull.org> <gnksa_pan-0.8.0_011@lull.org>"
		" <gnksa_pan-0.8.0_012@lull.org> <gnksa_pan-0.8.0_013@lull.org>"
		" <gnksa_pan-0.8.0_014@lull.org> <gnksa_pan-0.8.0_015@lull.org>"
		" <gnksa_pan-0.8.0_016@lull.org> <gnksa_pan-0.8.0_017@lull.org>"
		" <gnksa_pan-0.8.0_018@lull.org> <gnksa_pan-0.8.0_019@lull.org>"
		" <gnksa_pan-0.8.0_020@lull.org> <gnksa_pan-0.8.0_021@lull.org>"
		" <gnksa_pan-0.8.0_022@lull.org> <gnksa_pan-0.8.0_023@lull.org>"
		" <gnksa_pan-0.8.0_024@lull.org> <gnksa_pan-0.8.0_025@lull.org>"
		" <gnksa_pan-0.8.0_026@lull.org> <gnksa_pan-0.8.0_027@lull.org>"
		" <gnksa_pan-0.8.0_028@lull.org> <gnksa_pan-0.8.0_029@lull.org>"
		" <gnksa_pan-0.8.0_030@lull.org> <gnksa_pan-0.8.0_031@lull.org>"
  		" <gnksa_pan-0.8.0_035.12345@lull.org>",

		"<gnksa_pan-0.8.0_001@lull.org>"
		" <gnksa_pan-0.8.0_002@lull.org> <gnksa_pan-0.8.0_003@lull.org>"
		" <gnksa_pan-0.8.0_004@lull.org> <gnksa_pan-0.8.0_005@lull.org>"
		" <gnksa_pan-0.8.0_006@lull.org> <gnksa_pan-0.8.0_007@lull.org>"
		" <gnksa_pan-0.8.0_008@lull.org> <gnksa_pan-0.8.0_009@lull.org>"
		" <gnksa_pan-0.8.0_010@lull.org> <gnksa_pan-0.8.0_011@lull.org>"
		" <gnksa_pan-0.8.0_012@lull.org> <gnksa_pan-0.8.0_013@lull.org>"
		" <gnksa_pan-0.8.0_014@lull.org> <gnksa_pan-0.8.0_015@lull.org>"
		" <gnksa_pan-0.8.0_016@lull.org> <gnksa_pan-0.8.0_017@lull.org>"
		" <gnksa_pan-0.8.0_018@lull.org> <gnksa_pan-0.8.0_019@lull.org>"
		" <gnksa_pan-0.8.0_020@lull.org> <gnksa_pan-0.8.0_021@lull.org>"
		" <gnksa_pan-0.8.0_022@lull.org> <gnksa_pan-0.8.0_023@lull.org>"
		" <gnksa_pan-0.8.0_024@lull.org> <gnksa_pan-0.8.0_025@lull.org>"
		" <gnksa_pan-0.8.0_026@lull.org> <gnksa_pan-0.8.0_027@lull.org>"
		" <gnksa_pan-0.8.0_028@lull.org> <gnksa_pan-0.8.0_029@lull.org>"
		" <gnksa_pan-0.8.0_030@lull.org> <gnksa_pan-0.8.0_031@lull.org>"
  		" <gnksa_pan-0.8.0_035.12345@lull.org>"
	},
	{
		1024,

		"<gnksa.01@lull.org> <lull.org> <gnksa.03> <@lull.org>"
		" <gnksa.05@> <gnksa.06@lull.org> <>"
		" <gnksa.08@lull.org> <gnksa.09@lull.org"
		" <gnksa.10@lull.org> gnksa.11@lull.org>"
		" <gnksa.12@@lull.org> <gnksa.13@lull.org>.14@lull.org>"
		" gnksa.15@lull.org <gnksa.16@lull.org>",

		"<gnksa.01@lull.org> <gnksa.06@lull.org>"
		" <gnksa.08@lull.org> <gnksa.10@lull.org>"
		" <gnksa.13@lull.org>"
		" <gnksa.16@lull.org>"
	},
	{
		1024,

		"<a@b.a> <d@f.uk> <postmaster@l.uk> <n@o.uk> <@bar.uk>"
		" <foo@bar.com><baz@xyzzy.org> <foo@> <foo@"
		" <blah@trala> <blah@trala.org>",

		"<a@b.a> <d@f.uk> <n@o.uk> <foo@bar.com>"
		" <baz@xyzzy.org> <blah@trala> <blah@trala.org>"
	},
	{
		36,
		"<a@b.uk> <d@f.uk> <h@i.uk> <k@l.uk> <n@o.uk> <q@r.uk>",
		"<a@b.uk> <k@l.uk> <n@o.uk> <q@r.uk>"
	},
	{
		35,
		"<a@b.uk> <d@f.uk> <h@i.uk> <k@l.uk> <n@o.uk> <q@r.uk>",
		"<a@b.uk> <k@l.uk> <n@o.uk> <q@r.uk>"
	},
	{
		34,
		"<a@b.uk> <d@f.uk> <h@i.uk> <k@l.uk> <n@o.uk> <q@r.uk>",
		"<a@b.uk> <n@o.uk> <q@r.uk>"
	},
	{
		32,
		"<a@b.uk> <d@f.uk> <h@i.uk> <k@l.uk> <n@o.uk> <q@r.uk>",
		"<a@b.uk> <n@o.uk> <q@r.uk>",
	},
	{
		27,
		"<a@b.uk> <d@f.uk> <h@i.uk> <k@l.uk> <n@o.uk> <q@r.uk>",
		"<a@b.uk> <n@o.uk> <q@r.uk>",
	},
	{
		26,
		"<a@b.uk> <d@f.uk> <h@i.uk> <k@l.uk> <n@o.uk> <q@r.uk>",
		"<a@b.uk> <n@o.uk> <q@r.uk>",
	},
	{
		25,
		"<a@b.uk> <d@f.uk> <h@i.uk> <k@l.uk> <n@o.uk> <q@r.uk>",
		"<a@b.uk> <q@r.uk>",
	},
	/* make sure that twisted domains like [10.0.0.4] don't get stripped.
	   see comments on pan/base/gnksa.c gnksa_check_message_id() for details. */
	{
		998,

		"<B8CE15E0.2DBE%frederic.platzer@wanadoo.fr> "
		"<1f9yc83.l59f37ehygn4N%florian@****nachon.net> "
		"<1f9yzfg.1dxhomt17221j4N%mannucci@wild-works.net> "
		"<1fa2kwz.14tkecl3qg8uaN%moi@mapetitentreprise.com> "
		"<1fa7mt7.1t7eiqu1scmm0iN@[10.0.0.4]> "
		"<enlevezca.michel.depeige-E0EA3B.15534806042002@news.wanadoo.fr>",

		"<B8CE15E0.2DBE%frederic.platzer@wanadoo.fr> "
		"<1f9yc83.l59f37ehygn4N%florian@****nachon.net> "
		"<1f9yzfg.1dxhomt17221j4N%mannucci@wild-works.net> "
		"<1fa2kwz.14tkecl3qg8uaN%moi@mapetitentreprise.com> "
		"<1fa7mt7.1t7eiqu1scmm0iN@[10.0.0.4]> "
		"<enlevezca.michel.depeige-E0EA3B.15534806042002@news.wanadoo.fr>"
	}

};

static AddressCheck addresses [] =
{
	{ GNKSA::ILLEGAL_PLAIN_PHRASE, "Charles S. Kerr <charles@foo.com>" },
	{ GNKSA::OK, "\"Charles S. Kerr\" <charles@foo.com>" },
	{ GNKSA::OK, "charles kerr <charles@superpimp.org>" },
	{ GNKSA::OK, "Charles \"Pan Programmer\" Kerr <charles@superpimp.org>" },
	{ GNKSA::OK, "Charles \"Likes, to, put, commas, in, quoted, strings\" Kerr <charles@superpimp.org>" },
	{ GNKSA::OK, "\"Charles Kerr, Pan Programmer\" <charles@superpimp.org>" },
	{ GNKSA::ILLEGAL_PLAIN_PHRASE, "Charles Kerr, Pan Programmer <charles@superpimp.org>" },
	{ GNKSA::INVALID_DOMAIN, "charles kerr <charles>" },
	{ GNKSA::OK, "charles@superpimp.org" },
	{ GNKSA::OK, "charles@superpimp.org (Charles Kerr)" },
	{ GNKSA::SINGLE_DOMAIN, "Charles Kerr <charles@org>" },
	{ GNKSA::SINGLE_DOMAIN, "Charles Kerr <@org>" },
	{ GNKSA::OK, "Charles Kerr <charles@[127.0.0.1]>" },
	{ GNKSA::BAD_DOMAIN_LITERAL, "Charles Kerr <charles@[127..0.1]>" },
	{ GNKSA::BAD_DOMAIN_LITERAL, "Charles Kerr <charles@[127...1]>" },
	{ GNKSA::BAD_DOMAIN_LITERAL, "Charles Kerr <charles@[127.0.0.]>" },
	{ GNKSA::ILLEGAL_PLAIN_PHRASE, "<charles@pimp.org.>" },
	{ GNKSA::ILLEGAL_PLAIN_PHRASE, "<charles@pimp-.org>" },
	{ GNKSA::ILLEGAL_LABEL_HYPHEN, "Charles Kerr <charles@pimp-.org>" },
	{ GNKSA::OK, "Charles <charles@pimp.asf.fa>" },
	{ GNKSA::OK, "Charles <charles@pimp.asf.uk>" },
	{ GNKSA::ZERO_LENGTH_LABEL, "Charles <charles@>" },
	{ GNKSA::LOCALPART_MISSING, "Charles <@pimp.org>" },
	{ GNKSA::OK, "Charles Kerr <charles@skywalker.ecn.ou.edu>" },
	{ GNKSA::LPAREN_MISSING, "Charles Kerr" },
	{ GNKSA::OK, "looniii@aol.com (Looniii)" },
	{ GNKSA::OK, "Eric <scare.crow@oz.land>" },
	{ GNKSA::ILLEGAL_PLAIN_PHRASE, "<charles@pimp.org>" }
};

int
main (void)
{
  // 0.92 - Christophe
  StringView v = GNKSA :: get_short_author_name ("\"@nermos\" <anermosathotmaildotcom@>");
  check (v.len != ULONG_MAX)

  v = GNKSA :: get_short_author_name ("fclefrad@yahoo.com");
  check (v == "fclefrad")

  v = GNKSA :: get_short_author_name ("\"Ch@rvelle\" <fclefrad@yahoo.com>");
  check (v == "Ch@rvelle")



	/* test addresses */
	if (1)
	{
		int i;
		int qty = sizeof(addresses) / sizeof(addresses[0]);
		for (i=0; i!=qty; ++i) {
			const int retval = GNKSA::check_from (addresses[i].address, true);
			check (retval == addresses[i].expected_retval)
		}
	}

	/* test trimming */
	if (1)
	{
		int i;
		int qty = sizeof(references) / sizeof(references[0]);
		for (i=0; i!=qty; ++i) {
			const std::string s (GNKSA::trim_references (references[i].in, references[i].len));
			check (s == references[i].expected_out)
		}
	}


	/* test message-id generation */
	if (1)
	{
                std::string id1;
                std::string id2;

		id1 = GNKSA::generate_message_id_from_email_address ("<foo@bar.com>");
		id2 = GNKSA::generate_message_id_from_email_address ("<foo@bar.com>");
		check (!id1.empty())
		check (!id2.empty())

		id1 = GNKSA::generate_message_id_from_email_address ("Joe <joe@bar.com>");
		check (!id1.empty())
		check (id1.find("bar") != id1.npos)

		id1 = GNKSA::generate_message_id_from_email_address ("zzz.com");
		check (!id1.empty())
		check (id1.find("zzz") != id1.npos)

		id1 = GNKSA::generate_message_id_from_email_address ("@bar.com>");
		check (!id1.empty())
		check (id1.find("bar") != id1.npos)
	}

	if (1)
	{
		int i = test_generate_references ();
		if (i != 0)
			return i;
	}

	return 0;
}
