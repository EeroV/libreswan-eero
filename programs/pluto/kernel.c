/* routines that interface with the kernel's IPsec mechanism, for libreswan
 *
 * Copyright (C) 1997 Angelos D. Keromytis.
 * Copyright (C) 1998-2010  D. Hugh Redelmeier.
 * Copyright (C) 2003-2008 Michael Richardson <mcr@xelerance.com>
 * Copyright (C) 2007-2010 Paul Wouters <paul@xelerance.com>
 * Copyright (C) 2008-2010 David McCullough <david_mccullough@securecomputing.com>
 * Copyright (C) 2010 Bart Trojanowski <bart@jukie.net>
 * Copyright (C) 2009-2010 Tuomo Soini <tis@foobar.fi>
 * Copyright (C) 2010 Avesh Agarwal <avagarwa@redhat.com>
 * Copyright (C) 2010-2019 D. Hugh Redelmeier <hugh@mimosa.com>
 * Copyright (C) 2012-2015 Paul Wouters <paul@libreswan.org>
 * Copyright (C) 2013 Kim B. Heino <b@bbbs.net>
 * Copyright (C) 2016-2022 Andrew Cagney
 * Copyright (C) 2019 Paul Wouters <pwouters@redhat.com>
 * Copyright (C) 2017 Mayank Totale <mtotale@gmail.com>
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

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/wait.h>		/* for WIFEXITED() et.al. */
#include <unistd.h>
#include <fcntl.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/thread.h>

#include "sysdep.h"
#include "constants.h"

#include "defs.h"
#include "rnd.h"
#include "id.h"
#include "connections.h"        /* needs id.h */
#include "state.h"
#include "timer.h"
#include "kernel.h"
#include "kernel_ops.h"
#include "kernel_xfrm.h"
#include "kernel_policy.h"
#include "packet.h"
#include "x509.h"
#include "pluto_x509.h"
#include "certs.h"
#include "secrets.h"
#include "log.h"
#include "server.h"
#include "whack.h"      /* for RC_LOG_SERIOUS */
#include "keys.h"
#include "ike_alg.h"
#include "ike_alg_encrypt.h"
#include "ike_alg_integ.h"

#include "packet.h"  /* for pb_stream in nat_traversal.h */
#include "nat_traversal.h"
#include "ip_address.h"
#include "ip_info.h"
#include "lswfips.h" /* for libreswan_fipsmode() */
#include "kernel_xfrm_interface.h"
#include "iface.h"
#include "ip_selector.h"
#include "ip_encap.h"
#include "show.h"
#include "rekeyfuzz.h"
#include "orient.h"
#include "kernel_alg.h"
#include "updown.h"

static void delete_bare_shunt_kernel_policy(const struct bare_shunt *bsp,
					    enum expect_kernel_policy expect_kernel_policy,
					    struct logger *logger, where_t where);

/*
 * The priority assigned to a kernel policy.
 *
 * Lowest wins.
 */

kernel_priority_t highest_kernel_priority = { .value = 0, };

kernel_priority_t calculate_kernel_priority(const struct connection *c)
{
	if (c->sa_priority != 0) {
		ldbg(c->logger,
		     "priority calculation overruled by connection specification of %"PRIu32" (%#"PRIx32")",
		     c->sa_priority, c->sa_priority);
		return (kernel_priority_t) { c->sa_priority, };
	}

	if (c->kind == CK_GROUP) {
		llog_pexpect(c->logger, HERE,
			     "priority calculation of connection skipped - group template does not install SPDs");
		return highest_kernel_priority;
	}

	/* XXX: assume unsigned >= 32-bits */
	PASSERT(c->logger, sizeof(unsigned) >= sizeof(uint32_t));

	/*
	 * Accumulate the priority.
	 *
	 * Add things most-important to least-important. Before ORing
	 * in the new bits, left-shift PRIO to make space.
	 */
	unsigned prio = 0;

	/* Determine the base priority (2 bits) (0 is manual by user). */
	unsigned base;
	if (LIN(POLICY_GROUPINSTANCE, c->policy)) {
		if (c->remote->host.config->authby.null) {
			base = 3; /* opportunistic anonymous */
		} else {
			base = 2; /* opportunistic */
		}
	} else {
		base = 1; /* static connection */
	}

	/* XXX: yes the shift is pointless (but it is consistent) */
	prio = (prio << 2) | base;

	/* Penalize wildcard ports (2 bits). */
	unsigned portsw =
		((c->spd->local->client.hport == 0 ? 1 : 0) +
		 (c->spd->remote->client.hport == 0 ? 1 : 0));
	prio = (prio << 2) | portsw;

	/* Penalize wildcard protocol (1 bit). */
	unsigned protow = c->spd->local->client.ipproto == 0 ? 1 : 0;
	prio = (prio << 1) | protow;

	/*
	 * For transport mode or /32 to /32, the client mask bits are
	 * set based on the host_addr parameters.
	 *
	 * A longer prefix wins over a shorter prefix, hence the
	 * reversal.  Value needs to fit 0-128, hence 8 bits.
	 */
	unsigned srcw = 128 - c->spd->local->client.maskbits;
	prio = (prio << 8) | srcw;
	unsigned dstw = 128 - c->spd->remote->client.maskbits;
	prio = (prio << 8) | dstw;

	/*
	 * Penalize template (1 bit).
	 *
	 * "Ensure an instance always has preference over it's
	 * template/OE-group always has preference."
	 */
	unsigned instw = (c->kind == CK_INSTANCE ? 0 : 1);
	prio = (prio << 1) | instw;

	ldbg(c->logger,
	     "priority calculation of is %u (%#x) base=%u portsw=%u protow=%u, srcw=%u dstw=%u instw=%u",
	     prio, prio, base, portsw, protow, srcw, dstw, instw);
	return (kernel_priority_t) { prio, };
}

static global_timer_cb kernel_scan_shunts;

bool shunt_ok(enum shunt_kind shunt_kind, enum shunt_policy shunt_policy)
{
	static const bool ok[SHUNT_KIND_ROOF][SHUNT_POLICY_ROOF] = {
		[SHUNT_KIND_NEVER_NEGOTIATE] = {
			[SHUNT_UNSET] = true,
			[SHUNT_NONE] = false, [SHUNT_HOLD] = false, [SHUNT_TRAP] = false, [SHUNT_PASS] = true,  [SHUNT_DROP] = true,  [SHUNT_REJECT] = true,
		},
		[SHUNT_KIND_ONDEMAND] = {
			[SHUNT_NONE] = false, [SHUNT_HOLD] = false, [SHUNT_TRAP] = true,  [SHUNT_PASS] = false, [SHUNT_DROP] = false, [SHUNT_REJECT] = false,
		},
		[SHUNT_KIND_NEGOTIATION] = {
			[SHUNT_NONE] = false, [SHUNT_HOLD] = true,  [SHUNT_TRAP] = false, [SHUNT_PASS] = true,  [SHUNT_DROP] = false, [SHUNT_REJECT] = false,
		},
		[SHUNT_KIND_FAILURE] = {
			[SHUNT_NONE] = true,  [SHUNT_HOLD] = false, [SHUNT_TRAP] = false, [SHUNT_PASS] = true,  [SHUNT_DROP] = true,  [SHUNT_REJECT] = true,
		},
	};
	return ok[shunt_kind][shunt_policy];
}

/*
 * Add an outbound bare kernel policy, aka shunt.
 *
 * Such a kernel policy determines the fate of packets without the use
 * of any SAs.  These are defaults, in effect.  If a negotiation has
 * not been attempted, use %trap.  If negotiation has failed, the
 * choice between %trap/%pass/%drop/%reject is specified in the policy
 * of connection c.
 *
 * The kernel policy is refered to as bare (naked, global) as it is
 * not paired with a kernel state.
 */

static bool install_prospective_kernel_policies(const struct spd_route *spd,
						struct logger *logger, where_t where)
{
	const struct connection *c = spd->connection;

	/*
	 * Only the following shunts are valid.
	 */
	enum shunt_kind shunt_kind =
		(NEVER_NEGOTIATE(c->policy) ? SHUNT_KIND_NEVER_NEGOTIATE :
		 SHUNT_KIND_ONDEMAND);
	enum shunt_policy prospective = c->config->shunt[shunt_kind];
	passert(shunt_ok(shunt_kind, prospective));

	LDBGP_JAMBUF(DBG_BASE, logger, buf) {
		jam(buf, "kernel: %s() ", __func__);

		jam_connection(buf, c);

		enum_buf spb;
		jam(buf, " never_negotiate_shunt=%s",
		    str_enum_short(&shunt_policy_names, prospective, &spb));

		jam(buf, " ");
		jam_selector_pair(buf, &spd->local->client, &spd->remote->client);

		jam(buf, " config.sec_label=");
		if (c->config->sec_label.len > 0) {
			jam_sanitized_hunk(buf, c->config->sec_label);
		}

		jam_string(buf, " ");
		jam_where(buf, where);
	}

	/*
	 * Only the following shunts are valid.
	 */
	FOR_EACH_THING(direction, DIRECTION_OUTBOUND, DIRECTION_INBOUND) {

		/*
		 * Security labels install a full policy which
		 * includes REQID and assumed mode when adding the
		 * prospective shunt but normal connections do not.
		 *
		 * Note the NO_INBOUND_ENTRY.  It's a hack to get
		 * around a connection being unrouted, deleting both
		 * inbound and outbound policies when there's only the
		 * basic outbound policy installed.
		 */
		if (c->config->sec_label.len > 0) {
			if (!install_bare_sec_label_kernel_policy(spd,
								  KERNEL_POLICY_OP_ADD,
								  direction,
								  /*XXX: should no policy be expected?*/
								  EXPECT_KERNEL_POLICY_OK,
								  logger, HERE,
								  "prospective sec_label kernel policy")) {
				return false;
			}
		} else {
			if (!install_bare_spd_kernel_policy(spd, KERNEL_POLICY_OP_ADD, direction,
							    /*XXX: should no policy be expected?*/
							    EXPECT_KERNEL_POLICY_OK,
							    shunt_kind,
							    logger, HERE,
							    "prospective kernel_policy")) {
				return false;
			}
		}
	}
	return true;
}

struct bare_shunt {
	ip_selector our_client;
	ip_selector peer_client;
	enum shunt_kind shunt_kind;
	enum shunt_policy shunt_policy;
	const struct ip_protocol *transport_proto; /* XXX: same value in local/remote */
	unsigned long count;
	monotime_t last_activity;

	/*
	 * Note: "why" must be in stable storage (not auto, not heap)
	 * because we use it indefinitely without copying or pfreeing.
	 * Simple rule: use a string literal.
	 */
	const char *why;

	/* The connection to restore when the bare_shunt expires.  */
	co_serial_t restore_serialno;

	struct bare_shunt *next;
};

static struct bare_shunt *bare_shunts = NULL;

#ifdef IPSEC_CONNECTION_LIMIT
static int num_ipsec_eroute = 0;
#endif

static void jam_bare_shunt(struct jambuf *buf, const struct bare_shunt *bs)
{
	jam(buf, "bare shunt %p ", bs);
	jam_selector_pair(buf, &bs->our_client, &bs->peer_client);
	jam(buf, " => ");
	jam_enum_short(buf, &shunt_policy_names, bs->shunt_policy);
	jam(buf, " ");
	jam_connection_priority(buf, BOTTOM_PRIORITY);
	jam(buf, "    %s", bs->why);
	if (bs->restore_serialno != UNSET_CO_SERIAL) {
		jam(buf, " "PRI_CO, pri_co(bs->restore_serialno));
	}
}

static void llog_bare_shunt(lset_t rc_flags, struct logger *logger,
			    const struct bare_shunt *bs, const char *op)
{
	LLOG_JAMBUF(rc_flags, logger, buf) {
		jam(buf, "%s ", op);
		jam_bare_shunt(buf, bs);
	}
}

static void ldbg_bare_shunt(const struct logger *logger, const char *op, const struct bare_shunt *bs)
{
	LDBGP_JAMBUF(DBG_BASE, logger, buf) {
		jam(buf, "%s ", op);
		jam_bare_shunt(buf, bs);
	}
}

/*
 * Note: "why" must be in stable storage (not auto, not heap) because
 * we use it indefinitely without copying or pfreeing.
 *
 * Simple rule: use a string literal.
 */

static struct bare_shunt *add_bare_shunt(const ip_selector *our_client,
					 const ip_selector *peer_client,
					 enum shunt_policy shunt_policy,
					 co_serial_t restore_serialno,
					 const char *why, struct logger *logger)
{
	/* report any duplication; this should NOT happen */
	struct bare_shunt **bspp = bare_shunt_ptr(our_client, peer_client, why);

	if (bspp != NULL) {
		/* maybe: passert(bsp == NULL); */
		llog_bare_shunt(RC_LOG, logger, *bspp,
				"CONFLICTING existing");
	}

	struct bare_shunt *bs = alloc_thing(struct bare_shunt, "bare shunt");

	bs->why = why;
	bs->our_client = *our_client;
	bs->peer_client = *peer_client;
	const struct ip_protocol *transport_proto = selector_protocol(*our_client);
	pexpect(transport_proto == selector_protocol(*peer_client));
	bs->transport_proto = transport_proto;
	bs->restore_serialno = restore_serialno;

	bs->shunt_policy = shunt_policy;
	bs->count = 0;
	bs->last_activity = mononow();

	bs->next = bare_shunts;
	bare_shunts = bs;
	ldbg_bare_shunt(logger, "add", bs);

	/* report duplication; this should NOT happen */
	if (bspp != NULL) {
		llog_bare_shunt(RC_LOG, logger, bs,
				"CONFLICTING      new");
	}

	return bs;
}

