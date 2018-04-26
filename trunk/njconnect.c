/* -*- Mode: C ; c-basic-offset: 2 -*- */
/*****************************************************************************
 *
 * ncurses Jack patchbay
 *
 * Copyright (C) Xj <xj@wp.pl>
 *   with lots of patches from G.raud Meyer
 *
 * based on naconnect by Nedko Arnaudov <nedko@arnaudov.name>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/

#include <string.h>
#include <ncurses.h>
#include <jack/jack.h>
#include <jack/jslist.h>
#include <stdbool.h>

/* Functions forgotten by Jack-Devs */
#include "jslist_extra.h"

#include "port_connection.h"
#include "window.h"

#define APPNAME "njconnect"
#define VERSION "1.6"

#define KEY_TAB '\t'

#define WOUT_X 0
#define WOUT_Y 0
#define WOUT_W cols / 2
#define WOUT_H rows / 2

#define WIN_X cols / 2
#define WIN_Y 0
#define WIN_W cols - cols / 2
#define WIN_H rows / 2

#define WCON_X 0
#define WCON_Y rows / 2
#define WCON_W cols
#define WCON_H rows - rows / 2 - 1

#define WSTAT_X 0
#define WSTAT_Y rows - 1
#define WSTAT_W cols
#define WSTAT_H 0

#define MSG_OUT(format, arg...) printf(format "\n", ## arg)
#define ERR_OUT(format, arg...) ( endwin(), fprintf(stderr, format "\n", ## arg), refresh() )

// Common Strings
const char* CON_NAME_A          = "Audio Connections";
const char* CON_NAME_M          = "MIDI Connections";
const char* ERR_CONNECT         = "Connection failed";
const char* ERR_DISCONNECT      = "Disconnection failed";
const char* GRAPH_CHANGED       = "Graph changed";
const char* SAMPLE_RATE_CHANGED = "Sample rate changed";
const char* BUFFER_SIZE_CHANGED = "Buffer size changed";
const char* DEFAULT_STATUS      = "->> Press SHIFT+H or ? for help <<-";


typedef struct {
	jack_client_t* client;
	jack_nframes_t sample_rate;
	jack_nframes_t buffer_size;
	bool rt;
	bool want_refresh;
	const char* err_msg;

	/* Windows */
	unsigned short window_selection;
	Window windows[3];
	WINDOW* status_window;
	WINDOW* grid_window;
	bool grid_redraw;
	bool need_mark;
} NJ;

void suppress_jack_log(const char* msg) {
	/* Just suppress Jack SPAM here ;-) */
}

unsigned short
choose_color( Window* W, JSList* node, bool item_selected ) {
	bool item_mark = false;
	if ( W->type == WIN_PORTS ) {
		Port* p = node->data;
		if ( p->mark )
			item_mark = true;
	}

	if ( ! item_selected )
		return item_mark ? 8 : 1;

	if ( W->selected )
		return 3;

	/* not selected window, selected item */
	return item_mark ? 9 : 2;
}

void w_draw_list(Window* W) {
	unsigned short rows, cols;
	getmaxyx(W->window_ptr, rows, cols);

	short offset = W->index + 3 - rows; // first displayed index
	if(offset < 0) offset = 0;

	unsigned short row = 1, col = 1;
	JSList* node;
	for ( node=jack_slist_nth(W->list,offset); node; node=jack_slist_next(node) ) {
		char fmt[40];
		bool item_selected = ( row == W->index - offset + 1 );

		unsigned short color = choose_color( W, node, item_selected );
		wattron(W->window_ptr, COLOR_PAIR(color));

		switch( W->type ) {
			case WIN_PORTS:;
				Port* p = node->data;
				const char* pfmt = p->mark ? "*%%-%d.%ds" : "%%-%d.%ds";

				snprintf(fmt, sizeof(fmt), pfmt, cols - 2, cols - 2);
				mvwprintw(W->window_ptr, row, col, fmt, p->name);
				break;
			case WIN_CONNECTIONS:;
				Connection* c = node->data;
				snprintf(fmt, sizeof(fmt), "%%%d.%ds -> %%-%d.%ds",
					cols/2 - 3, cols/2 - 3, cols/2 - 3, cols/2 - 3);
				mvwprintw(W->window_ptr, row, col, fmt, c->out->name, c->in->name);
				break;
		}
		wattroff(W->window_ptr, COLOR_PAIR(color));
		wclrtoeol(W->window_ptr);
		row++;
	}
}

