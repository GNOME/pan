/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2006  Charles Kerr <charles@rebelbase.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>
#include <cassert>
#include <cerrno>

extern "C" {
#define PROTOTYPES
#include <stdio.h>
#include <uulib/uudeview.h>
#include <glib/gi18n.h>
#include <gmime/gmime-utils.h>
#include <zlib.h>
}

#include <fstream>
#include <iostream>
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/utf8-utils.h>
#include <pan/data/data.h>
#include "nntp.h"
#include "task-xover.h"

using namespace pan;

namespace {
std::string get_short_name(const StringView& in) {
	static const StringView moderated("moderated");
	static const StringView d("d");

	StringView myline, long_token;

	// find the long token -- use the last, unless that's "moderated" or "d"
	myline = in;
	myline.pop_last_token(long_token, '.');
	if (!myline.empty() && (long_token == moderated || long_token == d))
		myline.pop_last_token(long_token, '.');

	// build a new string where each token is shortened except for long_token
	std::string out;
	myline = in;
	StringView tok;
	while (myline.pop_token(tok, '.')) {
		out.insert(out.end(), tok.begin(),
				(tok == long_token ? tok.end() : tok.begin() + 1));
		out += '.';
	}
	if (!out.empty())
		out.erase(out.size() - 1);

	return out;
}

std::string get_description(const Quark& group, TaskXOver::Mode mode) {
	char buf[1024];
	if (mode == TaskXOver::ALL)
		snprintf(buf, sizeof(buf), _("Getting all headers for \"%s\""),
				group.c_str());
	else if (mode == TaskXOver::NEW)
		snprintf(buf, sizeof(buf), _("Getting new headers for \"%s\""),
				group.c_str());
	else
		// SAMPLE
		snprintf(buf, sizeof(buf), _("Sampling headers for \"%s\""),
				group.c_str());
	return std::string(buf);
}
}

TaskXOver::TaskXOver(Data & data, const Quark & group, Mode mode,
		unsigned long sample_size) :
		Task("XOVER", get_description(group, mode)), _data(data), _group(group), _short_group_name(
				get_short_name(StringView(group.c_str()))), _mode(mode), _sample_size(
				sample_size), _days_cutoff(
				mode == DAYS ? (time(0) - (sample_size * 24 * 60 * 60)) : 0), _group_xover_is_reffed(
				false), _bytes_so_far(0), _parts_so_far(0ul), _articles_so_far(
				0ul), _total_minitasks(0) {

	debug("ctor for " << group);

	// add a ``GROUP'' MiniTask for each server that has this group
	// initialize the _high lookup table to boundaries
	quarks_t servers;
	_data.group_get_servers(group, servers);
	foreach_const (quarks_t, servers, it)if (_data.get_server_limits(*it))
	{
		Data::Server* s (_data.find_server(*it));
		const MiniTask group_minitask (MiniTask::GROUP);
		_server_to_minitasks[*it].push_front (group_minitask);
		_high[*it] = data.get_xover_high (group, *it);
	}
	init_steps(0);

	// tell the users what we're up to
	set_status(group.c_str());

	update_work();
}

TaskXOver::~TaskXOver() {
	if (_group_xover_is_reffed) {
		foreach (server_to_high_t, _high, it)_data.set_xover_high (_group, it->first, it->second);
		_data.xover_unref (_group);
	}
	_data.fire_group_entered(_group, 1, 0);
}

void TaskXOver::use_nntp(NNTP* nntp) {

	const Quark& server(nntp->_server);
	CompressionType comp;
	_data.get_server_compression_type(server, comp);

	debug("got an nntp from " << nntp->_server);

	// if this is the first nntp we've gotten, ref the xover data
	if (!_group_xover_is_reffed) {
		_group_xover_is_reffed = true;
		_data.xover_ref(_group);
	}

	MiniTasks_t& minitasks(_server_to_minitasks[server]);
	if (minitasks.empty()) {
		debug(
				"That's interesting, I got a socket for " << server << " but have no use for it!");
		_state._servers.erase(server);
		check_in(nntp, OK);
	} else {
		const MiniTask mt(minitasks.front());
		minitasks.pop_front();
		switch (mt._type) {
		case MiniTask::GROUP:
			debug("GROUP " << _group << " command to " << server);
			nntp->group(_group, this);
			break;
		case MiniTask::XOVER:
			debug("XOVER " << mt._low << '-' << mt._high << " to " << server);
			_last_xover_number[nntp] = mt._low;
			if (comp == HEADER_COMPRESS_XFEATURE)
				nntp->xfeat(_group, mt._low, mt._high, this);
			else if (comp == HEADER_COMPRESS_XZVER)
				nntp->xzver(_group, mt._low, mt._high, this);
			else
				nntp->xover (_group, mt._low, mt._high, this);
			break;
		default:
			assert(0);
		}
		update_work();
	}
}

