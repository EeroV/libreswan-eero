/*
 * command interface to Pluto
 *
 * Copyright (C) 1997 Angelos D. Keromytis.
 * Copyright (C) 1998-2003  D. Hugh Redelmeier.
 * Copyright (C) 2004-2008 Michael Richardson <mcr@sandelman.ottawa.on.ca>
 * Copyright (C) 2007-2008 Paul Wouters <paul@xelerance.com>
 * Copyright (C) 2008 Shingo Yamawaki
 * Copyright (C) 2008-2009 David McCullough <david_mccullough@securecomputing.com>
 * Copyright (C) 2010-2019 D. Hugh Redelmeier <hugh@mimosa.com>
 * Copyright (C) 2011 Mika Ilmaranta <ilmis@foobar.fi>
 * Copyright (C) 2012-2020 Paul Wouters <pwouters@redhat.com>
 * Copyright (C) 2012 Philippe Vouters <philippe.vouters@laposte.net>
 * Copyright (C) 2013 David McCullough <ucdevel@gmail.com>
 * Copyright (C) 2013 Matt Rogers <mrogers@redhat.com>
 * Copyright (C) 2013-2018 Antony Antony <antony@phenome.org>
 * Copyright (C) 2017 Sahana Prasad <sahana.prasad07@gmail.com>
 * Copyright (C) 2019 Andrew Cagney <cagney@gnu.org>
 * Copyright (C) 2019 Tuomo Soini <tis@foobar.fi>
 * Copyright (C) 2020 Yulia Kuzovkova <ukuzovkova@gmail.com>
 * Copyright (C) 20212-2022 Paul Wouters <paul.wouters@aiven.io>
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

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <assert.h>
#include <limits.h>	/* for INT_MAX */

#include "lsw_socket.h"

#include "ttodata.h"
#include "lswversion.h"
#include "lswtool.h"
#include "constants.h"
#include "lswlog.h"
#include "whack.h"
#include "ip_address.h"
#include "ip_info.h"
#include "timescale.h"

#include "ipsecconf/confread.h" /* for DEFAULT_UPDOWN */
#include <net/if.h> /* for IFNAMSIZ */
/*
 * Print the 'ipsec --whack help' message
 */
static void help(void)
{
	fprintf(stderr,
		"Usage:\n"
		"\n"
		"all forms: [--rundir <path>] [--ctlsocket <file>] [--label <string>]\n"
		"\n"
		"help: whack [--help] [--version]\n"
		"\n"
		"connection: whack --name <connection_name> \\\n"
		"	--connalias <alias_names> \\\n"
		"	[--ipv4 | --ipv6] [--tunnelipv4 | --tunnelipv6] \\\n"
		"	(--host <ip-address> | --id <identity>) \\\n"
		"	[--ca <distinguished name>] \\\n"
		"	[--ikeport <port-number>] [--srcip <ip-address>] \\\n"
		"	[--vtiip <ip-address>/mask] \\\n"
		"	[--updown <updown>] \\\n"
		"	[--authby <psk | rsasig | rsa | ecdsa | null | eaponly>] \\\n"
		"	[--autheap <none | tls>] \\\n"
		"	[--groups <access control groups>] \\\n"
		"	[--cert <friendly_name> | --ckaid <ckaid>] \\\n"
		"	[--ca <distinguished name>] \\\n"
		"	[--sendca no|issuer|all] [--sendcert yes|always|no|never|ifasked] \\\n"
		"	[--nexthop <ip-address>] \\\n"
		"	[--client <subnet> \\\n"
		"	[--clientprotoport <protocol>/<port>] \\\n"
		"	[--dnskeyondemand] [--updown <updown>] \\\n"
		"	[--psk] | [--rsasig] | [--rsa-sha1] | [--rsa-sha2] | \\\n"
		"		[--rsa-sha2_256] | [--rsa-sha2_384 ] | [--rsa-sha2_512 ] | \\\n"
		"		[ --auth-null] | [--auth-never] \\\n"
		"	[--encrypt] [--authenticate] [--compress] [--sha2-truncbug] \\\n"
		"	[--ms-dh-downgrade] \\\n"
		"	[--overlapip] [--tunnel] [--pfs] \\\n"
		"	[--allow-cert-without-san-id] [--dns-match-id] \\\n"
		"	[--pfsgroup <modp1024 | modp1536 | modp2048 | \\\n"
		"		modp3072 | modp4096 | modp6144 | modp8192 \\\n"
		"		dh22 | dh23 | dh24>] \\\n"
		"	[--ike-lifetime <seconds>] [--ipsec-lifetime <seconds>] \\\n"
		"	[--ipsec-max-bytes <num>] [--ipsec-max-packets <num>] \\\n"
		"	[--rekeymargin <seconds>] [--rekeyfuzz <percentage>] \\\n"
		"	[--retransmit-timeout <seconds>] \\\n"
		"	[--retransmit-interval <msecs>] \\\n"
		"	[--send-redirect] [--redirect-to <ip>] \\\n"
		"	[--accept-redirect] [--accept-redirect-to <ip>] \\\n"
		"	[--keyingtries <count>] \\\n"
		"	[--replay-window <num>] \\\n"
		"	[--esp <esp-algos>] \\\n"
		"	[--remote-peer-type <cisco>] \\\n"
		"	[--mtu <mtu>] \\\n"
		"	[--priority <prio>] [--reqid <reqid>] \\\n"
		"	[--tfc <size>] [--send-no-esp-tfc] \\\n"
		"	[--ikev1 | --ikev2] \\\n"
		"	[--allow-narrowing] \\\n"
		"	[--ikefrag-allow | --ikefrag-force] [--no-ikepad] \\\n"
		"	[--esn ] [--no-esn] [--decap-dscp] [--nopmtudisc] [--mobike] \\\n"
		"	[--tcp <no|yes|fallback>] --tcp-remote-port <port>\\\n"
#ifdef HAVE_NM
		"	[--nm-configured] \\\n"
#endif
#ifdef HAVE_LABELED_IPSEC
		"	[--policylabel <label>] \\\n"
#endif
		"	[--xauthby file|pam|alwaysok] [--xauthfail hard|soft] \\\n"
		"	[--dontrekey] [--aggressive] \\\n"
		"	[--initialcontact] [--cisco-unity] [--fake-strongswan] \\\n"
		"	[--encaps <auto|yes|no>] [--no-nat-keepalive] \\\n"
		"	[--ikev1-natt <both|rfc|drafts>] [--no-nat_keepalive] \\\n"
		"	[--dpddelay <seconds> --dpdtimeout <seconds>] \\\n"
		"	[--dpdaction (clear|hold|restart)] \\\n"
		"	[--xauthserver | --xauthclient] \\\n"
		"	[--modecfgserver | --modecfgclient] [--modecfgpull] \\\n"
		"	[--addresspool <network range>] \\\n"
		"	[--modecfgdns <ip-address, ip-address>  \\\n"
		"	[--modecfgdomains <dns-domain, dns-domain, ..>] \\\n"
		"	[--modecfgbanner <login banner>] \\\n"
		"	[--metric <metric>] \\\n"
		"	[--nflog-group <groupnum>] \\\n"
		"	[--conn-mark <mark/mask>] [--conn-mark-in <mark/mask>] \\\n"
		"	[--conn-mark-out <mark/mask>] \\\n"
		"	[--ipsec-interface <num>] \\\n"
		"	[--vti-iface <iface> ] [--vti-routing] [--vti-shared] \\\n"
		"	[--failnone | --failpass | --faildrop | --failreject] \\\n"
		"	[--negopass ] \\\n"
		"	[--donotrekey ] [--reauth ] \\\n"
		"	[--nic-offload ] \\\n"
		"	--to\n"
		"\n"
		"routing: whack (--route | --unroute) --name <connection_name>\n"
		"\n"
		"initiation: whack (--initiate [--remote-host <ip or hostname>] | --terminate) \\\n"
		"	--name <connection_name> [--asynchronous] \\\n"
		"	[--username <name>] [--xauthpass <pass>]\n"
		"\n"
		"rekey: whack (--rekey-ike | --rekey-ipsec) \\\n"
		"	--name <connection_name> [--asynchronous] \\\n"
		"\n"
		"active redirect: whack [--name <connection_name>] \\\n"
		"	--redirect-to <ip-address(es)> \n"
		"\n"
		"global redirect: whack --global-redirect yes|no|auto\n"
		"	--global-redirect-to <ip-address, dns-domain, ..> \n"
		"\n"
		"opportunistic initiation: whack [--tunnelipv4 | --tunnelipv6] \\\n"
		"	--oppohere <ip-address> --oppothere <ip-address> \\\n"
		"	--opposport <port> --oppodport <port> \\\n"
		"	[--oppoproto <protocol>]\n"
		"\n"
		"delete: whack --delete --name <connection_name>\n"
		"\n"
		"delete: whack --deleteid --name <id>\n"
		"\n"
		"deletestate: whack --deletestate <state_object_number>\n"
		"\n"
		"delete user: whack --deleteuser --name <user_name> \\\n"
		"	[--crash <ip-address>]\n"
		"\n"
		"pubkey: whack --keyid <id> [--addkey] [--pubkeyrsa <key>]\n"
		"\n"
		"debug: whack [--name <connection_name>] \\\n"
		"	[--debug help|none|<class>] \\\n"
		"	[--no-debug <class>] \\\n"
		"	[--impair help|list|none|<behaviour>]  \\\n"
		"	[--no-impair <behaviour>]\n"
		"\n"
		"listen: whack (--listen | --unlisten)\n"
		"\n"
		"socket buffers: whack --ike-socket-bufsize <bufsize>\n"
		"socket errqueue: whack --ike-socket-errqueue-toggle\n"
		"\n"
		"ddos-protection: whack (--ddos-busy | --ddos-unlimited | \\\n"
		"	--ddos-auto)\n"
		"\n"
		"list: whack [--utc] [--checkpubkeys] [--listpubkeys] [--listcerts] \\\n"
		"	[--listcacerts] [--listcrls] [--listpsks] [--listevents] [--listall]\n"
		"\n"
		"purge: whack --purgeocsp\n"
		"\n"
		"reread: whack [--fetchcrls] [--rereadcerts] [--rereadsecrets] [--rereadall]\n"
		"\n"
		"status: whack [--status] | [--briefstatus] | \\\n"
		"       [--addresspoolstatus] | [--connectionstatus] [--fipsstatus] | \\\n"
		"       [--processstatus] | [--shuntstatus] | [--trafficstatus] | \\\n"
		"	[--showstates]\n"
		"\n"
		"statistics: [--globalstatus] | [--clearstats]\n"
		"\n"
		"refresh dns: whack --ddns\n"
		"\n"
#ifdef USE_SECCOMP
		"testing: whack --seccomp-crashtest (CAREFUL!)\n"
		"\n"
#endif
		"shutdown: whack --shutdown [--leave-state]\n"
		"\n"
		"Libreswan %s\n",
		ipsec_version_code());
}

/* --label operand, saved for diagnostics */
static const char *label = NULL;

/* --name operand, saved for diagnostics */
static const char *name = NULL;

static const char *remote_host = NULL;

/*
 * Print a string as a diagnostic, then exit whack unhappily
 *
 * @param mess The error message to print when exiting
 * @return NEVER
 */
static void diagw(const char *mess) NEVER_RETURNS;

static void diagw(const char *mess)
{
	if (mess != NULL) {
		fprintf(stderr, "whack error: ");
		if (label != NULL)
			fprintf(stderr, "%s ", label);
		if (name != NULL)
			fprintf(stderr, "\"%s\" ", name);
		fprintf(stderr, "%s\n", mess);
	}

	exit(RC_WHACK_PROBLEM);
}

/*
 * Conditially calls diag if ugh is set.
 * Prints second arg, if non-NULL, as quoted string
 *
 * @param ugh Error message
 * @param this Optional 2nd part of error message
 * @return void
 */
static void diagq(err_t ugh, const char *this)
{
	if (ugh != NULL) {
		if (this == NULL) {
			diagw(ugh);
		} else {
			char buf[120];	/* arbitrary limit */

			snprintf(buf, sizeof(buf), "%s \"%s\"", ugh, this);
			diagw(buf);
		}
	}
}

/*
 * complex combined operands return one of these enumerated values
 * Note: these become flags in an lset_t.  Since there could be more
 * than lset_t could hold (currently 64), we partition them into:
 * - OPT_* options (most random options)
 * - LST_* options (list various internal data)
 * - DBGOPT_* option (DEBUG options)
 * - END_* options (End description options)
 * - CD_* options (Connection Description options)
 * - CDP_* options (Connection Description Policy bit options)
 */
enum option_enums {

#define OPTS_SEEN_CD LELEM(OPTS_SEEN_CD_IX)
	OPTS_SEEN_CD_IX,	/* magic, indicates an option from the
				 * CD_{FIRST,LAST} or CDP_{FIRST,LAST}
				 * range was seen */
#define OPTS_SEEN_NORMAL LELEM(OPTS_SEEN_NORMAL_IX)
	OPTS_SEEN_NORMAL_IX,	/* magic, indicates an option from the
				 * OPT_{FIRST,LAST}{1,2} range was
				 * seen (which has lots of space) */
#define OPTS_SEEN_LST LELEM(OPTS_SEEN_LST_IX)
	OPTS_SEEN_LST_IX,	/* magic, indicates that an option
				 * from the LST_{FIRST,LAST} range was
				 * seen. */
#define OPTS_SEEN_DBG LELEM(OPTS_SEEN_DBG_IX)
	OPTS_SEEN_DBG_IX,	/* magic, indicates that an option
				 * from the DBGOPT_{FIRST,LAST} range
				 * was seen. */

#   define OPT_FIRST1    OPT_STATUS	/* first normal option, range 1 */

	OPT_STATUS,		/* must be part of first group */
	OPT_SHUTDOWN,		/* must be part of first group */

#   define OPT_LAST1 OPT_SHUTDOWN	/* last "normal" option, range 1 */

#define OPT_FIRST2  OPT_ASYNC	/* first "normal" option, range 2 */

	OPT_ASYNC,

	OPT_RUNDIR,
	OPT_CTLSOCKET,
	OPT_NAME,
	OPT_REMOTE_HOST,
	OPT_CONNALIAS,

	OPT_DELETECRASH,
	OPT_USERNAME,
	OPT_XAUTHPASS,

