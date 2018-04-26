#include <string.h>

#include "port_connection.h"

/* CONNECTIONS */
JSList* build_connections(jack_client_t* client, JSList* list, const char* type) {
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

void free_connections( JSList* list_con ) {
	JSList* node;
	for ( node=list_con; node; node=jack_slist_next(node) )
		free(node->data);
}

/* PORTS */
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

void free_all_ports(JSList* all_ports) {
	/* First node is pointer to calloc-ed big chunk */
	if (! all_ports) return;
	free(all_ports->data);
	jack_slist_free(all_ports);
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
