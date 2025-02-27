/* Connection database indexed by serialno, for libreswan
 *
 * Copyright (C) 2020 Andrew Cagney <cagney@gnu.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/gpl2.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "connection_db.h"
#include "spd_route_db.h"
#include "connections.h"
#include "log.h"
#include "hash_table.h"
#include "refcnt.h"
#include "virtual_ip.h"		/* for virtual_ip_addref() */

/*
 * A table hashed by serialno.
 */

static hash_t hash_connection_serialno(const co_serial_t *serialno)
{
	return hash_thing(*serialno, zero_hash);
}

HASH_TABLE(connection, serialno, .serialno, STATE_TABLE_SIZE);

struct connection *connection_by_serialno(co_serial_t serialno)
{
	/*
	 * Note that since SOS_NOBODY is never hashed, a lookup of
	 * SOS_NOBODY always returns NULL.
	 */
	struct connection *c;
	hash_t hash = hash_connection_serialno(&serialno);
	struct list_head *bucket = hash_table_bucket(&connection_serialno_hash_table, hash);
	FOR_EACH_LIST_ENTRY_NEW2OLD(c, bucket) {
		if (c->serialno == serialno) {
			return c;
		}
	}
	return NULL;
}

/*
 * An ID hash table.
 */

static hash_t hash_connection_that_id(const struct id *id)
{
	hash_t hash = zero_hash;
	if (id->kind != ID_NONE) {
		shunk_t body;
		enum ike_id_type type = id_to_payload(id, &unset_address/*ignored*/, &body);
		hash = hash_thing(type, hash);
		hash = hash_hunk(body, hash);
	}
	return hash;
}

HASH_TABLE(connection, that_id, .remote->host.id, STATE_TABLE_SIZE);

REHASH_DB_ENTRY(connection, that_id, .remote->host.id);

void replace_connection_that_id(struct connection *c, const struct id *src)
{
	struct id *dst = &c->remote->host.id;
	passert(dst->name.ptr == NULL || dst->name.ptr != src->name.ptr);
	free_id_content(dst);
	*dst = clone_id(src, "replaing connection id");
	connection_db_rehash_that_id(c);
}

/*
 * A serial_from table.
 */

static hash_t hash_connection_clonedfrom(struct connection *const *cpp)
{
	so_serial_t serial = (*cpp == NULL ? 0 : (*cpp)->serialno);
	return hash_thing(serial, zero_hash);
}

HASH_TABLE(connection, clonedfrom, .clonedfrom, STATE_TABLE_SIZE);

/*
 * Maintain the contents of the hash tables.
 */

HASH_DB(connection,
	&connection_clonedfrom_hash_table,
	&connection_serialno_hash_table,
	&connection_that_id_hash_table);

/*
 * See also {new2old,old2new}_state()
 */

static struct list_head *connection_filter_head(struct connection_filter *filter)
{
	if (filter->that_id_eq != NULL) {
		id_buf idb;
		dbg("FOR_EACH_CONNECTION[that_id_eq=%s].... in "PRI_WHERE,
		    str_id(filter->that_id_eq, &idb), pri_where(filter->where));
		hash_t hash = hash_connection_that_id(filter->that_id_eq);
		return hash_table_bucket(&connection_that_id_hash_table, hash);
	}

	if (filter->clonedfrom != NULL) {
		dbg("FOR_EACH_CONNECTION[clonedfrom="PRI_CO"].... in "PRI_WHERE,
		    pri_connection_co(filter->clonedfrom), pri_where(filter->where));
		hash_t hash = hash_connection_clonedfrom(&filter->clonedfrom);
		return hash_table_bucket(&connection_clonedfrom_hash_table, hash);
	}

	dbg("FOR_EACH_CONNECTION_.... in "PRI_WHERE, pri_where(filter->where));
	return &connection_db_list_head;
}

static bool matches_connection_filter(struct connection *c, struct connection_filter *filter)
{
	if (filter->kind != 0 && filter->kind != c->kind) {
		return false;
	}
	if (filter->clonedfrom != NULL &&
	    filter->clonedfrom != c->clonedfrom) {
		return false;
	}
	if (filter->name != NULL && !streq(filter->name, c->name)) {
		return false;
	}
	if (filter->this_id_eq != NULL && !id_eq(filter->this_id_eq, &c->local->host.id)) {
		return false;
	}
	if (filter->that_id_eq != NULL && !id_eq(filter->that_id_eq, &c->remote->host.id)) {
		return false;
	}
	return true; /* sure */
}

static bool next_connection(enum chrono adv, struct connection_filter *filter)
{
	if (filter->internal == NULL) {
		/*
		 * Advance to first entry of the circular list (if the
		 * list is entry it ends up back on HEAD which has no
		 * data).
		 */
		filter->internal = connection_filter_head(filter)->head.next[adv];
	}
	/* Walk list until an entry matches */
	filter->c = NULL;
	for (struct list_entry *entry = filter->internal;
	     entry->data != NULL /* head has DATA == NULL */;
	     entry = entry->next[adv]) {
		struct connection *c = (struct connection *) entry->data;
		if (matches_connection_filter(c, filter)) {
			/* save connection; but step off current entry */
			filter->internal = entry->next[adv];
			filter->count++;
			LDBGP_JAMBUF(DBG_BASE, &global_logger, buf) {
				jam_string(buf, "  found ");
				jam_connection(buf, c);
			}
			filter->c = c;
			return true;
		}
	}
	dbg("  matches: %d", filter->count);
	return false;
}

bool next_connection_old2new(struct connection_filter *filter)
{
	return next_connection(OLD2NEW, filter);
}

bool next_connection_new2old(struct connection_filter *filter)
{
	return next_connection(NEW2OLD, filter);
}