void w_draw(Window* W) {
	w_draw_list(W);
	wclrtobot(W->window_ptr);
	w_draw_border(W);
	wrefresh(W->window_ptr);
}

Port*
w_get_selected_port(Window* W) {
	JSList* list = jack_slist_nth(W->list, W->index);
	if (!list) return NULL;

	Port* p = list->data;
	return p;
}

bool nj_connect( NJ* nj ) {
	Window* Wsrc = nj->windows;
	Window* Wdst = nj->windows + 1;

	Port* src = w_get_selected_port(Wsrc);
	if(!src) return false;

	Port* dst = w_get_selected_port(Wdst);
	if(!dst) return false;

	if (jack_connect(nj->client, src->name, dst->name) ) return false;

	/* Move selections to next items */
	w_item_next(Wsrc);
	w_item_next(Wdst);
	return true;
}

bool nj_disconnect( NJ* nj ) {
	Window* W = nj->windows + 2;

	JSList* list_item = jack_slist_nth(W->list, W->index);
	if ( ! list_item) return false;

	Connection* c = list_item->data;
	int ret = jack_disconnect(nj->client, c->out->name, c->in->name);
	if ( ret != 0 ) return false;

	return true;
}

bool nj_disconnect_all( NJ* nj ) {
	Window* W = nj->windows + 2;

	JSList* node;
	for ( node=W->list; node; node=jack_slist_next(node) ) {
		Connection* c = node->data;
		int ret = jack_disconnect(nj->client, c->out->name, c->in->name);
		if ( ret != 0 ) return false;
	}
	return true;
}

void nj_select_window( NJ* nj, short new ) {
	short current = nj->window_selection;

	if (new > 2) {
		new = 0;
	} else if (new < 0) {
		new = 2;
	}

	/* Do not select connections window if is empty */
	if (new == 2 && nj->windows[2].count == 0)
		new = (new > current) ? 0 : 1;

	if (new == current) return;

	nj->windows[current].selected = false;
	nj->windows[current].redraw = true;

	nj->windows[new].selected = true;
	nj->windows[new].redraw = true;

	nj->window_selection = new;
}

Window* nj_get_selected_window( NJ* nj ) {
	return &( nj->windows[ nj->window_selection ] );
}

void nj_set_redraw( NJ* nj ) {
	Window* sw = nj_get_selected_window( nj );
	switch ( sw->type ) {
		case WIN_PORTS:
			nj->windows[0].redraw = true;
			nj->windows[1].redraw = true;
			break;
		case WIN_CONNECTIONS:
			nj->windows[2].redraw = true;
			break;
	}
	nj->need_mark = true;
}

int graph_order_handler(void *arg) {
	NJ* nj = arg;
	nj->err_msg = GRAPH_CHANGED;
	nj->want_refresh = true;
	return 0;
}

int buffer_size_handler( jack_nframes_t buffer_size, void *arg ) {
	NJ* nj = arg;
	nj->buffer_size = buffer_size;
	nj->err_msg = BUFFER_SIZE_CHANGED;
	return 0;
}

int sample_rate_handler( jack_nframes_t sample_rate, void *arg ) {
	NJ* nj = arg;
	nj->sample_rate = sample_rate;
	nj->err_msg = SAMPLE_RATE_CHANGED;
	return 0;
}

int process_handler( jack_nframes_t nframes, void *arg ) {
	return 0;
}

