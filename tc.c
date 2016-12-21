/*
Copyright (c) 2016, Carsten Kunze <carsten.kunze@arcor.de>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
*/

#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <stdarg.h>
#include "compat.h"
#include "ui.h"
#include "tc.h"
#include "main.h"
#include "ui2.h"
#include "fs.h"
#include "exec.h"
#include "uzp.h"
#include "db.h"

static void set_mb_bg(void);

int llstw, rlstw, rlstx, midoffs;
/* Used for bmode <-> fmode transitions the remember fmode column */
int old_col;
static unsigned old_top_idx, old_curs;
/* fmode <-> bmode: Path of other column */
char *fpath;
WINDOW *wllst, *wmid, *wrlst;
bool twocols;
bool fmode;
bool right_col;
bool from_fmode;

void
open2cwins(void)
{
	if (!(wllst = new_scrl_win(listh, llstw, 0, 0))) {
		return;
	}

	if (!(wmid = newwin(listh, 1, 0, llstw))) {
		printf("newwin failed\n");
		return;
	}

	if (!(wrlst = new_scrl_win(listh, rlstw, 0, rlstx))) {
		return;
	}

	set_mb_bg();
	wnoutrefresh(wmid);
}

static void
set_mb_bg(void)
{
	if (color) {
		wbkgd(wmid, COLOR_PAIR(PAIR_CURSOR));
	} else {
		wbkgd(wmid, A_STANDOUT);
	}
}

void
close2cwins(void)
{
	delwin(wllst);
	delwin(wmid);
	delwin(wrlst);
}

void
fmode_dmode(void)
{
	if (!fmode)
		return;

#if defined(TRACE)
	fprintf(debug, "->fmode_dmode lp(%s) rp(%s) bm=%u fm=%u 2c=%u\n",
	    lpath, rpath, bmode ? 1 : 0, fmode ? 1 : 0, twocols ? 1 : 0);
#endif
	fmode = FALSE;
	/*diff_db_free(?);*/
	old_col = right_col;
	right_col = 0;
	from_fmode = TRUE;
	pwd  = lpath + llen;
	rpwd = rpath + rlen;
	close2cwins();
#if defined(TRACE)
	fprintf(debug, "<-fmode_dmode lp(%s) rp(%s) bm=%u fm=%u 2c=%u\n",
	    lpath, rpath, bmode ? 1 : 0, fmode ? 1 : 0, twocols ? 1 : 0);
#endif
}

void
dmode_fmode(
    /* 1: Arg=1 for rebuild_db()
     * 2: Don't disp_list */
    unsigned mode)
{
	if (fmode)
		return;

#if defined(TRACE)
	fprintf(debug, "->dmode_fmode(%u) lp(%s) rp(%s) bm=%u fm=%u 2c=%u\n",
	    mode, lpath, rpath, bmode ? 1 : 0, fmode ? 1 : 0, twocols ? 1 : 0);
#endif
	while (!bmode && ui_stack)
		pop_state(0);

	if (bmode || /* bmode -> fmode */
	    twocols) { /* 2C diff -> fmode */
		bmode = FALSE; /* from tgl2c() */
		twocols = TRUE;
		fmode = TRUE;
		right_col = old_col;
		open2cwins();
	} else { /* 1C diff -> bmode */
		bmode = TRUE;
	}

	mk_abs_pth(lpath, &llen);
	mk_abs_pth(rpath, &rlen);
	rebuild_db((mode & 1) ? 1 : 0);

	if (!(mode & 2)) {
		disp_fmode();
	}
#if defined(TRACE)
	fprintf(debug, "<-dmode_fmode(%u) lp(%s) rp(%s) bm=%u fm=%u 2c=%u\n",
	    mode, lpath, rpath, bmode ? 1 : 0, fmode ? 1 : 0, twocols ? 1 : 0);
#endif
}

void
mk_abs_pth(char *p, size_t *l)
{
	char *s;

	if (*p == '/') {
		return;
	}

	if (!(s = realpath(p, NULL))) {
		printerr(strerror(errno), LOCFMT "realpath \"%s\"" LOCVAR, p);
		return;
	}

	*l = strlen(s);
	memcpy(p, s, *l + 1);
	free(s);
}

void
prt2chead(
    /* 2: mark && !fmode */
    /* 1: Show path */
    unsigned md)
{
	if (md & 1) {
		wmove(wstat, 1, 0);
		wclrtoeol(wstat);
		lpath[llen] = 0;
		putmbsra(wstat, lpath, llstw);
	}

	standoutc(wstat);
	mvwaddch(wstat, 0, llstw, ' ');

	if (!(md & 2)) {
		mvwaddch(wstat, 1, llstw, ' ');
	}

	standendc(wstat);

	/* Splitted to save one wmove */

	if (md & 1) {
		rpath[rlen] = 0;
		putmbsra(wstat, rpath, 0);
	}
}

