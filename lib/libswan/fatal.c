/* log wrapper, for libreswan
 *
 * Copyright (C) 2018 Andrew Cagney
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

#include <stdio.h>
#include <stdarg.h>

#include "fatal.h"

#include "constants.h"		/* for enum pluto_exit_code */
#include "lswlog.h"		/* for LOG_WIDTH et.al. */

void fatal(enum pluto_exit_code pec, const struct logger *logger, const char *fmt, ...)
{
	struct barfbuf barfbuf;
	struct jambuf *buf = jambuf_from_barfbuf(&barfbuf, logger, pec, NULL/*where*/, FATAL_FLAGS);
	{
		va_list ap;
		va_start(ap, fmt);
		jam_va_list(buf, fmt, ap);
		va_end(ap);
	}
	fatal_barfbuf_to_logger(&barfbuf);
}

void fatal_errno(enum pluto_exit_code pec, const struct logger *logger,
		 int error, const char *fmt, ...)
{
	struct barfbuf barfbuf;
	struct jambuf *buf = jambuf_from_barfbuf(&barfbuf, logger, pec, NULL/*where*/, FATAL_FLAGS);
	{
		va_list ap;
		va_start(ap, fmt);
		jam_va_list(buf, fmt, ap);
		va_end(ap);
		jam_string(buf, ": ");
		jam_errno(buf, error);
	}
	fatal_barfbuf_to_logger(&barfbuf);
}

void fatal_barfbuf_to_logger(struct barfbuf *barfbuf)
{
	barfbuf_to_logger(barfbuf);
	libreswan_exit(barfbuf->barf.pec);
}