	OPT_KEYID,
	OPT_ADDKEY,
	OPT_PUBKEYRSA,
	OPT_PUBKEYECDSA,

	OPT_ROUTE,
	OPT_UNROUTE,

	OPT_INITIATE,
	OPT_TERMINATE,
	OPT_DELETE,
	OPT_DELETEID,
	OPT_DELETESTATE,
	OPT_DELETEUSER,
	OPT_LISTEN,
	OPT_UNLISTEN,
	OPT_IKEBUF,
	OPT_IKE_MSGERR,
	OPT_REKEY_IKE,
	OPT_REKEY_IPSEC,

	OPT_ACTIVE_REDIRECT,
	OPT_GLOBAL_REDIRECT,
	OPT_GLOBAL_REDIRECT_TO,

	OPT_DDOS_BUSY,
	OPT_DDOS_UNLIMITED,
	OPT_DDOS_AUTO,

	OPT_DDNS,

	OPT_REREADSECRETS,
	OPT_REREADCRLS,
	OPT_FETCHCRLS,
	OPT_REREADCERTS,
	OPT_REREADALL,

	OPT_PURGEOCSP,

	OPT_GLOBAL_STATUS,
	OPT_CLEAR_STATS,
	OPT_SHUTDOWN_DIRTY,
	OPT_TRAFFIC_STATUS,
	OPT_SHUNT_STATUS,
	OPT_SHOW_STATES,
	OPT_ADDRESSPOOL_STATUS,
	OPT_CONNECTION_STATUS,
	OPT_FIPS_STATUS,
	OPT_BRIEF_STATUS,
	OPT_PROCESS_STATUS,

#ifdef USE_SECCOMP
	OPT_SECCOMP_CRASHTEST,
#endif

	OPT_OPPO_HERE,
	OPT_OPPO_THERE,
	OPT_OPPO_PROTO,
	OPT_OPPO_SPORT,
	OPT_OPPO_DPORT,

#define OPT_LAST2 OPT_OPPO_DPORT	/* last "normal" option, range 2 */

/* List options */

#   define LST_FIRST LST_UTC	/* first list option */
	LST_UTC,
	LST_CHECKPUBKEYS,
	LST_PUBKEYS,
	LST_CERTS,
	LST_CACERTS,
	LST_CRLS,
	LST_PSKS,
	LST_EVENTS,
	LST_ACERTS,
	LST_AACERTS,
	LST_GROUPS,
	LST_CARDS,
	LST_ALL,
#   define LST_LAST LST_ALL	/* last list option */

/* Connection End Description options */

#   define END_FIRST END_HOST	/* first end description */
	END_HOST,
	END_ID,
	END_CERT,
	END_CKAID,
	END_CA,
	END_GROUPS,
	END_IKEPORT,
	END_NEXTHOP,
	END_SUBNET,
	END_CLIENTPROTOPORT,
	END_DNSKEYONDEMAND,
	END_XAUTHNAME,
	END_XAUTHSERVER,
	END_XAUTHCLIENT,
	END_MODECFGCLIENT,
	END_MODECFGSERVER,
	END_ADDRESSPOOL,
	END_SENDCERT,
	END_SOURCEIP,
	END_VTIIP,
	END_AUTHBY,
	END_AUTHEAP,
	END_UPDOWN,

#define END_LAST  END_UPDOWN	/* last end description*/

/* Connection Description options -- segregated */

#   define CD_FIRST CD_TO	/* first connection description */
	CD_TO,

	CD_IKEv1,
	CD_IKEv2,

	CD_MODECFGDNS,
	CD_MODECFGDOMAINS,
	CD_MODECFGBANNER,
	CD_METRIC,
	CD_CONNMTU,
	CD_PRIORITY,
	CD_TFCPAD,
	CD_SEND_TFCPAD,
	CD_REQID,
	CD_NFLOG_GROUP,
	CD_CONN_MARK_BOTH,
	CD_CONN_MARK_IN,
	CD_CONN_MARK_OUT,
	CD_VTI_IFACE,
	CD_VTI_ROUTING,
	CD_VTI_SHARED,
	CD_IPSEC_IFACE,
	CD_TUNNELIPV4,
	CD_TUNNELIPV6,
	CD_CONNIPV4,
	CD_CONNIPV6,

	CD_RETRANSMIT_TIMEOUT,
	CD_RETRANSMIT_INTERVAL,
	CD_IKE_LIFETIME,
	CD_IPSEC_LIFETIME,
	CD_IPSEC_MAX_BYTES,
	CD_IPSEC_MAX_PACKETS,
	CD_REKEYMARGIN,
	CD_RKFUZZ,
	CD_KTRIES,
	CD_REPLAY_WINDOW,
	CD_DPDDELAY,
	CD_DPDTIMEOUT,
	CD_DPDACTION,
	CD_SEND_REDIRECT,
	CD_REDIRECT_TO,
	CD_ACCEPT_REDIRECT,
	CD_ACCEPT_REDIRECT_TO,
	CD_ENCAPS,
	CD_NO_NAT_KEEPALIVE,
	CD_IKEV1_NATT,
	CD_INITIAL_CONTACT,
	CD_CISCO_UNITY,
	CD_FAKE_STRONGSWAN,
	CD_MOBIKE,
	CD_IKE,
	CD_IKE_TCP,
	CD_IKE_TCP_REMOTE_PORT,
	CD_SEND_CA,
	CD_PFSGROUP,
	CD_REMOTEPEERTYPE,
	CD_SHA2_TRUNCBUG,
	CD_NMCONFIGURED,
	CD_LABELED_IPSEC,
	CD_SEC_LABEL,
	CD_XAUTHBY,
	CD_XAUTHFAIL,
	CD_NIC_OFFLOAD,
	CD_ESP,
	CD_INTERMEDIATE,
	CD_INITIATEONTRAFFIC,
#define CD_LAST CD_INITIATEONTRAFFIC	/* last connection description */

/*
 * Proof-of-identity options (just because CD_ was full) that fill in
 * the .authby and .sighash_policy fields.
 */
#define OPT_AUTHBY_FIRST	OPT_AUTHBY_RSASIG
	OPT_AUTHBY_PSK,
	OPT_AUTHBY_AUTH_NEVER,
	OPT_AUTHBY_AUTH_NULL,
	OPT_AUTHBY_RSASIG, /* SHA1 and (for IKEv2) SHA2 */
	OPT_AUTHBY_RSA_SHA1,
	OPT_AUTHBY_RSA_SHA2,
	OPT_AUTHBY_RSA_SHA2_256,
	OPT_AUTHBY_RSA_SHA2_384,
	OPT_AUTHBY_RSA_SHA2_512,
	OPT_AUTHBY_ECDSA, /* no SHA1 support */
	OPT_AUTHBY_ECDSA_SHA2_256,
	OPT_AUTHBY_ECDSA_SHA2_384,
	OPT_AUTHBY_ECDSA_SHA2_512,
#define OPT_AUTHBY_LAST	OPT_AUTHBY_ECDSA_SHA2_512

/*
 * Proof of identity options that set .auth (yes the options are
 * called authby, contradicting config files).
 */


/*
 * Shunt policies
 */

	CDS_NEVER_NEGOTIATE,
	CDS_NEGOTIATION = CDS_NEVER_NEGOTIATE + SHUNT_POLICY_ROOF,
	CDS_FAILURE = CDS_NEGOTIATION + SHUNT_POLICY_ROOF,
	CDS_LAST = CDS_FAILURE + SHUNT_POLICY_ROOF - 1,

/*
 * Policy options
 *
 * Really part of Connection Description but, seemingly, easier to
 * manipulate as separate policy bits.
 *
 * XXX: I'm skeptical.
 */

#define CDP_FIRST	CDP_SINGLETON

	/*
	 * The next range is for single-element policy options.
	 * It covers enum sa_policy_bits values.
	 */
	CDP_SINGLETON,
	/* large gap of unnamed values... */
	CDP_SINGLETON_LAST = CDP_SINGLETON + POLICY_IX_LAST,

#define CDP_LAST	CDP_SINGLETON_LAST

/* === end of correspondence with POLICY_* === */

#   define DBGOPT_FIRST DBGOPT_NONE
	DBGOPT_NONE,
	DBGOPT_ALL,

	DBGOPT_DEBUG,
	DBGOPT_NO_DEBUG,
	DBGOPT_IMPAIR,
	DBGOPT_NO_IMPAIR,

	DBGOPT_LAST = DBGOPT_NO_IMPAIR,

#define	OPTION_ENUMS_LAST	DBGOPT_LAST
#define OPTION_ENUMS_ROOF	(OPTION_ENUMS_LAST+1)
};

/*
 * Carve up space for result from getop_long.
 * Stupidly, the only result is an int.
 * Numeric arg is bit immediately left of basic value.
 *
 */
#define OPTION_OFFSET   256	/* to get out of the way of letter options */
#define NUMERIC_ARG (1 << 11)	/* expect a numeric argument */
#define AUX_SHIFT   12	/* amount to shift for aux information */

int long_index;

