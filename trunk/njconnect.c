/* -*- Mode: C ; c-basic-offset: 2 -*- */
/*****************************************************************************
 *
 * ncurses Jack patchbay
 *
 * Copyright (C) 2012 Xj <xj@wp.pl>
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
#define CON_NAME_A "Audio Connections"
#define CON_NAME_M "MIDI Connections"

#define ERR_CONNECT "Connection failed"
#define ERR_DISCONNECT "Disconnection failed"
#define GRAPH_CHANGED "Graph changed"
#define HELP "'q'uit, U/D-list selection, L/R-panels, TAB-focus, 'm'idi, 'a'udio, 'r'efresh, 'c'onnect, 'd'isconnect"
#define KEY_TAB '\t'

#define MSG_OUT(format, arg...) printf(format "\n", ## arg)
#define ERR_OUT(format, arg...) fprintf(stderr, format "\n", ## arg)

enum WinType {
   WIN_PORTS,
   WIN_CONNECTIONS
};

struct window {
  WINDOW * window_ptr;
  JSList* list_ptr;
  bool selected;
  int height;
  int width;
  const char * name;
  unsigned short index;
  unsigned short count;
  enum WinType type;
};

struct port {
   char name[64];
   char type[32];
   int flags;
};

struct connection {
   const char* type;
   struct port* in;
   struct port* out;
};

struct graph {
   bool* want_refresh;
   const char** err_message;
};

/* Functions forgotten by Jack-Devs */
JSList* jack_slist_nth(JSList* list_ptr, unsigned short n) {
   unsigned short i = 0;
   JSList* node;

   for(node=list_ptr; node ; node = jack_slist_next(node) )
      if (i++ == n) return node;
   return NULL;
}

void window_item_next(struct window* w) { if (w->index < w->count - 1) w->index++; }
void window_item_previous(struct window* w) { if (w->index > 0) w->index--; }

JSList* build_ports(jack_client_t* client) {
   JSList* new = NULL;
   jack_port_t* jp;
   unsigned short i, size=0;
   struct port* p;

   const char** jports = jack_get_ports (client, NULL, NULL, 0);
   if(! jports) return NULL;

   while(jports[size]) size++;
   p = calloc(size, sizeof(struct port));

   for (i=0; jports[i]; ++i, p++) {
       jp = jack_port_by_name( client, jports[i] );

//       p = malloc(sizeof(struct port));
       strncpy(p->name, jports[i], sizeof(p->name));
       strncpy(p->type, jack_port_type( jp ), sizeof(p->type));
       p->flags = jack_port_flags( jp );
       new = jack_slist_append(new, p);
   }
   jack_free(jports);
   
   return new;
}

JSList*
select_ports(JSList* list_ptr, int flags, const char* type) {
   JSList* new = NULL;
   JSList* node;
   struct port* p;
   for ( node=list_ptr; node; node=jack_slist_next(node) ) {
       p = node->data;
       if ( (p->flags & flags) && strcmp(p->type, type) == 0 )
          new = jack_slist_append(new, p);
   }

   return new;
}

struct port*
get_port_by_name(JSList* list_ptr, const char* name) {
   struct port* p;
   JSList* node;

   for ( node=list_ptr; node; node=jack_slist_next(node) ) {
       p = node->data;
       if (strcmp(p->name, name) == 0) return p;
   }
   return NULL;
}

JSList*
build_connections(jack_client_t* client, JSList* list_ptr, const char* type) {
   unsigned short i;
   struct connection* c;
   const char** connections;
   JSList* new = NULL;
   JSList* node;
   struct port *inp, *outp;

   for ( node=list_ptr; node; node=jack_slist_next(node) ) {
       // For all Input ports
       inp = node->data;
       if(! (inp->flags & JackPortIsInput)) continue;
       if( strcmp(inp->type, type) != 0 ) continue;

       connections = jack_port_get_all_connections( client, jack_port_by_name(client, inp->name) );
       if (!connections) continue;

       for (i=0; connections[i]; i++) {
           outp = get_port_by_name(list_ptr, connections[i]);
           if(!outp) continue; // WTF can't find OutPort in our list ?

       	   c = malloc(sizeof(struct connection));
           c->type = type;
           c->in = inp;
           c->out = outp;
           new = jack_slist_append(new, c);
       }
       jack_free(connections);
   }

   return new;
}

void draw_border(struct window * window_ptr) {
  int col = (window_ptr->width - strlen(window_ptr->name) - 4)/2;
  if (col < 0) col = 0;

  /* 0, 0 gives default characters for the vertical and horizontal lines */
  box(window_ptr->window_ptr, 0, 0);

  if (window_ptr->selected) {
     wattron(window_ptr->window_ptr, COLOR_PAIR(4));
     wattron(window_ptr->window_ptr, WA_BOLD);
     mvwprintw( window_ptr->window_ptr, 0, col, "=[%s]=", window_ptr->name);
     wattroff(window_ptr->window_ptr, COLOR_PAIR(4));
     wattroff(window_ptr->window_ptr, WA_BOLD);
  } else {
     mvwprintw( window_ptr->window_ptr, 0, col, " [%s] ", window_ptr->name);
  }
}