static reqid_t get_proto_reqid(reqid_t base, const struct ip_protocol *proto)
{
	if (proto == &ip_protocol_ipcomp)
		return reqid_ipcomp(base);

	if (proto == &ip_protocol_esp)
		return reqid_esp(base);

	if (proto == &ip_protocol_ah)
		return reqid_ah(base);

	llog_passert(&global_logger, HERE,
		     "bad protocol %s", proto->name);
}

static const char *said_str(const ip_address dst,
			    const struct ip_protocol *sa_proto,
			    ipsec_spi_t spi,
			    said_buf *buf)
{
	ip_said said = said_from_address_protocol_spi(dst, sa_proto, spi);
	return str_said(&said, buf);
}

ipsec_spi_t get_ipsec_spi(const struct connection *c,
			  const struct ip_protocol *proto,
			  ipsec_spi_t avoid,
			  struct logger *logger)
{
	passert(proto == &ip_protocol_ah || proto == &ip_protocol_esp);
	return kernel_ops_get_ipsec_spi(avoid,
					/*src*/&c->remote->host.addr,
					/*dst*/&c->local->host.addr,
					proto,
					get_proto_reqid(c->child.reqid, proto),
					IPSEC_DOI_SPI_OUR_MIN, 0xffffffffU,
					"SPI", logger);
}

/* Generate Unique CPI numbers.
 * The result is returned as an SPI (4 bytes) in network order!
 * The real bits are in the nework-low-order 2 bytes.
 * Modelled on get_ipsec_spi, but range is more limited:
 * 256-61439.
 * If we can't find one easily, return 0 (a bad SPI,
 * no matter what order) indicating failure.
 */
ipsec_spi_t get_ipsec_cpi(const struct connection *c, struct logger *logger)
{
	return kernel_ops_get_ipsec_spi(0,
					/*src*/&c->remote->host.addr,
					/*dst*/&c->local->host.addr,
					&ip_protocol_ipcomp,
					get_proto_reqid(c->child.reqid, &ip_protocol_ipcomp),
					IPCOMP_FIRST_NEGOTIATED,
					IPCOMP_LAST_NEGOTIATED,
					"CPI", logger);
}

/*
 * Build an array of encapsulation rules/tmpl.  Order things
 * inner-most to outer-most so the last entry is what will go across
 * the wire.  A -1 entry of the packet to be encapsulated is implied.
 */

struct kernel_route {
	enum encap_mode mode;
	struct {
		ip_address address; /* ip_endpoint? */
		ip_selector route; /* ip_address? */
	} src, dst;
};

static struct kernel_route kernel_route_from_state(const struct state *st, enum direction direction)
{
	const struct connection *c = st->st_connection;

	enum encap_mode mode = ENCAP_MODE_TRANSPORT;
	FOR_EACH_THING(proto, &st->st_esp, &st->st_ah) {
		if (proto->present && proto->attrs.mode == ENCAPSULATION_MODE_TUNNEL) {
			mode = ENCAP_MODE_TUNNEL;
			break;
		}
	}

	/*
	 * With pfkey and transport mode with nat-traversal we need to
	 * change the remote IPsec SA to point to external ip of the
	 * peer.  Here we substitute real client ip with NATD ip.
	 *
	 * Bug #1004 fix.
	 *
	 * There really isn't "client" with XFRM and transport mode so
	 * eroute must be done to natted, visible ip. If we don't hide
	 * internal IP, communication doesn't work.
	 */
	ip_selector local_route;
	ip_selector remote_route;
	const ip_selectors *local = &c->local->child.selectors.accepted;
	const ip_selectors *remote = &c->remote->child.selectors.accepted;
	switch (mode) {
	case ENCAP_MODE_TUNNEL:
		local_route = unset_selector;	/* XXX: kernel_policy has spd->client */
		remote_route = unset_selector;	/* XXX: kernel_policy has spd->client */
		break;
	case ENCAP_MODE_TRANSPORT:
		/*
		 * XXX: need to work around:
		 *
		 * - IKEv1 which is clueless to selectors.accepted
		 * - CP which skips setting TS
		 * - CK_PERMENANT that doesn't update TS
		 */
		local_route = (local->len > 0 ? local->list[0] :
			       c->spd->local->client);
		ip_selector remote_client = (remote->len > 0 ? remote->list[0] :
					     c->spd->remote->client);
		/* reroute remote to pair up with dest */
		remote_route = selector_from_address_protocol_port(c->remote->host.addr,
								   selector_protocol(remote_client),
								   selector_port(remote_client));
		break;
	default:
		bad_case(mode);
	}

	switch (direction) {
	case DIRECTION_INBOUND:
		return (struct kernel_route) {
			.mode = mode,
			.src.address = c->remote->host.addr,
			.dst.address = c->local->host.addr,
			.src.route = remote_route,
			.dst.route = local_route,
		};
	case DIRECTION_OUTBOUND:
		return (struct kernel_route) {
			.mode = mode,
			.src.address = c->local->host.addr,
			.dst.address = c->remote->host.addr,
			.src.route = local_route,
			.dst.route = remote_route,
		};
	default:
		bad_case(direction);
	}
}

/*
 * Find who currently owns the route and kernel policy matching the
 * SPD.
 */

static const struct spd_owner null_spd_owner;

struct spd_owner spd_owner(const struct spd_route *spd, unsigned indent)
{
	struct connection *c = spd->connection;
	struct logger *logger = c->logger;
	if (!oriented(c)) {
		llog(RC_LOG, logger,
		     "connection no longer oriented - system interface change?");
		return null_spd_owner;
	}

	selector_pair_buf spb;
	ldbg(logger, "%*slooking for SPD owners of %s",
	     indent, "",
	     str_selector_pair(&spd->local->client, &spd->remote->client, &spb));

	struct spd_owner owner = null_spd_owner;

	struct spd_route_filter srf = {
		.remote_client_range = &spd->remote->client,
		.where = HERE,
	};

	indent += 2;
	while (next_spd_route(NEW2OLD, &srf)) {
		struct spd_route *d_spd = srf.spd;
		struct connection *d = d_spd->connection;

		/*
		 * Part 1: eliminate cases common to both routes and
		 * policies.
		 */

		if (spd == d_spd) {
			ldbg(logger, "%*s%s skipped; same SPD",
			     indent, "", d->name);
			continue;
		}

		if (d->child.routing == RT_UNROUTED) {
			ldbg(logger, "%*s%s skipped; unrouted",
			     indent, "", d->name);
			continue;
		}

		if (!oriented(d)) {
			/* can happen during shutdown */
			ldbg(logger, "%*s%s skipped; not oriented",
			     indent, "", d->name);
			continue;
		}

		/* fast lookup did it's job! */
		PEXPECT(logger, selector_range_eq_selector_range(spd->remote->client,
								 d_spd->remote->client));
		if (!selector_eq_selector(spd->remote->client,
					  d_spd->remote->client)) {
			ldbg(logger, "%*s%s skipped; different selectors",
			     indent, "", d->name);
			continue;
		}

		/* XXX: why? */
		if (!address_eq_address(c->local->host.addr,
					d->local->host.addr)) {
			ldbg(logger, "%*s%s skipped; different local address?!?",
			     indent, "", d->name);
			continue;
		}

		/*
		 * Consider SPDs to be different when the either in or
		 * out marks differ (after masking).
		 */

		if ((c->sa_marks.in.val & c->sa_marks.in.mask) != (d->sa_marks.in.val & d->sa_marks.in.mask)) {
			ldbg(logger, "%*s%s skipped; marks.in %"PRIu32"/%#08"PRIx32" vs %"PRIu32"/%#08"PRIx32,
			     indent, "", d->name,
			     c->sa_marks.in.val, c->sa_marks.in.mask,
			     d->sa_marks.in.val, d->sa_marks.in.mask);
			continue;
		}

		if ((c->sa_marks.out.val & c->sa_marks.out.mask) != (d->sa_marks.out.val & d->sa_marks.out.mask)) {
			ldbg(logger, "%s()%s skipped; marks.out %"PRIu32"/%#08"PRIx32" vs %"PRIu32"/%#08"PRIx32,
			     __func__, d->name,
			     c->sa_marks.out.val, c->sa_marks.out.mask,
			     d->sa_marks.out.val, d->sa_marks.out.mask);
			continue;
		}

		/*
		 * Save either.
		 */

		switch (d->child.routing) {
		case RT_UNROUTED:
		case RT_UNROUTED_TUNNEL:	/* was RT_UNROUTED */
			bad_case(d->child.routing); /* see above */
		case RT_UNROUTED_NEGOTIATION:
			if (owner.policy == NULL) {
				ldbg(logger, "%*s%s saved SPD policy; first match",
				     indent, "", d->name);
				owner.policy = d_spd;
			} else if (owner.policy->connection->child.routing < d->child.routing) {
				ldbg(logger, "%*s%s saved SPD policy; better match",
				     indent, "", d->name);
				owner.policy = d_spd;
			}
			break;
		case RT_ROUTED_ONDEMAND:
		case RT_ROUTED_NEVER_NEGOTIATE:
		case RT_ROUTED_NEGOTIATION:
		case RT_ROUTED_FAILURE:
		case RT_ROUTED_TUNNEL:
			if (owner.route == NULL) {
				PEXPECT(logger, (owner.policy == NULL ||
						 owner.policy->connection->child.routing == RT_UNROUTED_NEGOTIATION));
				ldbg(logger, "%*s%s saved SPD route+policy; first route match",
				     indent, "", d->name);
				owner.route = owner.policy = d_spd;
			} else if (owner.route->connection->child.routing < d->child.routing) {
				ldbg(logger, "%*s%s saved SPD route+policy; better match",
				     indent, "", d->name);
				owner.route = owner.policy = d_spd;
			}
			break;
		}
	}
	indent -= 2;

	LDBGP_JAMBUF(DBG_BASE, logger, buf) {
		jam(buf, "%*s", indent, "");
		jam_connection(buf, c);
		jam_string(buf, " ");
		jam_enum_short(buf, &routing_story, c->child.routing);
		jam_string(buf, ":");

		const char *what = "route";
		FOR_EACH_THING(clash, owner.route, owner.policy) {
			jam_string(buf, what);
			jam_string(buf, " ");
			if (clash == NULL) {
				jam(buf, "NULL");
			} else if (clash->connection == spd->connection) {
				PEXPECT(logger, clash != spd); /*per-above*/
				jam_string(buf, "sibling");
			} else {
				PEXPECT(logger, clash != spd); /*per-above*/
				jam_connection(buf, clash->connection);
				jam_string(buf, " ");
				jam_enum_short(buf, &routing_story,
					       clash->connection->child.routing);
			}
			what = "policy";
		}
	}

	return owner;
}

struct spd_route *route_owner(struct spd_route *spd)
{
	return spd_owner(spd, 0).route;
}

/*
 * handle co-terminal attempt of the "near" kind
 *
 * Note: it mutates both inside and outside
 */

enum routability {
	ROUTE_IMPOSSIBLE,
	ROUTE_UNNECESSARY,
	ROUTEABLE,
};

static enum routability connection_routability(struct connection *c,
					       struct logger *logger)
{
	esb_buf b;
	ldbg(logger,
	     "kernel: %s() kind=%s remote.has_client=%s oppo=%s local.host.port=%u sec_label="PRI_SHUNK,
	     __func__,
	     enum_show(&connection_kind_names, c->kind, &b),
	     bool_str(c->remote->child.has_client),
	     bool_str(c->policy & POLICY_OPPORTUNISTIC),
	     c->local->host.port,
	     pri_shunk(c->config->sec_label));

	/* it makes no sense to route a connection that is ISAKMP-only */
	if (!NEVER_NEGOTIATE(c->policy) && !HAS_IPSEC_POLICY(c->policy)) {
		llog(RC_ROUTE, logger,
		     "cannot route an %s-only connection",
		     c->config->ike_info->sa_name[IKE_SA]);
		return ROUTE_IMPOSSIBLE;
	}

	/*
	 * if this is a transport SA, and overlapping SAs are supported, then
	 * this route is not necessary at all.
	 */
	PEXPECT(logger, !kernel_ops->overlap_supported); /* still WIP */
	if (kernel_ops->overlap_supported && !LIN(POLICY_TUNNEL, c->policy)) {
		ldbg(logger, "route-unnecessary: overlap and !tunnel");
		return ROUTE_UNNECESSARY;
	}

	/*
	 * If this is a template connection, we cannot route.
	 *
	 * However, opportunistic and sec_label templates can be
	 * routed (as in install the policy).
	 */
	if (c->kind == CK_TEMPLATE) {
		if (c->policy & POLICY_OPPORTUNISTIC) {
			ldbg(logger, "template-route-possible: opportunistic");
		} else if (c->policy & POLICY_GROUPINSTANCE) {
			ldbg(logger, "template-route-possible: groupinstance");
		} else if (c->config->sec_label.len > 0) {
			ldbg(logger, "template-route-possible: has sec-label");
		} else if (c->local->config->child.virt != NULL) {
			ldbg(logger, "template-route-possible: local is virtual");
		} else if (c->remote->child.has_client) {
			/* see extract_child_end() */
			ldbg(logger, "template-route-possible: remote %s.child.has_client==true",
			     c->remote->config->leftright);
		} else {
			policy_buf pb;
			llog(RC_ROUTE, logger,
			     "cannot route template policy of %s",
			     str_connection_policies(c, &pb));
			return ROUTE_IMPOSSIBLE;
		}
	}
	return ROUTEABLE; /* aka keep looking */
}