void draw_status( NJ* nj ) {
	WINDOW* w = nj->status_window;

	wmove(w, 0, 0);

	// Message
	int color;
	const char* msg;
	if ( nj->err_msg != NULL ) {
		msg = nj->err_msg;
		nj->err_msg = NULL;
		color = 6;
	} else {
		msg = DEFAULT_STATUS;
		color = 5;
	}

	wclrtoeol(w);

	// Jack stuff
	wattron(w, COLOR_PAIR(color));
	mvwprintw(w, 0, 1, msg);
	wattroff(w, COLOR_PAIR(color));

	unsigned short cols = getmaxx(w);
	wattron(w, COLOR_PAIR(7));
	mvwprintw(w, 0, cols-23,
		"%d/%d DSP:%4.2f%s",
		nj->sample_rate,
		nj->buffer_size,
		jack_cpu_load( nj->client ),
		nj->rt ? "@RT" : "!RT"
	);
	wattroff(w, COLOR_PAIR(7));

	wrefresh(w);
}

void nj_redraw_windows( NJ* nj ) {
	unsigned short i;
	for ( i=0; i < 3; i++ ) {
		Window* w = nj->windows + i;
		if ( w->redraw ) {
			w->redraw = false;
			w_draw( w );
		}
	}
}

enum Orientation { ORT_VERT, ORT_HORIZ };
void grid_draw_port_list ( WINDOW* w, JSList* list, int start, enum Orientation ort ) {
	unsigned short rows, cols;
	getmaxyx(w, rows, cols);

	unsigned short row, col;
	if ( ort == ORT_VERT ) {
		row = 1; col = start;
		mvwvline(w, row, col, ACS_VLINE, rows);
	} else { /* assume ORT_HORIZ */
		row = start; col = 1;
		mvwhline(w, row, col, ACS_HLINE, cols);
	}

	JSList* node;
	for ( node=jack_slist_nth(list,0); node; node=jack_slist_next(node) ) {
		Port* p = node->data;

		/* Draw port name */
		wattron(w, COLOR_PAIR(1));
		if ( ort == ORT_VERT ) {
			mvwprintw(w, row++, ++col, "%s", p->name);
		} else { /* assume ORT_HORIZ */
			mvwprintw(w, ++row, col, "%s", p->name);
		}
		wattroff(w, COLOR_PAIR(1));

		/* Draw line */
		if ( ort == ORT_VERT ) {
			mvwvline(w, row, ++col, ACS_VLINE, rows);
		} else { /* assume ORT_HORIZ */
			mvwhline(w, ++row, col, ACS_HLINE, cols);
		}
	}
}

void nj_draw_grid ( NJ* nj ) {
	if ( ! nj->grid_redraw ) return;

	nj->grid_redraw = false;

	WINDOW* w = nj->grid_window;
	JSList* list_out = nj->windows[0].list;
	JSList* list_in  = nj->windows[1].list;
	JSList* list_con = nj->windows[2].list;

	werase ( w );

	/* IN */
	int start_col = get_max_port_name ( list_out ) + 1;
	grid_draw_port_list ( w, list_in, start_col, ORT_VERT );

	/* OUT */
	int start_row = jack_slist_length( list_in ) + 1;
	grid_draw_port_list ( w, list_out, start_row, ORT_HORIZ );

	/* Draw Connections */
	JSList* node;
	for ( node=jack_slist_nth(list_con,0); node; node=jack_slist_next(node) ) {
		Connection* c = node->data;

		int in_pos = jack_slist_find_pos ( list_in, c->in );
		int col = start_col + 1 + in_pos * 2;

		int out_pos = jack_slist_find_pos ( list_out, c->out );
		int row = start_row + 1 + out_pos * 2;

		wattron(w, COLOR_PAIR(2));
		mvwprintw(w, row, col, "%c", 'X' );
		wattroff(w, COLOR_PAIR(2));
	}

	/* Draw border */
	wattron(w, COLOR_PAIR(1));
	box(w, 0, 0);
	wattroff(w, COLOR_PAIR(1));

	wrefresh(w);
}

