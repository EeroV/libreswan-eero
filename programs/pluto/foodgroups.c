/* Implement policy groups-style control files (aka "foodgroups")
 * Copyright (C) 2002  D. Hugh Redelmeier.
 * Copyright (C) 2005 Michael Richardson <mcr@xelerance.com>
 * Copyright (C) 2009-2010 Paul Wouters <paul@xelerance.com>
 * Copyright (C) 2019 Andrew Cagney <cagney@gnu.org>
 * Copyright (C) 2019 D. Hugh Redelmeier <hugh@mimosa.com>
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
 *
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <limits.h> /* PATH_MAX */
#include <errno.h>

#include "sysdep.h"
#include "constants.h"
#include "defs.h"
#include "id.h"
#include "x509.h"
#include "certs.h"
#include "connections.h"        /* needs id.h */
#include "foodgroups.h"
#include "kernel.h"             /* needs connections.h */
#include "lswconf.h"
#include "lex.h"
#include "log.h"
#include "whack.h"
#include "ip_info.h"
#include "ip_selector.h"
#include "orient.h"
#include "routing.h"
#include "instantiate.h"

/* Targets is a list of pairs: subnet and its policy group.
 * This list is bulk-updated on whack --listen and
 * incrementally updated when group connections are deleted.
 *
 * It is ordered by source subnet, and if those are equal, then target subnet.
 * A subnet is compared by comparing the network, and if those are equal,
 * comparing the mask.
 */

struct fg_targets {
	struct fg_targets *next;
	struct connection *group;
	ip_subnet subnet;
	const struct ip_protocol *proto;
	ip_port sport;
	ip_port dport;
	co_serial_t serialno;
};

static struct fg_targets *targets = NULL;

/*
 * An old target has disappeared for a group: delete instance.
 */
static void remove_group_instance(const struct connection *group,
				  co_serial_t serialno)
{
	passert(group->kind == CK_GROUP);
	ldbg(group->logger, "removing group instance "PRI_CO, pri_co(serialno));
	struct connection *gi = connection_by_serialno(serialno);
	if (gi == NULL) {
		/*
		 * This happens during shutdown when all connections
		 * are deleted in new-to-old order (i.e., group
		 * instances are deleted before the group).
		 */
		return;
	}

	/* now delete instances */
	struct connection_filter cq = {
		.clonedfrom = gi,
		.where = HERE,
	};
	while (next_connection_new2old(&cq)) {
		struct connection *c = cq.c;
		/* XXX: something better? */
		fd_delref(&c->logger->global_whackfd);
		c->logger->global_whackfd = fd_addref(group->logger->global_whackfd); /* freed by discard_conection() */
		delete_connection(&c);
	}

	/* and group instance */
	delete_connection(&gi);
}

/* subnetcmp compares the two ip_subnet values a and b.
 * It returns -1, 0, or +1 if a is, respectively,
 * less than, equal to, or greater than b.
 */
static int subnetcmp(const ip_subnet a, const ip_subnet b)
{
	int r;

	ip_address neta = subnet_prefix(a);
	ip_address maska = subnet_prefix_mask(a);
	ip_address netb = subnet_prefix(b);
	ip_address maskb = subnet_prefix_mask(b);
	r = addrcmp(&neta, &netb);
	if (r == 0)
		r = addrcmp(&maska, &maskb);
	return r;
}

static void read_foodgroup(struct file_lex_position *oflp,
			   struct connection *g,
			   struct fg_targets **new_targets)
{
	const char *fgn = g->name;
	const ip_subnet lsn = selector_subnet(g->spd->local->client);
	const struct lsw_conf_options *oco = lsw_init_options();
	char *fg_path = alloc_printf("%s/%s", oco->policies_dir, fgn); /* must free */

	struct file_lex_position *flp;
	if (!lexopen(&flp, fg_path, true, oflp)) {
		char cwd[PATH_MAX];
		dbg("no group file \"%s\" (pwd:%s)", fg_path, getcwd(cwd, sizeof(cwd)));
		pfreeany(fg_path);
		return;
	}
	pfreeany(fg_path);