/*
 * XXX: can this and/or route_owner() be merged?
 */

static void get_connection_spd_conflict(struct spd_route *spd, struct logger *logger, unsigned indent)
{
	zero(&spd->wip);
	spd->wip.conflicting.ok = true; /* hope for the best */

	struct connection *c = spd->connection;

	if (c->config->sec_label.len > 0) {
		/* sec-labels ignore conflicts */
		return;
	}

	/*
	 * Find how owns the installed SPD (kernel policy).
	 */

	spd->wip.conflicting.spd = spd_owner(spd, indent).policy;
	pexpect(spd->wip.conflicting.spd == NULL ||
		kernel_policy_installed(spd->wip.conflicting.spd->connection));

	/*
	 * If there's no SPD with a conflicting policy, perhaps
	 * there's a bare one.
	 *
	 * XXX: why not add this to the above hash table?
	 */

	spd->wip.conflicting.shunt =
		bare_shunt_ptr(&spd->local->client, &spd->remote->client, __func__);

	/*
	 * Report what was found.
	 */

	selector_pair_buf sb;
	ldbg(logger,
	     "%*s kernel: %s() %s; wip.conflicting.spd %s wip.conflicting.shunt=%s",
	     indent, "",
	     __func__, str_selector_pair(&spd->local->client, &spd->remote->client, &sb),
	     (spd->wip.conflicting.spd == NULL ? "<none>" : spd->wip.conflicting.spd->connection->name),
	     (spd->wip.conflicting.shunt == NULL ? "<none>" : (*spd->wip.conflicting.shunt)->why));

	/*
	 * If there is already an SPD owner for peer's client subnet
	 * and it disagrees about interface or nexthop, we cannot
	 * steal it.
	 *
	 * XXX: should route_owner() have filtered out this already?
	 *
	 * Note: if this connection is already routed (perhaps for
	 * another state object), the route will agree.  This is as it
	 * should be -- it will arise during rekeying.
	 */
	struct connection *ro = (spd->wip.conflicting.spd == NULL ? NULL :
				 spd->wip.conflicting.spd->connection);
	if (ro != NULL && (ro->interface->ip_dev != c->interface->ip_dev ||
			   !address_eq_address(ro->local->host.nexthop, c->local->host.nexthop))) {
		/*
		 * Another connection is already using the eroute.
		 *
		 * TODO: XFRM supports this. For now, only allow this
		 * for OE.
		 */
		if ((c->policy & POLICY_OPPORTUNISTIC) == LEMPTY) {
			connection_buf cib;
			llog(RC_LOG_SERIOUS, logger,
			     "cannot route -- route already in use for "PRI_CONNECTION"",
			     pri_connection(ro, &cib));
			spd->wip.conflicting.ok = false;
		} else {
			connection_buf cib;
			llog(RC_LOG_SERIOUS, logger,
			     "cannot route -- route already in use for "PRI_CONNECTION" - but allowing anyway",
			     pri_connection(ro, &cib));
		}
	}
}

static bool get_connection_spd_conflicts(struct connection *c, struct logger *logger)
{
	ldbg(logger, "checking %s for conflicts", c->name);
	bool routable = false;
	FOR_EACH_ITEM(spd, &c->child.spds) {
		get_connection_spd_conflict(spd, logger, 2);
		routable |= spd->wip.conflicting.ok;
	}
	return routable;
}

static void revert_kernel_policy(struct spd_route *spd,
				 struct state *st/*could be NULL*/,
				 struct logger *logger)
{
	struct connection *c = spd->connection;
	PEXPECT(logger, st == NULL || st->st_connection == c);
	PEXPECT(logger, (logger == c->logger ||
			 logger == st->st_logger));

	/*
	 * Kill the firewall if previously there was no owner.
	 */
	if (spd->wip.installed.firewall && c->child.newest_routing_sa == SOS_NOBODY) {
		PEXPECT(logger, st != NULL);
		ldbg(logger, "kernel: %s() reverting the firewall", __func__);
		if (!do_updown(UPDOWN_DOWN, c, spd, st, logger))
			dbg("kernel: down command returned an error");
	}

	/*
	 * Now unwind the policy.
	 *
	 * Of course, if things failed before the policy was
	 * installed, there's nothing to do.
	 */

	if (!spd->wip.installed.policy) {
		ldbg(logger, "kernel: %s() no kernel policy to revert", __func__);
		return;
	}

	/*
	 * If there was no bare shunt, just delete everything.
	 *
	 * XXX: is this overkill?  For instance, when an instance
	 * IPsec fails should things go back to the prospective
	 * template?
	 */

	if (spd->wip.conflicting.shunt == NULL) {
		ldbg(logger, "kernel: %s() no previous kernel policy or shunt: delete whatever we installed",
		     __func__);
		delete_spd_kernel_policy(spd, DIRECTION_OUTBOUND,
					 EXPECT_KERNEL_POLICY_OK,
					 c->logger, HERE,
					 "deleting failed policy");
		delete_spd_kernel_policy(spd, DIRECTION_INBOUND,
					 EXPECT_KERNEL_POLICY_OK,
					 c->logger, HERE,
					 "deleting failed policy");
		if (st != NULL && spd->local->child->has_cat && !spd->block) {
			ldbg(logger, "cleaning up CAT that had kittens");
			delete_cat_kernel_policy(spd,
						 EXPECT_KERNEL_POLICY_OK,
						 c->logger, HERE, "cleaning up CAT");
		}
		return;
	}

	/* only one - shunt set when no policy */
	PASSERT(logger, spd->wip.conflicting.shunt != NULL);

	/*
	 * If there's a bare shunt, restore it.
	 *
	 * I don't think that this case is very likely.  Normally a
	 * bare shunt would have been assigned to a connection before
	 * we've gotten this far.
	 */

	ldbg(logger, "kernel: %s() restoring bare shunt", __func__);
	struct bare_shunt *bs = *spd->wip.conflicting.shunt;
	if (!install_bare_kernel_policy(bs->our_client, bs->peer_client,
					bs->shunt_kind, bs->shunt_policy,
					logger, HERE)) {
		llog(RC_LOG, st->st_logger,
		     "%s() failed to restore/replace SA",
		     __func__);
	}
}

static bool unrouted_to_routed(struct connection *c)
{
	enum routability r = connection_routability(c, c->logger);
	switch (r) {
	case ROUTE_IMPOSSIBLE:
		return false;
	case ROUTE_UNNECESSARY:
		return true;
	case ROUTEABLE:
		break;
	}

	/*
	 * Pass +0: Lookup the status of each SPD.
	 *
	 * Still call find_spd_conflicts() when a sec_label so that
	 * the structure is zeroed (sec_labels ignore conflicts).
	 */

	if (!get_connection_spd_conflicts(c, c->logger)) {
		return false;
	}

	/*
	 * Pass +1: install / replace kernel policy where needed.
	 */

	bool ok = true;
	FOR_EACH_ITEM(spd, &c->child.spds) {

		if (!ok) {
			break;
		}

		/*
		 * When overlap isn't supported, the old clashing bare
		 * shunt needs to be deleted before the new one can be
		 * installed.  Else it can be deleted after.
		 *
		 * For linux this also requires SA_MARKS to be set
		 * uniquely.
		 */

		if (spd->wip.conflicting.shunt != NULL &&
		    PEXPECT(c->logger, !kernel_ops->overlap_supported)) {
			delete_bare_shunt_kernel_policy(*spd->wip.conflicting.shunt,
							EXPECT_KERNEL_POLICY_OK,
							c->logger, HERE);
			/* if everything succeeds, delete below */
		}

		ok &= spd->wip.installed.policy =
			install_prospective_kernel_policies(spd, c->logger, HERE);

		if (spd->wip.conflicting.shunt != NULL &&
		    PBAD(c->logger, kernel_ops->overlap_supported)) {
			delete_bare_shunt_kernel_policy(*spd->wip.conflicting.shunt,
							EXPECT_KERNEL_POLICY_OK,
							c->logger, HERE);
			/* if everything succeeds, delete below */
		}
	}

	/*
	 * Pass +2: add the route.
	 */

	ldbg(c->logger, "kernel: %s() running updown-prepare when needed", __func__);
	FOR_EACH_ITEM(spd, &c->child.spds) {
		if (!ok) {
			break;
		}
		if (spd->wip.conflicting.spd == NULL) {
			/* a new route: no deletion required, but preparation is */
			if (!do_updown(UPDOWN_PREPARE, c, spd, NULL/*state*/, c->logger))
				ldbg(c->logger, "kernel: prepare command returned an error");
		}
	}

	ldbg(c->logger, "kernel: %s() running updown-route when needed", __func__);
	FOR_EACH_ITEM(spd, &c->child.spds) {
		if (!ok) {
			break;
		}
		if (spd->wip.conflicting.spd == NULL) {
			ok &= spd->wip.installed.route =
				do_updown(UPDOWN_ROUTE, c, spd, NULL/*state*/, c->logger);
		}
	}

	/*
	 * If things failed bail.
	 */

	if (!ok) {
		FOR_EACH_ITEM(spd, &c->child.spds) {
			revert_kernel_policy(spd, NULL/*st*/, c->logger);
		}
		return false;
	}

	/*
	 * Now clean up any shunts that were replaced.
	 */

	FOR_EACH_ITEM(spd, &c->child.spds) {
		struct bare_shunt **bspp = spd->wip.conflicting.shunt;
		if (bspp != NULL) {
			free_bare_shunt(bspp);
		}
	}

	return true;
}

bool unrouted_to_routed_ondemand(struct connection *c)
{
	if (!unrouted_to_routed(c)) {
		return false;
	}
	set_child_routing(c, RT_ROUTED_ONDEMAND, SOS_NOBODY);
	return true;
}

bool unrouted_to_routed_never_negotiate(struct connection *c)
{
	if (!unrouted_to_routed(c)) {
		return false;
	}
	set_child_routing(c, RT_ROUTED_NEVER_NEGOTIATE, SOS_NOBODY);
	return true;
}

/*
 * Find a bare shunt that encompasses the selector pair.
 *
 * Since bare shunt kernel policies have the heighest priority (0) use
 * selector_in_selector for the match.  For instance a bare shunt
 * 1.2.3.4/32/tcp encompass the address 1.2.3.4/32/tcp/22.
 *
 * Trick: return a pointer to the pointer to the entry; this allows
 * the entry to be deleted.
 */
struct bare_shunt **bare_shunt_ptr(const ip_selector *our_client,
				   const ip_selector *peer_client,
				   const char *why)

{
	selector_pair_buf sb;
	dbg("kernel: %s looking for %s",
	    why, str_selector_pair(our_client, peer_client, &sb));
	for (struct bare_shunt **pp = &bare_shunts; *pp != NULL; pp = &(*pp)->next) {
		struct bare_shunt *p = *pp;
		ldbg_bare_shunt(&global_logger, "comparing", p);
		if (selector_in_selector(*our_client, p->our_client) &&
		    selector_in_selector(*peer_client, p->peer_client)) {
			return pp;
		}
	}
	return NULL;
}

/*
 * Free a bare_shunt entry, given a pointer to the pointer.
 */
void free_bare_shunt(struct bare_shunt **pp)
{
	struct bare_shunt *p;

	passert(pp != NULL);

	p = *pp;

	*pp = p->next;
	ldbg_bare_shunt(&global_logger, "delete", p);
	pfree(p);
}

unsigned shunt_count(void)
{
	unsigned i = 0;

	for (const struct bare_shunt *bs = bare_shunts; bs != NULL; bs = bs->next) {
		i++;
	}

	return i;
}

void show_shunt_status(struct show *s)
{
	show_separator(s);
	show_comment(s, "Bare Shunt list:");
	show_separator(s);

	for (const struct bare_shunt *bs = bare_shunts; bs != NULL; bs = bs->next) {
		/* Print interesting fields.  Ignore count and last_active. */
		SHOW_JAMBUF(RC_COMMENT, s, buf) {
			jam_selector_subnet_port(buf, &(bs)->our_client);
			jam(buf, " -%d-> ", bs->transport_proto->ipproto);
			jam_selector_subnet_port(buf, &(bs)->peer_client);
			jam_string(buf, " => ");
			jam_enum(buf, &shunt_policy_percent_names, bs->shunt_policy);
			jam_string(buf, "    ");
			jam_string(buf, bs->why);
			if (bs->restore_serialno != UNSET_CO_SERIAL) {
				jam_string(buf, " ");
				jam_co(buf, bs->restore_serialno);
			}
		}
	}
}

static void delete_bare_shunt_kernel_policy(const struct bare_shunt *bsp,
					    enum expect_kernel_policy expect_kernel_policy,
					    struct logger *logger, where_t where)
{
	/*
	 * XXX: bare_kernel_policy() does not strip the port but this
	 * code does.
	 *
	 * Presumably it is because bare shunts is widened to include
	 * all protocols / ports.  But if that were true the selectors
	 * would have already excluded the port.
	 *
	 * XXX: this is probably a bug.  Any widening should happen
	 * before the bare shunt is added.
	 */
#if 0
	pexpect(bsp->our_client.hport == 0);
	pexpect(bsp->peer_client.hport == 0);
#endif
	const struct ip_protocol *transport_proto = bsp->transport_proto;
	ip_address src_address = selector_prefix(bsp->our_client);
	ip_address dst_address = selector_prefix(bsp->peer_client);
	ip_selector src = selector_from_address_protocol(src_address, transport_proto);
	ip_selector dst = selector_from_address_protocol(dst_address, transport_proto);
	/* assume low code logged action */
	if (!delete_kernel_policy(DIRECTION_OUTBOUND,
				  expect_kernel_policy,
				  &src, &dst,
				  /*sa_marks*/NULL, /*xfrmi*/NULL, /*bare-shunt*/
				  DEFAULT_KERNEL_POLICY_ID,
				  /* bare-shunt: no sec_label XXX: ?!? */
				  null_shunk,
				  logger, where, "bare shunt")) {
		/* ??? we could not delete a bare shunt */
		llog_bare_shunt(RC_LOG, logger, bsp, "failed to delete kernel policy");
	}
}