static const struct option long_opts[] = {
#   define OO   OPTION_OFFSET
	/* name, has_arg, flag, val */

	{ "help", no_argument, NULL, 'h' },
	{ "version", no_argument, NULL, 'v' },
	{ "label", required_argument, NULL, 'l' },

	{ "rundir", required_argument, NULL, OPT_RUNDIR + OO },
	{ "ctlbase", required_argument, NULL, OPT_RUNDIR + OO }, /* backwards compat */
	{ "ctlsocket", required_argument, NULL, OPT_CTLSOCKET + OO },
	{ "name", required_argument, NULL, OPT_NAME + OO },
	{ "remote-host", required_argument, NULL, OPT_REMOTE_HOST + OO },
	{ "connalias", required_argument, NULL, OPT_CONNALIAS + OO },

	{ "keyid", required_argument, NULL, OPT_KEYID + OO },
	{ "addkey", no_argument, NULL, OPT_ADDKEY + OO },
	{ "pubkeyrsa", required_argument, NULL, OPT_PUBKEYRSA + OO },

	{ "route", no_argument, NULL, OPT_ROUTE + OO },
	{ "ondemand", no_argument, NULL, OPT_ROUTE + OO },	/* alias */
	{ "unroute", no_argument, NULL, OPT_UNROUTE + OO },

	{ "initiate", no_argument, NULL, OPT_INITIATE + OO },
	{ "terminate", no_argument, NULL, OPT_TERMINATE + OO },
	{ "delete", no_argument, NULL, OPT_DELETE + OO },
	{ "deleteid", no_argument, NULL, OPT_DELETEID + OO },
	{ "deletestate", required_argument, NULL, OPT_DELETESTATE + OO + NUMERIC_ARG },
	{ "deleteuser", no_argument, NULL, OPT_DELETEUSER + OO },
	{ "crash", required_argument, NULL, OPT_DELETECRASH + OO },
	{ "listen", no_argument, NULL, OPT_LISTEN + OO },
	{ "unlisten", no_argument, NULL, OPT_UNLISTEN + OO },
	{ "ike-socket-bufsize", required_argument, NULL, OPT_IKEBUF + OO + NUMERIC_ARG},
	{ "ike-socket-errqueue-toggle", no_argument, NULL, OPT_IKE_MSGERR + OO },

	{ "redirect-to", required_argument, NULL, OPT_ACTIVE_REDIRECT + OO },
	{ "global-redirect", required_argument, NULL, OPT_GLOBAL_REDIRECT + OO },
	{ "global-redirect-to", required_argument, NULL, OPT_GLOBAL_REDIRECT_TO + OO },

	{ "ddos-busy", no_argument, NULL, OPT_DDOS_BUSY + OO },
	{ "ddos-unlimited", no_argument, NULL, OPT_DDOS_UNLIMITED + OO },
	{ "ddos-auto", no_argument, NULL, OPT_DDOS_AUTO + OO },

	{ "ddns", no_argument, NULL, OPT_DDNS + OO },

	{ "rereadsecrets", no_argument, NULL, OPT_REREADSECRETS + OO },
	{ "rereadcrls", no_argument, NULL, OPT_REREADCRLS + OO }, /* obsolete */
	{ "rereadcerts", no_argument, NULL, OPT_REREADCERTS + OO },
	{ "fetchcrls", no_argument, NULL, OPT_FETCHCRLS + OO },
	{ "rereadall", no_argument, NULL, OPT_REREADALL + OO },

	{ "purgeocsp", no_argument, NULL, OPT_PURGEOCSP + OO },

	{ "clearstats", no_argument, NULL, OPT_CLEAR_STATS + OO },

	{ "status", no_argument, NULL, OPT_STATUS + OO },
	{ "globalstatus", no_argument, NULL, OPT_GLOBAL_STATUS + OO },
	{ "trafficstatus", no_argument, NULL, OPT_TRAFFIC_STATUS + OO },
	{ "shuntstatus", no_argument, NULL, OPT_SHUNT_STATUS + OO },
	{ "addresspoolstatus", no_argument, NULL, OPT_ADDRESSPOOL_STATUS + OO },
	{ "connectionstatus", no_argument, NULL, OPT_CONNECTION_STATUS + OO },
	{ "fipsstatus", no_argument, NULL, OPT_FIPS_STATUS + OO },
	{ "briefstatus", no_argument, NULL, OPT_BRIEF_STATUS + OO },
	{ "processstatus", no_argument, NULL, OPT_PROCESS_STATUS + OO },
	{ "statestatus", no_argument, NULL, OPT_SHOW_STATES + OO }, /* alias to catch typos */
	{ "showstates", no_argument, NULL, OPT_SHOW_STATES + OO },

#ifdef USE_SECCOMP
	{ "seccomp-crashtest", no_argument, NULL, OPT_SECCOMP_CRASHTEST + OO },
#endif
	{ "shutdown", no_argument, NULL, OPT_SHUTDOWN + OO },
	{ "leave-state", no_argument, NULL, OPT_SHUTDOWN_DIRTY + OO },
	{ "username", required_argument, NULL, OPT_USERNAME + OO },
	{ "xauthuser", required_argument, NULL, OPT_USERNAME + OO }, /* old name */
	{ "xauthname", required_argument, NULL, OPT_USERNAME + OO }, /* old name */
	{ "xauthpass", required_argument, NULL, OPT_XAUTHPASS + OO },

	{ "oppohere", required_argument, NULL, OPT_OPPO_HERE + OO },
	{ "oppothere", required_argument, NULL, OPT_OPPO_THERE + OO },
	{ "oppoproto", required_argument, NULL, OPT_OPPO_PROTO + OO },
	{ "opposport", required_argument, NULL, OPT_OPPO_SPORT + OO },
	{ "oppodport", required_argument, NULL, OPT_OPPO_DPORT + OO },

	{ "asynchronous", no_argument, NULL, OPT_ASYNC + OO },

	{ "rekey-ike", no_argument, NULL, OPT_REKEY_IKE + OO },
	{ "rekey-ipsec", no_argument, NULL, OPT_REKEY_IPSEC + OO },
	/* list options */

	{ "utc", no_argument, NULL, LST_UTC + OO },
	{ "checkpubkeys", no_argument, NULL, LST_CHECKPUBKEYS + OO },
	{ "listpubkeys", no_argument, NULL, LST_PUBKEYS + OO },
	{ "listcerts", no_argument, NULL, LST_CERTS + OO },
	{ "listcacerts", no_argument, NULL, LST_CACERTS + OO },
	{ "listcrls", no_argument, NULL, LST_CRLS + OO },
	{ "listpsks", no_argument, NULL, LST_PSKS + OO },
	{ "listevents", no_argument, NULL, LST_EVENTS + OO },
	{ "listall", no_argument, NULL, LST_ALL + OO },

	/* options for an end description */

	{ "host", required_argument, NULL, END_HOST + OO },
	{ "id", required_argument, NULL, END_ID + OO },
	{ "cert", required_argument, NULL, END_CERT + OO },
	{ "ckaid", required_argument, NULL, END_CKAID + OO },
	{ "ca", required_argument, NULL, END_CA + OO },
	{ "groups", required_argument, NULL, END_GROUPS + OO },
	{ "ikeport", required_argument, NULL, END_IKEPORT + OO + NUMERIC_ARG },
	{ "nexthop", required_argument, NULL, END_NEXTHOP + OO },
	{ "client", required_argument, NULL, END_SUBNET + OO },	/* alias / backward compat */
	{ "subnet", required_argument, NULL, END_SUBNET + OO },
	{ "clientprotoport", required_argument, NULL, END_CLIENTPROTOPORT + OO },
#ifdef USE_DNSSEC
	{ "dnskeyondemand", no_argument, NULL, END_DNSKEYONDEMAND + OO },
#endif
	{ "sourceip",  required_argument, NULL, END_SOURCEIP + OO },
	{ "srcip",  required_argument, NULL, END_SOURCEIP + OO },	/* alias / backwards compat */
	{ "vtiip",  required_argument, NULL, END_VTIIP + OO },
	{ "authby",  required_argument, NULL, END_AUTHBY + OO },
	{ "autheap",  required_argument, NULL, END_AUTHEAP + OO },
	{ "updown", required_argument, NULL, END_UPDOWN + OO },

	/* options for a connection description */

	{ "to", no_argument, NULL, CD_TO + OO },

	/* option for cert rotation */

#define PS(o, p)	{ o, no_argument, NULL, CDP_SINGLETON + POLICY_##p##_IX + OO }
	PS("intermediate", INTERMEDIATE),

	PS("encrypt", ENCRYPT),
	PS("authenticate", AUTHENTICATE),
	PS("compress", COMPRESS),
	PS("overlapip", OVERLAPIP),
	PS("tunnel", TUNNEL),
	{ "tunnelipv4", no_argument, NULL, CD_TUNNELIPV4 + OO },
	{ "tunnelipv6", no_argument, NULL, CD_TUNNELIPV6 + OO },
	PS("pfs", PFS),
	PS("ms-dh-downgrade", MSDH_DOWNGRADE),
	PS("dns-match-id", DNS_MATCH_ID),
	PS("allow-cert-without-san-id", ALLOW_NO_SAN),
	PS("sha2-truncbug", SHA2_TRUNCBUG),
	PS("sha2_truncbug", SHA2_TRUNCBUG), /* backwards compatibility */
	PS("aggressive", AGGRESSIVE),
	PS("aggrmode", AGGRESSIVE), /*  backwards compatibility */

	{ "initiateontraffic", no_argument, NULL, CD_INITIATEONTRAFFIC + OO }, /* obsolete */

	{ "pass", no_argument, NULL, CDS_NEVER_NEGOTIATE + SHUNT_PASS + OO },
	{ "drop", no_argument, NULL, CDS_NEVER_NEGOTIATE + SHUNT_DROP + OO },
	{ "reject", no_argument, NULL, CDS_NEVER_NEGOTIATE + SHUNT_REJECT + OO },

	{ "negopass", no_argument, NULL, CDS_NEGOTIATION + SHUNT_PASS + OO },

	{ "failnone", no_argument, NULL, CDS_FAILURE + SHUNT_NONE + OO },
	{ "failpass", no_argument, NULL, CDS_FAILURE + SHUNT_PASS + OO },
	{ "faildrop", no_argument, NULL, CDS_FAILURE + SHUNT_DROP + OO },
	{ "failreject", no_argument, NULL, CDS_FAILURE + SHUNT_REJECT + OO },

	PS("dontrekey", DONT_REKEY),
	PS("reauth", REAUTH),
	{ "encaps", required_argument, NULL, CD_ENCAPS + OO },
	{ "no-nat_keepalive", no_argument, NULL,  CD_NO_NAT_KEEPALIVE + OO },
	{ "ikev1_natt", required_argument, NULL, CD_IKEV1_NATT + OO },	/* obsolete _ */
	{ "ikev1-natt", required_argument, NULL, CD_IKEV1_NATT + OO },
	{ "initialcontact", no_argument, NULL,  CD_INITIAL_CONTACT + OO },
	{ "cisco_unity", no_argument, NULL, CD_CISCO_UNITY + OO },	/* obsolete _ */
	{ "cisco-unity", no_argument, NULL, CD_CISCO_UNITY + OO },
	{ "fake-strongswan", no_argument, NULL, CD_FAKE_STRONGSWAN + OO },
	PS("mobike", MOBIKE),

	{ "dpddelay", required_argument, NULL, CD_DPDDELAY + OO },
	{ "dpdtimeout", required_argument, NULL, CD_DPDTIMEOUT + OO },
	{ "dpdaction", required_argument, NULL, CD_DPDACTION + OO },
	{ "send-redirect", required_argument, NULL, CD_SEND_REDIRECT + OO },
	{ "redirect-to", required_argument, NULL, CD_REDIRECT_TO + OO },
	{ "accept-redirect", required_argument, NULL, CD_ACCEPT_REDIRECT + OO },
	{ "accept-redirect-to", required_argument, NULL, CD_ACCEPT_REDIRECT_TO + OO },

	{ "xauth", no_argument, NULL, END_XAUTHSERVER + OO },
	{ "xauthserver", no_argument, NULL, END_XAUTHSERVER + OO },
	{ "xauthclient", no_argument, NULL, END_XAUTHCLIENT + OO },
	{ "xauthby", required_argument, NULL, CD_XAUTHBY + OO },
	{ "xauthfail", required_argument, NULL, CD_XAUTHFAIL + OO },
	PS("modecfgpull", MODECFG_PULL),
	{ "modecfgserver", no_argument, NULL, END_MODECFGSERVER + OO },
	{ "modecfgclient", no_argument, NULL, END_MODECFGCLIENT + OO },
	{ "addresspool", required_argument, NULL, END_ADDRESSPOOL + OO },
	{ "modecfgdns", required_argument, NULL, CD_MODECFGDNS + OO },
	{ "modecfgdomains", required_argument, NULL, CD_MODECFGDOMAINS + OO },
	{ "modecfgbanner", required_argument, NULL, CD_MODECFGBANNER + OO },
	{ "modeconfigserver", no_argument, NULL, END_MODECFGSERVER + OO },
	{ "modeconfigclient", no_argument, NULL, END_MODECFGCLIENT + OO },

	{ "metric", required_argument, NULL, CD_METRIC + OO + NUMERIC_ARG },
	{ "mtu", required_argument, NULL, CD_CONNMTU + OO + NUMERIC_ARG },
	{ "priority", required_argument, NULL, CD_PRIORITY + OO + NUMERIC_ARG },
	{ "tfc", required_argument, NULL, CD_TFCPAD + OO + NUMERIC_ARG },
	{ "send-no-esp-tfc", no_argument, NULL, CD_SEND_TFCPAD + OO },
	{ "reqid", required_argument, NULL, CD_REQID + OO + NUMERIC_ARG },
	{ "nflog-group", required_argument, NULL, CD_NFLOG_GROUP + OO + NUMERIC_ARG },
	{ "conn-mark", required_argument, NULL, CD_CONN_MARK_BOTH + OO },
	{ "conn-mark-in", required_argument, NULL, CD_CONN_MARK_IN + OO },
	{ "conn-mark-out", required_argument, NULL, CD_CONN_MARK_OUT + OO },
	{ "vti-iface", required_argument, NULL, CD_VTI_IFACE + OO },
	{ "vti-routing", no_argument, NULL, CD_VTI_ROUTING + OO },
	{ "vti-shared", no_argument, NULL, CD_VTI_SHARED + OO },
	{ "ipsec-interface", required_argument, NULL, CD_IPSEC_IFACE + OO + NUMERIC_ARG },
	{ "sendcert", required_argument, NULL, END_SENDCERT + OO },
	{ "sendca", required_argument, NULL, CD_SEND_CA + OO },
	{ "ipv4", no_argument, NULL, CD_CONNIPV4 + OO },
	{ "ipv6", no_argument, NULL, CD_CONNIPV6 + OO },
	{ "ikelifetime", required_argument, NULL, CD_IKE_LIFETIME + OO },
	{ "ipseclifetime", required_argument, NULL, CD_IPSEC_LIFETIME + OO }, /* backwards compat */
	{ "ipsec-lifetime", required_argument, NULL, CD_IPSEC_LIFETIME + OO },
	{ "ipsec-max-bytes", required_argument, NULL, CD_IPSEC_MAX_BYTES + OO + NUMERIC_ARG},
	{ "ipsec-max-packets", required_argument, NULL, CD_IPSEC_MAX_PACKETS + OO + NUMERIC_ARG},
	{ "retransmit-timeout", required_argument, NULL, CD_RETRANSMIT_TIMEOUT + OO },
	{ "retransmit-interval", required_argument, NULL, CD_RETRANSMIT_INTERVAL + OO },
	{ "rekeymargin", required_argument, NULL, CD_REKEYMARGIN + OO },
	/* OBSOLETE */
	{ "rekeywindow", required_argument, NULL, CD_REKEYMARGIN + OO + NUMERIC_ARG },
	{ "rekeyfuzz", required_argument, NULL, CD_RKFUZZ + OO + NUMERIC_ARG },
	{ "keyingtries", required_argument, NULL, CD_KTRIES + OO + NUMERIC_ARG },
	{ "replay-window", required_argument, NULL, CD_REPLAY_WINDOW + OO },
	{ "ike",    required_argument, NULL, CD_IKE + OO },
	{ "ikealg", required_argument, NULL, CD_IKE + OO },
	{ "pfsgroup", required_argument, NULL, CD_PFSGROUP + OO },
	{ "esp", required_argument, NULL, CD_ESP + OO },
	{ "remote-peer-type", required_argument, NULL, CD_REMOTEPEERTYPE + OO },
	{ "nic-offload", required_argument, NULL, CD_NIC_OFFLOAD + OO},

#define AB(NAME, ENUM) { NAME, no_argument, NULL, OPT_AUTHBY_##ENUM + OO, }
	AB("psk", PSK),
	AB("auth-never", AUTH_NEVER),
	AB("auth-null", AUTH_NULL),
	AB("rsasig", RSASIG),
	AB("ecdsa", ECDSA),
	AB("ecdsa-sha2", ECDSA),
	AB("ecdsa-sha2_256", ECDSA_SHA2_256),
	AB("ecdsa-sha2_384", ECDSA_SHA2_384),
	AB("ecdsa-sha2_512", ECDSA_SHA2_512),
	AB("rsa-sha1", RSA_SHA1),
	AB("rsa-sha2", RSA_SHA2),
	AB("rsa-sha2_256", RSA_SHA2_256),
	AB("rsa-sha2_384", RSA_SHA2_384),
	AB("rsa-sha2_512", RSA_SHA2_512),
#undef AB

	{ "ikev1", no_argument, NULL, CD_IKEv1 + OO },
	{ "ikev1-allow", no_argument, NULL, CD_IKEv1 + OO }, /* obsolete name */
	{ "ikev2", no_argument, NULL, CD_IKEv2 +OO },
	{ "ikev2-allow", no_argument, NULL, CD_IKEv2 +OO }, /* obsolete name */
	{ "ikev2-propose", no_argument, NULL, CD_IKEv2 +OO }, /* obsolete, map onto allow */

	PS("allow-narrowing", IKEV2_ALLOW_NARROWING),
#ifdef AUTH_HAVE_PAM
	PS("ikev2-pam-authorize", IKEV2_PAM_AUTHORIZE),
#endif
	PS("ikefrag-allow", IKE_FRAG_ALLOW),
	PS("ikefrag-force", IKE_FRAG_FORCE),
	PS("no-ikepad", NO_IKEPAD),

	PS("no-esn", ESN_NO),
	PS("esn", ESN_YES),
	PS("decap-dscp", DECAP_DSCP),
	PS("nopmtudisc", NOPMTUDISC),
	PS("ms-dh-downgrade", MSDH_DOWNGRADE),
	PS("dns-match-id", DNS_MATCH_ID),
	PS("ignore-peer-dns", IGNORE_PEER_DNS),
#undef PS

	{ "tcp", required_argument, NULL, CD_IKE_TCP + OO },
	{ "tcp-remote-port", required_argument, NULL, CD_IKE_TCP_REMOTE_PORT + OO + NUMERIC_ARG},

#ifdef HAVE_NM
	{ "nm_configured", no_argument, NULL, CD_NMCONFIGURED + OO }, /* backwards compat */
	{ "nm-configured", no_argument, NULL, CD_NMCONFIGURED + OO },
#endif

	{ "policylabel", required_argument, NULL, CD_SEC_LABEL + OO },

	{ "debug-none", no_argument, NULL, DBGOPT_NONE + OO },
	{ "debug-all", no_argument, NULL, DBGOPT_ALL + OO },
	{ "debug", required_argument, NULL, DBGOPT_DEBUG + OO, },
	{ "no-debug", required_argument, NULL, DBGOPT_NO_DEBUG + OO, },
	{ "impair", required_argument, NULL, DBGOPT_IMPAIR + OO, },
	{ "no-impair", required_argument, NULL, DBGOPT_NO_IMPAIR + OO, },

#    undef DO
#   undef OO
	{ 0, 0, 0, 0 }
};

