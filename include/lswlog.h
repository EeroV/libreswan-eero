/* logging declaratons
 *
 * Copyright (C) 1998-2001,2013 D. Hugh Redelmeier <hugh@mimosa.com>
 * Copyright (C) 2004 Michael Richardson <mcr@xelerance.com>
 * Copyright (C) 2012-2013 Paul Wouters <paul@libreswan.org>
 * Copyright (C) 2017-2018 Andrew Cagney
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

#ifndef _LSWLOG_H_
#define _LSWLOG_H_

#include <stdarg.h>
#include <stdio.h>		/* for FILE */
#include <stddef.h>		/* for size_t */

#include "lset.h"
#include "lswcdefs.h"

#include "libreswan/passert.h"
#include "constants.h"		/* for DBG_... */

extern bool log_ip; /* false -> redact ip addresses */

/* Build up a diagnostic in a static buffer -- NOT RE-ENTRANT.
 * Although this would be a generally useful function, it is very
 * hard to come up with a discipline that prevents different uses
 * from interfering.  It is intended that by limiting it to building
 * diagnostics, we will avoid this problem.
 * Juggling is performed to allow an argument to be a previous
 * result: the new string may safely depend on the old one.  This
 * restriction is not checked in any way: violators will produce
 * confusing results (without crashing!).
 */
#define LOG_WIDTH	((size_t)1024)	/* roof of number of chars in log line */

extern err_t builddiag(const char *fmt, ...) PRINTF_LIKE(1);	/* NOT RE-ENTRANT */

/* sanitize a string */
extern void sanitize_string(char *buf, size_t size);

extern bool log_to_stderr;          /* should log go to stderr? */


/*
 * Codes for status messages returned to whack.
 *
 * These are 3 digit decimal numerals.  The structure is inspired by
 * section 4.2 of RFC959 (FTP).  Since these will end up as the exit
 * status of whack, they must be less than 256.
 *
 * NOTE: ipsec_auto(8) knows about some of these numbers -- change
 * carefully.
 */

enum rc_type {
	RC_COMMENT,		/* non-commital utterance (does not affect exit status) */
	RC_UNUSED,		/* just to keep the numbering the same */
	RC_LOG,			/* message aimed at log (does not affect exit status) */
	RC_LOG_SERIOUS,		/* serious message aimed at log (does not affect exit status) */
	RC_SUCCESS,		/* success (exit status 0) */
	RC_INFORMATIONAL,	/* should get relayed to user - if there is one */
	RC_INFORMATIONAL_TRAFFIC, /* status of an established IPSEC (aka Phase 2) state */

	/* failure, but not definitive */
	RC_RETRANSMISSION = 10,

	/* improper request */
	RC_DUPNAME = 20,	/* attempt to reuse a connection name */
	RC_UNKNOWN_NAME,	/* connection name unknown or state number */
	RC_ORIENT,		/* cannot orient connection: neither end is us */
	RC_CLASH,		/* clash between two Road Warrior connections OVERLOADED */
	RC_DEAF,		/* need --listen before --initiate */
	RC_ROUTE,		/* cannot route */
	RC_RTBUSY,		/* cannot unroute: route busy */
	RC_BADID,		/* malformed --id */
	RC_NOKEY,		/* no key found through DNS */
	RC_NOPEERIP,		/* cannot initiate when peer IP is unknown */
	RC_INITSHUNT,		/* cannot initiate a shunt-oly connection */
	RC_WILDCARD,		/* cannot initiate when ID has wildcards */
	RC_CRLERROR,		/* CRL fetching disabled or obsolete reread cmd */
	RC_WHACK_PROBLEM,	/* whack-detected problem */

	/* permanent failure */
	RC_BADWHACKMESSAGE = 30,
	RC_NORETRANSMISSION,
	RC_INTERNALERR,
	RC_OPPOFAILURE,		/* Opportunism failed */
	RC_CRYPTOFAILED,	/* system too busy to perform required
				* cryptographic operations */
	RC_AGGRALGO,		/* multiple algorithms requested in phase 1 aggressive */
	RC_FATAL,		/* fatal error encountered, and negotiation aborted */

	/* entry of secrets */
	RC_ENTERSECRET = 40,
	RC_USERPROMPT = 41,

