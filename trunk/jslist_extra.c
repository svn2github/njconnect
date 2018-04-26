#include "jslist_extra.h"

/* Functions forgotten by Jack-Devs */

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