	llog(RC_LOG, flp->logger, "loading group \"%s\"", flp->filename);
	while (flp->bdry == B_record) {

		/* force advance to first token */
		flp->bdry = B_none;     /* eat the Record Boundary */
		/* get real first token */
		if (!shift(flp)) {
			/* blank line or comment */
			continue;
		}

		/* address or address/mask */
		ip_subnet sn;
		if (strchr(flp->tok, '/') == NULL) {
			/* no /, so treat as /32 or V6 equivalent */
			ip_address t;
			err_t err = ttoaddress_num(shunk1(flp->tok), NULL, &t);
			if (err != NULL) {
				llog(RC_LOG_SERIOUS, flp->logger,
					    "ignored, '%s' is not an address: %s",
					    flp->tok, err);
				flushline(flp, NULL/*shh*/);
				continue;
			}
			sn = subnet_from_address(t);
		} else {
			const struct ip_info *afi = strchr(flp->tok, ':') == NULL ? &ipv4_info : &ipv6_info;
			ip_subnet snn;
			ip_address nonzero_host;
			err_t err = ttosubnet_num(shunk1(flp->tok), afi, &snn, &nonzero_host);
			if (err != NULL) {
				llog(RC_LOG_SERIOUS, flp->logger,
				     "ignored, '%s' is not a subnet: %s",
				     flp->tok, err);
				flushline(flp, NULL/*shh*/);
				continue;
			}
			if (nonzero_host.is_set) {
				address_buf hb;
				llog(RC_LOG, flp->logger,
				     "zeroing non-zero host identifier %s in '%s'",
				     str_address(&nonzero_host, &hb), flp->tok);
			}
			sn = snn;
		}

		const struct ip_info *afi = subnet_info(sn);
		if (afi == NULL) {
			llog(RC_LOG_SERIOUS, flp->logger,
				    "ignored, unsupported Address Family \"%s\"",
				    flp->tok);
			flushline(flp, NULL/*shh*/);
			continue;
		}

		const struct ip_protocol *proto = &ip_protocol_all;
		ip_port sport = unset_port;
		ip_port dport = unset_port;

		/* check for: [protocol sport dport] */
		if (shift(flp)) {
			err_t err;
			/* protocol */
			const struct ip_protocol *protocol;
			err = ttoprotocol(shunk1(flp->tok), &protocol);
			if (err != NULL) {
				llog(RC_LOG_SERIOUS, flp->logger,
				     "protocol '%s' invalid: %s",
				     flp->tok, err);
				break;
			}
			pexpect(protocol != NULL);
			if (protocol == &ip_protocol_all ||
			    protocol == &ip_protocol_esp ||
			    protocol == &ip_protocol_ah) {
				llog(RC_LOG_SERIOUS, flp->logger,
				     "invalid protocol '%s' - mistakenly defined to be 0 or %u(esp) or %u(ah)",
				     flp->tok, IPPROTO_ESP, IPPROTO_AH);
				break;
			}
			proto = protocol;
			/* source port */
			if (!shift(flp)) {
				llog(RC_LOG_SERIOUS, flp->logger,
					    "missing source_port: either only specify CIDR, or specify CIDR protocol source_port dest_port");
				break;
			}
			err = ttoport(flp->tok, &sport);
			if (err != NULL) {
				llog(RC_LOG_SERIOUS, flp->logger,
					    "source port '%s' invalid: %s",
					    flp->tok, err);
				break;
			}
			/* dest port */
			if (!shift(flp)) {
				llog(RC_LOG_SERIOUS, flp->logger,
					    "missing dest_port: either only specify CIDR, or specify CIDR protocol source_port dest_port");
				break;
			}
			err = ttoport(flp->tok, &dport);
			if (err != NULL) {
				llog(RC_LOG_SERIOUS, flp->logger,
					    "destination port '%s' invalid: %s",
					    flp->tok, err);
				break;
			}
			/* more stuff? */
			if (shift(flp)) {
				llog(RC_LOG_SERIOUS, flp->logger,
					    "garbage '%s' at end of line: either only specify CIDR, or specify CIDR protocol source_port dest_port",
					    flp->tok);
				break;
			}
		}

		pexpect(flp->bdry == B_record || flp->bdry == B_file);

		/* Find where new entry ought to go in new_targets. */
		struct fg_targets **pp;
		int r;

		for (pp = new_targets;;
		     pp = &(*pp)->next) {
			if (*pp == NULL) {
				r = -1; /* end of list is infinite */
				break;
			}
			r = subnetcmp(lsn, selector_subnet((*pp)->group->spd->local->client));
			if (r == 0) {
				r = subnetcmp(sn, (*pp)->subnet);
			}
			if (r != 0)
				break;

			if (proto == (*pp)->proto &&
			    port_eq(sport, (*pp)->sport) &&
			    port_eq(dport, (*pp)->dport)) {
				break;
			}
		}

		if (r == 0) {
			subnet_buf source;
			subnet_buf dest;
			llog(RC_LOG_SERIOUS, flp->logger,
			     "subnet \"%s\", proto %d, sport "PRI_HPORT" dport "PRI_HPORT", source %s, already \"%s\"",
			     str_subnet(&sn, &dest),
			     proto->ipproto, pri_hport(sport), pri_hport(dport),
			     str_subnet(&lsn, &source),
			     (*pp)->group->name);
		} else {
			struct fg_targets *f = alloc_thing(struct fg_targets,
							   "fg_target");
			f->next = *pp;
			f->group = g;
			f->subnet = sn;
			f->proto = proto;
			f->sport = sport;
			f->dport = dport;
			f->serialno = UNSET_CO_SERIAL;
			*pp = f;
		}
	}
	if (flp->bdry != B_file) {
		llog(RC_LOG_SERIOUS, flp->logger, "rest of file ignored");
	}
	lexclose(&flp);
}