	/* progress: start of range for successful state transition.
	 * Actual value is RC_NEW_STATE plus the new state code.
	 */
	RC_NEW_STATE = 100,

	/* start of range for notification.
	 * Actual value is RC_NOTIFICATION plus code for notification
	 * that should be generated by this Pluto.
	 */
	RC_NOTIFICATION = 200	/* as per IKE notification messages */
};


/*
 * A generic buffer for accumulating unbounded output.
 *
 * The buffer's contents can be directed to various logging streams.
 */

struct lswlog;

/*
 * The logging streams used by libreswan.
 *
 * So far three^D^D^D^D^D four^D^D^D^D five^D^D^D^D six^D^D^D
 * seven^D^D^D^D^D five.five streams have been identified; and let's
 * not forget that code writes to STDOUT and STDERR directly.
 *
 * The streams differ in the syslog severity and what PREFIX is
 * assumed to be present and the tool being run.
 *
 *                           PLUTO
 *              SEVERITY  WHACK  PREFIX    TOOLS    PREFIX
 *   default    WARNING    yes    state     -v
 *   log        WARNING     -     state     -v
 *   debug      DEBUG       -     "| "     debug?
 *   error      ERR         -    ERROR     STDERR  PROG:_...
 *   whack         -       yes    NNN      STDOUT  ...
 *   file          -        -      -         -
 *
 * The streams will then add additional prefixes as required.  For
 * instance, the log_whack stream will prefix a timestamp when sending
 * to a file (optional), and will prefix NNN(RC) when sending to
 * whack.
 *
 * For tools, the default and log streams go to STDERR when enabled;
 * and the debug stream goes to STDERR conditional on debug flags.
 * Should the whack stream go to stdout?
 *
 * As needed, return size_t - the number of bytes written - so that
 * implementations have somewhere to send values that should not be
 * ignored; for instance fwrite() :-/
 */

void lswlog_to_default_streams(struct lswlog *buf, enum rc_type rc);
void lswlog_to_log_stream(struct lswlog *buf);
void lswlog_to_debug_stream(struct lswlog *buf);
void lswlog_to_error_stream(struct lswlog *buf);
void lswlog_to_whack_stream(struct lswlog *buf);
size_t lswlog_to_file_stream(struct lswlog *buf, FILE *file);


/*
 * Log to the default stream(s):
 *
 * - for pluto this means 'syslog', and when connected, whack.
 *
 * - for standalone tools, this means stderr, but only when enabled.
 *
 * There are two variants, the first specify the RC (prefix sent to
 * whack), while the second default RC to RC_LOG.
 *
 * XXX: even though the the name loglog() gives the impression that
 * it, and not plog(), needs to be used when double logging (to both
 * 'syslog' and whack), it does not.  Hence LSWLOG_RC().
 */

void lswlog_log_prefix(struct lswlog *buf);

extern void libreswan_log_rc(enum rc_type, const char *fmt, ...) PRINTF_LIKE(2);
#define loglog	libreswan_log_rc

#define LSWLOG_RC(RC, BUF)						\
	LSWLOG_(true, BUF,						\
		lswlog_log_prefix(BUF),					\
		lswlog_to_default_streams(BUF, RC))

/* signature needs to match printf() */
extern int libreswan_log(const char *fmt, ...) PRINTF_LIKE(1);
#define plog	libreswan_log

#define LSWLOG(BUF) LSWLOG_RC(RC_LOG, BUF)

/*
 * Log to the main log stream, but _not_ the whack log stream.
 */

#define LSWLOG_LOG(BUF)							\
	LSWLOG_(true, BUF,						\
		lswlog_log_prefix(BUF),					\
		lswlog_to_log_stream(BUF))


/*
 * Log, at level RC, to the whack log (if attached).
 *
 * XXX: See programs/pluto/log.h for interface; should only be used in
 * pluto.  This code assumes that it is being called from the main
 * thread.
 *
 * LSWLOG_INFO() sends stuff just to "whack" (or for a tool STDERR?).
 * XXX: there is no prefix, bug?  Should it send stuff out with level
 * RC_COMMENT?
 */

#define LSWLOG_WHACK(RC, BUF)						\
	LSWLOG_(whack_log_p(), BUF,					\
		whack_log_pre(RC, BUF),					\
		lswlog_to_whack_stream(BUF))

