/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
   reiser4progs/COPYING.
   
   cde40_repair.c -- reiser4 default direntry plugin.
   
   Description:
   1) If offset is obviously wrong (no space for cde40_t,entry_t's, 
   objid40_t's) mark it as not relable - NR.
   2) If 2 offsets are not NR and entry contents between them are valid,
   (probably even some offsets between them can be recovered also) mark 
   all offsets beteen them as relable - R.
   3) If pair of offsets does not look ok and left is R, mark right as NR.
   4) If there is no R offset on the left, compare the current with 0-th 
   through the current if not NR.
   5) If there are some R offsets on the left, compare with the most right
   of them through the current if not NR. 
   6) If after 1-5 there is no one R - this is likely not to be cde40
   item.
   7) If there is a not NR offset and it has a neighbour which is R and
   its pair with the nearest R offset from the other side if any is ok,
   mark it as R. ?? Disabled. ??
   8) Remove all units with not R offsets.
   9) Remove the space between the last entry_t and the first R offset.
   10)Remove the space between the end of the last entry and the end of 
   the item. */

#ifndef ENABLE_STAND_ALONE
#include "cde40.h"
#include <aux/bitmap.h>
#include <repair/plugin.h>

#define S_NAME	0
#define L_NAME	1

/* short name is all in the key, long is 16 + '\0' */
#define NAME_LEN_MIN(kind)        \
        (kind ? 16 + 1: 0)

#define ENTRY_LEN_MIN(kind, pol)  \
        (ob_size(pol) + NAME_LEN_MIN(kind))

#define UNIT_LEN_MIN(kind, pol)        \
        (en_size(pol) + ENTRY_LEN_MIN(kind, pol))

#define DENTRY_LEN_MIN		  \
        (UNIT_LEN_MIN(S_NAME) + sizeof(cde40_t))

#define en_len_min(count, pol)    \
        ((uint64_t)count * UNIT_LEN_MIN(S_NAME, pol) + sizeof(cde40_t))

#define OFFSET(pl, i, pol)        \
        (uint32_t)((en_get_offset(cde_get_entry(pl, i, pol), pol)))

#define NR	0
#define R	1

struct entry_flags {
	uint8_t *elem;
	uint8_t count;
};
    
/* Extention for repair_flag_t */
#define REPAIR_SKIP	0
    
extern int32_t cde40_remove(place_t *place, uint32_t pos, 
			    uint32_t count);

extern errno_t cde40_get_key(place_t *place, uint32_t pos, 
			     key_entity_t *key);

extern lookup_t cde40_lookup(place_t *place, key_entity_t *key,
			     uint32_t *pos);

extern uint32_t cde40_size_units(place_t *place, uint32_t pos, 
				 uint32_t count);

extern uint32_t cde40_units(place_t *place);

extern errno_t cde40_maxposs_key(place_t *place,
				 key_entity_t *key);

extern errno_t cde40_rep(place_t *dst_place, uint32_t dst_pos,
			 place_t *src_place, uint32_t src_pos,
			 uint32_t count);

extern int32_t cde40_expand(place_t *place, uint32_t pos,
			    uint32_t count, uint32_t len);

extern inline uint32_t cde40_key_pol(place_t *place);

/* Check the i-th offset of the unit body within the item. */
static errno_t cde40_offset_check(place_t *place, uint32_t pos) {
	uint32_t pol;
	uint32_t offset;
    
	pol = cde40_key_pol(place);
	
	if (place->len < en_len_min(pos + 1, pol))
		return 1;

	offset = OFFSET(place, pos, pol);
    
	/* There must be enough space for the entry in the item. */
	if (offset != place->len - ENTRY_LEN_MIN(S_NAME, pol) && 
	    offset != place->len - 2 * ENTRY_LEN_MIN(S_NAME, pol) && 
	    offset > place->len - ENTRY_LEN_MIN(L_NAME, pol))
	{
		return 1;
	}
	
	/* There must be enough space for item header, set of unit headers and 
	   set of keys of objects entries point to. */
	return pos != 0 ?
		(offset < sizeof(cde40_t) + en_size(pol) * (pos + 1) + 
		 ob_size(pol) * pos) :
		(offset - sizeof(cde40_t)) % (en_size(pol)) != 0;
	
	/* If item was shorten, left entries should be recovered also. 
	   So this check is excessive as it can avoid such recovering 
	   if too many entry headers are left.
	 if (pos == 0) {
	 uint32_t count;
	
	 count = (offset - sizeof(cde40_t)) / en_size(pol);
		
	 if (offset + count * ENTRY_LEN_MIN(S_NAME, pol) > place->len)
	 return 1;
	
	 }
	*/
}

