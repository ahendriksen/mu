/* -*-mode: c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-*/

/*
** Copyright (C) 2011-2017 Dirk-Jan C. Binnema <djcb@cthulhu>
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of the GNU General Public License as published by the
** Free Software Foundation; either version 3, or (at your option) any
** later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation,
** Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
**
*/

/* gmime-test; compile with:
       gcc -o gmime-test gmime-test.c -Wall -O0 -ggdb \
	   `pkg-config --cflags --libs gmime-2.6`
 */

#include <gmime/gmime.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <locale.h>

static gchar*
get_recip (GMimeMessage *msg, GMimeAddressType rtype)
{
	char *recep;
	InternetAddressList *receps;
	GMimeFormatOptions *options;

	options = NULL;
	receps = g_mime_message_get_addresses (msg, rtype);
	recep = (char*)internet_address_list_to_string (receps, options, FALSE);

	if (!recep || !*recep) {
		g_free (recep);
		return NULL;
	}

	return recep;
}

static gchar*
get_refs_str (GMimeMessage *msg)
{
	const gchar *str;
	GMimeReferences *mime_refs;
	gchar *rv;
	GMimeParserOptions *options;
	int num_refs;

	str = g_mime_object_get_header (GMIME_OBJECT(msg), "References");
	if (!str)
		return NULL;

	options = NULL;
	mime_refs = g_mime_references_parse (options, str);

	num_refs = g_mime_references_length (mime_refs);
	for (int i = 0; i < num_refs; i++) {
		const char* msgid;
		msgid = g_mime_references_get_message_id(mime_refs, i);
		rv = g_strdup_printf ("%s%s%s",
				      rv ? rv : "",
				      rv ? "," : "",
				      msgid);
	}

	g_mime_references_free (mime_refs);

	return rv;
}

static void
print_date (GMimeMessage *msg)
{
	time_t		 t;
	char		 buf[64];
	size_t		 len;
	struct  tm	*t_m;
	GDateTime *time;

	time = g_mime_message_get_date (msg);
	t = g_date_time_to_unix(time);
	t_m = localtime (&t);

	len = strftime (buf, sizeof(buf) - 1, "%c", t_m);

	if (len > 0)
		g_print ("Date   : %s (%s%04ld)\n",
			 buf,t_m->tm_gmtoff < 0 ? "-" : "+", t_m->tm_gmtoff);
}


static void
print_body (GMimeMessage *msg)
{
	GMimeObject		*body;
	GMimeDataWrapper	*wrapper;
	GMimeStream		*stream;

	body = g_mime_message_get_body (msg);

	if (GMIME_IS_MULTIPART(body))
		body = g_mime_multipart_get_part (GMIME_MULTIPART(body), 0);

	if (!GMIME_IS_PART(body))
		return;

	wrapper = g_mime_part_get_content (GMIME_PART(body));
	if (!GMIME_IS_DATA_WRAPPER(wrapper))
		return;

	stream = g_mime_data_wrapper_get_stream (wrapper);
	if (!GMIME_IS_STREAM(stream))
		return;

	do {
		char	buf[512];
		ssize_t	len;

		len = g_mime_stream_read (stream, buf, sizeof(buf));
		if (len == -1)
			break;

		if (write (fileno(stdout), buf, len) == -1)
			break;

		if (len < (int)sizeof(buf))
			break;

	} while (1);
}

static gboolean
test_message (GMimeMessage *msg)
{
	gchar		*val;
	const gchar	*str;

	val = get_recip (msg, GMIME_ADDRESS_TYPE_FROM);
	g_print ("From   : %s\n", val ? val : "<none>" );
	g_free (val);

	val = get_recip (msg, GMIME_ADDRESS_TYPE_TO);
	g_print ("To     : %s\n", val ? val : "<none>" );
	g_free (val);

	val = get_recip (msg, GMIME_ADDRESS_TYPE_CC);
	g_print ("Cc     : %s\n", val ? val : "<none>" );
	g_free (val);

	val = get_recip (msg, GMIME_ADDRESS_TYPE_BCC);
	g_print ("Bcc    : %s\n", val ? val : "<none>" );
	g_free (val);

	str = g_mime_message_get_subject (msg);
	g_print ("Subject: %s\n", str ? str : "<none>");

	print_date (msg);

	str = g_mime_message_get_message_id (msg);
	g_print ("Msg-id : %s\n", str ? str : "<none>");

	{
		gchar	*refsstr;
		refsstr = get_refs_str (msg);
		g_print ("Refs   : %s\n", refsstr ? refsstr : "<none>");
		g_free (refsstr);
	}

	print_body (msg);

	return TRUE;
}



static gboolean
test_stream (GMimeStream *stream)
{
	GMimeParser *parser;
	GMimeMessage *msg;
	gboolean rv;
	GMimeParserOptions *options;

	parser = NULL;
	msg    = NULL;
	options = NULL;

	parser = g_mime_parser_new_with_stream (stream);
	if (!parser) {
		g_warning ("failed to create parser");
		rv = FALSE;
		goto leave;
	}

	msg = g_mime_parser_construct_message (parser, options);
	if (!msg) {
		g_warning ("failed to construct message");
		rv = FALSE;
		goto leave;
	}

	rv = test_message (msg);

leave:
	if (parser)
		g_object_unref (parser);
	else
		g_object_unref (stream);

	if (msg)
		g_object_unref (msg);

	return rv;
}


static gboolean
test_file (const char *path)
{
	FILE *file;
	GMimeStream *stream;
	gboolean rv;

	stream = NULL;
	file   = NULL;

	file = fopen (path, "r");
	if (!file) {
		g_warning ("cannot open file '%s': %s", path,
			   strerror(errno));
		rv = FALSE;
		goto leave;
	}

	stream = g_mime_stream_file_new (file);
	if (!stream) {
		g_warning ("cannot open stream for '%s'", path);
		rv = FALSE;
		goto leave;
	}

	rv = test_stream (stream);  /* test-stream will unref it */

leave:
	if (file)
		fclose (file);

	return rv;
}


int
main (int argc, char *argv[])
{
	gboolean rv;

	if (argc != 2) {
		g_printerr ("usage: %s <msg-file>\n", argv[0]);
		return 1;
	}

	setlocale (LC_ALL, "");

	g_mime_init();

	rv = test_file (argv[1]);

	g_mime_shutdown ();

	return rv ? 0 : 1;
}