/*
 * Clear any bare shunt holds that overlap with the network we have
 * just routed.  We only consider "narrow" holds: ones for a single
 * address to single address.
 */

static void clear_narrow_holds(const ip_selector *src_client,
			       const ip_selector *dst_client,
			       struct logger *logger)
{
	const struct ip_protocol *transport_proto = protocol_from_ipproto(src_client->ipproto);
	struct bare_shunt **bspp = &bare_shunts;
	while (*bspp != NULL) {
		/*
		 * is bsp->{local,remote} within {local,remote}.
		 */
		struct bare_shunt *bsp = *bspp;
		if (bsp->shunt_policy == SHUNT_HOLD &&
		    transport_proto == bsp->transport_proto &&
		    selector_in_selector(bsp->our_client, *src_client) &&
		    selector_in_selector(bsp->peer_client, *dst_client)) {
			delete_bare_shunt_kernel_policy(bsp,
							EXPECT_KERNEL_POLICY_OK,
							logger, HERE);
			free_bare_shunt(bspp);
		} else {
			bspp = &(*bspp)->next;
		}
	}
}

bool install_sec_label_connection_policies(struct connection *c, struct logger *logger)
{
	connection_buf cb;
	ldbg(logger,
	     "kernel: %s() "PRI_CO" "PRI_CO" "PRI_CONNECTION" routed %s sec_label="PRI_SHUNK,
	     __func__,
	     pri_connection_co(c),
	     pri_connection_co(c->clonedfrom),
	     pri_connection(c, &cb),
	     enum_name(&routing_story, c->child.routing),
	     pri_shunk(c->config->sec_label));

	if (PBAD(logger, !labeled_parent(c))) {
		return false;
	}

	if (PBAD(logger, kernel_policy_installed(c))) {
		dbg("kernel: %s() connection already routed", __func__);
		return true;
	}

	/*
	 * SE installs both an outgoing and incoming policy.  Normal
	 * connections do not.
	 */
	FOR_EACH_THING(direction, DIRECTION_OUTBOUND, DIRECTION_INBOUND) {
		if (!install_bare_sec_label_kernel_policy(c->spd,
							  KERNEL_POLICY_OP_ADD,
							  direction,
							  EXPECT_KERNEL_POLICY_OK,
							  /*logger*/logger, HERE,
							  "prospective security label")) {
			if (direction == DIRECTION_INBOUND) {
				/*
				 * Need to pull the just installed
				 * outbound policy.
				 *
				 * XXX: this call highlights why
				 * having both KP_*_REVERSED and and
				 * reversed parameters is just so
				 * lame.  raw_policy can handle this.
				 */
				ldbg(logger, "pulling previously installed outbound policy");
				pexpect(direction == DIRECTION_INBOUND);
				delete_spd_kernel_policy(c->spd, DIRECTION_OUTBOUND,
							 EXPECT_KERNEL_POLICY_OK,
							 /*logger*/logger,
							 HERE, "security label policy");
			}
			return false;
		}
	}

	/* a new route: no deletion required, but preparation is */
	if (!do_updown(UPDOWN_PREPARE, c, c->spd, NULL/*ST*/, logger)) {
		ldbg(logger, "kernel: %s() prepare command returned an error", __func__);
	}

	if (!do_updown(UPDOWN_ROUTE, c, c->spd, NULL/*ST*/, logger)) {
		/* Failure!  Unwind our work. */
		ldbg(logger, "kernel: %s() route command returned an error", __func__);
		if (!do_updown(UPDOWN_DOWN, c, c->spd, NULL/*st*/, logger)) {
			ldbg(logger, "kernel: down command returned an error");
		}
		delete_spd_kernel_policy(c->spd, DIRECTION_OUTBOUND,
					 EXPECT_KERNEL_POLICY_OK,
					 logger, HERE, "failed security label");
		delete_spd_kernel_policy(c->spd, DIRECTION_INBOUND,
					 EXPECT_KERNEL_POLICY_OK,
					 logger, HERE, "failed security label");
		return false;
	}

	/* Success! */
	set_child_routing(c, RT_ROUTED_ONDEMAND, c->child.newest_routing_sa);
	return true;
}

/*
 * Install the negotiation kernel policy.
 *
 * Either the automatically installed %hold eroute is broad enough or
 * we try to add a broader one and delete the automatic one.  Beware:
 * this %hold might be already handled, but still squeak through
 * because of a race.
 *
 * XXX: what race?
 *
 * XXX: description seems strange?
 */

void assign_holdpass(struct connection *c UNUSED,
		     struct spd_route *spd,
		     enum kernel_policy_op op,
		     struct logger *logger,
		     const char *reason)
{
	/*
	 * We need a broad %hold, not the narrow one.
	 *
	 * First we ensure that there is a broad %hold.  There may
	 * already be one (race condition): no need to create one.
	 * There may already be a %trap: replace it.  There may not be
	 * any broad eroute: add %hold.  Once the broad %hold is in
	 * place, delete the narrow one.
	 *
	 * XXX: what race condition?
	 *
	 * XXX: why is OE special (other than that's the way the code
	 * worked in the past)?
	 */
	PASSERT(logger, (op == KERNEL_POLICY_OP_ADD ||
			 op == KERNEL_POLICY_OP_REPLACE));
	if (spd->local->child->has_cat) {
		if (!install_bare_cat_kernel_policy(spd, op,
						    EXPECT_KERNEL_POLICY_OK,
						    SHUNT_KIND_NEGOTIATION,
						    logger, HERE, "CAT: acquired")) {
			llog(RC_LOG, logger,
			     "CAT: failed to install Client Address Translation kernel policy");
		}
	}

	if (!install_bare_spd_kernel_policy(spd, op, DIRECTION_OUTBOUND,
					    EXPECT_KERNEL_POLICY_OK,
					    SHUNT_KIND_NEGOTIATION,
					    logger, HERE, reason)) {
		llog(RC_LOG, logger,
		     "%s() eroute_connection() failed", __func__);
	}

	dbg("kernel: %s() done", __func__);
}

static void setup_esp_nic_offload(struct kernel_state *sa, struct connection *c,
		bool *nic_offload_fallback)
{
	if (c->config->nic_offload == yna_no ||
	    c->interface == NULL || c->interface->ip_dev == NULL ||
	    c->interface->ip_dev->id_rname == NULL) {
		dbg("kernel: NIC esp-hw-offload disabled for connection '%s'", c->name);
		return;
	}

	if (c->config->nic_offload == yna_auto) {
		if (!c->interface->ip_dev->id_nic_offload) {
			dbg("kernel: NIC esp-hw-offload not for connection '%s' not available on interface %s",
				c->name, c->interface->ip_dev->id_rname);
			return;
		}
		*nic_offload_fallback = true;
		dbg("kernel: NIC esp-hw-offload offload for connection '%s' enabled on interface %s",
		    c->name, c->interface->ip_dev->id_rname);
	}
	sa->nic_offload_dev = c->interface->ip_dev->id_rname;
}

/*
 * Set up one direction of the SA bundle
 */

