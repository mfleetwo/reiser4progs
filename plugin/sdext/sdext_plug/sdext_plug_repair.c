/* Copyright (C) 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by
   reiser4progs/COPYING.
   
   sdext_lt_repair.c -- large time stat data extension plugin recovery code. */

#ifndef ENABLE_STAND_ALONE
#include "sdext_plug.h"
#include <repair/plugin.h>

char *opset_name[OPSET_STORE_LAST] = {
	[OPSET_OBJ] =	  "object    ",
	[OPSET_DIR] =	  "directory ",
	[OPSET_PERM] =	  "permission",
	[OPSET_POLICY] =  "formatting",
	[OPSET_HASH] =	  "hash      ",
	[OPSET_FIBRE] =   "fibration ",
	[OPSET_STAT] =	  "statdata  ",
	[OPSET_DIRITEM] = "diritem   ",
	[OPSET_CRYPTO] =  "crypto    ",
	[OPSET_DIGEST] =  "digest    ",
	[OPSET_COMPRES] = "compress  "
};

errno_t sdext_plug_check_struct(stat_entity_t *stat, repair_hint_t *hint) {
	sdhint_plug_t plugh;
	sdext_plug_t *ext;
	uint64_t mask = 0;
	int8_t i, count;
	int8_t remove;
	uint32_t len;
	void *dst;
	
	ext = (sdext_plug_t *)stat_body(stat);
	count = sdext_plug_get_count(ext);
	len = sdext_plug_length(stat, NULL);
	
	if (stat->offset + len < stat->place->len) {
		aal_error("Node (%llu), item (%u): does not look like a "
			  "valid plug extention: wrong count of plugins "
			  "detected (%u).", place_blknr(stat->place),
			  stat->place->pos.item, count);
		return RE_FATAL;
	}
	    
	aal_memset(&plugh, 0, sizeof(plugh));
	remove = 0;
	
	for (i = 0; i < count; i++) {
		rid_t mem, id;

		mem = sdext_plug_get_member(ext, i);
		id = sdext_plug_get_pid(ext, i);

		if (mem >= OPSET_STORE_LAST) {
			/* Unknown member. */
			aal_error("Node (%llu), item (%u): the slot (%u) "
				  "contains the invalid opset member (%u).",
				  place_blknr(stat->place), 
				  stat->place->pos.item, i, mem);

			mask |= (1 << i);
			remove++;
		} else if (plugh.plug[mem]) {
			/* Was met already. */
			aal_error("Node (%llu), item (%u): the slot (%u) "
				  "contains the opset member (%s) that was "
				  "met already.", place_blknr(stat->place), 
				  stat->place->pos.item, i, opset_name[mem]);

			mask |= (1 << i);
			remove++;
		} else {
			/* Obtain the plugin. */
			plugh.plug[mem] = 
				sdext_plug_core->pset_ops.find(mem, id);

			/* Check if the member is valid. */
			if (plugh.plug[mem] == INVAL_PTR) {
				aal_error("Node (%llu), item (%u): the slot "
					  "(%u) contains the invalid opset "
					  "member (%s), id (%u).",
					  place_blknr(stat->place), 
					  stat->place->pos.item, i, 
					  opset_name[mem], id);
				
				mask |= (1 << i);
				remove++;
			} else if (!plugh.plug[mem]) {
				/* For those members where no one plugin is 
				   written, set INVAL_PTR to avoid meeting it 
				   another time. */
				plugh.plug[mem] = INVAL_PTR;
			}
		}
	}

	if (!mask) 
		return 0;
	
	/* Some broken slots are found. */
	if (hint->mode != RM_BUILD)
		return RE_FATAL;

	if (remove == count) {
		aal_error("Node (%llu), item (%u): no slot left. Does "
			  "not look like a valid (%s) statdata extention.",
			  place_blknr(stat->place), stat->place->pos.item,
			  stat->ext_plug->label);
		return RE_FATAL;
	}
	
	/* Removing broken slots. */
	aal_error("Node (%llu), item (%u): removing broken slots.",
		  place_blknr(stat->place), stat->place->pos.item);
	
	dst = stat_body(stat) + sizeof(sdext_plug_t) + 
		(count - 1) * (sizeof(sdext_plug_slot_t));
	
	for (i = count - 1; i >= 0; i--, dst -= sizeof(sdext_plug_slot_t)) {
		if (!(mask & (1 << i)))
			continue;

		aal_memmove(dst, dst + sizeof(sdext_plug_slot_t),
			    len - (dst - (void *)stat_body(stat)) - 
			    sizeof(sdext_plug_slot_t));
	}
	
	hint->len = remove * sizeof(sdext_plug_slot_t);
	
	return 0;
}

void sdext_plug_print(stat_entity_t *stat, 
		      aal_stream_t *stream, 
		      uint16_t options) 
{
	reiser4_plug_t *plug;
	sdext_plug_t *ext;
	uint16_t i;

	aal_assert("vpf-1603", ext != NULL);
	aal_assert("vpf-1604", stream != NULL);
	
	ext = (sdext_plug_t *)stat_body(stat);

	aal_stream_format(stream, "Pset count: \t%u\n",
			  sdext_plug_get_count(ext));

	for (i = 0; i < sdext_plug_get_count(ext); i++) {
		rid_t mem, id;

		mem = sdext_plug_get_member(ext, i);
		id = sdext_plug_get_pid(ext, i);
		
		plug = sdext_plug_core->pset_ops.find(mem, id);

		aal_stream_format(stream, "    %s : id = %u",
				  opset_name[mem], id);

		if (plug && plug != INVAL_PTR) 
			aal_stream_format(stream, " (%s)\n", plug->label);
		else
			aal_stream_format(stream, "\n");
	}
}

#endif