/***
 ****
 ***/

///TODO show low and high in UI (is this already there?)
void TaskXOver::on_nntp_group(NNTP * nntp, const Quark & group,
		unsigned long qty, uint64_t low, uint64_t high) {
	const Quark& servername(nntp->_server);
	CompressionType comp;
	_data.get_server_compression_type(servername, comp);
	const bool compression_enabled (comp != HEADER_COMPRESS_NONE);

	// new connections can tickle this...
	if (_servers_that_got_xover_minitasks.count(servername))
		return;

	_servers_that_got_xover_minitasks.insert(servername);

	debug(
			"got GROUP result from " << nntp->_server << " (" << nntp << "): " << " qty " << qty << " low " << low << " high " << high);

	uint64_t l(low), h(high);
	_data.set_xover_low(group, nntp->_server, low);
	//std::cerr << LINE_ID << " This group's range is [" << low << "..." << high << ']' << std::endl;

	if (_mode == ALL || _mode == DAYS)
		l = low;
	else if (_mode == SAMPLE) {
		_sample_size = std::min(_sample_size, high - low);
		//std::cerr << LINE_ID << " and I want to sample " <<  _sample_size << " messages..." << std::endl;
		l = std::max(low, high + 1 - _sample_size);
	} else { // NEW
		uint64_t xh(_data.get_xover_high(group, nntp->_server));
		//std::cerr << LINE_ID << " current xover high is " << xh << std::endl;
		l = std::max(xh + 1, low);
	}

	if (l <= high) {
		//std::cerr << LINE_ID << " okay, I'll try to get articles in [" << l << "..." << h << ']' << std::endl;
		add_steps(h - l);
		const int INCREMENT(compression_enabled ? 10000 : 1000);
		MiniTasks_t& minitasks(_server_to_minitasks[servername]);
		for (uint64_t m = l; m <= h; m += INCREMENT) {
			const MiniTask mt(MiniTask::XOVER, m, m + INCREMENT);
			debug(
					"adding MiniTask for " << servername << ": xover [" << mt._low << '-' << mt._high << "]");
			minitasks.push_front(mt);
			++_total_minitasks;
		}
	} else {
		//std::cerr << LINE_ID << " nothing new here..." << std::endl;
		_high[nntp->_server] = high;
	}
}

namespace {
unsigned long view_to_ul(const StringView& view) {
	unsigned long ul = 0ul;

	if (!view.empty()) {
		errno = 0;
		ul = strtoul(view.str, 0, 10);
		if (errno)
			ul = 0ul;
	}

	return ul;
}
uint64_t view_to_ull(const StringView& view) {
	uint64_t ul = 0ul;

	if (!view.empty()) {
		errno = 0;
		ul = g_ascii_strtoull(view.str, 0, 10);
		if (errno)
			ul = 0ul;
	}

	return ul;
}

bool header_is_nonencoded_utf8(const StringView& in) {
	const bool is_nonencoded(!in.strstr("=?"));
	const bool is_utf8(g_utf8_validate(in.str, in.len, 0));
	return is_nonencoded && is_utf8;
}
}

/*
 http://tools.ietf.org/html/rfc2980#section-2.8

 Each line of output will be formatted with the article number,
 followed by each of the headers in the overview database or the
 article itself (when the data is not available in the overview
 database) for that article separated by a tab character.  The
 sequence of fields must be in this order: subject, author, date,
 message-id, references, byte count, and line count.  Other optional
 fields may follow line count.  Other optional fields may follow line
 count.  These fields are specified by examining the response to the
 LIST OVERVIEW.FMT command.  Where no data exists, a null field must
 be provided (i.e. the output will have two tab characters adjacent to
 each other).  Servers should not output fields for articles that have
 been removed since the XOVER database was created.

 */

void TaskXOver::on_nntp_line(NNTP * nntp, const StringView & line) {

	const Quark& server(nntp->_server);
	CompressionType comp;
	_data.get_server_compression_type(server, comp);

	if (comp != HEADER_COMPRESS_NONE) {
		int sock_id = nntp->_socket->get_id();
		if (_streams.count(sock_id) == 0)
			_streams[sock_id] = new std::stringstream();
		*_streams[sock_id] << line << "\r\n";
	} else {
		on_nntp_line_process(nntp, line);
	}

}