/* XXX: should be stdout?!? */
#define LSWLOG_INFO(BUF)					\
	LSWLOG_(true, BUF, , lswlog_to_whack_stream(buf))

/*
 * Log the message to the main log stream, and (at level RC_LOG) to
 * the whack stream, but rate limit messages.
 *
 * But what does "rate limit" mean?  For the moment this simply means
 * limit things to the first 1000 messages.  So what could it mean:
 *
 * - limit messages to N entries per M minutes
 *
 * - limit each message type to N entries per M minutes
 *
 * - suppress but count (sequences of?) identical messages
 *
 * - support within the config file and from whack
 *
 * If debugging is enabled, the messages are logged to the debug
 * stream even after the rate limit is exceeded.
 */

extern void rate_log(const char *message, ...) PRINTF_LIKE(1);

#define LSWLOG_RATE(BUF) ... TBD ...


/*
 * Wrap <message> in a prefix and suffix where the suffix contains
 * errno and message.
 *
 * Notes:
 *
 * Because __VA_ARGS__ may contain function calls that modify ERRNO,
 * errno's value is first saved.
 *
 * While these common-case macros could be implemented directly using
 * LSWLOG_ERRNO_() et.al. they are not, instead they are implemented
 * as wrapper functions.  This is so that a crash will include the
 * below functions _including_ the with MESSAGE parameter - makes
 * debugging much easier.
 */

void libreswan_exit(enum rc_type rc) NEVER_RETURNS;
void libreswan_log_errno(int e, const char *message, ...) PRINTF_LIKE(2);
void libreswan_exit_log_errno(int e, const char *message, ...) PRINTF_LIKE(2) NEVER_RETURNS;

#define LOG_ERRNO(ERRNO, ...) {						\
		int log_errno = ERRNO; /* save value across va args */	\
		libreswan_log_errno(log_errno, __VA_ARGS__);		\
	}

#define EXIT_LOG_ERRNO(ERRNO, ...) {					\
		int exit_log_errno = ERRNO; /* save value across va args */ \
		libreswan_exit_log_errno(exit_log_errno, __VA_ARGS__);	\
	}


/*
 * Log debug messages to the main log stream, but not the WHACK log
 * stream.
 *
 * NOTE: DBG's action can be a { } block, but that block must not
 * contain commas that are outside quotes or parenthesis.
 * If it does, they will be interpreted by the C preprocesser
 * as macro argument separators.  This happens accidentally if
 * multiple variables are declared in one declaration.
 *
 * Naming: All DBG_*() prefixed functions send stuff to the debug
 * stream unconditionally.  Hence they should be wrapped in DBGP().
 */

extern lset_t cur_debugging;	/* current debugging level */

void dbg(const char *fmt, ...) PRINTF_LIKE(1);

#define DBGP(cond)	(cur_debugging & (cond))

#define DEBUG_PREFIX "| "

#define DBG(cond, action)	{ if (DBGP(cond)) { action; } }
#define DBGF(cond, ...) { if (DBGP(cond)) { DBG_log(__VA_ARGS__); } }
/*
 * XXX: signature needs to match printf() so that either it or
 * printf() can be passed to some functions
 */
int DBG_log(const char *message, ...) PRINTF_LIKE(1);

void DBG_dump(const char *label, const void *p, size_t len);

#define DBG_cond_dump(cond, label, p, len) DBG(cond, DBG_dump(label, p, len))

void lswlog_dbg_pre(struct lswlog *buf);

#define LSWDBG_(PREDICATE, BUF)						\
	LSWLOG_(PREDICATE, BUF,						\
		lswlog_dbg_pre(BUF),					\
		lswlog_to_debug_stream(BUF))

#define LSWDBGP(DEBUG, BUF) LSWDBG_(DBGP(DEBUG), BUF)
#define LSWLOG_DEBUG(BUF) LSWDBG_(true, BUF)

/*
 * Impair pluto's behaviour.
 *
 * IMPAIR currently uses the same lset_t as DBG.  Define a separate
 * macro so that, one day, that can change.
 */

#define IMPAIR(BEHAVIOUR) (cur_debugging & (IMPAIR_##BEHAVIOUR))


