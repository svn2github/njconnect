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

#include <ncurses.h>
#include <string.h>
#include <jack/jack.h>
#include <jack/jslist.h>
#include <stdbool.h>

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

enum WinType {
	WIN_PORTS,
	WIN_CONNECTIONS
};

typedef struct {
	WINDOW* window_ptr;
	JSList* list;
	bool selected;
	bool redraw;
	int height;
	int width;
	const char * name;
	unsigned short index;
	unsigned short count;
	enum WinType type;
} Window;

typedef struct {
	char name[128];
	char type[32];
	int flags;
} Port;

typedef struct {
	const char* type;
	Port* in;
	Port* out;
} Connection;

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
} NJ;

/* Function forgotten by Jack-Devs */
JSList* jack_slist_nth(JSList* list, unsigned short n) {
	unsigned short i = 0;
	JSList* node = list;
	while ( node ) {
		if (i == n) return node;
		node = jack_slist_next(node);
		i++;
	}
	return NULL;
}

int jack_slist_find_pos(JSList* list, void *data) {
	unsigned short i = 0;
	JSList* node = list;
	while ( node ) {
		if (node->data == data) return i;
		node = jack_slist_next(node);
		i++;
	}
	return -1;
}

void suppress_jack_log(const char* msg) {
	/* Just suppress Jack SPAM here ;-) */
}

JSList* build_ports(jack_client_t* client) {
	unsigned short i, count=0;

	const char** jports = jack_get_ports (client, NULL, NULL, 0);
	if(! jports) return NULL;

	while(jports[count]) count++;
	Port* p = calloc(count, sizeof(Port));

	JSList* new = NULL;
	for (i=0; jports[i]; ++i, p++) {
		jack_port_t* jp = jack_port_by_name( client, jports[i] );

		strncpy(p->name, jports[i], sizeof(p->name));
		strncpy(p->type, jack_port_type( jp ), sizeof(p->type));
		p->flags = jack_port_flags( jp );
		new = jack_slist_append(new, p);
	}
	jack_free(jports);

	return new;
}

JSList*
select_ports(JSList* list, int flags, const char* type) {
	JSList* new = NULL;
	JSList* node;
	for ( node=list; node; node=jack_slist_next(node) ) {
		Port* p = node->data;
		if ( (p->flags & flags) && strcmp(p->type, type) == 0 )
			new = jack_slist_append(new, p);
	}

	return new;
}

Port*
get_port_by_name(JSList* list, const char* name) {
	JSList* node;

	for ( node=list; node; node=jack_slist_next(node) ) {
		Port* p = node->data;
		if (strcmp(p->name, name) == 0) return p;
	}
	return NULL;
}

JSList*
build_connections(jack_client_t* client, JSList* list, const char* type) {
	JSList* new = NULL;

	JSList* node;
	for ( node=list; node; node=jack_slist_next(node) ) {
		// For all Input ports
		Port *inp = node->data;
		if(! (inp->flags & JackPortIsInput)) continue;
		if( strcmp(inp->type, type) != 0 ) continue;

		const char** connections = jack_port_get_all_connections (
				client, jack_port_by_name(client, inp->name) );
		if (!connections) continue;

		unsigned short i;
		for (i=0; connections[i]; i++) {
			Port *outp = get_port_by_name(list, connections[i]);
			if(!outp) continue; // WTF can't find OutPort in our list ?

			Connection* c = malloc(sizeof(Connection));
			c->type = type;
			c->in = inp;
			c->out = outp;
			new = jack_slist_append(new, c);
		}
		jack_free(connections);
	}

	return new;
}

void free_all_ports(JSList* all_ports) {
	/* First node is pointer to calloc-ed big chunk */
	if (! all_ports) return;
	free(all_ports->data);
	jack_slist_free(all_ports);
}

void w_draw_border(Window* W) {
	int col = (W->width - strlen(W->name) - 4) / 2;
	if (col < 0) col = 0;

	/* 0, 0 gives default characters for the vertical and horizontal lines */
	box(W->window_ptr, 0, 0);

	if (W->selected) {
		wattron(W->window_ptr, WA_BOLD|COLOR_PAIR(4));
		mvwprintw(W->window_ptr, 0, col, "=[%s]=", W->name);
		wattroff(W->window_ptr, WA_BOLD|COLOR_PAIR(4));
	} else {
		mvwprintw(W->window_ptr, 0, col, " [%s] ", W->name);
	}
}