static uint32_t cde40_count_estimate(place_t *place, uint32_t pos) {
	uint32_t pol = cde40_key_pol(place);
	uint32_t offset = OFFSET(place, pos, pol);
	
	aal_assert("vpf-761", place->len >= en_len_min(pos + 1, pol));
	
	return pos == 0 ?
		(offset - sizeof(cde40_t)) / en_size(pol) :
		((offset - pos * ob_size(pol) - sizeof(cde40_t)) / 
		 en_size(pol));
}

/* Check that 2 neighbour offsets look coorect. */
static errno_t cde40_pair_offsets_check(place_t *place, 
					uint32_t start_pos, 
					uint32_t end_pos) 
{    
	uint32_t pol;
	uint32_t offset, end_offset;
	uint32_t count = end_pos - start_pos;
	
	pol = cde40_key_pol(place);
	
	aal_assert("vpf-753", start_pos < end_pos);
	aal_assert("vpf-752", place->len >= en_len_min(end_pos + 1, pol));

	end_offset = OFFSET(place, end_pos, pol);
	
	if (end_offset == OFFSET(place, start_pos, pol) +
	    ENTRY_LEN_MIN(S_NAME, pol) * count)
	{
		return 0;
	}
	
	offset = OFFSET(place, start_pos, pol) +
		ENTRY_LEN_MIN(L_NAME, pol);
	
	return (end_offset < offset + (count - 1) * ENTRY_LEN_MIN(S_NAME, pol));
}

static uint32_t cde40_name_end(char *body, uint32_t start, uint32_t end) {
	uint32_t i;
	
	aal_assert("vpf-759", start < end);
	
	for (i = start; i < end; i++) {
		if (body[i] == '\0')
			break;
	}
	
	return i;
}

/* Returns amount of entries detected. */
static uint8_t cde40_short_entry_detect(place_t *place, 
					uint32_t start_pos, 
					uint32_t length, 
					uint8_t mode)
{
	uint32_t pol;
	uint32_t offset, limit;

	pol = cde40_key_pol(place);
	limit = OFFSET(place, start_pos, pol);
	
	aal_assert("vpf-770", length < place->len);
	aal_assert("vpf-769", limit <= place->len - length);
	
	if (length % ENTRY_LEN_MIN(S_NAME, pol))
		return 0;
	
	if (mode == REPAIR_SKIP)
		return length / ENTRY_LEN_MIN(S_NAME, pol);
	
	for (offset = ENTRY_LEN_MIN(S_NAME, pol); offset < length; 
	     offset += ENTRY_LEN_MIN(S_NAME, pol), start_pos++) 
	{
		aal_exception_error("Node (%llu), item (%u), unit (%u): unit "
				    "offset (%u) is wrong, should be (%u). %s", 
				    place->block->nr, place->pos.item,
				    start_pos, OFFSET(place, start_pos, pol),
				    limit + offset,  mode == RM_BUILD ? "Fixed." : "");
		
		if (mode == RM_BUILD) {
			void *entry = cde_get_entry(place, start_pos, pol);
			en_set_offset(entry, limit + offset, pol);
		}
	}
	
	return length / ENTRY_LEN_MIN(S_NAME, pol);
}

