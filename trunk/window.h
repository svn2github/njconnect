#ifndef WINDOW_H
#define WINDOW_H

#include <ncurses.h>
#include <jack/jslist.h>

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

void w_create(Window* W, int height, int width, int starty, int startx, const char* name, enum WinType type);
void w_cleanup(Window* windows);
void w_draw_border(Window* W);
void w_assign_list(Window* W, JSList* list);
void w_resize(Window* W, int height, int width, int starty, int startx);
void w_item_next(Window* W);
void w_item_previous(Window* W);

#endif /* WINDOW_H */