void w_draw_list(Window* W) {
	unsigned short rows, cols;
	getmaxyx(W->window_ptr, rows, cols);

	short offset = W->index + 3 - rows; // first displayed index
	if(offset < 0) offset = 0;

	unsigned short row =1, col = 1;
	JSList* node;
	for ( node=jack_slist_nth(W->list,offset); node; node=jack_slist_next(node) ) {
		char fmt[40];
		unsigned short color = (row == W->index - offset + 1)
			? (W->selected) ? 3 : 2 : 1;
		wattron(W->window_ptr, COLOR_PAIR(color));

		switch(W->type) {
			case WIN_PORTS:;
				Port* p = node->data;
				snprintf(fmt, sizeof(fmt), "%%-%d.%ds", cols - 2, cols - 2);
				mvwprintw(W->window_ptr, row, col, fmt, p->name);
				break;
			case WIN_CONNECTIONS:;
				Connection* c = node->data;
				snprintf(fmt, sizeof(fmt), "%%%d.%ds -> %%-%d.%ds",
					cols/2 - 3, cols/2 - 3, cols/2 - 3, cols/2 - 3);
				mvwprintw(W->window_ptr, row, col, fmt, c->out->name, c->in->name);
				break;
			default:
				ERR_OUT("Unknown WinType");
		}
		wattroff(W->window_ptr, COLOR_PAIR(color));
		wclrtoeol(W->window_ptr);
		row++;
	}
}

void w_draw(Window* W) {
	wclear(W->window_ptr);
	w_draw_list(W);
	w_draw_border(W);
	wrefresh(W->window_ptr);
}

void
w_create(Window* W, int height, int width, int starty, int startx, const char* name, enum WinType type) {
	W->window_ptr = newwin(height, width, starty, startx);
	W->selected = FALSE;
	W->width = width;
	W->height = height;
	W->name = name;
	W->index = 0;
	W->type = type;
	W->redraw = true;
	//  scrollok(w->window_ptr, TRUE);
}

void
w_assign_list(Window* W, JSList* list) {
	W->list = list;
	W->count = jack_slist_length(W->list);
	if (W->index > W->count - 1) W->index = 0;
}

void
w_resize(Window* W, int height, int width, int starty, int startx) {
	//delwin(W->window_ptr);
	//W->window_ptr = newwin(height, width, starty, startx);
	wresize(W->window_ptr, height, width);
	mvwin(W->window_ptr, starty, startx);
	W->width = width;
	W->height = height;
	W->redraw = true;
}

const char*
get_selected_port_name(Window* W) {
	JSList* list = jack_slist_nth(W->list, W->index);
	if (!list) return NULL;

	Port* p = list->data;
	return p->name;
}

void w_item_next(Window* W) {
	if (W->index < W->count - 1) {
		W->index++;
		W->redraw = true;
	}
}

void w_item_previous(Window* W) {
	if (W->index > 0) {
		W->index--;
		W->redraw = true;
	}
}

bool nj_connect( NJ* nj ) {
	Window* Wsrc = nj->windows;
	Window* Wdst = nj->windows + 1;

	const char* src = get_selected_port_name(Wsrc);
	if(!src) return FALSE;

	const char* dst = get_selected_port_name(Wdst);
	if(!dst) return FALSE;

	if (jack_connect(nj->client, src, dst) ) return FALSE;

	/* Move selections to next items */
	w_item_next(Wsrc);
	w_item_next(Wdst);
	return TRUE;
}

bool nj_disconnect( NJ* nj ) {
	Window* W = nj->windows + 2;

	JSList* list_item = jack_slist_nth(W->list, W->index);
	if (! list_item) return false;

	Connection* c = list_item->data;
	int ret = jack_disconnect(nj->client, c->out->name, c->in->name);
	if ( ret != 0 ) return false;

	/* Move back index if it was last on the list */
	if (W->index >= W->count - 1)
		w_item_previous(W);

	W->list = jack_slist_remove_link(W->list, list_item);
	W->count--;
	W->redraw = true;

	return true;
}

void w_cleanup(Window* windows) {
	short i;
	Window* w = windows;

	for(i = 0; i < 3; i++, w++) {
		JSList* l = w->list;
		if ( w->type == WIN_CONNECTIONS ) {
			JSList* node;
			for ( node=w->list; node; node=jack_slist_next(node) )
				free(node->data);
		}
		jack_slist_free(l);
		w->redraw = true;
	}
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

	nj->windows[current].selected = FALSE;
	nj->windows[current].redraw = true;

	nj->windows[new].selected = TRUE;
	nj->windows[new].redraw = true;

	nj->window_selection = new;
}