static void pfree_target(struct fg_targets **target)
{
	pfree((*target));
	*target = NULL;
}

void load_groups(struct logger *logger)
{
	struct fg_targets *new_targets = NULL;

	/*
	 * Find all the connection groups and, for each, add config
	 * file targets into new_targets.
	 */
	struct connection_filter cf = {
		.kind = CK_GROUP,
		.where = HERE,
	};
	while (next_connection_new2old(&cf)) {
		struct connection *g = cf.c;
		if (oriented(g)) {
			struct file_lex_position flp = {
				.logger = logger,
			};
			read_foodgroup(&flp, g, &new_targets);
		}
	}

	if (DBGP(DBG_BASE)) {
		/* dump old food groups */
		DBG_log("old food groups:");
		for (struct fg_targets *t = targets; t != NULL; t = t->next) {
			selector_buf asource;
			subnet_buf atarget;
			DBG_log("  %s->%s %s sport "PRI_HPORT" dport "PRI_HPORT" %s",
				str_selector_subnet_port(&t->group->spd->local->client, &asource),
				str_subnet(&t->subnet, &atarget),
				t->proto->name, pri_hport(t->sport), pri_hport(t->dport),
				t->group->name);
		}
		/* dump new food groups */
		DBG_log("new food groups:");
		for (struct fg_targets *t = new_targets; t != NULL; t = t->next) {
			selector_buf asource;
			subnet_buf atarget;
			DBG_log("  %s->%s %s sport "PRI_HPORT" dport "PRI_HPORT" %s",
				str_selector_subnet_port(&t->group->spd->local->client, &asource),
				str_subnet(&t->subnet, &atarget),
				t->proto->name, pri_hport(t->sport), pri_hport(t->dport),
				t->group->name);
		}
	}

	/*
	 * determine and deal with differences between targets and
	 * new_targets.  Structured like a merge of old into new.
	 */
	{
		struct fg_targets **opp = &targets;
		struct fg_targets **npp = &new_targets;

		while ((*opp) != NULL || (*npp) != NULL) {
			struct fg_targets *op = *opp;
			struct fg_targets *np = *npp;

			/* select next: -1:old; 0:merge; +1:new? */
			int r = 0;
			if (op == NULL) {
				r = 1; /* no more old; next is new */
			}
			if (np == NULL) {
				r = -1; /* no more new; next is old */
			}
			if (r == 0)
				r = subnetcmp(selector_subnet(op->group->spd->local->client),
					      selector_subnet(np->group->spd->local->client));
			if (r == 0)
				r = subnetcmp(op->subnet, np->subnet);
			if (r == 0)
				r = op->proto - np->proto;
			if (r == 0)
				r = hport(op->sport) - hport(np->sport);
			if (r == 0)
				r = hport(op->dport) - hport(np->dport);

			if (r == 0 && op->group == np->group) {
				/* unchanged; transfer connection &
				 * skip over */
				ldbg(op->group->logger,
				     "transfering "PRI_CO, pri_co(op->serialno));
				passert(op->serialno != UNSET_CO_SERIAL);
				passert(np->serialno == UNSET_CO_SERIAL);
				np->serialno = op->serialno;
				op->serialno = UNSET_CO_SERIAL;
				/* free old; advance new */
				*opp = op->next;
				pfree_target(&op);
				npp = &np->next;
			} else {
				/* note: r>=0 || r<= 0: following cases overlap! */
				if (r <= 0) {
					remove_group_instance(op->group, op->serialno);
					/* free old */
					*opp = op->next;
					pfree_target(&op);
				}
				if (r >= 0) {
					struct connection *g = np->group;
					/* XXX: something better? */
					fd_delref(&g->logger->global_whackfd);
					g->logger->global_whackfd = fd_addref(logger->global_whackfd);
					/* group instance (which is a template) */
					struct connection *t = group_instantiate(g,
										 np->subnet,
										 np->proto,
										 np->sport,
										 np->dport,
										 HERE);
					if (t != NULL) {
						/* instance when remote addr valid */
						PEXPECT(logger, (t->kind == CK_TEMPLATE ||
								 t->kind == CK_INSTANCE));
						/* route if group is routed */
						if (g->policy & POLICY_ROUTE) {
							/* XXX: something better? */
							fd_delref(&t->logger->global_whackfd);
							t->logger->global_whackfd = fd_addref(g->logger->global_whackfd);
							connection_route(t);
							/* XXX: something better? */
							fd_delref(&t->logger->global_whackfd);
						}
						ldbg(g->logger, "setting "PRI_CO, pri_co(t->serialno));
						passert(np->serialno == UNSET_CO_SERIAL);
						np->serialno = t->serialno;
						/* advance new */
						npp = &np->next;
					} else {
						/* XXX: is this really a pexpect()? */
						dbg("add group instance failed");
						/* free new; advance new */
						*npp = np->next;
						pfree_target(&np);
					}
					/* XXX: something better? */
					fd_delref(&g->logger->global_whackfd);
				}
			}
		}

		/* update: new_targets replaces targets */
		passert(targets == NULL);
		targets = new_targets;
	}
}