static bool setup_half_kernel_state(struct state *st, enum direction direction)
{
	/* Build an inbound or outbound SA */

	struct connection *c = st->st_connection;
	bool replace = (direction == DIRECTION_INBOUND && (kernel_ops->get_ipsec_spi != NULL));
	bool nic_offload_fallback = false;

	/* SPIs, saved for spigrouping or undoing, if necessary */
	struct kernel_state said[EM_MAXRELSPIS];
	struct kernel_state *said_next = said;

	/* same scope as said[] */
	said_buf text_ipcomp;
	said_buf text_esp;
	said_buf text_ah;

	uint64_t sa_ipsec_soft_bytes =  c->config->sa_ipsec_max_bytes;
	uint64_t sa_ipsec_soft_packets = c->config->sa_ipsec_max_packets;

	if (!LIN(POLICY_DONT_REKEY, c->policy)) {
		sa_ipsec_soft_bytes = fuzz_soft_limit("ipsec-max-bytes",st->st_sa_role,
						      c->config->sa_ipsec_max_bytes,
						      IPSEC_SA_MAX_SOFT_LIMIT_PERCENTAGE,
						      st->st_logger);
		sa_ipsec_soft_packets = fuzz_soft_limit("ipsec-max-packets", st->st_sa_role,
							c->config->sa_ipsec_max_packets,
							IPSEC_SA_MAX_SOFT_LIMIT_PERCENTAGE,
							st->st_logger);
	}


	struct kernel_route route = kernel_route_from_state(st, direction);

	const struct kernel_state said_boilerplate = {
		.src.address = route.src.address,
		.dst.address = route.dst.address,
		.src.route = route.src.route,
		.dst.route = route.dst.route,
		.direction = direction,
		.tunnel = (route.mode == ENCAP_MODE_TUNNEL),
		.sa_lifetime = c->config->sa_ipsec_max_lifetime,
		.sa_max_soft_bytes = sa_ipsec_soft_bytes,
		.sa_max_soft_packets = sa_ipsec_soft_packets,
		.sa_ipsec_max_bytes = c->config->sa_ipsec_max_bytes,
		.sa_ipsec_max_packets = c->config->sa_ipsec_max_packets,
		.sec_label = (st->st_v1_seen_sec_label.len > 0 ? st->st_v1_seen_sec_label :
			      st->st_v1_acquired_sec_label.len > 0 ? st->st_v1_acquired_sec_label :
			      c->child.sec_label /* assume connection outlive their kernel_sa's */),
	};

	address_buf sab, dab;
	selector_buf scb, dcb;
	dbg("kernel: %s() %s %s->[%s=%s=>%s]->%s sec_label="PRI_SHUNK"%s",
	    __func__,
	    enum_name_short(&direction_names, said_boilerplate.direction),
	    str_selector(&said_boilerplate.src.route, &scb),
	    str_address(&said_boilerplate.src.address, &sab),
	    enum_name_short(&encap_mode_names, route.mode),
	    str_address(&said_boilerplate.dst.address, &dab),
	    str_selector(&said_boilerplate.dst.route, &dcb),
	    /* see above */
	    pri_shunk(said_boilerplate.sec_label),
	    (st->st_v1_seen_sec_label.len > 0 ? " (IKEv1 seen)" :
	     st->st_v1_acquired_sec_label.len > 0 ? " (IKEv1 acquired)" :
	     c->child.sec_label.len > 0 ? " (IKEv2 this)" :
	     ""))

	/* set up IPCOMP SA, if any */

	if (st->st_ipcomp.present) {
		ipsec_spi_t ipcomp_spi = (direction == DIRECTION_INBOUND ? st->st_ipcomp.inbound.spi :
					  st->st_ipcomp.outbound.spi);
		*said_next = said_boilerplate;
		said_next->spi = ipcomp_spi;
		said_next->proto = &ip_protocol_ipcomp;

		said_next->ipcomp = st->st_ipcomp.attrs.transattrs.ta_ipcomp;
		said_next->level = said_next - said;
		said_next->reqid = reqid_ipcomp(c->child.reqid);
		said_next->story = said_str(route.dst.address,
					    &ip_protocol_ipcomp,
					    ipcomp_spi, &text_ipcomp);

		if (!kernel_ops_add_sa(said_next, replace, st->st_logger)) {
			log_state(RC_LOG, st, "add_sa ipcomp failed");
			goto fail;
		}
		said_next++;
	}

	/* set up ESP SA, if any */

	if (st->st_esp.present) {
		ipsec_spi_t esp_spi = (direction == DIRECTION_INBOUND ? st->st_esp.inbound.spi :
				       st->st_esp.outbound.spi);
		chunk_t esp_keymat = (direction == DIRECTION_INBOUND ? st->st_esp.inbound.keymat :
				      st->st_esp.outbound.keymat);
		const struct trans_attrs *ta = &st->st_esp.attrs.transattrs;

		const struct ip_encap *encap_type = NULL;
		uint16_t encap_sport = 0, encap_dport = 0;
		ip_address natt_oa;

		if (st->hidden_variables.st_nat_traversal & NAT_T_DETECTED ||
		    st->st_interface->io->protocol == &ip_protocol_tcp) {
			encap_type = st->st_interface->io->protocol->encap_esp;
			switch (direction) {
			case DIRECTION_INBOUND:
				encap_sport = endpoint_hport(st->st_remote_endpoint);
				encap_dport = endpoint_hport(st->st_interface->local_endpoint);
				break;
			case DIRECTION_OUTBOUND:
				encap_sport = endpoint_hport(st->st_interface->local_endpoint);
				encap_dport = endpoint_hport(st->st_remote_endpoint);
				break;
			default:
				bad_case(direction);
			}
			natt_oa = st->hidden_variables.st_nat_oa;
			dbg("kernel: natt/tcp sa encap_type="PRI_IP_ENCAP" sport=%d dport=%d",
			    pri_ip_encap(encap_type), encap_sport, encap_dport);
		}

		dbg("kernel: looking for alg with encrypt: %s keylen: %d integ: %s",
		    ta->ta_encrypt->common.fqn, ta->enckeylen, ta->ta_integ->common.fqn);

		/*
		 * Check that both integrity and encryption are
		 * supported by the kernel.
		 *
		 * Since the parser uses these exact same checks when
		 * loading the connection, they should never fail (if
		 * they do then strange things have been going on
		 * since the connection was loaded).
		 */
		if (!kernel_alg_integ_ok(ta->ta_integ)) {
			log_state(RC_LOG_SERIOUS, st,
				  "ESP integrity algorithm %s is not implemented or allowed",
				  ta->ta_integ->common.fqn);
			goto fail;
		}
		if (!kernel_alg_encrypt_ok(ta->ta_encrypt)) {
			log_state(RC_LOG_SERIOUS, st,
				  "ESP encryption algorithm %s is not implemented or allowed",
				  ta->ta_encrypt->common.fqn);
			goto fail;
		}

		/*
		 * Validate the encryption key size.
		 */
		size_t encrypt_keymat_size;
		if (!kernel_alg_encrypt_key_size(ta->ta_encrypt, ta->enckeylen,
						 &encrypt_keymat_size)) {
			log_state(RC_LOG_SERIOUS, st,
				  "ESP encryption algorithm %s with key length %d not implemented or allowed",
				  ta->ta_encrypt->common.fqn, ta->enckeylen);
			goto fail;
		}

		/* Fixup key lengths for special cases */
#ifdef USE_3DES
		if (ta->ta_encrypt == &ike_alg_encrypt_3des_cbc) {
			/* Grrrrr.... f*cking 7 bits jurassic algos */
			/* 168 bits in kernel, need 192 bits for keymat_len */
			if (encrypt_keymat_size == 21) {
				dbg("kernel: %s requires a 7-bit jurassic adjust",
				    ta->ta_encrypt->common.fqn);
				encrypt_keymat_size = 24;
			}
		}
#endif

		if (ta->ta_encrypt->salt_size > 0) {
			dbg("kernel: %s requires %zu salt bytes",
			    ta->ta_encrypt->common.fqn, ta->ta_encrypt->salt_size);
			encrypt_keymat_size += ta->ta_encrypt->salt_size;
		}

		size_t integ_keymat_size = ta->ta_integ->integ_keymat_size; /* BYTES */

		dbg("kernel: st->st_esp.keymat_len=%zu is encrypt_keymat_size=%zu + integ_keymat_size=%zu",
		    esp_keymat.len, encrypt_keymat_size, integ_keymat_size);

		PASSERT(st->st_logger, esp_keymat.len == encrypt_keymat_size + integ_keymat_size);

		*said_next = said_boilerplate;
		said_next->spi = esp_spi;
		said_next->proto = &ip_protocol_esp;
		said_next->replay_window = c->sa_replay_window;
		dbg("kernel: setting IPsec SA replay-window to %d", c->sa_replay_window);

		if (c->xfrmi != NULL) {
			said_next->xfrm_if_id = c->xfrmi->if_id;
			said_next->mark_set = c->sa_marks.out;
		}

		if (direction == DIRECTION_OUTBOUND && c->sa_tfcpad != 0 && !st->st_seen_no_tfc) {
			dbg("kernel: Enabling TFC at %d bytes (up to PMTU)", c->sa_tfcpad);
			said_next->tfcpad = c->sa_tfcpad;
		}

		if (c->policy & POLICY_DECAP_DSCP) {
			dbg("kernel: Enabling Decap ToS/DSCP bits");
			said_next->decap_dscp = true;
		}
		if (c->policy & POLICY_NOPMTUDISC) {
			dbg("kernel: Disabling Path MTU Discovery");
			said_next->nopmtudisc = true;
		}

		said_next->integ = ta->ta_integ;
#ifdef USE_SHA2
		if (said_next->integ == &ike_alg_integ_sha2_256 &&
			LIN(POLICY_SHA2_TRUNCBUG, c->policy)) {
			if (kernel_ops->sha2_truncbug_support) {
				if (libreswan_fipsmode() == 1) {
					log_state(RC_LOG_SERIOUS, st,
						  "Error: sha2-truncbug=yes is not allowed in FIPS mode");
					goto fail;
				}
				dbg("kernel:  authalg converted for sha2 truncation at 96bits instead of IETF's mandated 128bits");
				/*
				 * We need to tell the kernel to mangle
				 * the sha2_256, as instructed by the user
				 */
				said_next->integ = &ike_alg_integ_hmac_sha2_256_truncbug;
			} else {
				log_state(RC_LOG_SERIOUS, st,
					  "Error: %s stack does not support sha2_truncbug=yes",
					  kernel_ops->interface_name);
				goto fail;
			}
		}
#endif
		if (st->st_esp.attrs.transattrs.esn_enabled) {
			dbg("kernel: Enabling ESN");
			said_next->esn = true;
		}

		/*
		 * XXX: Assume SADB_ and ESP_ numbers match!  Clearly
		 * setting .compalg is wrong, don't yet trust
		 * lower-level code to be right.
		 */
		said_next->encrypt = ta->ta_encrypt;

		/* divide up keying material */
		said_next->encrypt_key = shunk2(esp_keymat.ptr, encrypt_keymat_size); /*BYTES*/
		said_next->integ_key = shunk2(esp_keymat.ptr + encrypt_keymat_size, integ_keymat_size); /*BYTES*/

		said_next->level = said_next - said;
		said_next->reqid = reqid_esp(c->child.reqid);

		said_next->src.encap_port = encap_sport;
		said_next->dst.encap_port = encap_dport;
		said_next->encap_type = encap_type;
		said_next->natt_oa = &natt_oa;
		said_next->story = said_str(route.dst.address,
					    &ip_protocol_esp,
					    esp_spi, &text_esp);

		if (DBGP(DBG_PRIVATE) || DBGP(DBG_CRYPT)) {
			DBG_dump_hunk("ESP encrypt key:",  said_next->encrypt_key);
			DBG_dump_hunk("ESP integrity key:", said_next->integ_key);
		}

		setup_esp_nic_offload(said_next, c, &nic_offload_fallback);

		bool ret = kernel_ops_add_sa(said_next, replace, st->st_logger);

		if (!ret && nic_offload_fallback &&
		    said_next->nic_offload_dev != NULL) {
			/* Fallback to non-nic-offload crypto */
			said_next->nic_offload_dev = NULL;
			ret = kernel_ops_add_sa(said_next, replace, st->st_logger);
		}

		/* scrub keys from memory */
		memset(esp_keymat.ptr, 0, esp_keymat.len);

		if (!ret)
			goto fail;

		said_next++;
	}

	/* set up AH SA, if any */

	if (st->st_ah.present) {
		ipsec_spi_t ah_spi = (direction == DIRECTION_INBOUND ? st->st_ah.inbound.spi :
				      st->st_ah.outbound.spi);
		chunk_t ah_keymat = (direction == DIRECTION_INBOUND ? st->st_ah.inbound.keymat :
				     st->st_ah.outbound.keymat);

		const struct integ_desc *integ = st->st_ah.attrs.transattrs.ta_integ;
		if (integ->integ_ikev1_ah_transform <= 0) {
			log_state(RC_LOG_SERIOUS, st,
				  "%s not implemented",
				  integ->common.fqn);
			goto fail;
		}

		PASSERT(st->st_logger, ah_keymat.len == integ->integ_keymat_size);

		*said_next = said_boilerplate;
		said_next->spi = ah_spi;
		said_next->proto = &ip_protocol_ah;
		said_next->integ = integ;
		said_next->integ_key = shunk2(ah_keymat.ptr, ah_keymat.len);
		said_next->level = said_next - said;
		said_next->reqid = reqid_ah(c->child.reqid);
		said_next->story = said_str(route.dst.address,
					    &ip_protocol_ah,
					    ah_spi, &text_ah);

		said_next->replay_window = c->sa_replay_window;
		dbg("kernel: setting IPsec SA replay-window to %d", c->sa_replay_window);

		if (st->st_ah.attrs.transattrs.esn_enabled) {
			dbg("kernel: Enabling ESN");
			said_next->esn = true;
		}

		if (DBGP(DBG_PRIVATE) || DBGP(DBG_CRYPT)) {
			DBG_dump_hunk("AH authkey:", said_next->integ_key);
		}

		bool ret = kernel_ops_add_sa(said_next, replace, st->st_logger);
		/* scrub key from memory */
		memset(ah_keymat.ptr, 0, ah_keymat.len);

		if (!ret) {
			goto fail;
		}

		said_next++;
	}

	/* If there are multiple SPIs, group them. */

	if (kernel_ops->grp_sa != NULL && said_next > &said[1]) {
		/*
		 * group SAs, two at a time, inner to outer (backwards in
		 * said[])
		 *
		 * The grouping is by pairs.  So if said[] contains
		 * ah esp ipip,
		 *
		 * the grouping would be ipip:esp, esp:ah.
		 */
		for (struct kernel_state *s = said; s < said_next - 1; s++) {
			dbg("kernel: grouping %s and %s",
			    s[0].story, s[1].story);
			if (!kernel_ops->grp_sa(s + 1, s)) {
				log_state(RC_LOG, st, "grp_sa failed");
				goto fail;
			}
		}
		/* could update said, but it will not be used */
	}
	/* if the impaired is set, pretend this fails */
	if (impair.sa_creation) {
		DBG_log("Impair SA creation is set, pretending to fail");
		goto fail;
	}
	return true;

fail:
	log_state(RC_LOG, st, "setup_half_ipsec_sa() hit fail:");
	/*
	 * Undo the done SPIs.
	 *
	 * Deleting the SPI also deletes any SAs attached to them.
	 */
	while (said_next-- != said) {
		if (said_next->proto != NULL) {
			kernel_ops_del_ipsec_spi(said_next->spi,
						 said_next->proto,
						 &said_next->src.address,
						 &said_next->dst.address,
						 st->st_logger);
		}
	}
	return false;
}

static bool install_inbound_ipsec_kernel_policies(struct state *st)
{
	const struct connection *c = st->st_connection;
	struct logger *logger = st->st_logger;
	/*
	 * Add an inbound eroute to enforce an arrival check.
	 *
	 * If inbound,
	 * ??? and some more mysterious conditions,
	 * Note reversed ends.
	 * Not much to be done on failure.
	 */
	ldbg(logger, "kernel: %s() owner="PRI_SO,
	     __func__, pri_so(c->child.newest_routing_sa));

	if (c->child.newest_routing_sa != SOS_NOBODY) {
		ldbg(logger, "kernel: %s() skipping as already has owner "PRI_SO,
		     __func__, pri_so(c->child.newest_routing_sa));
		return true;
	}

	if (c->config->sec_label.len > 0 &&
	    c->config->ike_version == IKEv2) {
		ldbg(logger, "kernel: %s() skipping as IKEv2 config.sec_label="PRI_SHUNK,
		     __func__, pri_shunk(c->config->sec_label));
		return true;
	}

	FOR_EACH_ITEM(spd, &c->child.spds) {
		selector_pair_buf spb;
		ldbg(logger, "kernel: %s() is installing SPD for %s",
		     __func__, str_selector_pair(&spd->remote->client, &spd->local->client, &spb));
		install_inbound_ipsec_kernel_policy(pexpect_child_sa(st), spd, HERE);
	}

	return true;
}

/*
 * XXX: Two cases:
 *
 * - the protocol was negotiated (and presumably installed)
 *   (.present)
 *
 * - the protocol was proposed but never finished (.out_spi
 *   inbound)
 */

struct dead_sa {	/* XXX: this is ip_said+src */
	const struct ip_protocol *protocol;
	ipsec_spi_t spi;
	ip_address src;
	ip_address dst;
};

static unsigned append_teardown(struct dead_sa *dead, enum direction direction,
				const struct ipsec_proto_info *proto,
				ip_address host_addr, ip_address effective_remote_address)
{
	bool present = proto->present;
	if (!present &&
	    direction == DIRECTION_INBOUND &&
	    proto->inbound.spi != 0 &&
	    proto->outbound.spi == 0) {
		dbg("kernel: forcing inbound delete of %s as .inbound.spi: "PRI_IPSEC_SPI"; attrs.spi: "PRI_IPSEC_SPI,
		    proto->protocol->name,
		    pri_ipsec_spi(proto->inbound.spi),
		    pri_ipsec_spi(proto->outbound.spi));
		present = true;
	}
	if (present) {
		dead->protocol = proto->protocol;
		switch (direction) {
		case DIRECTION_INBOUND:
			if (proto->inbound.kernel_sa_expired & SA_HARD_EXPIRED) {
				dbg("kernel expired SPI 0x%x skip deleting",
				    ntohl(proto->inbound.spi));
				return 0;
			}
			dead->spi = proto->inbound.spi; /* incoming */
			dead->src = effective_remote_address;
			dead->dst = host_addr;
			break;
		case DIRECTION_OUTBOUND:
			if (proto->outbound.kernel_sa_expired & SA_HARD_EXPIRED) {
				dbg("kernel hard expired SPI 0x%x skip deleting",
				    ntohl(proto->outbound.spi));
				return 0;
			}
			dead->spi = proto->outbound.spi; /* outgoing */
			dead->src = host_addr;
			dead->dst = effective_remote_address;
			break;
		default:
			bad_case(direction);
		}
		return 1;
	}
	return 0;
}