Window* nj_get_selected_window( NJ* nj ) {
	return &( nj->windows[ nj->window_selection ] );
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
	wclrtoeol(w);

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

int get_max_port_name ( JSList* list ) {
	int ret = 0;
	JSList* node;
	for ( node=list; node; node=jack_slist_next(node) ) {
		Port* p = node->data;
		int len = strlen ( p->name );
		if ( len > ret ) ret = len;
	}
	return ret;
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
	WINDOW* w = nj->grid_window;
	JSList* list_out = nj->windows[0].list;
	JSList* list_in  = nj->windows[1].list;
	JSList* list_con = nj->windows[2].list;

	wclear ( w );

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
	nj->want_refresh = FALSE;

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
		{ "RIGHT / j", "select input ports window" },
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

enum ViewMode { VIEW_MODE_NORMAL, VIEW_MODE_GRID };
int main() {
	unsigned short ret, rows, cols;
	enum ViewMode ViewMode = VIEW_MODE_NORMAL;
	const char* PortsType = JACK_DEFAULT_MIDI_TYPE;
	JSList *all_list = NULL;
	NJ nj;
	nj.grid_window = NULL;
	nj.window_selection = 0;

	/* Initialize ncurses */
	initscr();
	curs_set(0); /* set cursor invisible */
	noecho();
	getmaxyx(stdscr, rows, cols);

	if (has_colors() == FALSE) {
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

	if ( ! init_jack(&nj) ) {
		ret = 2;
		goto qxit;
	}

	/* Create Help/Status Window */
	nj.status_window = newwin(WSTAT_H, WSTAT_W, WSTAT_Y, WSTAT_X);
	keypad(nj.status_window, TRUE);
	wtimeout(nj.status_window, 1000);

	/* Create windows */
	w_create(nj.windows, WOUT_H, WOUT_W, WOUT_Y, WOUT_Y, "Output Ports", WIN_PORTS);
	w_create(nj.windows+1, WIN_H, WIN_W, WIN_Y, WIN_X, "Input Ports", WIN_PORTS);
	w_create(nj.windows+2, WCON_H, WCON_W, WCON_Y, WCON_X, CON_NAME_M, WIN_CONNECTIONS);
	nj.windows[nj.window_selection].selected = TRUE;

lists:
	/* Build ports, connections list */
	all_list = build_ports( nj.client );
	w_assign_list( nj.windows, select_ports(all_list, JackPortIsOutput, PortsType) );
	w_assign_list( nj.windows+1, select_ports(all_list, JackPortIsInput, PortsType) );
	w_assign_list( nj.windows+2, build_connections( nj.client, all_list, PortsType ) );

loop:
	if ( ViewMode == VIEW_MODE_GRID ) {
		nj_draw_grid( &nj );
	} else { /* Assume VIEW_MODE_NORMAL */
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
			nj.windows[2].name = CON_NAME_A;
			PortsType = JACK_DEFAULT_AUDIO_TYPE;
			goto refresh;
		case 'm': /* Show MIDI Ports */
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
			if ( ! nj_disconnect(&nj ) )
				nj.err_msg = ERR_DISCONNECT;

			goto loop;
		case 'D': /* Disconnect all */
			while ( nj_disconnect(&nj) );
			goto loop;
		case 'j': /* Select next item on list */
		case KEY_DOWN:
			w_item_next( selected_window );
			goto loop;
		case KEY_UP: /* Select previous item on list */
		case 'k':
			w_item_previous( selected_window );
			goto loop;
		case KEY_HOME: /* Select first item on list */
			selected_window->index = 0;
			selected_window->redraw = true;
			goto loop;
		case KEY_END: /* Select last item on list */
			selected_window->index = selected_window->count - 1;
			selected_window->redraw = true;
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
	nj.want_refresh = FALSE;
	free_all_ports(all_list);
	w_cleanup(nj.windows); /* Clean windows lists */

	goto lists;
quit:
	free_all_ports(all_list);
	w_cleanup(nj.windows); /* Clean windows lists */
	jack_deactivate( nj.client );
	jack_client_close( nj.client );
qxit:
	endwin();
	return ret;
}