/* Returns amount of entries detected. */
static uint8_t cde40_long_entry_detect(place_t *place, 
				       uint32_t start_pos, 
				       uint32_t length, 
				       uint8_t mode)
{
	uint32_t pol;
	int count = 0;
	uint32_t offset;
	uint32_t l_limit;
	uint32_t r_limit;

	pol = cde40_key_pol(place);
	
	aal_assert("umka-2405", place != NULL);
	aal_assert("vpf-771", length < place->len);
	
	aal_assert("vpf-772", OFFSET(place, start_pos, pol)
		   <= (place->len - length));
	
	l_limit = OFFSET(place, start_pos, pol);
	r_limit = l_limit + length;
	
	while (l_limit < r_limit) {
		offset = cde40_name_end(place->body, l_limit +
					ob_size(pol), r_limit);
		
		if (offset == r_limit)
			return 0;
		
		offset++;
		
		if (offset - l_limit < ENTRY_LEN_MIN(L_NAME, pol))
			return 0;
		
		l_limit = offset;
		count++;
		
		if (mode != REPAIR_SKIP && 
		    l_limit != OFFSET(place, start_pos + count, pol)) 
		{
			aal_exception_error("Node %llu, item %u, unit (%u): unit "
					    "offset (%u) is wrong, should be (%u). "
					    "%s", place->block->nr, place->pos.item,
					    start_pos + count,
					    OFFSET(place, start_pos + count, pol),
					    l_limit, mode == RM_BUILD ? "Fixed." : "");
			
			if (mode == RM_BUILD) {
				void *entry = cde_get_entry(place, start_pos +
							    count, pol);
				
				en_set_offset(entry, l_limit, pol);
			}
		}
	}
	
	return l_limit == r_limit ? count : 0;
}

static inline uint8_t cde40_entry_detect(place_t *place, uint32_t start_pos,
					 uint32_t end_pos, uint8_t mode)
{
	uint32_t pol;
	uint8_t count;

	pol = cde40_key_pol(place);
	count = cde40_short_entry_detect(place, start_pos,
					 OFFSET(place, end_pos, pol) - 
					 OFFSET(place, start_pos, pol), 0);
    
	if (count == end_pos - start_pos) {
		cde40_short_entry_detect(place, start_pos, 
					 OFFSET(place, end_pos, pol) - 
					 OFFSET(place, start_pos, pol), mode);
		
		return count;
	}
	
	count = cde40_long_entry_detect(place, start_pos, 
					OFFSET(place, end_pos, pol) - 
					OFFSET(place, start_pos, pol), 0);
	
	if (count == end_pos - start_pos) {
		cde40_long_entry_detect(place, start_pos, 
					OFFSET(place, end_pos, pol) - 
					OFFSET(place, start_pos, pol), mode);
		
		return count;
	}
	
	return 0;
}