/*
 * Routines for accumulating output in the lswlog buffer.
 *
 * If there is insufficient space, the output is truncated and "..."
 * is appended.
 *
 * Similar to C99 snprintf() et.al., these functions return the
 * untruncated size of output that the call would append (the value
 * can never be negative).
 *
 * While probably not directly useful, it provides a sink for code
 * that needs to consume an otherwise ignored return value (the
 * compiler attribute warn_unused_result can't be suppressed using a
 * (void) cast).
 */

size_t lswlogvf(struct lswlog *log, const char *format, va_list ap);
size_t lswlogf(struct lswlog *log, const char *format, ...) PRINTF_LIKE(2);
size_t lswlogs(struct lswlog *log, const char *string);
size_t lswlogl(struct lswlog *log, struct lswlog *buf);

/* _(in FUNC() at FILE:LINE) */
size_t lswlog_source_line(struct lswlog *log, const char *func,
			  const char *file, unsigned long line);
/* <string without binary characters> */
size_t lswlog_sanitized(struct lswlog *log, const char *string);
/* _Errno E: <strerror(E)> */
size_t lswlog_errno(struct lswlog *log, int e);
/* <hex-byte>:<hex-byte>... */
size_t lswlog_bytes(struct lswlog *log, const uint8_t *bytes,
		    size_t sizeof_bytes);


/*
 * Code wrappers that cover up the details of allocating,
 * initializing, de-allocating (and possibly logging) a 'struct
 * lswlog' buffer.
 *
 * BUF (a C variable name) is declared locally as a pointer to a
 * per-thread 'struct lswlog' buffer.
 *
 * Implementation notes:
 *
 * This implementation stores the output in an array on the thread's
 * stack.  It could just as easily use the heap (but that would
 * involve memory overheads) or even a per-thread static variable.
 * Since the BUF variable is a pointer the specifics of the
 * implementation are hidden.
 *
 * This implementation, unlike DBG(), does not have a code block
 * parameter.  Instead it uses a sequence of for-loops to set things
 * up for a code block.  This avoids problems with "," within macro
 * parameters confusing the parser.  It also permits a simple
 * consistent indentation style.
 *
 * The stack array is left largely uninitialized (just a few strategic
 * entries are set).  This avoids the need to zero LOG_WITH bytes.
 *
 * Apparently chaining void function calls using a comma is valid C?
 */

/* primitive to point the LSWBUF at and initialize ARRAY.  */
#define LSWBUF_ARRAY_(ARRAY, SIZEOF_ARRAY, BUF)				\
	/* point BUF at LSWLOG at ARRAY */				\
	for (struct lswlog lswlog_, *BUF = lswlog(&lswlog_, ARRAY, SIZEOF_ARRAY); \
	     lswlog_p; lswlog_p = false)

/* primitive to construct an LSWBUF on the stack.  */
#define LSWBUF_(BUF)							\
	for (char lswbuf[LOG_WIDTH]; lswlog_p; lswlog_p = false)	\
		LSWBUF_ARRAY_(lswbuf, sizeof(lswbuf), BUF)

/*
 * Create an LSWLOG using an existing array.
 *
 * Useful when a function passed an array wants to use lswlog routines
 * or wants to capture the output for later use.  For instance:
 */

#if 0
void lswbuf_array(char *array, size_t sizeof_array)
{
	LSWBUF_ARRAY(array, sizeof_array, buf) {
		lswlogf(buf, "written to the array");
	}
}
#endif

#define LSWBUF_ARRAY(ARRAY, SIZEOF_ARRAY, BUF)				\
	for (bool lswlog_p = true; lswlog_p; lswlog_p = false)		\
		LSWBUF_ARRAY_(ARRAY, SIZEOF_ARRAY, BUF)


/*
 * Scratch buffer for accumulating extra output.
 *
 * XXX: case should be expanded to illustrate how to stuff a truncated
 * version of the output into the LOG buffer.
 *
 * XXX: Is this one redundant?  Code can use LSWLOG_ARRAY() or
 * LSWLOG_STRING().
 *
 * For instance:
 */

#if 0
void lswbuf(struct lswlog *log)
{
	LSWBUF(buf) {
		lswlogf(buf, "written to buf");
		lswlogl(log, buf); /* add to calling array */
	}
}
#endif