void draw_list(struct window* window_ptr) {
  int row, col, color, rows, cols, offset;
  JSList* node;
  struct port* p;
  struct connection* c;
  char fmt[20];

  row = col = 1;
  getmaxyx(window_ptr->window_ptr, rows, cols);
  snprintf(fmt, sizeof(fmt), "%%%ds -> %%-%ds", cols/2 - 3, cols/2 - 3);

  offset = window_ptr->index + 2 - rows;
  if (offset < 0) offset = 0;
  node = (offset) ? jack_slist_nth(window_ptr->list_ptr, offset) : window_ptr->list_ptr;
  for ( ; node; node=jack_slist_next(node) ) {
    color = (row == window_ptr->index - offset + 1) ? (window_ptr->selected) ? 3 : 2 : 1;
    wattron(window_ptr->window_ptr, COLOR_PAIR(color));

    switch(window_ptr->type) {
    case WIN_PORTS:
       p = node->data;
       mvwprintw(window_ptr->window_ptr, row, col, p->name);
       break;
    case WIN_CONNECTIONS:
       c = node->data;
       mvwprintw(window_ptr->window_ptr, row, col, fmt, c->out->name, c->in->name);
       break;
    default:
       ERR_OUT("Unknown WinType");
    }

    wattroff(window_ptr->window_ptr, COLOR_PAIR(color));
    row++;
  }
  draw_border(window_ptr);
  wrefresh(window_ptr->window_ptr);
}

void
create_window(struct window * window_ptr, int height, int width, int starty, int startx, const char * name, enum WinType type) {
//  window_ptr->list_ptr = list_ptr;
  window_ptr->window_ptr = newwin(height, width, starty, startx);
  window_ptr->selected = FALSE;
  window_ptr->width = width;
  window_ptr->height = height;
  window_ptr->name = name;
  window_ptr->index = 0;
  window_ptr->count = jack_slist_length(window_ptr->list_ptr);
  window_ptr->type = type;
//  scrollok(window_ptr->window_ptr, TRUE);
}

const char*
get_selected_port_name(struct window* window_ptr) {
   JSList* list = jack_slist_nth(window_ptr->list_ptr, window_ptr->index);
   struct port* p = list->data;
   return p->name;
}


bool
w_connect(jack_client_t* client, struct window* window_src_ptr, struct window* window_dst_ptr) {
   const char* src = get_selected_port_name(window_src_ptr);
   if(!src) return FALSE;
   const char* dst = get_selected_port_name(window_dst_ptr);
   if(!dst) return FALSE;

   if (jack_connect(client, src, dst) ) return FALSE;

   /* Move selections to next items */
   window_item_next(window_src_ptr);
   window_item_next(window_dst_ptr);
   return TRUE;
}

bool 
w_disconnect(jack_client_t* client, struct window* window_ptr) {
   JSList* list = jack_slist_nth(window_ptr->list_ptr, window_ptr->index);
   if (! list) return FALSE;

   struct connection* c = list->data;
   window_ptr->index = 0;
   return jack_disconnect(client, c->out->name, c->in->name) ? FALSE : TRUE;
}

void free_all_ports(JSList* all_ports) {
  /* First node is pointer to calloc-ed big chunk */
  if (! all_ports) return;
  free(all_ports->data);
  jack_slist_free(all_ports);
}

void cleanup(struct window* windows) {
  short i;
  struct window* w = windows;
  JSList *l, *node;

  for(i = 0; i < 3; i++, w++) {
     l = w->list_ptr;
     if( w->type == WIN_CONNECTIONS ) {
       for ( node=w->list_ptr; node; node=jack_slist_next(node) )
          free(node->data);
     }
     jack_slist_free(l);
  }
}

unsigned short
select_window(struct window* windows, int current, int new) {
   if (new == current) {
      return current;
   } else if (new > 2) {
      new = 0;
   } else if (new < 0) {
      new = 2;
   }

   if (new == 2 && ! jack_slist_length( windows[2].list_ptr )) {
      new = (new > current) ? 0 : 1;
   }

   windows[current].selected = FALSE;
   windows[new].selected = TRUE;
   return new;
}

int graph_order_handler(void *arg) {
    struct graph *graph = arg;
    *(graph->want_refresh) = TRUE;
    *(graph->err_message) = GRAPH_CHANGED;
    return 0;
}

