#include <string.h>

#include "window.h"

void w_create(Window* W, int height, int width, int starty, int startx, const char* name, enum WinType type) {
	W->window_ptr = newwin(height, width, starty, startx);
	W->selected = false;
	W->width = width;
	W->height = height;
	W->name = name;
	W->index = 0;
	W->type = type;
	W->redraw = true;
	//  scrollok(w->window_ptr, true);
}

void w_cleanup(Window* windows) {
	short i;
	Window* w = windows;

	for (i = 0; i < 3; i++, w++) {
		JSList* l = w->list;
		jack_slist_free(l);
		w->redraw = true;
	}
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

void w_assign_list(Window* W, JSList* list) {
	W->list = list;
	W->count = jack_slist_length(W->list);
	W->redraw = true;

	if (W->index > W->count - 1)
		W->index = 0;
}

void w_resize(Window* W, int height, int width, int starty, int startx) {
	//delwin(W->window_ptr);
	//W->window_ptr = newwin(height, width, starty, startx);
	wresize(W->window_ptr, height, width);
	mvwin(W->window_ptr, starty, startx);
	W->width = width;
	W->height = height;
	W->redraw = true;
}

void w_item_next(Window* W) {
	if (W->index < W->count - 1)
		W->index++;
}

void w_item_previous(Window* W) {
	if (W->index > 0)
		W->index--;
}
