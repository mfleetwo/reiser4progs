/*
  dir40.h -- reiser4 hashed directory plugin structures.

  Copyright (C) 2001, 2002, 2003 by Hans Reiser, licensing governed by
  reiser4progs/COPYING.
*/

#ifndef DIR40_H
#define DIR40_H

#include <aal/aal.h>
#include <sys/stat.h>
#include <reiser4/plugin.h>
#include <plugin/object/object40/object40.h>

/* Compaund directory structure */
struct dir40 {
	
	/* Common fields for all files (statdata, etc) */
	object40_t obj;

	/* Current body item coord */
	place_t body;

	/* Current position in the directory */
	uint32_t offset;

	/* Hash plugin in use */
	reiser4_plugin_t *hash;
};

typedef struct dir40 dir40_t;

#endif