bool init_jack( NJ* nj ) {
	/* Some Jack versions are very aggressive in breaking view */
	jack_set_info_function(suppress_jack_log);
	jack_set_error_function(suppress_jack_log);

	/* Initialize jack */
	jack_status_t status;
	nj->client = jack_client_open (APPNAME, JackNoStartServer, &status);
	if (! nj->client) {
		if (status & JackServerFailed) ERR_OUT ("JACK server not running");
		else ERR_OUT ("jack_client_open() failed, status = 0x%2.0x", status);
		return false;
	}
	nj->sample_rate = jack_get_sample_rate( nj->client );
	nj->buffer_size = jack_get_buffer_size( nj->client );
	nj->rt = jack_is_realtime( nj->client );
	nj->err_msg = NULL;
	nj->want_refresh = false;

	jack_set_graph_order_callback( nj->client, graph_order_handler, nj );
	jack_set_buffer_size_callback( nj->client, buffer_size_handler, nj );
	jack_set_sample_rate_callback( nj->client, sample_rate_handler, nj );

	/* NOTE: need minimal process callback for Jack1 to call graph order handler */
	jack_set_process_callback ( nj->client, process_handler, NULL );

	jack_activate( nj->client );

	return true;
}

void show_help() {
	struct help {
		const char* keys;
		const char* action;
	};

	struct help h[] = {
		{ "a", "manage audio" },
		{ "m", "manage MIDI" },
		{ "g", "Toggle grid view" },
		{ "TAB / SHIFT + j", "select next window" },
		{ "SHIFT + TAB / K", "select previous window" },
		{ "SPACE", "select connections window" },
		{ "LEFT / h", "select output ports window" },
		{ "RIGHT / l", "select input ports window" },
		{ "UP / k", "select previous item on list" },
		{ "DOWN / j", "select previous item on list" },
		{ "HOME", "select first item on list" },
		{ "END", "select last item on list" },
		{ "c / ENTER", "connect" },
		{ "d / BACKSPACE", "disconnect" },
		{ "SHIFT + d", "disconnect all" },
		{ "r", "refresh" },
		{ "q", "quit" },
		{ "SHIFT + h / ?", "help info (just what you see right now ;-)" },
		{ NULL, NULL }
	};

	unsigned short rows, cols;
	getmaxyx(stdscr, rows, cols);

	WINDOW* w = newwin(rows , cols, 0, 0);
	wattron(w, COLOR_PAIR(6));
	wprintw( w, "\n"
			"          _                                _\n"
			"   _ _   (_) __  ___  _ _   _ _   ___  __ | |_\n"
			"  | ' \\  | |/ _|/ _ \\| ' \\ | ' \\ / -_)/ _||  _|\n"
			"  |_||_|_/ |\\__|\\___/|_||_||_||_|\\___|\\__| \\__|\n"
			"       |__/ version %s by Xj\n", VERSION
	);
	wattroff(w, COLOR_PAIR(6));

	struct help* hh;
	for (hh = h; hh->keys; hh++)
		wprintw( w, "  %15s - %s\n", hh->keys, hh->action );

	wattron(w, COLOR_PAIR(1));
	box(w, 0, 0);
	wattroff(w, COLOR_PAIR(1));

	wrefresh(w);
	wgetch(w);
	delwin(w);
}

void nj_mark_ports ( NJ* nj, JSList* all_ports ) {
	if ( ! nj->need_mark ) return;
	nj->need_mark=false;

	/* Unmark all ports */
	JSList* node;
	for ( node=all_ports; node; node=jack_slist_next(node) ) {
		Port* p = node->data;
		p->mark = false;
	}

	/* Mark connected */
	Port* current_out = w_get_selected_port( nj->windows );
	Port* current_in  = w_get_selected_port( nj->windows + 1 );

	JSList* list_con = nj->windows[2].list;
	for ( node=list_con; node; node=jack_slist_next(node) ) {
		Connection* c = node->data;
		if ( c->in == current_in )
			c->out->mark = true;

		if ( c->out == current_out )
			c->in->mark = true;
	}
}

