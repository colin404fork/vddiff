/*
Copyright (c) 2016-2017, Carsten Kunze <carsten.kunze@arcor.de>

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

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include "ui.h"
#include "tc.h"
#include "main.h"
#include "ui2.h"
#include "fs.h"
#include "exec.h"
#include "uzp.h"
#include "db.h"
#include "diff.h"

static void set_mb_bg(void);
static void fmode_copy_side(void);

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
	/* Else glyphs are left in right column with ncursesw */
	wclear(wlist);
}

void
fmode_dmode(void)
{
	if (!fmode)
    {
		return;
    }
#   if defined(TRACE)
    fprintf(debug, "->fmode_dmode bm=%u fm=%u 2c=%u\n", bmode ? 1 : 0, fmode ? 1 : 0, twocols ? 1 : 0);
    TRCVPTH;
#   endif
	fmode = FALSE;
	/*diff_db_free(?);*/
	old_col = right_col;
	right_col = 0;
	from_fmode = TRUE;
	pwd  = syspth[0] + pthlen[0];
	rpwd = syspth[1] + pthlen[1];
	close2cwins();
	diff_db_free(0);
	diff_db_free(1);
#if defined(TRACE)
    fprintf(debug, "<-fmode_dmode bm=%u fm=%u 2c=%u\n", bmode ? 1 : 0, fmode ? 1 : 0, twocols ? 1 : 0);
    TRCVPTH;
#endif
}

void
dmode_fmode(
    /* 1: Arg=1 for rebuild_db()
     * 2: Don't disp_list */
    unsigned mode)
{
#   if defined(TRACE)
    fprintf(debug, "->dmode_fmode(%u) bm=%u fm=%u 2c=%u\n", mode, bmode ? 1 : 0, fmode ? 1 : 0, twocols ? 1 : 0);
    TRCVPTH;
#   endif
    if (!fmode)
    {
        while (!bmode && ui_stack)
        {
            pop_state(2);
        }
        if (bmode /* bmode -> fmode */
                || twocols) /* 2C diff -> fmode */
        {
            if (bmode)
            {
                bmode = FALSE; /* from tgl2c() */
                sys_path_tmp_len[0] = sys_path_tmp_len[1];
                free_path_offsets(0);
                copy_path_offsets(1, 0);
            }
            twocols = TRUE;
            fmode = TRUE;
            right_col = old_col;
            open2cwins();
        }
        else /* 1C diff -> bmode */
        {
            bmode = TRUE;
        }
        mk_abs_pth(syspth[0], &pthlen[0]);
        mk_abs_pth(syspth[1], &pthlen[1]);
        rebuild_db((mode & 1) ? 1 : 0);
        if (!(mode & 2))
        {
            disp_fmode();
        }
    }
#   if defined(TRACE)
    fprintf(debug, "<-dmode_fmode(%u) bm=%u fm=%u 2c=%u\n", mode, bmode ? 1 : 0, fmode ? 1 : 0, twocols ? 1 : 0);
    TRCVPTH;
#   endif
}