/*
 * figure out an address family.
 */
struct family {
	const char *used_by;
	const struct ip_info *type;
};

static err_t opt_ttoaddress_num(struct family *family, ip_address *address)
{
	err_t err = ttoaddress_num(shunk1(optarg), family->type, address);
	if (err == NULL && family->type == NULL) {
		family->type = address_type(address);
		family->used_by = long_opts[long_index].name;
	}
	return err;
}

static err_t opt_ttoaddress_dns(struct family *family, ip_address *address)
{
	err_t err = ttoaddress_dns(shunk1(optarg), family->type, address);
	if (err == NULL && family->type == NULL) {
		family->type = address_type(address);
		family->used_by = long_opts[long_index].name;
	}
	return err;
}

static void opt_to_address(struct family *family, ip_address *address)
{
	diagq(opt_ttoaddress_dns(family, address), optarg);
}

static void opt_to_cidr(struct family *family, ip_cidr *cidr)
{
	diagq(ttocidr_num(shunk1(optarg), family->type, cidr), optarg);
	if (family->type == NULL) {
		family->type = cidr_type(cidr);
		family->used_by = long_opts[long_index].name;
	}
}

static const struct ip_info *get_address_family(struct family *family)
{
	if (family->type == NULL) {
		family->type = &ipv4_info;
		family->used_by = long_opts[long_index].name;
	}
	return family->type;
}

static ip_address get_address_any(struct family *family)
{
	return get_address_family(family)->address.unspec;
}

struct sockaddr_un ctl_addr = {
	.sun_family = AF_UNIX,
	.sun_path  = DEFAULT_CTL_SOCKET,
#ifdef USE_SOCKADDR_LEN
	.sun_len = sizeof(struct sockaddr_un),
#endif
};

static void optarg_to_deltatime(deltatime_t *deltatime, const struct timescale *timescale)
{
	diag_t diag = ttodeltatime(optarg, deltatime, timescale);
	if (diag != NULL) {
		diagq(str_diag(diag), optarg);
	}
}

static void optarg_to_uintmax(uintmax_t *val)
{
	err_t err = shunk_to_uintmax(shunk1(optarg), NULL, /*base*/0, val);
	if (err != NULL) {
		diagq(err, optarg);
	}
}

/* ??? there seems to be no consequence for invalid life_time. */
static void check_max_lifetime(deltatime_t life, time_t raw_limit,
			       const char *which,
			       const struct whack_message *msg)
{
	deltatime_t limit = deltatime(raw_limit);
	deltatime_t mint = deltatime_scale(msg->sa_rekey_margin, 100 + msg->sa_rekey_fuzz/*percent*/, 100);

	if (deltatime_cmp(limit, <, life)) {
		char buf[200];	/* arbitrary limit */

		snprintf(buf, sizeof(buf),
			 "%s [%ld seconds] must be less than %ld seconds",
			 which,
			 (long)deltasecs(life),
			 (long)deltasecs(limit));
		diagw(buf);
	}
	if ((msg->policy & POLICY_DONT_REKEY) == LEMPTY && !deltatime_cmp(mint, <, life)) {
		char buf[200];	/* arbitrary limit */

		snprintf(buf, sizeof(buf),
			 "%s [%ld] must be greater than rekeymargin*(100+rekeyfuzz)/100 [%ld*(100+%lu)/100 = %ld]",
			 which,
			 (long)deltasecs(life),
			 (long)deltasecs(msg->sa_rekey_margin),
			 msg->sa_rekey_fuzz,
			 (long)deltasecs(mint));
		diagw(buf);
	}
}

static void send_reply(int sock, char *buf, ssize_t len)
{
	/* send the secret to pluto */
	if (write(sock, buf, len) != len) {
		int e = errno;

		fprintf(stderr, "whack: write() failed (%d %s)\n", e,
			strerror(e));
		exit(RC_WHACK_PROBLEM);
	}
}

/* This is a hack for initiating ISAKMP exchanges. */