/*
 * Delete any AH, ESP, and IPCOMP kernel states.
 *
 * Deleting only requires the addresses, protocol, and IPsec SPIs.
 */

static bool uninstall_kernel_state(struct child_sa *child, enum direction direction)
{
	struct connection *const c = child->sa.st_connection;
	ldbg_sa(child, "kernel: %s() deleting %s",
		__func__, enum_name_short(&direction_names, direction));

	/*
	 * If we have a new address in c->remote->host.addr,
	 * we are the initiator, have been redirected,
	 * and yet this routine must use the old address.
	 *
	 * We point effective_remote_host_address to the appropriate
	 * address.
	 */

	ip_address effective_remote_address = c->remote->host.addr;
	if (!endpoint_address_eq_address(child->sa.st_remote_endpoint, effective_remote_address) &&
	    address_is_specified(c->temp_vars.redirect_ip)) {
		effective_remote_address = endpoint_address(child->sa.st_remote_endpoint);
	}

	/* collect each proto SA that needs deleting */

	struct dead_sa dead[3];	/* at most 3 entries */
	unsigned nr = 0;
	nr += append_teardown(dead + nr, direction, &child->sa.st_ah,
			      c->local->host.addr, effective_remote_address);
	nr += append_teardown(dead + nr, direction, &child->sa.st_esp,
			      c->local->host.addr, effective_remote_address);
	nr += append_teardown(dead + nr, direction, &child->sa.st_ipcomp,
			      c->local->host.addr, effective_remote_address);
	passert(nr < elemsof(dead));

	/*
	 * If the SAs have been grouped, deleting any one will do: we
	 * just delete the first one found.
	 */
	if (kernel_ops->grp_sa != NULL && nr > 1) {
		nr = 1;
	}

	/*
	 * Delete each proto that needs deleting.
	 *
	 * Deleting the SPI also deletes any corresponding SA.
	 */
	bool result = true;
	for (unsigned i = 0; i < nr; i++) {
		const struct dead_sa *tbd = &dead[i];
		result &= kernel_ops_del_ipsec_spi(tbd->spi,
						   tbd->protocol,
						   &tbd->src, &tbd->dst,
						   child->sa.st_logger);
	}

	return result;
}

static void kernel_process_msg_cb(int fd, void *arg, struct logger *logger)
{
	const struct kernel_ops *kernel_ops = arg;

	dbg("kernel: %s() process %s message", __func__, kernel_ops->interface_name);
	threadtime_t start = threadtime_start();
	kernel_ops->process_msg(fd, logger);
	threadtime_stop(&start, SOS_NOBODY, "kernel message");
}

static global_timer_cb kernel_process_queue_cb;

static void kernel_process_queue_cb(struct logger *unused_logger UNUSED)
{
	if (pexpect(kernel_ops->process_queue != NULL)) {
		kernel_ops->process_queue();
	}
}

const struct kernel_ops *const kernel_stacks[] = {
#ifdef KERNEL_XFRM
	&xfrm_kernel_ops,
#endif
#ifdef KERNEL_PFKEYV2
	&pfkeyv2_kernel_ops,
#endif
	NULL,
};

const struct kernel_ops *kernel_ops = NULL/*kernel_stacks[0]*/;

deltatime_t bare_shunt_interval = DELTATIME_INIT(SHUNT_SCAN_INTERVAL);

void init_kernel(struct logger *logger)
{
	struct utsname un;

	/* get kernel version */
	uname(&un);
	llog(RC_LOG, logger,
	     "using %s %s kernel support code on %s",
	     un.sysname, kernel_ops->interface_name, un.version);

	passert(kernel_ops->init != NULL);
	kernel_ops->init(logger);

	/* Add the port bypass polcies */

	if (kernel_ops->v6holes != NULL) {
		/* may not return */
		kernel_ops->v6holes(logger);
	}

	enable_periodic_timer(EVENT_SHUNT_SCAN, kernel_scan_shunts,
			      bare_shunt_interval);

	dbg("kernel: setup kernel fd callback");

	if (kernel_ops->async_fdp != NULL)
		/* Note: kernel_ops is const but pluto_event_add cannot know that */
		add_fd_read_listener(*kernel_ops->async_fdp, "KERNEL_XRM_FD",
				     kernel_process_msg_cb, (void*)kernel_ops);

	if (kernel_ops->route_fdp != NULL && *kernel_ops->route_fdp > NULL_FD) {
		add_fd_read_listener(*kernel_ops->route_fdp, "KERNEL_ROUTE_FD",
				     kernel_process_msg_cb, (void*)kernel_ops);
	}

	if (kernel_ops->process_queue != NULL) {
		/*
		 * AA_2015 this is untested code. only for non xfrm ???
		 * It seems in klips we should, besides kernel_process_msg,
		 * call process_queue periodically.  Does the order
		 * matter?
		 */
		enable_periodic_timer(EVENT_PROCESS_KERNEL_QUEUE,
				      kernel_process_queue_cb,
				      deltatime(KERNEL_PROCESS_Q_PERIOD));
	}
}

void show_kernel_interface(struct show *s)
{
	if (kernel_ops != NULL) {
		show_comment(s, "using kernel interface: %s",
			     kernel_ops->interface_name);
	}
}

/*
 * Note: install_inbound_ipsec_sa is only used by the Responder.
 * The Responder will subsequently use install_ipsec_sa for the outbound.
 * The Initiator uses install_ipsec_sa to install both at once.
 */
bool install_inbound_ipsec_sa(struct child_sa *child, where_t where)
{
	struct connection *const c = child->sa.st_connection;

	/*
	 * If our peer has a fixed-address client, check if we already
	 * have a route for that client that conflicts.  We will take
	 * this as proof that that route and the connections using it
	 * are obsolete and should be eliminated.  Interestingly, this
	 * is the only case in which we can tell that a connection is
	 * obsolete.
	 *
	 * XXX: can this make use of connection_routability() and / or
	 * get_connection_spd_conflicts() below?
	 */
	passert(c->kind == CK_PERMANENT || c->kind == CK_INSTANCE);
	if (c->remote->child.has_client) {
		for (;; ) {
			struct spd_route *ro = route_owner(c->spd);

			if (ro == NULL)
				break; /* nobody interesting has a route */
			struct connection *co = ro->connection;
			if (co == c) {
				break; /* nobody interesting has a route */
			}

			/* note: we ignore the client addresses at this end */
			/* XXX: but compating interfaces doesn't ?!? */
			if (sameaddr(&co->remote->host.addr,
				     &c->remote->host.addr) &&
			    co->interface == c->interface)
				break;  /* existing route is compatible */

			if (kernel_ops->overlap_supported) {
				/*
				 * Both are transport mode, allow overlapping.
				 * [bart] not sure if this is actually
				 * intended, but am leaving it in to make it
				 * behave like before
				 */
				if (!LIN(POLICY_TUNNEL, c->policy | co->policy))
					break;

				/* Both declared that overlapping is OK. */
				if (LIN(POLICY_OVERLAPIP, c->policy & co->policy))
					break;
			}

			address_buf b;
			connection_buf cib;
			llog_sa(RC_LOG_SERIOUS, child,
				"route to peer's client conflicts with "PRI_CONNECTION" %s; releasing old connection to free the route",
				pri_connection(co, &cib),
				str_address_sensitive(&co->remote->host.addr, &b));
			if (co->kind == CK_INSTANCE) {
				delete_connection(&co);
			} else {
				release_connection(co);
			}
		}
	}
	return install_ipsec_sa(child, DIRECTION_INBOUND, where);
}

static bool install_outbound_ipsec_kernel_policies(struct state *st)
{
	struct connection *c = st->st_connection;

	if (c->config->ike_version == IKEv2 &&
	    labeled_child(c)) {
		ldbg(st->st_logger, "kernel: %s() skipping install of IPsec policies as security label", __func__);
		return true;
	}

	if (c->child.newest_routing_sa == st->st_serialno) {
		ldbg(st->st_logger, "kernel: %s() skipping kernel policies as already owner", __func__);
		return true;
	}

	ldbg(st->st_logger,
	     "kernel: %s() installing IPsec policies for "PRI_SO": connection is currently "PRI_SO" %s",
	    __func__,
	    pri_so(st->st_serialno),
	    pri_so(c->child.newest_routing_sa),
	    enum_name(&routing_story, c->child.routing));

#ifdef USE_CISCO_SPLIT
	struct spd_route *start = c->spd;
	if (c->remotepeertype == CISCO && start->spd_next != NULL) {
		/* XXX: why is CISCO skipped? */
		start = start->spd_next;
	}
#endif

#ifdef IPSEC_CONNECTION_LIMIT
	unsigned new_spds = 0;
	for (struct spd_route *spd = start; spd != NULL; spd = spd->spd_next) {
		if (spd->wip.conflicting.shunt == NULL) {
			new_spds++;
		}
	}
	if (num_ipsec_eroute + new_spds >= IPSEC_CONNECTION_LIMIT) {
		llog(RC_LOG_SERIOUS, logger,
		     "Maximum number of IPsec connections reached (%d)",
		     IPSEC_CONNECTION_LIMIT);
		return false;
	}
#endif

	bool ok = true;	/* sticky: once false, stays false */

	/*
	 * Install the IPsec kernel policies.
	 */

	FOR_EACH_ITEM(spd, &c->child.spds) {

		if (!ok) {
			break;
		}

#ifdef USE_CISCO_SPLIT
		if (c->remotepeertype == CISCO &&
		    spd == c->spd &&
		    spd->spd_next != NULL) {
			continue;
		}
#endif

		enum kernel_policy_op op =
			(spd->wip.conflicting.shunt != NULL ? KERNEL_POLICY_OP_REPLACE :
			 KERNEL_POLICY_OP_ADD);
		if (spd->block) {
			llog(RC_LOG, st->st_logger, "state spd requires a block (and no CAT?)");
			ok = spd->wip.installed.policy =
				install_bare_spd_kernel_policy(spd, op, DIRECTION_OUTBOUND,
							       EXPECT_KERNEL_POLICY_OK,
							       SHUNT_KIND_BLOCK,
							       st->st_logger, HERE,
							       "install IPsec block policy");
		} else {
			ok = spd->wip.installed.policy =
				install_outbound_ipsec_kernel_policy(pexpect_child_sa(st), spd,
								     spd->wip.conflicting.shunt != NULL,
								     HERE);
		}
	}

	/*
	 * Do we have to notify the firewall?
	 *
	 * Yes, if we are installing a tunnel eroute and the firewall
	 * wasn't notified for a previous tunnel with the same
	 * clients.  Any Previous tunnel would have to be for our
	 * connection, so the actual test is simple.
	 */

	FOR_EACH_ITEM(spd, &c->child.spds) {

		if (!ok) {
			break;
		}

#ifdef USE_CISCO_SPLIT
		if (c->remotepeertype == CISCO &&
		    spd == c->spd &&
		    spd->spd_next != NULL) {
			continue;
		}
#endif
		if (c->child.newest_routing_sa != SOS_NOBODY) {
			/* already notified */
			spd->wip.installed.firewall = true;
		} else {
			/* go ahead and notify */
			ok = spd->wip.installed.firewall =
				do_updown(UPDOWN_UP, c, spd, st, st->st_logger);
		}
	}

	/*
	 * Do we have to make a mess of the routing?
	 *
	 * Probably.  This code path needs a re-think.
	 */

	ldbg(st->st_logger, "kernel: %s() running updown-prepare", __func__);
	if (ok) {
		FOR_EACH_ITEM(spd, &c->child.spds) {
#ifdef USE_CISCO_SPLIT
			if (c->remotepeertype == CISCO &&
			    spd == c->spd &&
			    spd->spd_next != NULL) {
				continue;
			}
#endif
			if (spd->wip.conflicting.spd == NULL) {
				/* a new route: no deletion required, but preparation is */
				if (!do_updown(UPDOWN_PREPARE, c, spd, st, st->st_logger))
					dbg("kernel: prepare command returned an error");
			}
		}
	}

	ldbg(st->st_logger, "kernel: %s() running updown-route", __func__);
	FOR_EACH_ITEM(spd, &c->child.spds) {
		if (!ok) {
			break;
		}
#ifdef USE_CISCO_SPLIT
		if (c->remotepeertype == CISCO &&
		    spd == c->spd &&
		    spd->spd_next != NULL) {
			continue;
		}
#endif
		if (spd->wip.conflicting.spd == NULL) {
			/* a new route: no deletion required, but preparation is */
			ok = spd->wip.installed.route =
				do_updown(UPDOWN_ROUTE, c, spd, st, st->st_logger);
		}
	}

	/*
	 * Finally clean up.
	 */

	if (!ok) {
		FOR_EACH_ITEM(spd, &c->child.spds) {
			revert_kernel_policy(spd, st, st->st_logger);
		}
		return false;
	}

	FOR_EACH_ITEM(spd, &c->child.spds) {
#ifdef USE_CISCO_SPLIT
		if (c->remotepeertype == CISCO &&
		    spd == c->spd &&
		    spd->spd_next != NULL) {
			continue;
		}
#endif
		struct bare_shunt **bspp = spd->wip.conflicting.shunt;
		if (bspp != NULL) {
			free_bare_shunt(bspp);
		}
		/* clear host shunts that clash with freshly installed route */
		clear_narrow_holds(&spd->local->client, &spd->remote->client, st->st_logger);
	}


#ifdef IPSEC_CONNECTION_LIMIT
	num_ipsec_eroute += new_spds;
	llog(RC_COMMENT, st->st_logger,
	     "%d IPsec connections are currently being managed",
	     num_ipsec_eroute);
#endif

	/* include CISCO's SPD */
	set_child_routing(c, RT_ROUTED_TUNNEL, st->st_serialno);
	return true;
}