void connection_group_route(struct connection *g)
{
	if (!PEXPECT(g->logger, g->kind == CK_GROUP)) {
		return;
	}

	/*
	 * It makes no sense to route a connection that is IKE-only.
	 *
	 * ... except when it is an IKE only connection such is
	 * created for labeled IPsec, but lets ignore that.
	 */
	if (!NEVER_NEGOTIATE(g->policy) && !HAS_IPSEC_POLICY(g->policy)) {
		llog(RC_ROUTE, g->logger,
		     "cannot route an IKE-only group connection");
		return;
	}

	if (!oriented(g)) {
		llog(RC_ORIENT, g->logger,
		     "we cannot identify ourselves with either end of this connection");
		return;
	}

	g->policy |= POLICY_ROUTE;
	for (struct fg_targets *t = targets; t != NULL; t = t->next) {
		if (t->group == g) {
			struct connection *ci = connection_by_serialno(t->serialno);
			if (ci != NULL) {
				connection_route(ci);
			}
		}
	}
}

void connection_group_unroute(struct connection *g)
{
	if (!PEXPECT(g->logger, g->kind == CK_GROUP)) {
		return;
	}

	g->policy &= ~POLICY_ROUTE;
	for (struct fg_targets *t = targets; t != NULL; t = t->next) {
		if (t->group == g) {
			struct connection *ci = connection_by_serialno(t->serialno);
			if (ci != NULL) {
				connection_unroute(ci);
			}
		}
	}
}

void delete_connection_group_instances(const struct connection *g)
{
	/*
	 * find and remove from targets
	 */
	struct fg_targets **pp = &targets;
	while (*pp != NULL) {
		struct fg_targets *t = *pp;
		if (t->group == g) {
			/* remove *PP but advance first */
			*pp = t->next;
			remove_group_instance(t->group, t->serialno);
			pfree_target(&t);
			/* pp is ready for next iteration */
		} else {
			/* advance PP */
			pp = &t->next;
		}
	}
}