void TaskXOver::on_nntp_line_process(NNTP * nntp, const StringView & line) {

	pan_return_if_fail(nntp != 0);
	pan_return_if_fail(!nntp->_server.empty());
	pan_return_if_fail(!nntp->_group.empty());

	_bytes_so_far += line.len;

	unsigned int lines = 0u;
	unsigned long bytes = 0ul;
	uint64_t number = 0;
	StringView subj, author, date, mid, tmp, xref, l(line);
	std::string ref;
	bool ok = !l.empty();
	ok = ok && l.pop_token(tmp, '\t');
	if (ok)
		number = view_to_ull(tmp);
	tmp.clear();
	ok = ok && l.pop_token(subj, '\t');
	if (ok)
		subj.trim();
	ok = ok && l.pop_token(author, '\t');
	if (ok)
		author.trim();
	ok = ok && l.pop_token(date, '\t');
	if (ok)
		date.trim();
	ok = ok && l.pop_token(mid, '\t');
	if (ok)
		mid.trim();

	//handle multiple "References:"-message-ids correctly. (hack for some faulty servers)
	ok = ok && l.pop_token(tmp, '\t');
	do {
		// usenetbucket uses a (null) (sic!) value for an empty reference list. hence the following hack
		if (tmp.empty() || tmp == "(null)" || tmp == "null")
			continue;
		if (tmp.front() == '<') {
			tmp.trim();
			ref += tmp;
			tmp.clear();
		} else
			break;
	} while ((ok = ok && l.pop_token(tmp, '\t')));
	if (ok)
		bytes = view_to_ul(tmp);
	tmp.clear();
	ok = ok && l.pop_token(tmp, '\t');
	if (ok)
		lines = view_to_ul(tmp);
	ok = ok && l.pop_token(xref, '\t');
	if (ok)
		xref.trim();

	if (xref.len > 6 && !strncmp(xref.str, "Xref: ", 6)) {
		xref = xref.substr(xref.str + 6, 0);
		xref.trim();
	}

	// is this header corrupt?
	if (!number // missing number
	|| subj.empty() // missing subject
			|| author.empty() // missing author
			|| date.empty() // missing date
			|| mid.empty() // missing mid
			|| mid.front() != '<') // corrupt mid
	/// Concerning bug : https://bugzilla.gnome.org/show_bug.cgi?id=650042
	/// Even if we didn't get a proper reference here, continue.
	//|| (!ref.empty() && ref.front()!='<'))
		return;

	// if news server doesn't provide an xref, fake one
	char * buf(0);
	if (xref.empty())
		xref = buf = g_strdup_printf("%s %s:%"G_GUINT64_FORMAT,
				nntp->_server.c_str(), nntp->_group.c_str(), number);

	uint64_t& h(_high[nntp->_server]);
	h = std::max(h, number);

	const char * fallback_charset = NULL; // FIXME

	// are we done?
	const time_t time_posted = g_mime_utils_header_decode_date(date.str, NULL);
	if (_mode == DAYS && time_posted < _days_cutoff) {
		_server_to_minitasks[nntp->_server].clear();
		return;
	}

	++_parts_so_far;

	const Article * article = _data.xover_add(nntp->_server, nntp->_group,
			(header_is_nonencoded_utf8(subj) ?
					subj : header_to_utf8(subj, fallback_charset).c_str()),
			(header_is_nonencoded_utf8(author) ?
					author : header_to_utf8(author, fallback_charset).c_str()),
			time_posted, mid, StringView(ref), bytes, lines, xref);

	if (article)
		++_articles_so_far;

	// emit a status update
	uint64_t& prev = _last_xover_number[nntp];
	increment_step(number - prev);
	prev = number;
	if (!(_parts_so_far % 500))
		set_status_va(_("%s (%lu parts, %lu articles)"),
				_short_group_name.c_str(), _parts_so_far, _articles_so_far);

	// cleanup
	g_free(buf);
}