int main() {
	enum {
		VIEW_MODE_NORMAL,
		VIEW_MODE_GRID
	} ViewMode = VIEW_MODE_NORMAL;

	unsigned short ret, rows, cols;
	const char* PortsType = JACK_DEFAULT_MIDI_TYPE;
	JSList* all_ports_list = NULL;
	NJ nj;
	nj.grid_window = NULL;
	nj.grid_redraw = true;
	nj.window_selection = 0;

	/* Initialize ncurses */
	initscr();
	curs_set(0); /* set cursor invisible */
	noecho();
	getmaxyx(stdscr, rows, cols);

	if (has_colors() == false) {
		ERR_OUT("Your terminal does not support color");
		ret = -1;
		goto qxit;
	}

	start_color();
	use_default_colors();
	init_pair(1, COLOR_CYAN, -1);
	init_pair(2, COLOR_BLACK, COLOR_WHITE);
	init_pair(3, COLOR_BLACK, COLOR_GREEN);
	init_pair(4, COLOR_WHITE, -1);
	init_pair(5, COLOR_BLACK, COLOR_RED);
	init_pair(6, COLOR_YELLOW, -1);
	init_pair(7, COLOR_BLUE, -1);
	init_pair(8, COLOR_RED, -1);
	init_pair(9, COLOR_RED, COLOR_WHITE);

	if ( ! init_jack(&nj) ) {
		ret = 2;
		goto qxit;
	}

	/* Create Help/Status Window */
	nj.status_window = newwin(WSTAT_H, WSTAT_W, WSTAT_Y, WSTAT_X);
	keypad(nj.status_window, true);
	wtimeout(nj.status_window, 1000);

	/* Create windows */
	w_create(nj.windows, WOUT_H, WOUT_W, WOUT_Y, WOUT_Y, "Output Ports", WIN_PORTS);
	w_create(nj.windows+1, WIN_H, WIN_W, WIN_Y, WIN_X, "Input Ports", WIN_PORTS);
	w_create(nj.windows+2, WCON_H, WCON_W, WCON_Y, WCON_X, CON_NAME_M, WIN_CONNECTIONS);
	nj.windows[nj.window_selection].selected = true;

lists:
	/* Build ports, connections list */
	all_ports_list = build_ports( nj.client );
	w_assign_list( nj.windows, select_ports(all_ports_list, JackPortIsOutput, PortsType) );
	w_assign_list( nj.windows+1, select_ports(all_ports_list, JackPortIsInput, PortsType) );
	w_assign_list( nj.windows+2, build_connections( nj.client, all_ports_list, PortsType ) );
	nj.need_mark = true;

loop:
	if ( ViewMode == VIEW_MODE_GRID ) {
		nj_draw_grid( &nj );
	} else { /* Assume VIEW_MODE_NORMAL */
		nj_mark_ports( &nj, all_ports_list );
		nj_redraw_windows( &nj );
	}

	draw_status( &nj );

	Window* selected_window = nj_get_selected_window(&nj);

	int c = wgetch(nj.status_window);
	switch ( c ) {
		/************* Common keys ***********************/
		case 'g': /* Toggle grid */
			if ( ViewMode == VIEW_MODE_GRID ) {
				ViewMode = VIEW_MODE_NORMAL;
				delwin(nj.grid_window);
				nj.grid_window = NULL;
			} else { /* Assume VIEW_MODE_NORMAL */
				ViewMode = VIEW_MODE_GRID;
				unsigned short rows, cols;
				getmaxyx(stdscr, rows, cols);
				nj.grid_window = newwin(rows - 1, cols, 0, 0);
			}
			goto refresh;
		case 'a': /* Show Audio Ports */
			if ( strcmp(PortsType,JACK_DEFAULT_AUDIO_TYPE) == 0 )
				goto loop;

			nj.windows[2].name = CON_NAME_A;
			PortsType = JACK_DEFAULT_AUDIO_TYPE;
			goto refresh;
		case 'm': /* Show MIDI Ports */
			if ( strcmp(PortsType,JACK_DEFAULT_MIDI_TYPE) == 0 )
				goto loop;

			nj.windows[2].name = CON_NAME_M;
			PortsType = JACK_DEFAULT_MIDI_TYPE;
			goto refresh;
		case 'q': /* Quit from app */
		case KEY_EXIT: 
			ret =0;
			goto quit;
		case 'r': /* Force refresh or terminal resize */
		case KEY_RESIZE:
			getmaxyx(stdscr, rows, cols);
			wresize(nj.status_window, WSTAT_H, WSTAT_W);
			mvwin(nj.status_window, WSTAT_Y, WSTAT_X);
			w_resize(nj.windows, WOUT_H, WOUT_W, WOUT_Y, WOUT_X);
			w_resize(nj.windows+1, WIN_H, WIN_W, WIN_Y, WIN_X);
			w_resize(nj.windows+2, WCON_H, WCON_W, WCON_Y, WCON_X);

			if ( ViewMode == VIEW_MODE_GRID )
				wresize(nj.grid_window, rows - 1, cols);

			goto refresh;
		case '?': /* Help */
		case 'H':
			show_help();
			goto refresh;
		/************* Normal mode keys *******************/
		case 'J': /* Select Next window */
		case KEY_TAB:
			nj_select_window( &nj, nj.window_selection + 1 );
			goto loop;
		case 'K': /* Select Previous window */
		case KEY_BTAB:
			nj_select_window( &nj, nj.window_selection - 1 );
			goto loop;
		case 'c': /* Connect */
		case '\n':
		case KEY_ENTER:
			if ( nj_connect(&nj) )
				goto refresh;
			
			nj.err_msg = ERR_CONNECT;
			goto loop;
		case 'd': /* Disconnect */
		case KEY_BACKSPACE:
			if ( nj_disconnect(&nj ) )
				goto refresh;

			nj.err_msg = ERR_DISCONNECT;
			goto loop;
		case 'D': /* Disconnect all */
			if ( ! nj_disconnect_all(&nj) )
				nj.err_msg = ERR_DISCONNECT;

			goto refresh;
		case 'j': /* Select next item on list */
		case KEY_DOWN:
			w_item_next( selected_window );
			nj_set_redraw( &nj );
			goto loop;
		case KEY_UP: /* Select previous item on list */
		case 'k':
			w_item_previous( selected_window );
			nj_set_redraw( &nj );
			goto loop;
		case KEY_HOME: /* Select first item on list */
			selected_window->index = 0;
			nj_set_redraw( &nj );
			goto loop;
		case KEY_END: /* Select last item on list */
			selected_window->index = selected_window->count - 1;
			nj_set_redraw( &nj );
			goto loop;
		case 'h': /* Select left window */
		case KEY_LEFT:
			nj_select_window( &nj, 0 );
			goto loop;
		case 'l': /* Select right window */
		case KEY_RIGHT:
			nj_select_window( &nj, 1 );
			goto loop;
		case ' ': /* Select bottom window */
			nj_select_window( &nj, 2 );
			goto loop;
	}

	if (! nj.want_refresh) goto loop;
refresh:
	nj.want_refresh = false;
	if ( ViewMode == VIEW_MODE_GRID )
		nj.grid_redraw = true;

	free_connections( nj.windows[2].list );
	free_all_ports(all_ports_list);
	w_cleanup(nj.windows); /* Clean windows lists */

	goto lists;
quit:
	free_connections( nj.windows[2].list );
	free_all_ports(all_ports_list);
	w_cleanup(nj.windows); /* Clean windows lists */
	jack_deactivate( nj.client );
	jack_client_close( nj.client );
qxit:
	endwin();
	return ret;
}