/* Build a bitmap of not R offstes. */
static errno_t cde40_offsets_range_check(place_t *place, 
					 struct entry_flags *flags, 
					 uint8_t mode) 
{
	uint32_t pol;
	uint32_t i, j;
	errno_t res = 0;
	uint32_t to_compare;
	
	aal_assert("vpf-757", flags != NULL);

	pol = cde40_key_pol(place);
	to_compare = MAX_UINT32;
	
	for (i = 0; i < flags->count; i++) {
		/* Check if the offset is valid. */
		if (cde40_offset_check(place, i)) {
			aal_exception_error("Node %llu, item %u, unit %u: unit "
					    "offset (%u) is wrong.", 
					    place->block->nr, place->pos.item, 
					    i, OFFSET(place, i, pol));
			
			/* mark offset wrong. */	    
			aal_set_bit(flags->elem + i, NR);
			continue;
		}
		
		/* If there was not any R offset, skip pair comparing. */
		if (to_compare == MAX_UINT32) {
			if ((i == 0) && (cde40_count_estimate(place, i) == 
					 cde_get_units(place)))
			{
				flags->count = cde_get_units(place);
				aal_set_bit(flags->elem + i, R);
			}
			
			to_compare = i;
			continue;
		}
		
		for (j = to_compare; j < i; j++) {
			/* If to_compare is a R element, do just 1 comparing.
			   Otherwise, compare with all not NR elements. */
			if (aal_test_bit(flags->elem + j, NR))
				continue;
			
			/* Check that a pair of offsets is valid. */
			if (!cde40_pair_offsets_check(place, j, i)) {
				/* Pair looks ok. Try to recover it. */
				if (cde40_entry_detect(place, j, i, mode)) {
					uint32_t limit;
					
					/* Do not compair with elements before 
					   the last R. */
					to_compare = i;
					
					/* It's possible to decrease the count 
					   when first R found. */
					limit = cde40_count_estimate(place, j);
					
					if (flags->count > limit)
						flags->count = limit;
					
					/* If more then 1 item were detected, 
					   some offsets have been recovered, 
					   set result properly. */
					if (i - j > 1) {
						if (mode == RM_BUILD)
							place_mkdirty(place);
						else
							res |= RE_FATAL;
					}
					
					/* Mark all recovered elements as R. */
					for (j++; j <= i; j++)
						aal_set_bit(flags->elem + j, R);
					
					break;
				}
				
				continue;
			}
			
			/* Pair does not look ok, if left is R offset, this is 
			   NR offset. */
			if (aal_test_bit(flags->elem + j, R)) {
				aal_set_bit(flags->elem + i, NR);
				break;
			}
		}
	}
	
	return res;
}