namespace {
unsigned int crc_table[] = { /* CRC polynomial 0xedb88320 */
0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
		0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
		0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
		0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
		0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
		0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
		0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
		0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
		0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
		0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
		0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
		0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
		0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
		0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
		0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
		0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
		0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
		0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
		0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
		0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
		0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
		0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
		0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
		0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
		0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
		0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
		0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
		0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
		0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
		0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
		0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
		0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
		0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
		0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
		0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
		0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
		0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
		0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
		0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
		0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
		0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
		0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
		0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d };

static unsigned int _crc32(const char *buf, size_t len, unsigned int crc) {
	crc ^= 0xffffffffU;

	while (len--)
		crc = (crc >> 8) ^ crc_table[(crc ^ *buf++) & 0xff];

	return crc ^ 0xffffffffU;
}

void ydecode(std::stringstream* in, std::stringstream* out) {
	int gotbeg = 0, len, outlen = 0;
	char buf1[512], buf2[512], c, *p, *p2 = buf2;
	//unsigned int crc1 = 0, crc = _crc32(NULL, 0, 0);

	while (!in->getline(buf1, sizeof(buf1)).eof()) {
		if (gotbeg == 0 && strncmp(buf1, "=ybegin ", 8) == 0) {
			gotbeg = 1;
		} else if (gotbeg == 1 && strncmp(buf1, "=yend ", 6) == 0) {
			//p = strstr(buf1, "crc32=");
			//if (p)
			//	sscanf(p + 6, "%x", &crc1);
			break;
		} else if (gotbeg == 1) {
			len = strlen(buf1);
			/* strip the CR LF */
			if (len > 2 && buf1[len - 1]) {
				buf1[len - 1] = '\0';
				len--;
			}
			p = buf1;
			while (*p) {
				c = *p++;
				if (c == '=') {
					c = *p++;
					if (c == 0)
						break; /* can't have escape char as last char in line */
					c = (unsigned char) (c - 64);
				}

				c = (unsigned char) (c - 42);
				*p2++ = c;
				/* flush when buffer full */
				if (++outlen >= sizeof(buf2)) {
					//crc = _crc32(buf2, outlen, crc);
					out->write(buf2, outlen);
					p2 = buf2;
					outlen = 0;
				}
			}
		}
	}
	/* flush remaining data */
	if (outlen) {
		//crc = _crc32(buf2, outlen, crc);
		out->write(buf2, outlen);
	}

	// todo log, callback
	//assert(crc == crc1);
}

#define MEMCHUNK 4096

int inflate_zlib(std::stringstream *source, std::stringstream *dest) {
	int ret;
	size_t have;
	z_stream strm;
	char in[MEMCHUNK];
	char out[MEMCHUNK];

	/* allocate inflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;
	/*ret = inflateInit(&strm);*/
	ret = inflateInit2(&strm, -MAX_WBITS); /* use -MAX_WBITS to indicate gzip style */
	if (ret != Z_OK)
		return ret;

	/* decompress until deflate stream ends or end of file */
	do {
		strm.avail_in = source->read(in, MEMCHUNK).gcount();
		if (source->bad()) {
			(void) inflateEnd(&strm);
			return Z_ERRNO;
		}
		if (strm.avail_in == 0)
			break;
		strm.next_in = (unsigned char*) in;

		/* run inflate() on input until output buffer not full */
		do {
			strm.avail_out = MEMCHUNK;
			strm.next_out = (unsigned char*) out;
			ret = inflate(&strm, Z_NO_FLUSH);
			assert(ret != Z_STREAM_ERROR);
			/* state not clobbered */
			switch (ret) {
			case Z_NEED_DICT:
				ret = Z_DATA_ERROR; /* and fall through */
			case Z_DATA_ERROR:
			case Z_MEM_ERROR:
				(void) inflateEnd(&strm);
				return ret;
			}
			have = MEMCHUNK - strm.avail_out;
			dest->write(out, have);
		} while (strm.avail_out == 0);

		/* done when inflate() says it's done */
	} while (ret != Z_STREAM_END);

	/* clean up and return */
	(void) inflateEnd(&strm);
	return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}
}

void TaskXOver::on_nntp_done(NNTP * nntp, Health health,
		const StringView & response) {

	const Quark& servername(nntp->_server);
	CompressionType comp;
	_data.get_server_compression_type(servername, comp);
	const bool compression_enabled (comp != HEADER_COMPRESS_NONE);

	if (response == "." && compression_enabled) {
		std::stringstream* buffer = _streams[nntp->_socket->get_id()];
		std::stringstream out, out2;
		ydecode(buffer, &out);
		inflate_zlib(&out, &out2);
		char buf1[4096];
		while (!out2.getline(buf1, sizeof(buf1)).eof()) {
			on_nntp_line_process(nntp, buf1);
		}
	}
	update_work(true);
	check_in(nntp, health);
}

void TaskXOver::update_work(bool subtract_one_from_nntp_count) {
	int nntp_count(get_nntp_count());
	if (subtract_one_from_nntp_count)
		--nntp_count;

	// find any servers we still need
	quarks_t servers;
	foreach_const (server_to_minitasks_t, _server_to_minitasks, it)if (!it->second.empty())
	servers.insert (it->first);

	//std::cerr << LINE_ID << " servers: " << servers.size() << " nntp: " << nntp_count << std::endl;

	if (!servers.empty())
		_state.set_need_nntp(servers);
	else if (nntp_count)
		_state.set_working();
	else {
		_state.set_completed();
		set_finished(OK);
	}
}

unsigned long TaskXOver::get_bytes_remaining() const {
	unsigned int minitasks_left(0);
	foreach_const (server_to_minitasks_t, _server_to_minitasks, it)minitasks_left += it->second.size();

	const double percent_done(
			_total_minitasks ?
					(1.0 - minitasks_left / (double) _total_minitasks) : 0.0);
	if (percent_done < 0.1) // impossible to estimate
		return 0;
	const unsigned long total_bytes = (unsigned long) (_bytes_so_far
			/ percent_done);
	return total_bytes - _bytes_so_far;
}