bool install_ipsec_sa(struct child_sa *child, lset_t direction, where_t where)
{
	struct logger *logger = child->sa.st_logger;
	struct connection *c = child->sa.st_connection;
	ldbg(logger, "kernel: %s() for "PRI_SO": %s%s%s "PRI_WHERE,
	     __func__, pri_so(child->sa.st_serialno),
	     (direction & DIRECTION_INBOUND ? "inbound" : ""),
	     (direction == (DIRECTION_INBOUND | DIRECTION_OUTBOUND) ? "+" : ""),
	     (direction & DIRECTION_OUTBOUND ? "outbound" : ""),
	     pri_where(where));

	PASSERT(logger, (direction & (DIRECTION_INBOUND|DIRECTION_OUTBOUND)) != LEMPTY);
	PASSERT(logger, (direction & ~(DIRECTION_INBOUND|DIRECTION_OUTBOUND)) == LEMPTY);

	/*
	 * Pass +0: Lookup the status of each SPD.
	 *
	 * Still call find_spd_conflicts() when a sec_label so that
	 * the structure is zeroed (sec_labels ignore conflicts).
	 */

	enum routability r = connection_routability(c, logger);

	switch (r) {
	case ROUTEABLE:
		ldbg(logger, "kernel:    routing is easy");
		break;
	case ROUTE_UNNECESSARY:
		ldbg(logger, "kernel:    routing unnecessary");
		/*
		 * in this situation, we should look and see if there
		 * is a state that our connection references, that we
		 * are in fact replacing.
		 */
		break;
	case ROUTE_IMPOSSIBLE:
		dbg("kernel:    impossible");
		return false;
	default:
		bad_case(r);
	}

	if (!get_connection_spd_conflicts(c, logger)) {
		return false;
	}

	/* (attempt to) actually set up the SA group */


	/*
	 * Always setup the outgoing SA first, so that we can refer to
	 * it in the incoming SA (this happens when installing SAs in
	 * both directions and installing an incomming SA).
	 *
	 * XXX: what exactly is being refered to?
	 */
	if (!child->sa.st_outbound_done) {
		if (!setup_half_kernel_state(&child->sa, DIRECTION_OUTBOUND)) {
			ldbg(logger, "kernel: %s() failed to install outbound kernel state", __func__);
			return false;
		}
		ldbg(logger, "kernel: %s() setup outbound SA (kernel policy installed earlier?)", __func__);
		child->sa.st_outbound_done = true;
	}

	/* now setup inbound SA */
	if (direction & DIRECTION_INBOUND) {
		if (!setup_half_kernel_state(&child->sa, DIRECTION_INBOUND)) {
			ldbg(logger, "kernel: %s() failed to install inbound kernel state", __func__);
			return false;
		}
		if (!install_inbound_ipsec_kernel_policies(&child->sa)) {
			ldbg(logger, "kernel: %s() failed to install inbound kernel policy", __func__);
			return false;
		}
		ldbg(logger, "kernel: %s() installed inbound kernel state+policy", __func__);

		/*
		 * We successfully installed an IPsec SA, meaning it
		 * is safe to clear our revival back-off delay. This
		 * is based on the assumption that an unwilling
		 * partner might complete an IKE SA to us, but won't
		 * complete an IPsec SA to us.
		 */
		child->sa.st_connection->temp_vars.revive_delay = 0;
	}

	if (PEXPECT(logger, r == ROUTEABLE)
	    && (direction & DIRECTION_OUTBOUND)) {
		if (!install_outbound_ipsec_kernel_policies(&child->sa)) {
			teardown_ipsec_kernel_policies(child);
			uninstall_kernel_states(child);
			return false;
		}
	}

	/*
	 * Transfer ownership of the connection's IPsec SA (kernel
	 * state) to the new Child.
	 *
	 * For IKEv2, both inbound and outbound IPsec SAs are
	 * installed at the same time so direction doesn't matter.
	 *
	 * For IKEv1, on the responder during quick mode, inbound and
	 * then outbound IPsec SAs are installed during separate
	 * exchanges, hence direction does matter.
	 *
	 * Since the above code updates routing, the routing owner
	 * should match the child.
	 */
	if (direction & DIRECTION_OUTBOUND) {
		so_serial_t old_ipsec_sa = c->newest_ipsec_sa;
		so_serial_t new_ipsec_sa = child->sa.st_serialno;
		so_serial_t routing_sa = c->child.newest_routing_sa;
		connection_buf cb;
		ldbg(logger,
		     "kernel: %s() .newest_ipsec_sa "PRI_SO"->"PRI_SO" (routing SA "PRI_SO") "PRI_CONNECTION" "PRI_WHERE,
		     __func__, pri_so(old_ipsec_sa), pri_so(new_ipsec_sa),
		     pri_so(routing_sa),
		     pri_connection(c, &cb),
		     pri_where(where));
#if 0
		/*
		 * XXX: triggers when two peers initiate
		 * simultaneously eventually finding themselves
		 * fighting over the same Child SA, for instance in
		 * ikev2-systemrole-04-mesh.
		 */
		PEXPECT(child->sa.st_logger, new_ipsec_sa >= old_ipsec_sa);
#endif
#if 0
		PEXPECT(child->sa.st_logger, routing_sa == new_ipsec_sa);
#endif
		child->sa.st_connection->newest_ipsec_sa = new_ipsec_sa;
	}

	/* we only audit once for IPsec SA's, we picked the inbound SA */
	if (direction & DIRECTION_INBOUND) {
		linux_audit_conn(&child->sa, LAK_CHILD_START);
	}

	return true;
}

/*
 * Delete an IPSEC SA
 *
 * We may not succeed, but we bull ahead anyway because we cannot do
 * anything better by recognizing failure.
 *
 * This used to have a parameter inbound_only, but the saref code
 * changed to always install inbound before outbound so this it was
 * always false, and thus removed.  But this means that while there's
 * now always an outbound policy, there may not yet be an inbound
 * policy!  For instance, IKEv2 IKE AUTH initiator gets rejected.  So
 * what is there, and should this even be called?
 *
 * EXPECT_KERNEL_POLICY is trying to help sort this out.
 */

void teardown_ipsec_kernel_policies(struct child_sa *child)
{
	struct connection *c = child->sa.st_connection;
	struct logger *logger = child->sa.st_logger;

	if (c->child.routing != RT_ROUTED_TUNNEL) {
		ldbg(logger,
		     "kernel: %s() skipping, "PRI_SO" is not a routed tunnel",
		     __func__, pri_so(child->sa.st_serialno));
		return;
	}

	if (c->child.newest_routing_sa != child->sa.st_serialno) {
		ldbg(logger,
		     "kernel: %s() skipping, "PRI_SO" is not the routed owner "PRI_SO,
		     __func__, pri_so(child->sa.st_serialno),
		     pri_so(c->child.newest_routing_sa));
		return;
	}

	enum routing new_routing;
	if (c->kind == CK_INSTANCE &&
	    ((c->policy & POLICY_OPPORTUNISTIC) ||
	     (c->policy & POLICY_DONT_REKEY))) {
		new_routing = RT_UNROUTED;
	} else if (c->config->failure_shunt == SHUNT_NONE) {
		/*
		 * If the .failure_shunt==SHUNT_NONE then the
		 * .never_negotiate_shunt is chosen and that can't be
		 * SHUNT_NONE.
		 */
		new_routing = RT_ROUTED_ONDEMAND;
	} else {
		 /*
		 * If the .failure_shunt!=SHUNT_NONE then the
		 * .failure_shunt is chosen, and that isn't
		 * SHUNT_NONE.
		 */
		new_routing = RT_ROUTED_FAILURE;
	}


	struct spds spds = c->child.spds;
#ifdef USE_CISCO_SPLIT
	if (c->remotepeertype == CISCO) {
		/*
		 * XXX: this comment is out-of-date:
		 *
		 * XXX: this is currently the only reason for
		 * spd_next walking.
		 *
		 * Routing should become RT_ROUTED_FAILURE,
		 * but if POLICY_FAIL_NONE, then we just go
		 * right back to RT_ROUTED_PROSPECTIVE as if
		 * no failure happened.
		 */
		ldbg_sa(child,
			"kernel: %s() skipping, first SPD and remotepeertype is CISCO, damage done",
			__func__);
		passert(spds.len > 0);
		spds.list++;
		spds.len--;
	}
#endif

	switch (new_routing) {
	case RT_UNROUTED:
		/*
		 * update routing; route_owner() will see this and not
		 * think this route is the owner?
		 */
		set_child_routing(c, RT_UNROUTED, SOS_NOBODY);
		do_updown_spds(UPDOWN_DOWN, c, &spds, &child->sa, child->sa.st_logger);
		delete_spd_kernel_policies(&spds, EXPECT_KERNEL_POLICY_OK,
					   child->sa.st_logger, HERE, "unroute");
		do_updown_unowned_spds(UPDOWN_UNROUTE, c, &spds, NULL, child->sa.st_logger);
		break;
	case RT_ROUTED_ONDEMAND:
		replace_ipsec_with_bare_kernel_policies(child, RT_ROUTED_ONDEMAND,
							EXPECT_KERNEL_POLICY_OK, HERE);
		break;
	case RT_ROUTED_FAILURE:
		replace_ipsec_with_bare_kernel_policies(child, RT_ROUTED_FAILURE,
							EXPECT_KERNEL_POLICY_OK, HERE);
		break;
	case RT_UNROUTED_NEGOTIATION:
	case RT_ROUTED_NEGOTIATION:
	case RT_ROUTED_TUNNEL:
	case RT_UNROUTED_TUNNEL:
	case RT_ROUTED_NEVER_NEGOTIATE:
		bad_case(new_routing);
	}
}

void uninstall_kernel_states(struct child_sa *child)
{
	if (child->sa.st_esp.present || child->sa.st_ah.present) {
		/* ESP or AH means this was an established IPsec SA */
		linux_audit_conn(&child->sa, LAK_CHILD_DESTROY);
	}

	uninstall_kernel_state(child, DIRECTION_OUTBOUND);
	/* For larval IPsec SAs this may not exist */
	uninstall_kernel_state(child, DIRECTION_INBOUND);
}

void teardown_ipsec_kernel_states(struct child_sa *child)
{
	/* caller snafued with pexpect_child_sa(st) */
	if (pbad(child == NULL)) {
		return;
	}

	switch (child->sa.st_ike_version) {
	case IKEv1:
		if (IS_IPSEC_SA_ESTABLISHED(&child->sa)) {
#if 0
			/* see comments below about multiple calls */
			PEXPECT(logger, c->child.routing == RT_ROUTED_TUNNEL);
#endif
			uninstall_kernel_states(child);
		}
		break;
	case IKEv2:
		if (IS_CHILD_SA_ESTABLISHED(&child->sa)) {
			/*
			 * XXX: There's a race when an SA is replaced
			 * simultaneous to the pluto being shutdown.
			 *
			 * For instance, ikev2-13-ah, this pexpect is
			 * triggered because #2, which was replaced by
			 * #3, tries to tear down the SA.
			 */
#if 0
			PEXPECT(logger, c->child.routing == RT_ROUTED_TUNNEL);
#endif
			uninstall_kernel_states(child);
		} else if (child->sa.st_sa_role == SA_INITIATOR &&
			   child->sa.st_establishing_sa == IPSEC_SA) {
			/*
			 * XXX: so much for dreams of becoming an
			 * established Child SA.
			 *
			 * This seems to be is overkill as just the
			 * outgoing SA needs to be deleted?
			 *
			 * Actually, no.  During acquire the
			 * prospective hold installs both inbound and
			 * outbound kernel policies?
			 */
			uninstall_kernel_states(child);
		}
		break;
	}
}

/*
 * Check if there was traffic on given SA during the last idle_max
 * seconds.  If TRUE, the SA was idle and DPD exchange should be
 * performed.  If FALSE, DPD is not necessary.  We also return TRUE
 * for errors, as they could mean that the SA is broken and needs to
 * be replace anyway.
 *
 * note: this mutates *st by calling get_sa_bundle_info
 *
 * XXX:
 *
 * The use of get_sa_bundle_info() here is likely bogus.  The function
 * returns the SA's add time (PF_KEY v2 documents it as such, xfrm
 * returns the .add_time field so presumably ...) when it is assumed
 * to be returning the idle time.
 *
 * Code most likely needs to track data+call-time and see if traffic
 * flowed since the last call.
 */

bool was_eroute_idle(struct state *st, deltatime_t since_when)
{
	passert(st != NULL);
	struct ipsec_proto_info *first_proto_info =
		(st->st_ah.present ? &st->st_ah :
		 st->st_esp.present ? &st->st_esp :
		 st->st_ipcomp.present ? &st->st_ipcomp :
		 NULL);

	if (!get_ipsec_traffic(st, first_proto_info, DIRECTION_INBOUND)) {
		/* snafu; assume idle!?! */
		return true;
	}
	deltatime_t idle_time = realtimediff(realnow(), first_proto_info->inbound.last_used);
	return deltatime_cmp(idle_time, >=, since_when);
}