static errno_t cde40_filter(place_t *place, struct entry_flags *flags,
			    uint8_t mode)
{
	uint32_t pol;
	uint32_t i, last;
	uint32_t e_count;
	errno_t res = 0;
	
	aal_assert("vpf-757", flags != NULL);

	pol = cde40_key_pol(place);
	for (last = flags->count; 
	     last && (aal_test_bit(flags->elem + last - 1, NR) || 
		      !aal_test_bit(flags->elem + last - 1, R)); last--) {}
	
	if (last == 0) {
		/* No one R unit was found */
		aal_exception_error("Node %llu, item %u: no one valid unit has "
				    "been found. Does not look like a valid `%s` "
				    "item.", place->block->nr, place->pos.item, 
				    place->plug->label);
		
		return RE_FATAL;
	}
	
	flags->count = --last;
	
	/* Last is the last valid offset. If the last unit is valid also, count 
	   is the last + 1. */
	if (OFFSET(place, last, pol) + ob_size(pol) == place->len) {
		flags->count++;
	} else if (OFFSET(place, last, pol) + ob_size(pol) < place->len) {
		uint32_t offset;
		
		/* The last offset is correct,but the last entity is not checked yet. */
		offset = cde40_name_end(place->body, OFFSET(place, last, pol) + 
					ob_size(pol), place->len);
		if (offset == place->len - 1)
			flags->count++;
	}
	
	/* Count is the amount of recovered elements. */
	
	/* Find the first relable. */
	for (i = 0; i < flags->count && !aal_test_bit(flags->elem + i, R); i++) {}
	
	/* Estimate the amount of units on the base of the first R element. */
	e_count = cde40_count_estimate(place, i);
	
	/* Estimated count must be less then count found on the base of the last 
	 * valid offset. */
	aal_assert("vpf-765", e_count >= flags->count);
	
	/* If there is enough space for another entry header, and the @last entry 
	   is valid also, set @count unit offset to the item length. */
	if (e_count > flags->count && last != flags->count) {
		if (mode == RM_BUILD) {
			void *entry = cde_get_entry(place, flags->count, pol);
			en_set_offset(entry, place->len, pol);
			place_mkdirty(place);
		} else {
			res |= RE_FATAL;
		}
	}
 	
	if (flags->count == last && mode == RM_BUILD) {
		/* Last unit is not valid. */
		if (mode == RM_BUILD) {
			place->len = OFFSET(place, last, pol);
			place_mkdirty(place);
		} else {
			res |= RE_FATAL;
		}
	}
	
	if (i) {
		/* Some first offset are not relable. Consider count as 
		   the correct count and set the first offset just after 
		   the last unit.*/
		e_count = flags->count;
		
		if (mode == RM_BUILD) {
			void *entry = cde_get_entry(place, 0, pol);
			en_set_offset(entry, sizeof(cde40_t) +
				      en_size(pol) * flags->count, pol);
			place_mkdirty(place);
		}
	}
	
	if (e_count != cde_get_units(place)) {
		aal_exception_error("Node %llu, item %u: unit count (%u) "
				    "is not correct. Should be (%u). %s",
				    place->block->nr,  place->pos.item,
				    cde_get_units(place), e_count, 
				    mode == RM_CHECK ? "" : "Fixed.");
		
		if (mode == RM_CHECK) {
			res |= RE_FIXABLE;
		} else {
			cde_set_units(place, e_count);
			place_mkdirty(place);
		}
	}
	
	if (flags->count != e_count) {
		/* Estimated count is greater then the recovered count, in other 
		   words there are some last unit headers should be removed. */
		aal_exception_error("Node %llu, item %u: entries [%u..%u] look "
				    "corrupted. %s", place->block->nr,
				    place->pos.item, flags->count, e_count - 1, 
				    mode == RM_BUILD ? "Removed." : "");
		
		if (mode == RM_BUILD) {
			if (cde40_remove(place, flags->count, 
					 e_count - flags->count) < 0) 
			{
				aal_exception_error("Node %llu, item %u: remove "
						    "of the unit (%u), count (%u) "
						    "failed.", place->block->nr, 
						    place->pos.item, flags->count, 
						    e_count - flags->count);
				return -EINVAL;
			}
			
			place_mkdirty(place);
		} else {
			res |= RE_FATAL;
		}
	}
	
	if (i) {
		/* Some first units should be removed. */
		aal_exception_error("Node %llu, item %u: entries [%u..%u] look "
				    " corrupted. %s", place->block->nr, 
				    place->pos.item, 0, i - 1, 
				    mode == RM_BUILD ? "Removed." : "");
		
		if (mode == RM_BUILD) {
			if (cde40_remove(place, 0, i) < 0) {
				aal_exception_error("Node %llu, item %u: remove of "
						    "the unit (%u), count (%u) "
						    "failed.", place->block->nr,
						    place->pos.item, 0, i);
				return -EINVAL;
			}
			
			place_mkdirty(place);
			aal_memmove(flags->elem, flags->elem + i, 
				    flags->count - i);
			
			flags->count -= i;
			i = 0;
		} else {
			res |= RE_FATAL;
		}
	}
	
	/* Units before @i and after @count were handled, do not care about them 
	   anymore. Handle all not relable units between them. */
	last = MAX_UINT32;
	for (; i < flags->count; i++) {
		if (last == MAX_UINT32) {
			/* Looking for the problem interval start. */
			if (!aal_test_bit(flags->elem + i, R))
				last = i - 1;
			
			continue;
		}
		
		/* Looking for the problem interval end. */
		if (aal_test_bit(flags->elem + i, R)) {
			aal_exception_error("Node %llu, item %u: entries "
					    "[%u..%u] look corrupted. %s", 
					    place->block->nr, place->pos.item,
					    last, i - 1, mode == RM_BUILD ? 
					    "Removed." : "");

			if (mode != RM_BUILD) {
				res |= RE_FATAL;
				last = MAX_UINT32;
				continue;
			}

			if (cde40_remove(place, last, i - last) < 0) {
				aal_exception_error("Node %llu, item %u: remove"
						    "of unit (%u), count (%u) "
						    "failed.", place->block->nr,
						    place->pos.item, last, 
						    i - last);
				return -EINVAL;
			}

			aal_memmove(flags->elem + last, flags->elem + i,
				    flags->count - i);

			flags->count -= (i - last);
			i = last;
			last = MAX_UINT32;

			place_mkdirty(place);
		}
	}
	
	aal_assert("vpf-766", cde_get_units(place));
	
	return res;
}