void
restore_fmode(void)
{
#if defined(TRACE)
	fprintf(debug, "->restore_fmode fpath=%s\n", fpath);
#endif
	if (fpath) {
		size_t l = strlen(fpath);

		if (old_col) {
			memcpy(syspth[0], fpath, l+1);
			pthlen[0] = l;
		} else {
			memcpy(syspth[1], fpath, l+1);
			pthlen[1] = l;
		}

#if defined(TRACE)
		fprintf(debug, "  restore_fmode: free fpath (%s)\n", fpath);
#endif
		free(fpath);
		fpath = NULL;
	}

	dmode_fmode(1);
#if defined(TRACE)
	fprintf(debug, "<-restore_fmode\n");
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
#   if defined(TRACE) && 1
    fprintf(debug, "->prt2chead()\n");
    TRCVPTH;
#   endif
    if (md & 1)
    {
		wmove(wstat, 1, 0);
		wclrtoeol(wstat);
		set_path_display_name(0);
		putmbsra(wstat, path_display_name[0], llstw);
	}
	standoutc(wstat);
	mvwaddch(wstat, 0, llstw, ' ');
    if (!(md & 2))
    {
		mvwaddch(wstat, 1, llstw, ' ');
	}
	standendc(wstat);
	/* Splitted to save one wmove */
    if (md & 1)
    {
		set_path_display_name(1);
		putmbsra(wstat, path_display_name[1], 0);
	}
#   if defined(TRACE) && 1
    fprintf(debug, "<-prt2chead()\n");
    TRCVPTH;
#   endif
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

#   if defined(TRACE)
    fprintf(debug, "->tgl2c(%u) bm=%u fm=%u 2c=%u\n", md, bmode ? 1 : 0, fmode ? 1 : 0, twocols ? 1 : 0);
    TRCVPTH;
#   endif
	if (bmode) { /* -> fmode */
		bool sc = one_scan;

		one_scan = FALSE; /* useless */
		clr_mark();
		s = !(md & 1) && fpath ? fpath : syspth[1];
		l1 = strlen(syspth[1]);
		l2 = strlen(s);

		if (old_col) {
			pthlen[0] = l2;
			pthlen[1] = l1;
			memcpy(syspth[0], s, pthlen[0] + 1);
		} else {
			pthlen[0] = l1;
			pthlen[1] = l2;
			memcpy(syspth[0], syspth[1], pthlen[0] + 1);
			memcpy(syspth[1], s    , pthlen[1] + 1);
		}

		if (md & 1) {
			bmode = FALSE;
			rebuild_db(0);
		} else {
			if (fpath) {
#if defined(TRACE)
				fprintf(debug,
				    "  tgl2c(): free fpath (%s)\n",
				    fpath);
#endif
				free(fpath);
				fpath = NULL;
			}

			dmode_fmode(2); /* 2: no disp_fmode() */

			if (old_col) {
				top_idx[0] = old_top_idx;
				curs[0] = old_curs;
			}

			disp_fmode();
		}

		one_scan = sc;
	} else if (fmode || /* fmode -> bmode */
	    (md & 1)) { /* 1C diff -> bmode */
		clr_mark();
		syspth[0][pthlen[0]] = 0;
		syspth[1][pthlen[1]] = 0;

		/* not relevant for 1C -> bmode */
		if (!(md & 1)) {

			if (right_col) {
#if defined(TRACE)
			fprintf(debug,
			    "  tgl2c(): set fpath[0] = %s\n",
			    syspth[0]);
#endif
				fpath = strdup(syspth[0]);
				old_top_idx = top_idx[0];
				old_curs = curs[0];
			} else {
#if defined(TRACE)
			fprintf(debug,
			    "  tgl2c(): set fpath[1] = %s\n",
			    syspth[1]);
#endif
				fpath = strdup(syspth[1]);
				memcpy(syspth[1], syspth[0], pthlen[0] + 1);
			}
		}

		if (chdir(syspth[1]) == -1) {
			printerr(strerror(errno), "chdir \"%s\":", syspth[1]);
		}

		*syspth[0] = '.';
		syspth[0][1] = 0;
		pthlen[0] = 1;
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
    fprintf(debug, "<-tgl2c(%u) bm=%u fm=%u 2c=%u\n", md, bmode ? 1 : 0, fmode ? 1 : 0, twocols ? 1 : 0);
    TRCVPTH;
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
#   if defined(TRACE)
    fprintf(debug, "->disp_fmode()\n");
    TRCVPTH;
#   endif
    if (fmode) {
		right_col = right_col ? 0 : 1;
		disp_list(0);
		wnoutrefresh(getlstwin());
		touchwin(wmid);
		wnoutrefresh(wmid);
		right_col = right_col ? 0 : 1;
	}

	disp_list(1);
#   if defined(TRACE)
    fprintf(debug, "<-disp_fmode()\n");
    TRCVPTH;
#   endif
}

void fmode_cp_pth(void)
{
#   if defined(TRACE)
    fprintf(debug, "->fmode_cp_pth() right_col=%d\n", right_col);
    TRCVPTH;
#   endif
    fmode_copy_side();
    enter_dir(NULL, NULL, FALSE, FALSE, 0 LOCVAR);
#   if defined(TRACE)
    fprintf(debug, "<-fmode_cp_pth()\n");
    TRCVPTH;
#   endif
}

static void fmode_copy_side(void)
{
    int left_col = right_col ? 0 : 1;
    syspth[left_col][pthlen[left_col]] = 0;
    memcpy(syspth[right_col], syspth[left_col], pthlen[left_col] + 1);
    pthlen[right_col] = pthlen[left_col];
    path_display_name_offset[right_col] = path_display_name_offset[left_col];
    sys_path_tmp_len[right_col] = sys_path_tmp_len[left_col];
    path_display_buffer_size[right_col] = path_display_buffer_size[left_col];
    free(path_display_name[right_col]);
    path_display_name[right_col] = malloc(path_display_buffer_size[right_col]);
    memcpy(path_display_name[right_col], path_display_name[left_col], path_display_buffer_size[left_col]);
    sys_path_tmp_len[right_col] = sys_path_tmp_len[left_col];
    free_path_offsets(right_col);
    copy_path_offsets(left_col, right_col);
#   if defined(TRACE)
    fprintf(debug, "<-fmode_copy_side()\n");
    TRCVPTH;
#   endif
}

void stmove(int i)
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
stmbsra(const char *s1, const char *s2)
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

	syspth[0][pthlen[0]] = 0;
	syspth[1][pthlen[1]] = 0;
	s = right_col ? syspth[1] : syspth[0];

	if (chdir(s) == -1) {
		printerr(strerror(errno), "chdir \"%s\":", s);
	}

	if (printwd) {
		save_last_path(s);
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
