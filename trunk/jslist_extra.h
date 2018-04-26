#ifndef JSLIST_EXTRA_H
#define JSLIST_EXTRA_H

#include <jack/jslist.h>

JSList* jack_slist_nth(JSList* list, unsigned short n);

int jack_slist_find_pos(JSList* list, void *data);

#endif /* JSLIST_EXTRA_H */
