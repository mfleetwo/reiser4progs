/*
  nodeptr40_repair.c -- repair default node pointer item plugin methods.
  
  Copyright (C) 2001, 2002 by Hans Reiser, licensing governed by
  reiser4progs/COPYING.
*/

#include "nodeptr40.h"

int32_t nodeptr40_layout_check(item_entity_t *item, region_func_t func, 
    void *data) 
{
    nodeptr40_t *nodeptr;
    blk_t blk;
    int res;
	
    aal_assert("vpf-721", item != NULL, return -1);

    nodeptr = nodeptr40_body(item);

    blk = np40_get_ptr(nodeptr);    
    res = func(item, blk, blk + 1, data);
    
    if (res > 0) 
	return item->len;
    else if (res < 0)
	return res;

    return 0;
}