#define LSWBUF(BUF)							\
	for (bool lswlog_p = true; lswlog_p; lswlog_p = false)		\
		LSWBUF_(BUF)


/*
 * Template for constructing logging output intended for a logger
 * stream.
 *
 * The code is equivlaent to:
 *
 *   if (PREDICATE) {
 *     LSWBUF(BUF) {
 *       PREFIX;
 *          BLOCK;
 *       SUFFIX;
 *    }
 */

#define LSWLOG_(PREDICATE, BUF, PREFIX, SUFFIX)				\
	for (bool lswlog_p = PREDICATE; lswlog_p; lswlog_p = false)	\
		LSWBUF_(BUF)						\
			for (PREFIX; lswlog_p; lswlog_p = false, SUFFIX)

/*
 * Write a line of output to the FILE stream as a single block;
 * includes an implicit new-line.
 *
 * For instance:
 */

#if 0
void lswlog_file(FILE f)
{
	LSWLOG_FILE(f, buf) {
		lswlogf(buf, "written to file");
	}
}
#endif

#define LSWLOG_FILE(FILE, BUF)						\
	LSWLOG_(true, BUF,						\
		,							\
		lswlog_to_file_stream(BUF, FILE))

/*
 * Capture LSWLOG into a heap allocated string.
 */

#if 0
char *lswlog_string(void)
{
	char *string;
	LSWLOG_STRING(string, buf) {
		lswlogf(buf, "written to string");
	}
	return string;
}
#endif

#define LSWLOG_STRING(STRING, BUF)					\
	LSWLOG_(true, BUF,						\
		,							\
		STRING = clone_str(BUF->array, "lswlog string"))


/*
 * Log an expectation failure message to the error streams.  That is
 * the main log (level LOG_ERR) and whack log (level RC_LOG_SERIOUS).
 */

#if 0
void lswlog_pexpect(void *p)
{
	if (!pexpect(p != NULL)) {
		return;
	}
}
#endif

void libreswan_pexpect_fail(const char *func,
		       const char *file, unsigned long line,
		       const char *assertion);

/*
 * Do not wrap ASSERTION in parentheses as it will suppress the
 * warning for 'foo = bar'.
 *
 * Because static analizer tools are easily confused, explicitly
 * return the assertion result.
 */
#define pexpect(ASSERTION)						\
	({								\
		bool assertion__ = ASSERTION;				\
		if (!assertion__)					\
			libreswan_pexpect_fail(__func__,		\
			      PASSERT_BASENAME, __LINE__, #ASSERTION);	\
		assertion__; /* result */				\
	})

void lswlog_pexpect_prefix(struct lswlog *buf);
void lswlog_pexpect_suffix(struct lswlog *buf, const char *func,
			   const char *file, unsigned long line);

#define LSWLOG_PEXPECT_SOURCE(FUNC, FILE, LINE, BUF)	   \
	LSWLOG_(true, BUF,				   \
		lswlog_pexpect_prefix(BUF),		   \
		lswlog_pexpect_suffix(BUF, FUNC, FILE, LINE))

#define LSWLOG_PEXPECT(BUF)				   \
	LSWLOG_PEXPECT_SOURCE(__func__, PASSERT_BASENAME, __LINE__, BUF)


/*
 * Old style
 *
 * According to C99, the expansion of PEXPECT_LOG(FMT) will include a
 * stray comma vis: "pexpect_log(file, line, FMT,)".  Plenty of
 * workarounds.
 */

void libreswan_pexpect_log(const char *func,
			   const char *file, unsigned long line,
			   const char *message, ...) PRINTF_LIKE(4);

#define PEXPECT_LOG(FMT, ...)						\
	libreswan_pexpect_log(__func__,					\
			      PASSERT_BASENAME, __LINE__,		\
			      FMT, __VA_ARGS__)


/*
 * Log an assertion failure to the main log, and the whack log; and
 * then call abort().
 */

void lswlog_passert_prefix(struct lswlog *buf);
void lswlog_passert_suffix(struct lswlog *buf, const char *func,
			   const char *file, unsigned long line) NEVER_RETURNS;

#define LSWLOG_PASSERT_SOURCE(FUNC, FILE, LINE, BUF)	   \
	LSWLOG_(true, BUF,				   \
		lswlog_passert_prefix(BUF),		   \
		lswlog_passert_suffix(BUF, FUNC, FILE, LINE))

#define LSWLOG_PASSERT(BUF)			\
	LSWLOG_PASSERT_SOURCE(__func__, PASSERT_BASENAME, __LINE__, BUF)

/* for a switch statement */

void libreswan_bad_case(const char *expression, long value,
			const char *func, const char *file,
			unsigned long line) NEVER_RETURNS;

#define bad_case(N)	libreswan_bad_case(#N, (N), __func__, \
					   PASSERT_BASENAME, __LINE__)

