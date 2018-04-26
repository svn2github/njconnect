#ifndef PORT_CONNECTION_H
#define PORT_CONNECTION_H

#include <stdbool.h>
#include <jack/jack.h>
#include <jack/jslist.h>

typedef struct {
	char name[128];
	char type[32];
	int flags;
	bool mark;
} Port;

typedef struct {
	const char* type;
	Port* in;
	Port* out;
} Connection;

JSList* build_connections(jack_client_t* client, JSList* list, const char* type);
void free_connections( JSList* list_con );
JSList* build_ports(jack_client_t* client);
void free_all_ports(JSList* all_ports);
JSList* select_ports(JSList* list, int flags, const char* type);
Port* get_port_by_name(JSList* list, const char* name);
int get_max_port_name ( JSList* list );

#endif /* PORT_CONNECTION_H */