errno_t cde40_check_struct(place_t *place, uint8_t mode) {
	struct entry_flags flags;
	uint32_t pol;
	errno_t res = 0;
	int i;
	
	aal_assert("vpf-267", place != NULL);

	pol = cde40_key_pol(place);
	
	if (place->len < en_len_min(1, pol)) {
		aal_exception_error("Node %llu, item %u: item length (%u) is too "
				    "small to contain a valid item.", 
				    place->block->nr, place->pos.item, place->len);
		return RE_FATAL;
	}
	
	/* Try to recover even if item was shorten and not all entries exist. */
	flags.count = (place->len - sizeof(cde40_t)) / (en_size(pol));
	
	/* map consists of bit pairs - [not relable -R, relable - R] */
	flags.elem = aal_calloc(flags.count, 0);
	
	res |= cde40_offsets_range_check(place, &flags, mode);
	
	if (repair_error_exists(res))
		goto error;
	
	/* Filter units with relable offsets from others. */
	res |= cde40_filter(place, &flags, mode);

	aal_free(flags.elem);
	
	/* Check order of entries -- FIXME-VITALY: just simple check for now, 
	   the whole item is thrown away if smth wrong, to be improved later. */
	for (i = 1; i < flags.count; i++) {
		void *prev_hash;
		void *curr_hash;

		prev_hash = cde_get_entry(place, i - 1, pol);
		curr_hash = cde_get_entry(place, i, pol);
		
		if (aal_memcmp(prev_hash, curr_hash, 
			       ha_size(pol)) == 1)
		{
			aal_exception_error("Node (%llu), item (%u): wrong "
					    "order of units {%d, %d}. The whole"
					    "item has to be removed -- will be "
					    "improved soon.", place->block->nr, 
					    place->pos.item, i - 1, i);
			return res & RE_FATAL;
		}
	}
	
	return res;
	
 error:
	aal_free(flags.elem);
	return res;
}

errno_t cde40_estimate_copy(place_t *dst, uint32_t dst_pos,
			    place_t *src, uint32_t src_pos, 
			    copy_hint_t *hint)
{
	uint32_t units, next_pos, pos;
	key_entity_t dst_key;
	lookup_t lookup;
	
	aal_assert("vpf-957", dst  != NULL);
	aal_assert("vpf-958", src  != NULL);
	aal_assert("vpf-959", hint != NULL);
	
	units = cde40_units(src);
	
	lookup = cde40_lookup(src, &hint->end, &pos);
	if (lookup == FAILED)
		return -EINVAL;
	
	cde40_get_key(dst, dst_pos, &dst_key);
	cde40_lookup(src, &dst_key, &next_pos);
	
	if (pos < next_pos)
		next_pos = pos;
	
	aal_assert("vpf-1015", next_pos >= src_pos);
	
	/* FIXME-VITALY: Key collisions are not supported yet. */
	
	hint->src_count = next_pos - src_pos;
	hint->dst_count = 0;
	hint->len_delta = (en_size(cde40_key_pol(dst)) * hint->src_count) +
		cde40_size_units(src, src_pos, hint->src_count);
	
	while (next_pos < units) {
		cde40_get_key(src, next_pos, &hint->end);
		lookup = cde40_lookup(dst, &hint->end, &pos);
		
		if (lookup == FAILED)
			return -EINVAL;
		
		if (lookup == ABSENT)
			return 0;
		
		next_pos++;
	}
	
	cde40_maxposs_key(src, &hint->end);
	
	return 0;
}

errno_t cde40_copy(place_t *dst, uint32_t dst_pos, 
			place_t *src, uint32_t src_pos, 
			copy_hint_t *hint)
{
	aal_assert("vpf-1014", dst != NULL);
	aal_assert("vpf-1013", src != NULL);
	aal_assert("vpf-1012", hint != NULL);
	aal_assert("vpf-1011", hint->dst_count == 0);
	
	/* Preparing root for copying units into it */
	cde40_expand(dst, dst_pos, hint->src_count, hint->len_delta);
	return cde40_rep(dst, dst_pos, src, src_pos, hint->src_count);
}
#endif