#define impaired_passert(BEHAVIOUR, ASSERTION) {			\
		if (IMPAIR(BEHAVIOUR)) {				\
			bool assertion_ = ASSERTION;			\
			if (!assertion_) {				\
				libreswan_log("IMPAIR: assertion '%s' failed", #ASSERTION); \
			}						\
		} else {						\
			passert(ASSERTION);				\
		}							\
	}

/*
 * Wrap <message> in a prefix and suffix where the suffix contains
 * errno and message.  Since calls may alter ERRNO, it needs to be
 * saved.
 *
 * XXX: Is error stream really the right place for this?
 *
 * LSWLOG_ERROR() sends an arbitrary message to the error stream (in
 * tools that's STDERR).  XXX: Should LSWLOG_ERRNO() and LSWERR() be
 * merged.  XXX: should LSWLOG_ERROR() use a different prefix?
 */

void lswlog_errno_prefix(struct lswlog *buf, const char *prefix);
void lswlog_errno_suffix(struct lswlog *buf, int e);

#define LSWLOG_ERRNO_(PREFIX, ERRNO, BUF)				\
	for (bool lswlog_p = true; lswlog_p; lswlog_p = false)		\
		for (int lswlog_errno = ERRNO; lswlog_p; lswlog_p = false) \
			LSWBUF_(BUF)					\
				for (lswlog_errno_prefix(buf, PREFIX);	\
				     lswlog_p;				\
				     lswlog_p = false,			\
					     lswlog_errno_suffix(buf, lswlog_errno))

#define LSWLOG_ERRNO(ERRNO, BUF)					\
	LSWLOG_ERRNO_("ERROR: ", ERRNO, BUF)

#define LSWLOG_ERROR(BUF)			\
	LSWLOG_(true, BUF,			\
		lswlog_log_prefix(BUF),		\
		lswlog_to_error_stream(buf))

/*
 * ARRAY, a previously allocated array, containing the accumulated
 * NUL-terminated output.
 *
 * The following offsets into ARRAY are maintained:
 *
 *    0 <= LEN <= BOUND < ROOF < sizeof(ARRAY)
 *
 * ROOF < sizeof(ARRAY); ARRAY[ROOF]==CANARY
 *
 * The offset to the last character in the array.  It contains a
 * canary intended to catch overflows.  When sizeof(ARRAY) is needed,
 * ROOF should be used as otherwise the canary may be corrupted.
 *
 * BOUND < ROOF; ARRAY[BOUND]=='\0'
 *
 * Limit on how many characters can be appended.
 *
 * LEN < BOUND; ARRAY[LEN]=='\0'
 *
 * Equivalent to strlen(BUF).  BOUND-LEN is always the amount of
 * unused space in the array.
 *
 * When LEN<BOUND, space for BOUND-LEN characters, including the
 * terminating NUL, is still available (when BOUND-LEN==1, a single
 * NUL (empty string) write is possible).
 *
 * When LEN==BOUND, the array is full and writes are discarded.
 *
 * When the ARRAY fills, the last few characters are overwritten with
 * DOTS.
 */

struct lswlog {
	char *array;
	/* 0 <= LEN < BOUND < ROOF */
	size_t len;
	size_t bound;
	size_t roof;
	const char *dots;
};

struct lswlog *lswlog(struct lswlog *buf, char *array, size_t sizeof_array);

/*
 * To debug, set this to printf or similar.
 */
extern int (*lswlog_debugf)(const char *format, ...) PRINTF_LIKE(1);

/*
 * Since 'char' can be unsigned need to cast -2 onto a char sized
 * value.
 *
 * The octal equivalent would be something like '\376' but who uses
 * octal :-)
 */
#define LSWBUF_CANARY ((char) -2)

#endif /* _LSWLOG_H_ */
