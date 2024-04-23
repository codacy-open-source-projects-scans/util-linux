/*
 * Copyright (C) 2016 Sami Kerola <kerolasa@iki.fi>
 * Copyright (C) 2016 Karel Zak <kzak@redhat.com>
 *
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 *	added Native Language Support
 * 1999-09-19 Bruno Haible <haible@clisp.cons.org>
 *	modified to work correctly in multi-byte locales
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include "nls.h"
#include "c.h"
#include "widechar.h"
#include "closestream.h"
#include "fgetwc_or_err.h"

/*
 * colcrt - replaces col for crts with new nroff esp. when using tbl.
 * Bill Joy UCB July 14, 1977
 *
 * This filter uses the up and down sequences generated by the new
 * nroff when used with tbl and by \u \d and \r.
 * General overstriking doesn't work correctly.
 * Underlining is split onto multiple lines, etc.
 *
 * Option - suppresses all underlining.
 * Option -2 forces printing of all half lines.
 */

enum { OUTPUT_COLS = 132 };

struct colcrt_control {
	FILE		*f;
	wchar_t		line[OUTPUT_COLS + 1];
	wchar_t		line_under[OUTPUT_COLS + 1];
	unsigned int	print_nl:1,
			need_line_under:1,
			no_underlining:1,
			half_lines:1;
};

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] [<file>...]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Filter nroff output for CRT previewing.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -,  --no-underlining    suppress all underlining\n"), out);
	fputs(_(" -2, --half-lines        print all half-lines\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fprintf(out, USAGE_HELP_OPTIONS(25));

	fprintf(out, USAGE_MAN_TAIL("colcrt(1)"));

	exit(EXIT_SUCCESS);
}

static void trim_trailing_spaces(wchar_t *s)
{
	size_t size;
	wchar_t *end;

	size = wcslen(s);
	if (!size)
		return;
	end = s + size - 1;
	while (s <= end && iswspace(*end))
		end--;
	*(end + 1) = L'\0';
}


static void output_lines(struct colcrt_control *ctl, int col)
{
	/* first line */
	trim_trailing_spaces(ctl->line);
	fputws(ctl->line, stdout);

	if (ctl->print_nl)
		fputwc(L'\n', stdout);
	if (!ctl->half_lines && !ctl->no_underlining)
		ctl->print_nl = 0;

	wmemset(ctl->line, L'\0', OUTPUT_COLS);

	/* second line */
	if (ctl->need_line_under) {
		ctl->need_line_under = 0;
		ctl->line_under[col] = L'\0';
		trim_trailing_spaces(ctl->line_under);
		fputws(ctl->line_under, stdout);
		fputwc(L'\n', stdout);
		wmemset(ctl->line_under, L' ', OUTPUT_COLS);

	} else if (ctl->half_lines && 0 < col)
		fputwc(L'\n', stdout);
}

static int rubchars(struct colcrt_control *ctl, int col, int n)
{
	while (0 < n && 0 < col) {
		ctl->line[col] = L'\0';
		ctl->line_under[col] = L' ';
		n--;
		col--;
	}
	return col;
}

static void colcrt(struct colcrt_control *ctl)
{
	int col;
	wint_t c = 0;
	long old_pos;

	ctl->print_nl = 1;
	if (ctl->half_lines)
		fputwc(L'\n', stdout);

	for (col = 0; /* nothing */; col++) {
		if (OUTPUT_COLS - 1 < col) {
			output_lines(ctl, col);
			errno = 0;
			old_pos = ftell(ctl->f);

			while (fgetwc_or_err(ctl->f) != L'\n') {
				long new_pos;

				if (ferror(ctl->f) || feof(ctl->f))
					return;
				new_pos = ftell(ctl->f);
				if (old_pos == new_pos) {
					if (fseek(ctl->f, 1, SEEK_CUR) < 1)
						return;
				} else
					old_pos = new_pos;
			}
			col = -1;
			continue;
		}
		c = fgetwc_or_err(ctl->f);
		switch (c) {
		case 033:	/* ESC */
			c = fgetwc_or_err(ctl->f);
			if (c == L'8') {
				col = rubchars(ctl, col, 1);
				continue;
			}
			if (c == L'7') {
				col = rubchars(ctl, col, 2);
				continue;
			}
			continue;
		case WEOF:
			ctl->print_nl = 0;
			output_lines(ctl, col);
			return;
		case L'\n':
			output_lines(ctl, col);
			col = -1;
			continue;
		case L'\t':
			for (/* nothing */; col % 8 && col < OUTPUT_COLS; col++) {
				ctl->line[col] = L' ';
			}
			col--;
			continue;
		case L'_':
			ctl->line[col] = L' ';
			if (!ctl->no_underlining) {
				ctl->need_line_under = 1;
				ctl->line_under[col] = L'-';
			}
			continue;
		default:
			if (!iswprint(c)) {
				col--;
				continue;
			}
			ctl->print_nl = 1;
			ctl->line[col] = c;
		}
	}
}

int main(int argc, char **argv)
{
	struct colcrt_control ctl = { NULL };
	int opt;
	enum { NO_UL_OPTION = CHAR_MAX + 1 };

	static const struct option longopts[] = {
		{"no-underlining", no_argument, NULL, NO_UL_OPTION},
		{"half-lines", no_argument, NULL, '2'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	/* Take care of lonely hyphen option. */
	for (opt = 0; opt < argc; opt++) {
		if (argv[opt][0] == '-' && argv[opt][1] == '\0') {
			ctl.no_underlining = 1;
			argc--;
			memmove(argv + opt, argv + opt + 1,
				sizeof(char *) * (argc - opt));
			opt--;
		}
	}

	while ((opt = getopt_long(argc, argv, "2Vh", longopts, NULL)) != -1) {
		switch (opt) {
		case NO_UL_OPTION:
			ctl.no_underlining = 1;
			break;
		case '2':
			ctl.half_lines = 1;
			break;

		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	argc -= optind;
	argv += optind;

	do {
		wmemset(ctl.line, L'\0', OUTPUT_COLS);
		wmemset(ctl.line_under, L' ', OUTPUT_COLS);

		if (argc > 0) {
			if (!(ctl.f = fopen(*argv, "r")))
				err(EXIT_FAILURE, _("cannot open %s"), *argv);
			argc--;
			argv++;
		} else
			ctl.f = stdin;

		colcrt(&ctl);
		if (ctl.f != stdin)
			fclose(ctl.f);
	} while (argc > 0);

	return EXIT_SUCCESS;
}