void draw_help(WINDOW* w, int c, const char* msg) {
    wattron(w, COLOR_PAIR(c));
    mvwprintw(w, 0, 1, msg);
    wattroff(w, COLOR_PAIR(c));

    wclrtoeol(w);
    wrefresh(w);
    wmove(w, 0, 0);
}

int main() {
  unsigned short i, ret, rows, cols, window_selection=0;
  struct window windows[3];
  WINDOW* help_window;
  const char* err_message = NULL;
  const char* PortsType = JACK_DEFAULT_MIDI_TYPE;
  jack_client_t* client;
  jack_status_t status;
  JSList *all_list = NULL;
  bool want_refresh = FALSE;
  struct graph g = { &want_refresh, &err_message };

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
  init_pair(1, COLOR_CYAN, COLOR_BLACK);
  init_pair(2, COLOR_BLACK, COLOR_WHITE);
  init_pair(3, COLOR_BLACK, COLOR_GREEN);
  init_pair(4, COLOR_WHITE, COLOR_BLACK);
  init_pair(5, COLOR_BLACK, COLOR_RED);
  init_pair(6, COLOR_YELLOW, COLOR_BLACK);

  /* Create Help Window */
  help_window = newwin(0, cols, rows-1, 0);
  keypad(help_window, TRUE);
  wtimeout(help_window, 3000);

  /* Initialize jack */
  client = jack_client_open (APPNAME, JackNoStartServer, &status);
  if (! client) {
    if (status & JackServerFailed) ERR_OUT ("JACK server not running");
    else ERR_OUT ("jack_client_open() failed, status = 0x%2.0x", status);
    ret = 2;
    goto quit_no_clean;
  }

  jack_set_graph_order_callback(client, graph_order_handler, &g);
  jack_activate(client);

  /* Build ports, connections list */
  all_list = build_ports(client);
  windows[0].list_ptr = select_ports(all_list, JackPortIsOutput, PortsType);
  windows[1].list_ptr = select_ports(all_list, JackPortIsInput, PortsType);
  windows[2].list_ptr = build_connections(client, all_list, PortsType);

  /* Create windows */
  create_window(windows, rows/2, cols/2, 0, 0, "Output Ports", WIN_PORTS);
  create_window(windows+1, rows/2, cols - cols/2, 0, cols/2, "Input Ports", WIN_PORTS);
  create_window(windows+2, rows-rows/2-1, cols, rows/2, 0, CON_NAME_M, WIN_CONNECTIONS);
  windows[window_selection].selected = TRUE;

loop:
  for (i=0; i < 3; i++) draw_list(windows+i);

  if (err_message) {
    draw_help(help_window, 5, err_message);
    err_message = NULL;
  } else {
    draw_help(help_window, 6, HELP);
  }

  switch ( wgetch(help_window) ) {
  case KEY_TAB:
    window_selection = select_window(windows, window_selection, window_selection+1);
    goto loop;
  case KEY_BTAB:
    window_selection = select_window(windows, window_selection, window_selection-1);
    goto loop;
  case 'a':
     windows[2].name = CON_NAME_A;
     PortsType = JACK_DEFAULT_AUDIO_TYPE;
     goto refresh;
  case 'm':
     windows[2].name = CON_NAME_M;
     PortsType = JACK_DEFAULT_MIDI_TYPE;
     goto refresh;
  case 'q': ret =0; goto quit;
  case 'r': goto refresh;
  case 'c':
    if ( w_connect(client, windows, windows+1) ) goto refresh;
    err_message = ERR_CONNECT;
    goto loop;
  case 'd':
    if (w_disconnect(client, windows+2) ) goto refresh;
    err_message = ERR_DISCONNECT;
    goto loop;
  case KEY_DOWN:
     window_item_next(windows+window_selection);
     goto loop;
  case KEY_UP:
     window_item_previous(windows+window_selection);
     goto loop;
  case KEY_LEFT:
     window_selection = select_window(windows, window_selection, 0);
     goto loop;
  case KEY_RIGHT:
     window_selection = select_window(windows, window_selection, 1);
     goto loop;
  }
  if (! want_refresh) goto loop;
refresh:
  want_refresh = FALSE;
  free_all_ports(all_list);
  cleanup(windows);

  all_list = build_ports(client);
  windows[0].list_ptr = select_ports(all_list, JackPortIsOutput, PortsType);
  windows[1].list_ptr = select_ports(all_list, JackPortIsInput, PortsType);
  windows[2].list_ptr = build_connections(client, all_list, PortsType);

  for(i=0; i < 3; i++) {
     windows[i].count = jack_slist_length( windows[i].list_ptr );
     wclear(windows[i].window_ptr);
  }

  goto loop;
quit_no_clean:
  free_all_ports(all_list);
  cleanup(windows);
quit:
  jack_deactivate(client);
  jack_client_close (client);
qxit:
  endwin();

  return ret;
}