static void set_sa_info(struct ipsec_proto_info *p2, uint64_t bytes,
			 uint64_t add_time, bool inbound, deltatime_t *ago)
{
	if (p2->add_time == 0 && add_time != 0) {
		p2->add_time = add_time; /* this should happen exactly once */
	}

	pexpect(p2->add_time == add_time);

	realtime_t now = realnow();

	if (inbound) {
		if (bytes > p2->inbound.bytes) {
			p2->inbound.bytes = bytes;
			p2->inbound.last_used = now;
		}
		if (ago != NULL) {
			*ago = realtimediff(now, p2->inbound.last_used);
		}
	} else {
		if (bytes > p2->outbound.bytes) {
			p2->outbound.bytes = bytes;
			p2->outbound.last_used = now;
		}
		if (ago != NULL)
			*ago = realtimediff(now, p2->outbound.last_used);
	}
}

/*
 * get information about a given SA bundle
 *
 * Note: this mutates *st.
 * Note: this only changes counts in the first SA in the bundle!
 */
bool get_ipsec_traffic(struct state *st,
		       struct ipsec_proto_info *proto_info,
		       enum direction direction)
{
	struct connection *const c = st->st_connection;

	if (!pexpect(proto_info != NULL)) {
		/* pacify coverity */
		return false;
	}

	if (kernel_ops->get_kernel_state == NULL) {
		return false;
	}

	/*
	 * If we're being redirected (using the REDIRECT mechanism),
	 * then use the state's current remote endpoint, and not the
	 * connection's value.
	 *
	 * XXX: why not just use redirect_ip?
	 */
	bool redirected = (!endpoint_address_eq_address(st->st_remote_endpoint, c->remote->host.addr) &&
			   address_is_specified(c->temp_vars.redirect_ip));
	ip_address remote_ip = (redirected ?  endpoint_address(st->st_remote_endpoint) :
				c->remote->host.addr);

	struct ipsec_flow *flow;
	ip_address src, dst;
	switch (direction) {
	case DIRECTION_INBOUND:
		flow = &proto_info->inbound;
		src = remote_ip;
		dst = c->local->host.addr;
		break;
	case DIRECTION_OUTBOUND:
		flow = &proto_info->outbound;
		src = c->local->host.addr;
		dst = remote_ip;
		break;
	default:
		bad_case(direction);
	}

	if (flow->kernel_sa_expired & SA_HARD_EXPIRED) {
		ldbg(st->st_logger,
		     "kernel: %s() expired %s SA SPI "PRI_IPSEC_SPI" get_sa_info()",
		     __func__, enum_name_short(&direction_names, direction),
		     pri_ipsec_spi(flow->spi));
		return true; /* all is well use last known info */
	}

	said_buf sb;
	struct kernel_state sa = {
		.spi = flow->spi,
		.proto = proto_info->protocol,
		.src.address = src,
		.dst.address = dst,
		.story = said_str(dst, proto_info->protocol, flow->spi, &sb),
	};

	ldbg(st->st_logger, "kernel: %s() %s", __func__, sa.story);

	uint64_t bytes = 0;
	uint64_t add_time = 0;
	uint64_t lastused = 0;
	if (!kernel_ops->get_kernel_state(&sa, &bytes, &add_time, &lastused, st->st_logger))
		return false;
	ldbg(st->st_logger, "kernel: %s() bytes=%"PRIu64" add_time=%"PRIu64" lastused=%"PRIu64,
	     __func__, bytes, add_time, lastused);

	proto_info->add_time = add_time;

	/* field has been set? */
	passert(!is_realtime_epoch(flow->last_used));

	if (bytes > flow->bytes) {
		flow->bytes = bytes;
		if (lastused > 0)
			flow->last_used = realtime(lastused);
		else
			flow->last_used = realnow();
	}

	return true;
}

void orphan_holdpass(struct connection *c,
		     struct spd_route *sr,
		     struct logger *logger)
{
	/*
	 * ... UPDATE kernel policy if needed.
	 *
	 * This really causes the name to remain "oe-failing",
	 * we should be able to update only the name of the
	 * shunt.
	 */

	dbg("kernel: installing bare_shunt/failure_shunt");

	/* fudge up parameter list */
	const ip_address *src_address = &sr->local->host->addr;
	const ip_address *dst_address = &sr->remote->host->addr;
	const char *why = "oe-failing";

	/* fudge up replace_bare_shunt() */
	const struct ip_info *afi = address_type(src_address);
	passert(afi == address_type(dst_address));
	const struct ip_protocol *protocol = protocol_from_ipproto(sr->local->client.ipproto);
	/* ports? assumed wide? */
	ip_selector src = selector_from_address_protocol(*src_address, protocol);
	ip_selector dst = selector_from_address_protocol(*dst_address, protocol);

	selector_pair_buf sb;
	dbg("kernel: replace bare shunt %s for %s",
	    str_selector_pair(&src, &dst, &sb), why);

	/*
	 * ??? this comment might be obsolete.
	 *
	 * If the transport protocol is not the wildcard (0),
	 * then we need to look for a host<->host shunt, and
	 * replace that with the shunt spi, and then we add a
	 * %HOLD for what was there before.
	 *
	 * This is at odds with !repl, which should delete
	 * things.
	 *
	 * XXX: does replacing a sec_label kernel policy with
	 * something bare make sense?  Should sec_label be
	 * included?
	 */

	if (install_bare_kernel_policy(src, dst,
				       SHUNT_KIND_FAILURE, c->config->shunt[SHUNT_KIND_FAILURE],
				       logger, HERE)) {
		/*
		 * If the bare shunt exactly matches the template,
		 * then mark the template as needing restoring when
		 * the bare shunt expires.
		 *
		 * XXX: as a quick and dirty hack, assume there's only
		 * one selector.
		 */
		co_serial_t restore;
		struct connection *t = c->clonedfrom;
		if (selector_eq_selector(src, t->local->child.selectors.proposed.list[0]) &&
		    selector_eq_selector(dst, t->remote->child.selectors.proposed.list[0])) {
			restore = t->serialno;
		} else {
			restore = UNSET_CO_SERIAL;
		}

		/*
		 * Change over to new bare eroute ours, peers,
		 * transport_proto are the same.
		 */


		struct bare_shunt *bs =
			add_bare_shunt(&src, &dst,
				       c->config->failure_shunt,
				       restore,
				       why, logger);
		ldbg_bare_shunt(logger, "replace", bs);
	} else {
		llog(RC_LOG, logger,
		     "replace kernel shunt %s failed - deleting from pluto shunt table",
		     str_selector_pair_sensitive(&src, &dst, &sb));
	}

}

static void expire_bare_shunts(struct logger *logger)
{
	dbg("kernel: checking for aged bare shunts from shunt table to expire");
	for (struct bare_shunt **bspp = &bare_shunts; *bspp != NULL; ) {
		struct bare_shunt *bsp = *bspp;
		time_t age = deltasecs(monotimediff(mononow(), bsp->last_activity));

		if (age > deltasecs(pluto_shunt_lifetime)) {
			ldbg_bare_shunt(logger, "expiring old", bsp);
			if (co_serial_is_set(bsp->restore_serialno)) {
				/*
				 * Time to restore the connection's
				 * shunt.  Presumably the bare shunt
				 * was a place holder while things
				 * were given time to rest (back-off).
				 */
				struct connection *c = connection_by_serialno(bsp->restore_serialno);
				if (c != NULL) {
					if (!install_prospective_kernel_policies(c->spd, logger, HERE)) {
						llog(RC_LOG, logger,
						     "trap shunt install failed ");
					}
				}
			} else {
				delete_bare_shunt_kernel_policy(bsp,
								EXPECT_KERNEL_POLICY_OK,
								logger, HERE);
			}
			free_bare_shunt(bspp);
		} else {
			ldbg_bare_shunt(logger, "keeping recent", bsp);
			bspp = &bsp->next;
		}
	}
}

static void delete_bare_shunt_kernel_policies(struct logger *logger)
{
	dbg("kernel: emptying bare shunt table");
	while (bare_shunts != NULL) { /* nothing left */
		const struct bare_shunt *bsp = bare_shunts;
		delete_bare_shunt_kernel_policy(bsp,
						EXPECT_KERNEL_POLICY_OK,
						logger, HERE);
		free_bare_shunt(&bare_shunts); /* also updates BARE_SHUNTS */
	}
}

static void kernel_scan_shunts(struct logger *logger)
{
	expire_bare_shunts(logger);
}

void shutdown_kernel(struct logger *logger)
{
	delete_bare_shunt_kernel_policies(logger);
	kernel_ops->shutdown(logger);
}

void handle_sa_expire(ipsec_spi_t spi, uint8_t protoid, ip_address *dst,
		       bool hard, uint64_t bytes, uint64_t packets, uint64_t add_time)
{
	struct child_sa *child = find_v2_child_sa_by_spi(spi, protoid, dst);

	if (child == NULL) {
		address_buf a;
		dbg("received kernel %s EXPIRE event for IPsec SPI 0x%x, but there is no connection with this SPI and dst %s bytes %" PRIu64 " packets %" PRIu64,
		     hard ? "hard" : "soft",
		     ntohl(spi), str_address(dst, &a), bytes, packets);
		return;
	}

	const struct connection *c = child->sa.st_connection;

	if ((hard && impair.ignore_hard_expire) ||
	    (!hard && impair.ignore_soft_expire)) {
		address_buf a;
		llog_sa(RC_LOG, child,
			"IMPAIR: suppressing a %s EXPIRE event spi 0x%x dst %s bytes %" PRIu64 " packets %" PRIu64,
			hard ? "hard" : "soft", ntohl(spi), str_address(dst, &a),
			bytes, packets);
		return;
	}

	bool rekey = !LIN(POLICY_DONT_REKEY, c->policy);
	bool newest = c->newest_ipsec_sa == child->sa.st_serialno;
	struct state *st =  &child->sa;
	struct ipsec_proto_info *pr = (st->st_esp.present ? &st->st_esp :
				       st->st_ah.present ? &st->st_ah :
				       st->st_ipcomp.present ? &st->st_ipcomp :
				       NULL);

	bool already_softexpired = ((pr->inbound.kernel_sa_expired & SA_SOFT_EXPIRED) ||
				    (pr->outbound.kernel_sa_expired & SA_SOFT_EXPIRED));

	bool already_hardexpired = ((pr->inbound.kernel_sa_expired & SA_HARD_EXPIRED) ||
				    (pr->outbound.kernel_sa_expired & SA_HARD_EXPIRED));

	enum sa_expire_kind expire = hard ? SA_HARD_EXPIRED : SA_SOFT_EXPIRED;

	/*
	 * OUR_SPI was sent by us to our peer, so that our peer can
	 * include it in all inbound IPsec messages.
	 */
	const bool inbound = (pr->inbound.spi == spi);

	llog_sa(RC_LOG, child,
		"received %s EXPIRE for %s SPI "PRI_IPSEC_SPI" bytes %" PRIu64 " packets %" PRIu64 " rekey=%s%s%s%s%s",
		hard ? "hard" : "soft",
		(inbound ? "inbound" : "outbound"), pri_ipsec_spi(spi),
		bytes, packets,
		rekey ?  "yes" : "no",
		already_softexpired ? "; already soft expired" : "",
		already_hardexpired ? "; already hard expired" : "",
		(newest ? "" : "; deleting old SA"),
		(newest && rekey && !already_softexpired && !already_hardexpired) ? "; replacing" : "");

	if ((already_softexpired && expire == SA_SOFT_EXPIRED)  ||
	    (already_hardexpired && expire == SA_HARD_EXPIRED)) {
		dbg("#%lu one of the SA has already expired ignore this %s EXPIRE",
		    child->sa.st_serialno, hard ? "hard" : "soft");
		/*
		 * likely the other direction SA EXPIRED, it triggered a rekey first.
		 * It should be safe to ignore the second one. No need to log.
		 */
	} else if (!already_hardexpired && expire == SA_HARD_EXPIRED) {
		if (inbound) {
			pr->inbound.kernel_sa_expired |= expire;
			set_sa_info(pr, bytes, add_time, true /* inbound */, NULL);
		} else {
			pr->outbound.kernel_sa_expired |= expire;
			set_sa_info(pr, bytes, add_time, false /* outbound */, NULL);
		}
		set_sa_expire_next_event(EVENT_SA_EXPIRE, &child->sa);
	} else if (newest && rekey && !already_hardexpired && !already_softexpired && expire == SA_SOFT_EXPIRED) {
		if (inbound) {
			pr->inbound.kernel_sa_expired |= expire;
			set_sa_info(pr, bytes, add_time, true /* inbound */, NULL);
		} else {
			pr->outbound.kernel_sa_expired |= expire;
			set_sa_info(pr, bytes, add_time, false /* outbound */, NULL);
		}
		set_sa_expire_next_event(EVENT_NULL/*either v2 REKEY or v1 REPLACE*/, &child->sa);
	} else {
		/*
		 * 'if' and multiple 'else if's are using multiple variables.
		 * I may have overlooked some cases. lets break hard on unexpected cases.
		 */
		passert(1); /* lets break! */
	}
}

void jam_kernel_acquire(struct jambuf *buf, const struct kernel_acquire *b)
{
	jam(buf, "initiate on-demand for packet ");
	jam_packet(buf, &b->packet);
	if (!b->by_acquire) {
		jam(buf, " by whack");
	}
	if (b->sec_label.len > 0) {
		jam(buf, " sec_label=");
		jam_sanitized_hunk(buf, b->sec_label);
	}
#if 0
	if (b->state_id > 0) {
		jam(buf, " seq=%u", (unsigned)b->state_id);
	}
	if (b->policy_id > 0) {
		jam(buf, " policy=%u", (unsigned)b->policy_id);
	}
#endif
}