WINDOW *
getlstwin(void)
{
	return right_col ? wrlst :
	       fmode     ? wllst :
	                   wlist ;
}

void
tgl2c(
    /* 1: bmode <-> 1C diff mode */
    unsigned md)
{
	size_t l1, l2;
	char *s;

#if defined(TRACE)
	fprintf(debug, "->tgl2c(%u) lp(%s) rp(%s) bm=%u fm=%u 2c=%u\n",
	    md, lpath, rpath, bmode ? 1 : 0, fmode ? 1 : 0, twocols ? 1 : 0);
#endif
	if (bmode) { /* -> fmode */
		clr_mark();
		s = !(md & 1) && fpath ? fpath : rpath;
		l1 = strlen(rpath);
		l2 = strlen(s);

		if (old_col) {
			llen = l2;
			rlen = l1;
			memcpy(lpath, s, llen + 1);
		} else {
			llen = l1;
			rlen = l2;
			memcpy(lpath, rpath, llen + 1);
			memcpy(rpath, s    , rlen + 1);
		}

		if (md & 1) {
			bmode = FALSE;
			rebuild_db(0);
		} else {
			if (fpath) {
				free(fpath);
				fpath = NULL;
			}

			dmode_fmode(2);

			if (old_col) {
				top_idx[0] = old_top_idx;
				curs[0] = old_curs;
			}
		}

		disp_fmode();

	} else if (fmode || /* fmode -> bmode */
	    (md & 1)) { /* 1C diff -> bmode */
		clr_mark();
		lpath[llen] = 0;
		rpath[rlen] = 0;

		if (right_col) {
			fpath = strdup(lpath);
			old_top_idx = top_idx[0];
			old_curs = curs[0];
		} else {
			fpath = strdup(rpath);
			memcpy(rpath, lpath, llen + 1);
		}

		if (chdir(rpath) == -1) {
			printerr(strerror(errno), "chdir \"%s\":", rpath);
		}

		*lpath = '.';
		lpath[1] = 0;
		llen = 1;
		fmode_dmode();
		bmode = TRUE;
		twocols = FALSE;
		rebuild_db(0);

		if (old_col) {
			top_idx[0] = top_idx[1];
			curs[0] = curs[1];
		}

		disp_list(1);

	} else { /* 1C <-> 2C diff modes */
		twocols = twocols ? FALSE : TRUE;
		disp_fmode();
	}
#if defined(TRACE)
	fprintf(debug, "<-tgl2c(%u) lp(%s) rp(%s) bm=%u fm=%u 2c=%u\n",
	    md, lpath, rpath, bmode ? 1 : 0, fmode ? 1 : 0, twocols ? 1 : 0);
#endif
}

void
resize_fmode(void)
{
	set_win_dim();

	if (fmode) {
		close2cwins();
		open2cwins();
	}

	disp_fmode();
}

void
disp_fmode(void)
{
	if (fmode) {
		right_col = right_col ? 0 : 1;
		disp_list(0);
		wnoutrefresh(getlstwin());
		touchwin(wmid);
		wnoutrefresh(wmid);
		right_col = right_col ? 0 : 1;
	}

	disp_list(1);
}

void
fmode_cp_pth(void)
{
	if (right_col) {
		lpath[llen] = 0;
		memcpy(rpath, lpath, llen + 1);
		rlen = llen;
	} else {
		rpath[rlen] = 0;
		memcpy(lpath, rpath, rlen + 1);
		llen = rlen;
	}

	enter_dir(NULL, NULL, FALSE, FALSE, 0);
}

void
stmove(int i)
{
	if (twocols) {
		if (i)
			wmove(wstat, 1, rlstx);
		else
			wmove(wstat, 1, 0);
	} else {
		if (i)
			wmove(wstat, 1, 2);
		else
			wmove(wstat, 0, 2);
	}
}

void
stmbsra(char *s1, char *s2)
{
	stmove(0);
	putmbsra(wstat, s1, twocols ? llstw : 0);
	stmove(1);
	putmbsra(wstat, s2, 0);
}

void
fmode_chdir(void)
{
	char *s;

	lpath[llen] = 0;
	rpath[rlen] = 0;
	s = right_col ? rpath : lpath;

	if (chdir(s) == -1) {
		printerr(strerror(errno), "chdir \"%s\":", s);
	}
}

#ifdef NCURSES_MOUSE_VERSION
void
movemb(int x)
{
	int dx;

	if (!(dx = x - llstw)) {
		return;
	}

	llstw += dx;
	midoffs += dx;

	if (!fmode) {
		return;
	}

	wbkgd(wmid, 0);
	wrefresh(wmid);
	mvwin(wmid, 0, llstw);
	set_mb_bg();
	wrefresh(wmid);
}

void
doresizecols(void)
{
	set_def_mouse_msk();
	resize_fmode();
}
#endif