int main(int argc, char **argv)
{
	struct logger *logger = tool_init_log(argv[0]);

	struct whack_message msg;
	struct whackpacker wp;
	char esp_buf[256];	/* uses snprintf */
	bool seen[OPTION_ENUMS_ROOF] = {0};
	lset_t opts_seen = LEMPTY;
	lset_t end_seen = LEMPTY;

	char xauthusername[MAX_XAUTH_USERNAME_LEN];
	char xauthpass[XAUTH_MAX_PASS_LENGTH];
	int usernamelen = 0;
	int xauthpasslen = 0;
	bool gotusername = false, gotxauthpass = false;
	const char *ugh;
	bool ignore_errors = false;

	/* check division of numbering space */
	assert(OPTION_OFFSET + OPTION_ENUMS_LAST < NUMERIC_ARG);
	assert(OPT_LAST1 - OPT_FIRST1 < LELEM_ROOF);
	assert(END_LAST - END_FIRST < LELEM_ROOF);

	zero(&msg);	/* ??? pointer fields might not be NULLed */

	clear_end("left", &msg.left);
	clear_end("right", &msg.right);
	struct whack_end *end = &msg.left;

	struct family host_family = { 0, };
	struct family child_family = { 0, };

	msg.name = NULL;
	msg.remote_host = NULL;
	msg.dnshostname = NULL;

	msg.keyid = NULL;
	msg.keyval.ptr = NULL;
	msg.esp = NULL;
	msg.ike = NULL;
	msg.pfsgroup = NULL;

	msg.remotepeertype = NON_CISCO;
	msg.nat_keepalive = true;

	/* Network Manager support */
#ifdef HAVE_NM
	msg.nmconfigured = false;
#endif

	msg.xauthby = XAUTHBY_FILE;
	msg.xauthfail = XAUTHFAIL_HARD;
	msg.modecfg_domains = NULL;
	msg.modecfg_dns = NULL;
	msg.modecfg_banner = NULL;

	msg.nic_offload = yna_auto;
	msg.sa_ipsec_max_bytes = IPSEC_SA_MAX_OPERATIONS; /* max uint_64_t */
	msg.sa_ipsec_max_packets = IPSEC_SA_MAX_OPERATIONS; /* max uint_64_t */
	msg.sa_ike_max_lifetime = deltatime(IKE_SA_LIFETIME_DEFAULT);
	msg.sa_ipsec_max_lifetime = deltatime(IPSEC_SA_LIFETIME_DEFAULT);
	msg.sa_rekey_margin = deltatime(SA_REPLACEMENT_MARGIN_DEFAULT);
	msg.sa_rekey_fuzz = SA_REPLACEMENT_FUZZ_DEFAULT;
	msg.sa_keying_tries = SA_REPLACEMENT_RETRIES_DEFAULT;
	/* whack cannot access kernel_ops->replay_window */
	msg.sa_replay_window = IPSEC_SA_DEFAULT_REPLAY_WINDOW;
	msg.retransmit_timeout = deltatime(RETRANSMIT_TIMEOUT_DEFAULT);
	msg.retransmit_interval = deltatime_ms(RETRANSMIT_INTERVAL_DEFAULT_MS);

	msg.active_redirect_dests = NULL;

	msg.host_afi = NULL;
	msg.child_afi = NULL;

	msg.right.updown = DEFAULT_UPDOWN;
	msg.left.updown = DEFAULT_UPDOWN;

	msg.iketcp = IKE_TCP_NO;
	msg.remote_tcpport = NAT_IKE_UDP_PORT;

	msg.xfrm_if_id = UINT32_MAX;

	/* set defaults to ICMP PING request */
	msg.oppo.ipproto = IPPROTO_ICMP;
	msg.oppo.local.port = ip_hport(8);
	msg.oppo.remote.port = ip_hport(0);

	msg.dpd_action = DPD_ACTION_UNSET;

	for (;;) {
		/* numeric argument for some flags */
		uintmax_t opt_whole = 0;

		/*
		 * Note: we don't like the way short options get parsed
		 * by getopt_long, so we simply pass an empty string as
		 * the list.  It could be "hp:d:c:o:eatfs" "NARXPECK".
		 */
		int c = getopt_long(argc, argv, "", long_opts, &long_index)
			- OPTION_OFFSET;
		int aux = 0;

		/* decode a numeric argument, if expected */
		if (0 <= c) {
			if (c & NUMERIC_ARG) {
				c -= NUMERIC_ARG;
				optarg_to_uintmax(&opt_whole);
			}
			if (c >= (1 << AUX_SHIFT)) {
				aux = c >> AUX_SHIFT;
				c -= aux << AUX_SHIFT;
			}
		}

		/*
		 * per-class option processing
		 *
		 * Mostly detection of repeated flags.
		 */
		if (OPT_FIRST1 <= c && c <= OPT_LAST1) {
			/*
			 * OPT_* options get added to "opts_seen" and
			 * "seen[]".  Any repeated options are
			 * rejected.
			 */
			if (seen[c])
				diagq("duplicated flag",
				      long_opts[long_index].name);
			opts_seen |= LELEM(c);
			seen[c] = true;
			opts_seen |= OPTS_SEEN_NORMAL;
		} else if (OPT_FIRST2 <= c && c <= OPT_LAST2) {
			/*
			 * OPT_* options in the above range get added
			 * to "seen[]".  Any repeated options are
			 * rejected.  The marker OPTS_SEEN_NORMAL is
			 * also added to "opts_seen".
			 */
			if (seen[c]) {
				diagq("duplicated flag",
				      long_opts[long_index].name);
			}
			seen[c] = true;
			opts_seen |= OPTS_SEEN_NORMAL;
		} else if (LST_FIRST <= c && c <= LST_LAST) {
			/*
			 * LST_* options get added to seen[].
			 * Repeated options are rejected.  The marker
			 * OPT_LST_SEEN is also added to "opts_seen".
			 */
			if (seen[c]) {
				diagq("duplicated flag",
				      long_opts[long_index].name);
			}
			seen[c] = true;
			opts_seen |= OPTS_SEEN_LST;
		} else if (DBGOPT_FIRST <= c && c <= DBGOPT_LAST) {
			/*
			 * DBGOPT_* options are treated separately.
			 * For instance, repeats are alloowed.
			 */
#if 0
			seen[c] = true;
#endif
			opts_seen |= OPTS_SEEN_DBG;
		} else if (END_FIRST <= c && c <= END_LAST) {
			/*
			 * END_* options are added to end_seen.
			 * Reject repeated options for the current end
			 * (code below, when parsing --to, will clear
			 * END_SEEN so that the second end can
			 * duplicate options from the first end).  The
			 * marker OPTS_SEEN_CD is also added to
			 * "opts_seen".
			 */
			lset_t f = LELEM(c - END_FIRST);

			if (end_seen & f)
				diagq("duplicated flag",
				      long_opts[long_index].name);
			end_seen |= f;
			opts_seen |= OPTS_SEEN_CD;
		} else if (CD_FIRST <= c && c <= CD_LAST) {
			/*
			 * CD_* options are added to seen[].  Repeated
			 * options are rejected.  The marker
			 * OPTS_SEEN_CD is also added to "opts_seen".
			 */
			if (seen[c])
				diagq("duplicated flag",
				      long_opts[long_index].name);
			seen[c] = true;
			opts_seen |= OPTS_SEEN_CD;
		} else if (CDP_FIRST <= c && c <= CDP_LAST) {
			/*
			 * CDP_* options are added to seen[].
			 * Repeated options are rejected.  The marker
			 * OPTS_SEEN_CD is also added to "opts_seen".
			 */
			if (seen[c])
				diagq("duplicated flag",
				      long_opts[long_index].name);
			seen[c] = true;
			opts_seen |= OPTS_SEEN_CD;
		} else if (OPT_AUTHBY_FIRST <= c && c <= OPT_AUTHBY_LAST) {
			/*
 			 * ALGO options all translate into two lset's
 			 * msg.policy and msg.sighash.  Don't allow
 			 * duplicates(!?!).
 			 */
			if (seen[c]) {
				diagq("duplicated flag",
				      long_opts[long_index].name);
			}
			seen[c] = true;
			opts_seen |= OPTS_SEEN_CD;
		}

		/*
		 * Note: "break"ing from switch terminates loop.
		 * most cases should end with "continue".
		 */
		switch (c) {
		case EOF - OPTION_OFFSET:	/* end of flags */
			break;

		case 0 - OPTION_OFFSET:	/* long option already handled */
			continue;

		/* diagnostic already printed by getopt_long */
		case ':' - OPTION_OFFSET:
		/* diagnostic already printed by getopt_long */
		case '?' - OPTION_OFFSET:
			/* print no additional diagnostic, but exit sadly */
			diagw(NULL);
			/* not actually reached */
			break;

		case 'h' - OPTION_OFFSET:	/* --help */
			help();
			/* GNU coding standards say to stop here */
			return 0;

		case 'v' - OPTION_OFFSET:	/* --version */
		{
			printf("%s\n", ipsec_version_string());
		}
			/* GNU coding standards say to stop here */
			return 0;

		case 'l' - OPTION_OFFSET:	/* --label <string> */
			label = optarg;	/* remember for diagnostics */
			continue;

		/* the rest of the options combine in complex ways */

		case OPT_RUNDIR:	/* --rundir <dir> */
			if (snprintf(ctl_addr.sun_path,
				     sizeof(ctl_addr.sun_path),
				     "%s/pluto.ctl", optarg) == -1)
				diagw("Invalid rundir for sun_addr");

			continue;

		case OPT_CTLSOCKET:	/* --ctlsocket <file> */
			if (snprintf(ctl_addr.sun_path,
				     sizeof(ctl_addr.sun_path),
				     "%s", optarg) == -1)
				diagw("Invalid ctlsocket for sun_addr");

			continue;

		case OPT_NAME:	/* --name <connection-name> */
			name = optarg;
			msg.name = optarg;
			continue;

		case OPT_REMOTE_HOST:	/* --remote-host <ip or hostname> */
			remote_host = optarg;
			msg.remote_host = optarg;
			continue;

		case OPT_KEYID:	/* --keyid <identity> */
			msg.whack_key = true;
			msg.keyid = optarg;	/* decoded by Pluto */
			continue;

		case OPT_IKEBUF:	/* --ike-socket-bufsize <bufsize> */
			if (opt_whole < 1500) {
				diagw("Ignoring extremely unwise IKE buffer size choice");
			} else {
				msg.ike_buf_size = opt_whole;
				msg.whack_listen = true;
			}
			continue;

		case OPT_IKE_MSGERR:	/* --ike-socket-errqueue-toggle */
			msg.ike_sock_err_toggle = true;
			msg.whack_listen = true;
			continue;

		case OPT_ADDKEY:	/* --addkey */
			msg.whack_addkey = true;
			continue;

		case OPT_PUBKEYRSA:	/* --pubkeyrsa <key> */
			if (msg.keyval.ptr != NULL)
				diagq("only one RSA public-key allowed", optarg);

			/* let msg.keyval leak */
			ugh = ttochunk(shunk1(optarg), 0, &msg.keyval);
			if (ugh != NULL) {
				/* perhaps enough space */
				char ugh_space[80];

				snprintf(ugh_space, sizeof(ugh_space),
					 "RSA public-key data malformed (%s)",
					 ugh);
				diagq(ugh_space, optarg);
			}
			msg.pubkey_alg = IPSECKEY_ALGORITHM_RSA;
			continue;

		case OPT_PUBKEYECDSA:	/* --pubkeyecdsa <key> */
			if (msg.keyval.ptr != NULL)
				diagq("only one ECDSA public-key allowed", optarg);

			/* let msg.keyval leak */
			ugh = ttochunk(shunk1(optarg), 0, &msg.keyval);
			if (ugh != NULL) {
				/* perhaps enough space */
				char ugh_space[80];

				snprintf(ugh_space, sizeof(ugh_space),
					 "ECDSA public-key data malformed (%s)",
					 ugh);
				diagq(ugh_space, optarg);
			}
			msg.pubkey_alg = IPSECKEY_ALGORITHM_ECDSA;
			continue;

		case OPT_ROUTE:	/* --route */
			msg.whack_route = true;
			continue;

		case OPT_UNROUTE:	/* --unroute */
			msg.whack_unroute = true;
			continue;

		case OPT_INITIATE:	/* --initiate */
			msg.whack_initiate = true;
			continue;

		case OPT_TERMINATE:	/* --terminate */
			msg.whack_terminate = true;
			continue;

		case OPT_REKEY_IKE: /* --rekey-ike */
			msg.whack_rekey_ike = true;
			continue;

		case OPT_REKEY_IPSEC: /* --rekey-ipsec */
			msg.whack_rekey_ipsec = true;
			continue;

		case OPT_DELETE:	/* --delete */
			msg.whack_delete = true;
			continue;

		case OPT_DELETEID: /* --deleteid --name <id> */
			msg.whack_deleteid = true;
			continue;

		case OPT_DELETESTATE: /* --deletestate <state_object_number> */
			msg.whack_deletestate = true;
			msg.whack_deletestateno = opt_whole;
			continue;

		case OPT_DELETECRASH:	/* --crash <ip-address> */
			msg.whack_crash = true;
			opt_to_address(&host_family, &msg.whack_crash_peer);
			if (!address_is_specified(msg.whack_crash_peer)) {
				/* either :: or 0.0.0.0; unset rejected */
				address_buf ab;
				diagq("invalid --crash <address>",
				      str_address(&msg.whack_crash_peer, &ab));
			}
			continue;

		/* --deleteuser --name <xauthusername> */
		case OPT_DELETEUSER:
			msg.whack_deleteuser = true;
			continue;

		case OPT_ACTIVE_REDIRECT:	/* --redirect-to */
			msg.active_redirect_dests = strdup(optarg);
			continue;

		case OPT_GLOBAL_REDIRECT:	/* --global-redirect */
			if (streq(optarg, "yes")) {
				msg.global_redirect = GLOBAL_REDIRECT_YES;
			} else if (streq(optarg, "no")) {
				msg.global_redirect = GLOBAL_REDIRECT_NO;
			} else if (streq(optarg, "auto")) {
				msg.global_redirect = GLOBAL_REDIRECT_AUTO;
			} else {
				diagw("invalid option argument for --global-redirect (allowed arguments: yes, no, auto)");
			}
			continue;

		case OPT_GLOBAL_REDIRECT_TO:	/* --global-redirect-to */
			if (!strlen(optarg)) {
				msg.global_redirect_to = strdup("<none>");
			} else {
				msg.global_redirect_to = strdup(optarg);
			}
			continue;

		case OPT_DDOS_BUSY:	/* --ddos-busy */
			msg.whack_ddos = DDOS_FORCE_BUSY;
			continue;

		case OPT_DDOS_UNLIMITED:	/* --ddos-unlimited */
			msg.whack_ddos = DDOS_FORCE_UNLIMITED;
			continue;

		case OPT_DDOS_AUTO:	/* --ddos-auto */
			msg.whack_ddos = DDOS_AUTO;
			continue;

		case OPT_DDNS:	/* --ddns */
			msg.whack_ddns = true;
			continue;

		case OPT_LISTEN:	/* --listen */
			msg.whack_listen = true;
			continue;

		case OPT_UNLISTEN:	/* --unlisten */
			msg.whack_unlisten = true;
			continue;

		case OPT_REREADSECRETS:	/* --rereadsecrets */
			msg.whack_rereadsecrets = true;
			continue;
		case OPT_REREADCERTS:	/* --rereadcerts */
			msg.whack_rereadcerts = true;
			continue;
		case OPT_FETCHCRLS:	/* --fetchcrls */
			msg.whack_fetchcrls = true;
			continue;
		case OPT_REREADALL:	/* --rereadall */
			msg.whack_rereadsecrets = true;
			msg.whack_rereadcerts = true;
			msg.whack_fetchcrls = true;
			continue;
		case OPT_REREADCRLS:	/* --rereadcrls */
			fprintf(stderr, "whack warning: rereadcrls command obsoleted did you mean ipsec whack --fetchcrls\n");
			continue;

		case OPT_PURGEOCSP:	/* --purgeocsp */
			msg.whack_purgeocsp = true;
			continue;

		case OPT_STATUS:	/* --status */
			msg.whack_status = true;
			ignore_errors = true;
			continue;

		case OPT_GLOBAL_STATUS:	/* --global-status */
			msg.whack_global_status = true;
			ignore_errors = true;
			continue;

		case OPT_CLEAR_STATS:	/* --clearstats */
			msg.whack_clear_stats = true;
			continue;

		case OPT_TRAFFIC_STATUS:	/* --trafficstatus */
			msg.whack_traffic_status = true;
			ignore_errors = true;
			continue;

		case OPT_SHUNT_STATUS:	/* --shuntstatus */
			msg.whack_shunt_status = true;
			ignore_errors = true;
			continue;

		case OPT_ADDRESSPOOL_STATUS:	/* --addresspoolstatus */
			msg.whack_addresspool_status = true;
			ignore_errors = true;
			continue;

		case OPT_CONNECTION_STATUS:	/* --connectionstatus */
			msg.whack_connection_status = true;
			ignore_errors = true;
			continue;

		case OPT_FIPS_STATUS:	/* --fipsstatus */
			msg.whack_fips_status = true;
			ignore_errors = true;
			continue;

		case OPT_BRIEF_STATUS:	/* --briefstatus */
			msg.whack_brief_status = true;
			ignore_errors = true;
			continue;

		case OPT_PROCESS_STATUS:	/* --processstatus */
			msg.whack_process_status = true;
			ignore_errors = true;
			continue;

		case OPT_SHOW_STATES:	/* --showstates */
			msg.whack_show_states = true;
			ignore_errors = true;
			continue;
#ifdef USE_SECCOMP
		case OPT_SECCOMP_CRASHTEST:	/* --seccomp-crashtest */
			msg.whack_seccomp_crashtest = true;
			continue;
#endif

		case OPT_SHUTDOWN:	/* --shutdown */
			msg.whack_shutdown = true;
			continue;

		case OPT_SHUTDOWN_DIRTY:	/* --leave-state */
			msg.whack_leave_state = true;
			continue;

		case OPT_OPPO_HERE:	/* --oppohere <ip-address> */
			opt_to_address(&child_family, &msg.oppo.local.address);
			if (!address_is_specified(msg.oppo.local.address)) {
				/* either :: or 0.0.0.0; unset rejected */
				address_buf ab;
				diagq("invalid --opphere <address>",
				      str_address(&msg.oppo.local.address, &ab));
			}
			continue;

		case OPT_OPPO_THERE:	/* --oppothere <ip-address> */
			opt_to_address(&child_family, &msg.oppo.remote.address);
			if (!address_is_specified(msg.oppo.remote.address)) {
				/* either :: or 0.0.0.0; unset rejected */
				address_buf ab;
				diagq("invalid --oppothere <address>",
				      str_address(&msg.oppo.remote.address, &ab));
			}
			continue;

		case OPT_OPPO_PROTO:	/* --oppoproto <protocol> */
			msg.oppo.ipproto = strtol(optarg, NULL, 0);
			continue;

		case OPT_OPPO_SPORT:	/* --opposport <port> */
			msg.oppo.local.port = ip_hport(strtol(optarg, NULL, 0));
			continue;

		case OPT_OPPO_DPORT:	/* --oppodport <port> */
			msg.oppo.remote.port = ip_hport(strtol(optarg, NULL, 0));
			continue;

		case OPT_ASYNC:	/* --asynchronous */
			msg.whack_async = true;
			continue;

		/* List options */

		case LST_UTC:	/* --utc */
			msg.whack_utc = true;
			continue;

		case LST_CERTS:	/* --listcerts */
		case LST_CACERTS:	/* --listcacerts */
		case LST_CRLS:	/* --listcrls */
		case LST_PSKS:	/* --listpsks */
		case LST_EVENTS:	/* --listevents */
			msg.whack_list |= LELEM(c - LST_PUBKEYS);
			ignore_errors = true;
			continue;

		case LST_PUBKEYS:	/* --listpubkeys */
			msg.whack_listpubkeys = true;
			ignore_errors = true;
			continue;

		case LST_CHECKPUBKEYS:	/* --checkpubkeys */
			msg.whack_checkpubkeys = true;
			ignore_errors = true;
			continue;

		case LST_ALL:	/* --listall */
			msg.whack_list = LIST_ALL;
			msg.whack_listpubkeys = true;
			ignore_errors = true;
			continue;

		/* Connection Description options */

		case END_HOST:	/* --host <ip-address> */
		{
			lset_t new_policy;
			if (streq(optarg, "%any")) {
				new_policy = LEMPTY;
				end->host_addr = get_address_any(&host_family);
			} else if (streq(optarg, "%opportunistic")) {
				/* always use tunnel mode; mark as opportunistic */
				new_policy = POLICY_TUNNEL | POLICY_OPPORTUNISTIC;
				end->host_addr = get_address_any(&host_family);
			} else if (streq(optarg, "%group")) {
				/* always use tunnel mode; mark as group */
				new_policy = POLICY_TUNNEL;
				msg.is_connection_group = true;
				end->host_addr = get_address_any(&host_family);
			} else if (streq(optarg, "%opportunisticgroup")) {
				/* always use tunnel mode; mark as opportunistic */
				new_policy = POLICY_TUNNEL | POLICY_OPPORTUNISTIC;
				msg.is_connection_group = true;
				end->host_addr = get_address_any(&host_family);
			} else if (msg.left.id != NULL && !streq(optarg, "%null")) {
				new_policy = LEMPTY;
				if (opt_ttoaddress_num(&host_family, &end->host_addr) == NULL) {
					/*
					 * we have a proper numeric IP
					 * address.
					 */
				} else {
					/*
					 * We assume that we have a DNS name.
					 * This logic matches confread.c.
					 * ??? it would be kind to check
					 * the syntax.
					 */
					msg.dnshostname = optarg;
					opt_ttoaddress_dns(&host_family, &end->host_addr);
					/*
					 * we don't fail here.  pluto
					 * will re-check the DNS later
					 */
				}
			} else {
				new_policy = LEMPTY;
				opt_to_address(&host_family, &end->host_addr);
			}

			msg.policy |= new_policy;

			if (msg.is_connection_group) {
				/*
				 * client subnet must not be specified by
				 * user: it will come from the group's file.
				 */
				if (LHAS(end_seen, END_SUBNET - END_FIRST))
					diagw("--host %group clashes with --client");

				end_seen |= LELEM(END_SUBNET - END_FIRST);
			}
			if (new_policy & POLICY_OPPORTUNISTIC)
				end->key_from_DNS_on_demand = true;
			continue;
		}

		case END_ID:	/* --id <identity> */
			end->id = optarg;	/* decoded by Pluto */
			continue;

		case END_SENDCERT:	/* --sendcert */
			if (streq(optarg, "yes") || streq(optarg, "always")) {
				end->sendcert = CERT_ALWAYSSEND;
			} else if (streq(optarg,
					 "no") || streq(optarg, "never")) {
				end->sendcert = CERT_NEVERSEND;
			} else if (streq(optarg, "ifasked")) {
				end->sendcert = CERT_SENDIFASKED;
			} else {
				diagq("whack sendcert value is not legal",
				      optarg);
				continue;
			}
			continue;

		case END_CERT:	/* --cert <path> */
			if (end->ckaid != NULL)
				diagw("only one --cert <nickname> or --ckaid <ckaid> allowed");
			end->cert = optarg;	/* decoded by Pluto */
			continue;

		case END_CKAID:	/* --ckaid <ckaid> */
			if (end->cert != NULL)
				diagw("only one --cert <nickname> or --ckaid <ckaid> allowed");
			/* try parsing it; the error isn't the most specific */
			const char *ugh = ttodata(optarg, 0, 16, NULL, 0, NULL);
			diagq(ugh, optarg);
			end->ckaid = optarg;	/* decoded by Pluto */
			continue;

		case END_CA:	/* --ca <distinguished name> */
			end->ca = optarg;	/* decoded by Pluto */
			continue;

		case END_GROUPS:	/* --groups <access control groups> */
			end->groups = optarg;	/* decoded by Pluto */
			continue;

		case END_IKEPORT:	/* --ikeport <port-number> */
			if (opt_whole <= 0 || opt_whole >= 0x10000) {
				diagq("<port-number> must be a number between 1 and 65535",
					optarg);
			}
			end->host_ikeport = opt_whole;
			continue;

		case END_NEXTHOP:	/* --nexthop <ip-address> */
			if (streq(optarg, "%direct")) {
				end->host_nexthop = get_address_any(&host_family);
			} else {
				opt_to_address(&host_family, &end->host_nexthop);
			}
			continue;

		case END_SOURCEIP:	/* --sourceip <ip-address> */
			end->sourceip = optarg;
			continue;

		case END_VTIIP:	/* --vtiip <ip-address/mask> */
			opt_to_cidr(&child_family, &end->host_vtiip);
			continue;

		/*
		 * --authby secret | rsasig | rsa | ecdsa | null | eaponly
		 *  Note: auth-never cannot be asymmetrical
		 */
		case END_AUTHBY:
			if (streq(optarg, "psk"))
				end->auth = AUTH_PSK;
			else if (streq(optarg, "null"))
				end->auth = AUTH_NULL;
			else if (streq(optarg, "rsasig") || streq(optarg, "rsa"))
				end->auth = AUTH_RSASIG;
			else if (streq(optarg, "ecdsa"))
				end->auth = AUTH_ECDSA;
			else if (streq(optarg, "eaponly"))
				end->auth = AUTH_EAPONLY;
			else
				diagw("authby option is not one of psk, ecdsa, rsasig, rsa, null or eaponly");
			continue;

		case END_AUTHEAP:
			if (streq(optarg, "tls"))
				end->eap = IKE_EAP_TLS;
			else if (streq(optarg, "none"))
				end->eap = IKE_EAP_NONE;
			else diagw("--autheap option is not one of none, tls");
			continue;

		case END_SUBNET: /* --subnet <subnet> | --client <subnet> */
			if (startswith(optarg, "vhost:") ||
			    startswith(optarg, "vnet:")) {
				end->virt = optarg;
			} else {
				end->subnet = optarg;	/* decoded by Pluto */
			}
			msg.policy |= POLICY_TUNNEL;	/* client => tunnel */
			continue;

		/* --clientprotoport <protocol>/<port> */
		case END_CLIENTPROTOPORT:
			diagq(ttoprotoport(optarg, &end->protoport),
				optarg);
			continue;

		case END_DNSKEYONDEMAND:	/* --dnskeyondemand */
			end->key_from_DNS_on_demand = true;
			continue;

		case END_UPDOWN:	/* --updown <updown> */
			end->updown = optarg;
			continue;

		case CD_TO:	/* --to */
			/* process right end, move it to left, reset it */
			if (!LHAS(end_seen, END_HOST - END_FIRST))
				diagw("connection missing --host before --to");

			end = &msg.right;
			end_seen = LEMPTY;
			continue;

		/* --ikev1 --ikev2 --ikev2-propose */
		case CD_IKEv1:
		case CD_IKEv2:
		{
			const enum ike_version ike_version = IKEv1 + c - CD_IKEv1;
			if (msg.ike_version != 0 && msg.ike_version != ike_version) {
				diagw("connection can no longer have --ikev1 and --ikev2");
			}
			msg.ike_version = ike_version;
			continue;
		}

		case CDP_SINGLETON + POLICY_ENCRYPT_IX:	/* --encrypt */
		/* --authenticate */
		case CDP_SINGLETON + POLICY_AUTHENTICATE_IX:
		/* --compress */
		case CDP_SINGLETON + POLICY_COMPRESS_IX:
		case CDP_SINGLETON + POLICY_TUNNEL_IX:	/* --tunnel */
		case CDP_SINGLETON + POLICY_PFS_IX:	/* --pfs */

		/* --donotrekey */
		case CDP_SINGLETON + POLICY_DONT_REKEY_IX:

		/* --reauth */
		case CDP_SINGLETON + POLICY_REAUTH_IX:

		/* --modecfgpull */
		case CDP_SINGLETON + POLICY_MODECFG_PULL_IX:
		/* --aggrmode */
		case CDP_SINGLETON + POLICY_AGGRESSIVE_IX:
		/* --overlapip */
		case CDP_SINGLETON + POLICY_OVERLAPIP_IX:

		/* --allow-narrowing */
		case CDP_SINGLETON + POLICY_IKEV2_ALLOW_NARROWING_IX:

		/* --mobike */
		case CDP_SINGLETON + POLICY_MOBIKE_IX:

		/* --intermediate */
		case CDP_SINGLETON + POLICY_INTERMEDIATE_IX:

		/* --ikefrag-allow */
		case CDP_SINGLETON + POLICY_IKE_FRAG_ALLOW_IX:
		/* --ikefrag-force */
		case CDP_SINGLETON + POLICY_IKE_FRAG_FORCE_IX:
		/* --no-ikepad */
		case CDP_SINGLETON + POLICY_NO_IKEPAD_IX:
		/* --no-esn */
		case CDP_SINGLETON + POLICY_ESN_NO_IX:
		/* --esn */
		case CDP_SINGLETON + POLICY_ESN_YES_IX:
		/* --decap-dscp */
		case CDP_SINGLETON + POLICY_DECAP_DSCP_IX:
		/* --nopmtudisc */
		case CDP_SINGLETON + POLICY_NOPMTUDISC_IX:
		/* --ms-dh-downgrade */
		case CDP_SINGLETON + POLICY_MSDH_DOWNGRADE_IX:
		/* --dns-match-id */
		case CDP_SINGLETON + POLICY_DNS_MATCH_ID_IX:
		/* --allow-cert-without-san-id */
		case CDP_SINGLETON + POLICY_ALLOW_NO_SAN_IX:
		/* --sha2-truncbug or --sha2_truncbug */
		case CDP_SINGLETON + POLICY_SHA2_TRUNCBUG_IX:

			msg.policy |= LELEM(c - CDP_SINGLETON);
			continue;

		case CD_INITIATEONTRAFFIC:	/* --initiateontraffic */
			continue;

		case CDS_NEVER_NEGOTIATE + SHUNT_PASS:	/* --pass */
		case CDS_NEVER_NEGOTIATE + SHUNT_DROP:	/* --drop */
		case CDS_NEVER_NEGOTIATE + SHUNT_REJECT:	/* --reject */
			msg.never_negotiate_shunt = c - CDS_NEVER_NEGOTIATE;
			continue;

		case CDS_NEGOTIATION + SHUNT_PASS:	/* --negopass */
			msg.negotiation_shunt = c - CDS_NEGOTIATION;
			continue;

		case CDS_FAILURE + SHUNT_NONE:		/* --failnone */
		case CDS_FAILURE + SHUNT_PASS:		/* --failpass */
		case CDS_FAILURE + SHUNT_DROP:		/* --faildrop */
		case CDS_FAILURE + SHUNT_REJECT:	/* --failreject */
			msg.failure_shunt = c - CDS_FAILURE;
			continue;

		case CD_RETRANSMIT_TIMEOUT:	/* --retransmit-timeout <seconds> */
			optarg_to_deltatime(&msg.retransmit_timeout, &timescale_seconds);
			continue;

		case CD_RETRANSMIT_INTERVAL:	/* --retransmit-interval <milliseconds> (not seconds) */
			optarg_to_deltatime(&msg.retransmit_interval, &timescale_milliseconds);
			continue;

		case CD_IKE_LIFETIME:	/* --ike-lifetime <seconds> */
			optarg_to_deltatime(&msg.sa_ike_max_lifetime, &timescale_seconds);
			continue;

		case CD_IPSEC_LIFETIME:	/* --ipsec-lifetime <seconds> */
			optarg_to_deltatime(&msg.sa_ipsec_max_lifetime, &timescale_seconds);
			continue;

		case CD_IPSEC_MAX_BYTES:	/* --ipsec-max-bytes <bytes> */
			msg.sa_ipsec_max_bytes = opt_whole; /* TODO accept K/M/G/T etc */
			continue;

		case CD_IPSEC_MAX_PACKETS:	/* --ipsec-max-packets <packets> */
			msg.sa_ipsec_max_packets = opt_whole; /* TODO accept K/M/G/T etc */
			continue;

		case CD_REKEYMARGIN:	/* --rekeymargin <seconds> */
			optarg_to_deltatime(&msg.sa_rekey_margin, &timescale_seconds);
			continue;

		case CD_RKFUZZ:	/* --rekeyfuzz <percentage> */
			msg.sa_rekey_fuzz = opt_whole;
			continue;

		case CD_KTRIES:	/* --keyingtries <count> */
			msg.sa_keying_tries = opt_whole;
			continue;

		case CD_REPLAY_WINDOW: /* --replay-window <num> */
			/*
			 * Upper bound is determined by the kernel.
			 * Pluto will check against this when
			 * processing the message.  The value is
			 * relatively small.
			 */
			optarg_to_uintmax(&msg.sa_replay_window);
			continue;

		case CD_SEND_CA:	/* --sendca */
			if (streq(optarg, "issuer"))
				msg.send_ca = CA_SEND_ISSUER;
			else if (streq(optarg, "all"))
				msg.send_ca = CA_SEND_ALL;
			else
				msg.send_ca = CA_SEND_NONE;
			continue;

		case CD_ENCAPS:	/* --encaps */
			if (streq(optarg, "auto"))
				msg.encaps = yna_auto;
			else if (streq(optarg, "yes"))
				msg.encaps = yna_yes;
			else if (streq(optarg, "no"))
				msg.encaps = yna_no;
			else
				diagw("--encaps options are 'auto', 'yes' or 'no'");
			continue;

		case CD_NIC_OFFLOAD:  /* --nic-offload */
			if (streq(optarg, "no"))
				msg.nic_offload = yna_no;
			else if (streq(optarg, "yes"))
				msg.nic_offload = yna_yes;
			else if (streq(optarg, "auto"))
				msg.nic_offload = yna_auto;
			else
				diagw("--nic-offload options are 'no', 'yes' or 'auto'");
			continue;

		case CD_NO_NAT_KEEPALIVE:	/* --no-nat_keepalive */
			msg.nat_keepalive = false;
			continue;

		case CD_IKEV1_NATT:	/* --ikev1-natt */
			if (streq(optarg, "both"))
				msg.ikev1_natt = NATT_BOTH;
			else if (streq(optarg, "rfc"))
				msg.ikev1_natt = NATT_RFC;
			else if (streq(optarg, "drafts"))
				msg.ikev1_natt = NATT_DRAFTS;
			else if (streq(optarg, "none"))
				msg.ikev1_natt = NATT_NONE;
			else
				diagw("--ikev1-natt options are 'both', 'rfc' or 'drafts'");
			continue;

		case CD_INITIAL_CONTACT:	/* --initialcontact */
			msg.initial_contact = true;
			continue;

		case CD_CISCO_UNITY:	/* --cisco-unity */
			msg.cisco_unity = true;
			continue;

		case CD_FAKE_STRONGSWAN:	/* --fake-strongswan */
			msg.fake_strongswan = true;
			continue;

		case CD_DPDDELAY:	/* --dpddelay <seconds> */
			msg.dpd_delay = strdup(optarg);
			continue;

		case CD_DPDTIMEOUT:	/* --dpdtimeout <seconds> */
			msg.dpd_timeout = strdup(optarg);
			continue;

		case CD_DPDACTION:	/* --dpdaction */
			if (streq(optarg, "clear")) {
				msg.dpd_action = DPD_ACTION_CLEAR;
			} else if (streq(optarg, "hold")) {
				msg.dpd_action = DPD_ACTION_HOLD;
			} else if (streq(optarg, "restart")) {
				msg.dpd_action = DPD_ACTION_RESTART;
			} else if (streq(optarg, "restart_by_peer")) {
				/*
				 * obsolete (not advertised) option for
				 * compatibility
				 */
				msg.dpd_action = DPD_ACTION_RESTART;
			} else {
				diagw("dpdaction can only be \"clear\", \"hold\" or \"restart\"");
			}
			continue;

		case CD_SEND_REDIRECT:	/* --send-redirect */
		{
			lset_t new_policy = LEMPTY;

			if (streq(optarg, "yes"))
				new_policy |= POLICY_SEND_REDIRECT_ALWAYS;
			else if (streq(optarg, "no"))
				new_policy |= POLICY_SEND_REDIRECT_NEVER;
			else if (streq(optarg, "auto"))
				new_policy = LEMPTY;	/* avoid compiler error for no expression */
			else
				diagw("--send-redirect options are 'yes', 'no' or 'auto'");

			msg.policy = msg.policy & ~(POLICY_SEND_REDIRECT_MASK);
			msg.policy |= new_policy;
		}
			continue;

		case CD_REDIRECT_TO:	/* --redirect-to */
			msg.redirect_to = strdup(optarg);
			continue;

		case CD_ACCEPT_REDIRECT:	/* --accept-redirect */
		{
			lset_t new_policy = LEMPTY;

			if (streq(optarg, "yes"))
				new_policy |= POLICY_ACCEPT_REDIRECT_YES;
			else if (streq(optarg, "no"))
				new_policy |= LEMPTY;
			else
				diagw("--accept-redirect options are 'yes' and 'no'");

			if (new_policy != LEMPTY)
				msg.policy |= new_policy;
			else
				msg.policy = msg.policy & ~POLICY_ACCEPT_REDIRECT_YES;
		}
			continue;

		case CD_ACCEPT_REDIRECT_TO:	/* --accept-redirect-to */
			msg.accept_redirect_to = strdup(optarg);
			continue;

		case CD_IKE:	/* --ike <ike_alg1,ike_alg2,...> */
			msg.ike = optarg;
			continue;

		case CD_PFSGROUP:	/* --pfsgroup modpXXXX */
			msg.pfsgroup = optarg;
			continue;

		case CD_ESP:	/* --esp <esp_alg1,esp_alg2,...> */
			msg.esp = optarg;
			continue;

		case CD_REMOTEPEERTYPE:	/* --remote-peer-type <cisco> */
			if (streq(optarg, "cisco"))
				msg.remotepeertype = CISCO;
			else
				msg.remotepeertype = NON_CISCO;
			continue;

#ifdef HAVE_NM
		case CD_NMCONFIGURED:	/* --nm-configured */
			msg.nmconfigured = true;
			continue;
#endif

		case CD_IKE_TCP: /* --tcp */
			if (streq(optarg, "yes"))
				msg.iketcp = IKE_TCP_ONLY;
			else if (streq(optarg, "no"))
				msg.iketcp = IKE_TCP_NO;
			else if (streq(optarg, "fallback"))
				msg.iketcp = IKE_TCP_FALLBACK;
			else
				diagw("--tcp-options are 'yes', 'no' or 'fallback'");
			continue;

		case CD_LABELED_IPSEC:	/* obsolete --labeledipsec */
			/* ignore */
			continue;

		case CD_SEC_LABEL:	/* --sec-label */
			/* we only support symmetric labels but put it in struct end */
			msg.sec_label = optarg;
			continue;


		/* RSASIG/ECDSA need more than a single policy bit */
		case OPT_AUTHBY_PSK:		/* --psk */
			msg.authby.psk = true;
			continue;
		case OPT_AUTHBY_AUTH_NEVER:	/* --auth-never */
			msg.authby.never = true;
			continue;
		case OPT_AUTHBY_AUTH_NULL:	/* --auth-null */
			msg.authby.null = true;
			continue;
		case OPT_AUTHBY_RSASIG: /* --rsasig */
			msg.authby.rsasig = true;
			msg.authby.rsasig_v1_5 = true;
			msg.sighash_policy |= POL_SIGHASH_SHA2_256;
			msg.sighash_policy |= POL_SIGHASH_SHA2_384;
			msg.sighash_policy |= POL_SIGHASH_SHA2_512;
			continue;
		case OPT_AUTHBY_RSA_SHA1: /* --rsa-sha1 */
			msg.authby.rsasig_v1_5 = true;
			continue;
		case OPT_AUTHBY_RSA_SHA2: /* --rsa-sha2 */
			msg.authby.rsasig = true;
			msg.sighash_policy |= POL_SIGHASH_SHA2_256;
			msg.sighash_policy |= POL_SIGHASH_SHA2_384;
			msg.sighash_policy |= POL_SIGHASH_SHA2_512;
			continue;
		case OPT_AUTHBY_RSA_SHA2_256:	/* --rsa-sha2_256 */
			msg.authby.rsasig = true;
			msg.sighash_policy |= POL_SIGHASH_SHA2_256;
			continue;
		case OPT_AUTHBY_RSA_SHA2_384:	/* --rsa-sha2_384 */
			msg.authby.rsasig = true;
			msg.sighash_policy |= POL_SIGHASH_SHA2_384;
			continue;
		case OPT_AUTHBY_RSA_SHA2_512:	/* --rsa-sha2_512 */
			msg.authby.rsasig = true;
			msg.sighash_policy |= POL_SIGHASH_SHA2_512;
			continue;

		case OPT_AUTHBY_ECDSA: /* --ecdsa and --ecdsa-sha2 */
			msg.authby.ecdsa = true;
			msg.sighash_policy |= POL_SIGHASH_SHA2_256;
			msg.sighash_policy |= POL_SIGHASH_SHA2_384;
			msg.sighash_policy |= POL_SIGHASH_SHA2_512;
			continue;
		case OPT_AUTHBY_ECDSA_SHA2_256:	/* --ecdsa-sha2_256 */
			msg.authby.ecdsa = true;
			msg.sighash_policy |= POL_SIGHASH_SHA2_256;
			continue;
		case OPT_AUTHBY_ECDSA_SHA2_384:	/* --ecdsa-sha2_384 */
			msg.authby.ecdsa = true;
			msg.sighash_policy |= POL_SIGHASH_SHA2_384;
			continue;
		case OPT_AUTHBY_ECDSA_SHA2_512:	/* --ecdsa-sha2_512 */
			msg.authby.ecdsa = true;
			msg.sighash_policy |= POL_SIGHASH_SHA2_512;
			continue;

		case CD_CONNIPV4:	/* --ipv4; mimic --ipv6 */
			if (host_family.type == &ipv4_info) {
				/* ignore redundant options */
				continue;
			}

			if (seen[CD_CONNIPV6]) {
				/* i.e., --ipv6 ... --ipv4 */
				diagw("--ipv4 conflicts with --ipv6");
			}

			if (host_family.used_by != NULL) {
				/* i.e., --host ::1 --ipv4; useful? wrong message? */
				diagq("--ipv4 must precede", host_family.used_by);
			}
			host_family.used_by = long_opts[long_index].name;
			host_family.type = &ipv4_info;
			continue;

		case CD_CONNIPV6:	/* --ipv6; mimic ipv4 */
			if (host_family.type == &ipv6_info) {
				/* ignore redundant options */
				continue;
			}

			if (seen[CD_CONNIPV4]) {
				/* i.e., --ipv4 ... --ipv6 */
				diagw("--ipv6 conflicts with --ipv4");
			}

			if (host_family.used_by != NULL) {
				/* i.e., --host 0.0.0.1 --ipv6; useful? wrong message? */
				diagq("--ipv6 must precede", host_family.used_by);
			}
			host_family.used_by = long_opts[long_index].name;
			host_family.type = &ipv6_info;
			continue;

		case CD_TUNNELIPV4:	/* --tunnelipv4 */
			if (seen[CD_TUNNELIPV6]) {
				diagw("--tunnelipv4 conflicts with --tunnelipv6");
			}
			if (child_family.used_by != NULL)
				diagq("--tunnelipv4 must precede", child_family.used_by);
			child_family.used_by = long_opts[long_index].name;
			child_family.type = &ipv4_info;
			continue;

		case CD_TUNNELIPV6:	/* --tunnelipv6 */
			if (seen[CD_TUNNELIPV4]) {
				diagw("--tunnelipv6 conflicts with --tunnelipv4");
			}
			if (child_family.used_by != NULL)
				diagq("--tunnelipv6 must precede", child_family.used_by);
			child_family.used_by = long_opts[long_index].name;
			child_family.type = &ipv6_info;
			continue;

		case END_XAUTHSERVER:	/* --xauthserver */
			end->xauth_server = true;
			continue;

		case END_XAUTHCLIENT:	/* --xauthclient */
			end->xauth_client = true;
			continue;

		case OPT_USERNAME:	/* --username, was --xauthname */
			/*
			 * we can't tell if this is going to be --initiate, or
			 * if this is going to be an conn definition, so do
			 * both actions
			 */
			end->xauth_username = optarg;
			gotusername = true;
			/* ??? why does this length include NUL? */
			usernamelen = jam_str(xauthusername, sizeof(xauthusername),
					optarg) -
				xauthusername + 1;
			continue;

		case OPT_XAUTHPASS:	/* --xauthpass */
			gotxauthpass = true;
			/* ??? why does this length include NUL? */
			xauthpasslen = jam_str(xauthpass, sizeof(xauthpass),
					optarg) -
				xauthpass + 1;
			continue;

		case END_MODECFGCLIENT:	/* --modeconfigclient */
			end->modecfg_client = true;
			continue;

		case END_MODECFGSERVER:	/* --modeconfigserver */
			end->modecfg_server = true;
			continue;

		case END_ADDRESSPOOL:	/* --addresspool */
			end->addresspool = strdup(optarg);
			continue;

		case CD_MODECFGDNS:	/* --modecfgdns */
			msg.modecfg_dns = strdup(optarg);
			continue;
		case CD_MODECFGDOMAINS:	/* --modecfgdomains */
			msg.modecfg_domains = strdup(optarg);
			continue;
		case CD_MODECFGBANNER:	/* --modecfgbanner */
			msg.modecfg_banner = strdup(optarg);
			continue;

		case CD_CONN_MARK_BOTH:      /* --conn-mark */
			msg.conn_mark_both = strdup(optarg);
			continue;
		case CD_CONN_MARK_IN:      /* --conn-mark-in */
			msg.conn_mark_in = strdup(optarg);
			continue;
		case CD_CONN_MARK_OUT:      /* --conn-mark-out */
			msg.conn_mark_out = strdup(optarg);
			continue;

		case CD_VTI_IFACE:      /* --vti-iface */
			if (optarg != NULL && strlen(optarg) < IFNAMSIZ)
				msg.vti_iface = strdup(optarg);
			else
				fprintf(stderr, "whack: invalid interface name '%s' ignored\n",
					optarg);
			continue;
		case CD_VTI_ROUTING:	/* --vti-routing */
			msg.vti_routing = true;
			continue;
		case CD_VTI_SHARED:	/* --vti-shared */
			msg.vti_shared = true;
			continue;

		case CD_IPSEC_IFACE:      /* --ipsec-interface */
			msg.xfrm_if_id = opt_whole;
			continue;

		case CD_XAUTHBY:	/* --xauthby */
			if (streq(optarg, "file")) {
				msg.xauthby = XAUTHBY_FILE;
				continue;
#ifdef AUTH_HAVE_PAM
			} else if (streq(optarg, "pam")) {
				msg.xauthby = XAUTHBY_PAM;
				continue;
#endif
			} else if (streq(optarg, "alwaysok")) {
				msg.xauthby = XAUTHBY_ALWAYSOK;
				continue;
			} else {
				diagq("whack: unknown xauthby method", optarg);
			}
			continue;

		case CD_XAUTHFAIL:	/* --xauthfail */
			if (streq(optarg, "hard")) {
				msg.xauthfail = XAUTHFAIL_HARD;
				continue;
			} else if (streq(optarg, "soft")) {
				msg.xauthfail = XAUTHFAIL_SOFT;
				continue;
			} else {
				fprintf(stderr,
					"whack: unknown xauthfail method '%s' ignored\n",
					optarg);
			}
			continue;

		case CD_METRIC:	/* --metric */
			msg.metric = opt_whole;
			continue;

		case CD_CONNMTU:	/* --mtu */
			msg.connmtu = opt_whole;
			continue;

		case CD_PRIORITY:	/* --priority */
			msg.sa_priority = opt_whole;
			continue;

		case CD_TFCPAD:	/* --tfc */
			msg.sa_tfcpad = opt_whole;
			continue;

		case CD_SEND_TFCPAD:	/* --send-no-esp-tfc */
			msg.send_no_esp_tfc = true;
			continue;

		case CD_NFLOG_GROUP:	/* --nflog-group */
			if (opt_whole <= 0  ||
			    opt_whole > 65535) {
				char buf[120];

				snprintf(buf, sizeof(buf),
					"invalid nflog-group value - range must be 1-65535 \"%s\"",
					optarg);
				diagw(buf);
			}
			msg.nflog_group = opt_whole;
			continue;

		case CD_REQID:	/* --reqid */
			if (opt_whole <= 0  ||
			    opt_whole > IPSEC_MANUAL_REQID_MAX) {
				char buf[120];

				snprintf(buf, sizeof(buf),
					"invalid reqid value - range must be 1-%u \"%s\"",
					IPSEC_MANUAL_REQID_MAX,
					optarg);
				diagw(buf);
			}

			msg.sa_reqid = opt_whole;
			continue;

		case DBGOPT_NONE:	/* --debug-none (obsolete) */
			/*
			 * Clear all debug and impair options.
			 *
			 * This preserves existing behaviour where
			 * sequences like:
			 *
			 *     --debug-none
			 *     --debug-none --debug something
			 *
			 * force all debug/impair options to values
			 * defined by whack.
			 */
			msg.debugging = lmod_clr(msg.debugging, DBG_MASK);
			continue;

		case DBGOPT_ALL:	/* --debug-all (obsolete) */
			/*
			 * Set most debug options ('all' does not
			 * include PRIVATE which is cleared) and clear
			 * all impair options.
			 *
			 * This preserves existing behaviour where
			 * sequences like:
			 *
			 *     --debug-all
			 *     --debug-all --impair something
			 *
			 * force all debug/impair options to values
			 * defined by whack.
			 */
			msg.debugging = lmod_clr(msg.debugging, DBG_MASK);
			msg.debugging = lmod_set(msg.debugging, DBG_ALL);
			continue;

		case DBGOPT_DEBUG:	/* --debug */
		case DBGOPT_NO_DEBUG:	/* --no-debug */
		{
			bool enable = (c == DBGOPT_DEBUG);
			if (streq(optarg, "list") || streq(optarg, "help") || streq(optarg, "?")) {
				fprintf(stderr, "aliases:\n");
				for (struct lmod_alias *a = debug_lmod_info.aliases;
				     a->name != NULL; a++) {
					JAMBUF(buf) {
						jam(buf, "  %s: ", a->name);
						jam_lset_short(buf, debug_lmod_info.names, "+", a->bits);
						fprintf(stderr, PRI_SHUNK"\n",
							pri_shunk(jambuf_as_shunk(buf)));
					}
				}
				fprintf(stderr, "bits:\n");
				for (long e = next_enum(&debug_names, -1);
				     e != -1; e = next_enum(&debug_names, e)) {
					JAMBUF(buf) {
						jam(buf, "  ");
						jam_enum_short(buf, &debug_names, e);
						const char *help = enum_name(&debug_help, e);
						if (help != NULL) {
							jam(buf, ": ");
							jam_string(buf, help);
						}
						fprintf(stderr, PRI_SHUNK"\n",
							pri_shunk(jambuf_as_shunk(buf)));
					}
				}
				exit(1);
			} else if (!lmod_arg(&msg.debugging, &debug_lmod_info, optarg, enable)) {
				fprintf(stderr, "whack: unrecognized -%s-debug '%s' option ignored\n",
					enable ? "" : "-no", optarg);
			}
			continue;
		}

		case DBGOPT_IMPAIR:	/* --impair */
		case DBGOPT_NO_IMPAIR:	/* --no-impair */
		{
			bool enable = (c == DBGOPT_IMPAIR);
			realloc_things(msg.impairments, msg.nr_impairments,
				       msg.nr_impairments+1, "impairments");
			switch (parse_impair(optarg, &msg.impairments[msg.nr_impairments],
					     enable, logger)) {
			case IMPAIR_OK:
				break;
			case IMPAIR_HELP:
				/* parse_impair() printed help */
				exit(0);
			case IMPAIR_ERROR:
				/* parse_impair() printed the error */
				exit(1);
			}
			msg.nr_impairments++;
			continue;
		}

		default:
			bad_case(c);
		}
		break;
	}

	if (msg.ike_version == 0) {
		/* no ike version specified, default to IKEv2 */
		msg.ike_version = IKEv2;
	}

	switch (msg.ike_version) {
	case IKEv1:
		if (msg.authby.ecdsa) {
			diagw("connection cannot specify --ecdsa and --ikev1");
		}
		/* delete any inherited sighash_poliyc from --rsasig including sha2 */
		msg.sighash_policy = LEMPTY;
		break;
	case IKEv2:
		if (msg.policy & POLICY_AGGRESSIVE)
			diagw("connection cannot specify --ikev2 and --aggressive");
		break;
	}

	msg.child_afi = child_family.type;
	msg.host_afi = host_family.type;

	if (!authby_is_set(msg.authby)) {
		/*
		 * Since any option potentially setting SIGHASH bits
		 * always sets AUTHBY, check that.
		 *
		 * Mimic addconn's behaviour: specifying auth= (yes,
		 * whack calls it --authby) does not clear the
		 * policy_authby defaults.  That is left to pluto.
		 */
		msg.sighash_policy |= POL_SIGHASH_DEFAULTS;
	}

	if (optind != argc) {
		/*
		 * If you see this message unexpectedly, perhaps the
		 * case for the previous option ended with "break"
		 * instead of "continue"
		 */
		diagq("unexpected argument", argv[optind]);
	}

	/*
	 * For each possible form of the command, figure out if an argument
	 * suggests whether that form was intended, and if so, whether all
	 * required information was supplied.
	 */

	/* check opportunistic initiation simulation request */
	if (seen[OPT_OPPO_HERE] && seen[OPT_OPPO_THERE]) {
		msg.whack_oppo_initiate = true;
		/*
		 * When the only CD (connection description) option is
		 * TUNNELIPV[46] scrub that a connection description
		 * was seen (OPTS_SEEN will be left with
		 * OPT_OPPO_{HERE,THERE}.
		 *
		 * XXX: is this broken?  It scrubs the OPTS_SEEN_CD
		 * bit when though a CDP_* or OPT_AUTHBY_* bit was set
		 * (both of which seem to be incompatible with OPPO).
		 */
		for (enum option_enums e = CD_FIRST; e <= CD_LAST; e++) {
			if (e != CD_TUNNELIPV4 &&
			    e != CD_TUNNELIPV6 &&
			    seen[e]) {
				pexpect(opts_seen & OPTS_SEEN_CD);
				opts_seen &= ~OPTS_SEEN_CD;
				break;
			}
		}
	} else if (seen[OPT_OPPO_HERE] || seen[OPT_OPPO_THERE]) {
		diagw("--oppohere and --oppothere must be used together");
	}

	/* check connection description */
	if (opts_seen & OPTS_SEEN_CD) {
		if (!seen[CD_TO]) {
			diagw("connection description option, but no --to");
		}

		if (!LHAS(end_seen, END_HOST - END_FIRST))
			diagw("connection missing --host after --to");

		if (msg.policy & POLICY_OPPORTUNISTIC) {
			if (msg.authby.psk) {
				diagw("PSK is not supported for opportunism");
			}
			if (!authby_has_digsig(msg.authby)) {
				diagw("only Digital Signatures are supported for opportunism");
			}

			if ((msg.policy & POLICY_PFS) == 0)
				diagw("PFS required for opportunism");
			if ((msg.policy & POLICY_ENCRYPT) == 0)
				diagw("encryption required for opportunism");
		}

		if (msg.authby.never) {
			if (msg.never_negotiate_shunt == SHUNT_UNSET) {
				diagw("shunt connection must have shunt policy (eg --pass, --drop or --reject). Is this a non-shunt connection missing an authentication method such as --psk or --rsasig or --auth-null ?");
			}
		} else {
			/* not just a shunt: a real ipsec connection */
			if (!authby_is_set(msg.authby) &&
			    msg.left.auth == AUTH_NEVER &&
			    msg.right.auth == AUTH_NEVER)
				diagw("must specify connection authentication, eg --rsasig, --psk or --auth-null for non-shunt connection");
			/*
			 * ??? this test can never fail:
			 *	!NEVER_NEGOTIATE=>HAS_IPSEC_POLICY
			 * These interlocking tests should be redone.
			 */
			if (!HAS_IPSEC_POLICY(msg.policy) &&
			    (msg.left.subnet != NULL || msg.right.subnet != NULL))
				diagw("must not specify clients for ISAKMP-only connection");
		}

		msg.whack_add = true;
	}

	/* decide whether --name is mandatory or forbidden */
	if (seen[OPT_ROUTE] ||
	    seen[OPT_UNROUTE] ||
	    seen[OPT_INITIATE] ||
	    seen[OPT_TERMINATE] ||
	    seen[OPT_DELETE] ||
	    seen[OPT_DELETEID] ||
	    seen[OPT_DELETEUSER] ||
	    seen[OPT_REKEY_IKE] ||
	    seen[OPT_REKEY_IPSEC] ||
	    (opts_seen & OPTS_SEEN_CD)) {
		if (!seen[OPT_NAME])
			diagw("missing --name <connection_name>");
#if 0
		/*
		 * XXX: these checks are broken:
		 *
		 * Since !LELEM(OPT_TRAFFIC_STATUS) is always false
		 * the diagw() never appears.
		 *
		 * .whack_options is a "bool" and not an "lset_t".
		 *
		 * .whack_options is only set when there's a DBGOPT,
		 * testing that directly would be better.
		 */
	} else if (msg.whack_options == LEMPTY) {
		if (seen[OPT_NAME] && !LELEM(OPT_TRAFFIC_STATUS))
			diagw("no reason for --name");
#endif
	}

	if (seen[OPT_REMOTE_HOST] && !seen[OPT_INITIATE]) {
		diagw("--remote-host can only be used with --initiate");
	}

	if (seen[OPT_PUBKEYRSA] || seen[OPT_ADDKEY]) {
		if (!seen[OPT_KEYID]) {
			diagw("--addkey and --pubkeyrsa require --keyid");
		}
	}

	if (!(msg.whack_add || msg.whack_key ||
	      msg.whack_delete ||msg.whack_deleteid || msg.whack_deletestate ||
	      msg.whack_deleteuser || msg.active_redirect_dests != NULL ||
	      msg.global_redirect || msg.global_redirect_to ||
	      msg.whack_initiate || msg.whack_oppo_initiate ||
	      msg.whack_terminate ||
	      msg.whack_route || msg.whack_unroute || msg.whack_listen ||
	      msg.whack_unlisten || msg.whack_list || msg.ike_buf_size ||
	      msg.whack_ddos != DDOS_undefined || msg.whack_ddns ||
	      msg.whack_rereadcerts ||
	      msg.whack_fetchcrls ||
	      msg.whack_rereadsecrets ||
	      msg.whack_crash || msg.whack_shunt_status ||
	      msg.whack_status || msg.whack_global_status || msg.whack_traffic_status ||
	      msg.whack_addresspool_status ||
	      msg.whack_connection_status ||
	      msg.whack_process_status ||
	      msg.whack_fips_status || msg.whack_brief_status || msg.whack_clear_stats ||
	      !lmod_empty(msg.debugging) ||
	      msg.nr_impairments > 0 ||
	      msg.whack_shutdown || msg.whack_purgeocsp || msg.whack_seccomp_crashtest || msg.whack_show_states ||
	      msg.whack_rekey_ike || msg.whack_rekey_ipsec ||
	      msg.whack_listpubkeys || msg.whack_checkpubkeys))
		diagw("no action specified; try --help for hints");

	if (msg.policy & POLICY_AGGRESSIVE) {
		if (msg.ike == NULL)
			diagw("cannot specify aggressive mode without ike= to set algorithm");
	}

	/*
	 * Check for wild values
	 * Must never overflow: rekeymargin*(100+rekeyfuzz)/100
	 * We don't know the maximum value for a time_t, so we use INT_MAX
	 * ??? this should be checked wherever any of these is set in Pluto
	 * too.
	 */
	if (msg.sa_rekey_fuzz > INT_MAX - 100 ||
	    deltasecs(msg.sa_rekey_margin) > (time_t)(INT_MAX / (100 + msg.sa_rekey_fuzz)))
		diagw("rekeymargin or rekeyfuzz values are so large that they cause overflow");

	check_max_lifetime(msg.sa_ike_max_lifetime, IKE_SA_LIFETIME_MAXIMUM, "ike-lifetime", &msg);
	check_max_lifetime(msg.sa_ipsec_max_lifetime, IPSEC_SA_LIFETIME_MAXIMUM, "ipsec-lifetime", &msg);

	if (msg.remotepeertype != CISCO &&
	    msg.remotepeertype != NON_CISCO) {
		diagw("remote-peer-type can only be \"CISCO\" or \"NON_CISCO\" - defaulting to non-cisco mode");
		msg.remotepeertype = NON_CISCO;	/*NON_CISCO=0*/
	}

	/* pack strings for inclusion in message */
	wp.msg = &msg;

	/* build esp message as esp="<esp>;<pfsgroup>" */
	if (msg.pfsgroup != NULL) {
		snprintf(esp_buf, sizeof(esp_buf), "%s;%s",
			 msg.esp != NULL ? msg.esp : "",
			 msg.pfsgroup != NULL ? msg.pfsgroup : "");
		msg.esp = esp_buf;
	}
	ugh = pack_whack_msg(&wp);
	if (ugh != NULL)
		diagw(ugh);

	msg.magic = ((opts_seen & ~(LELEM(OPT_SHUTDOWN) | LELEM(OPT_STATUS))) != LEMPTY ? WHACK_MAGIC :
		     WHACK_BASIC_MAGIC);

	/* send message to Pluto */
	if (access(ctl_addr.sun_path, R_OK | W_OK) < 0) {
		int e = errno;

		switch (e) {
		case EACCES:
			fprintf(stderr,
				"whack: no right to communicate with pluto (access(\"%s\"))\n",
				ctl_addr.sun_path);
			break;
		case ENOENT:
			fprintf(stderr,
				"whack: Pluto is not running (no \"%s\")\n",
				ctl_addr.sun_path);
			break;
		default:
			fprintf(stderr,
				"whack: access(\"%s\") failed with %d %s\n",
				ctl_addr.sun_path, errno, strerror(e));
			break;
		}
		exit(RC_WHACK_PROBLEM);
	}

	int sock = cloexec_socket(AF_UNIX, SOCK_STREAM, 0);
	int exit_status = 0;
	ssize_t len = wp.str_next - (unsigned char *)&msg;

	if (sock == -1) {
		int e = errno;

		fprintf(stderr, "whack: socket() failed (%d %s)\n", e, strerror(
				e));
		exit(RC_WHACK_PROBLEM);
	}

	if (connect(sock, (struct sockaddr *)&ctl_addr,
		    offsetof(struct sockaddr_un,
			     sun_path) + strlen(ctl_addr.sun_path)) < 0)
	{
		int e = errno;

		fprintf(stderr,
			"whack:%s connect() for \"%s\" failed (%d %s)\n",
			e == ECONNREFUSED ? " is Pluto running? " : "",
			ctl_addr.sun_path, e, strerror(e));
		exit(RC_WHACK_PROBLEM);
	}

	if (write(sock, &msg, len) != len) {
		int e = errno;

		fprintf(stderr, "whack: write() failed (%d %s)\n",
			e, strerror(e));
		exit(RC_WHACK_PROBLEM);
	}

	/* for now, just copy reply back to stdout */

	char buf[4097];	/* arbitrary limit on log line length */
	char *be = buf;

	for (;;) {
		char *ls = buf;
		ssize_t rl = read(sock, be, (buf + sizeof(buf) - 1) - be);

		if (rl < 0) {
			int e = errno;

			fprintf(stderr,
				"whack: read() failed (%d %s)\n",
				e, strerror(e));
			exit(RC_WHACK_PROBLEM);
		}
		if (rl == 0) {
			if (be != buf)
				fprintf(stderr,
					"whack: last line from pluto too long or unterminated\n");
			break;
		}

		be += rl;
		*be = '\0';

		for (;;) {
			char *le = strchr(ls, '\n');

			if (le == NULL) {
				/*
				 * move last, partial line
				 * to start of buffer
				 */
				memmove(buf, ls, be - ls);
				be -= ls - buf;
				break;
			}
			le++;	/* include NL in line */

			/*
			 * figure out prefix number and how it should
			 * affect our exit status and printing
			 */
			char *lpe = NULL; /* line-prefix-end */
			unsigned long s = strtoul(ls, &lpe, 10);
			if (lpe == ls || *lpe != ' ') {
				/* includes embedded NL, see above */
				fprintf(stderr, "whack: log line missing NNN prefix: %*s",
					(int)(le - ls), ls);
#if 0
				ls = le;
				continue;
#else
				exit(RC_WHACK_PROBLEM);
#endif
			}
			if (s == RC_RAW) {
				ls = lpe + 1; /* skip NNN_ */
			}

			if (write(STDOUT_FILENO, ls, le - ls) != (le - ls)) {
				int e = errno;

				fprintf(stderr,
					"whack: write() failed to stdout(%d %s)\n",
					e, strerror(e));
			}

			switch (s) {
			/* these logs are informational only */
			case RC_COMMENT:
			case RC_RAW:
			case RC_INFORMATIONAL:
			case RC_INFORMATIONAL_TRAFFIC:
			case RC_LOG:
			/* RC_LOG_SERIOUS is supposed to be here according to lswlog.h, but seems oudated? */
				/* ignore */
				break;
			case RC_SUCCESS:
				/* be happy */
				exit_status = 0;
				break;

			case RC_ENTERSECRET:
				if (!gotxauthpass) {
					xauthpasslen = whack_get_secret(
						xauthpass,
						sizeof(xauthpass));
				}
				send_reply(sock,
					   xauthpass,
					   xauthpasslen);
				break;

			case RC_USERPROMPT:
				if (!gotusername) {
					usernamelen = whack_get_value(
						xauthusername,
						sizeof(xauthusername));
				}
				send_reply(sock,
					   xauthusername,
					   usernamelen);
				break;

			default:
				/*
				 * Only RC_ codes between
				 * RC_EXIT_FLOOR (RC_DUPNAME) and
				 * RC_EXIT_ROOF are errors
				 */
				if (s > 0 && (s < RC_EXIT_FLOOR || s >= RC_EXIT_ROOF))
					s = 0;
				exit_status = msg.whack_async ?
					0 : s;
				break;
			}

			ls = le;
		}
	}

	if (ignore_errors)
		return 0;

	return exit_status;
}
