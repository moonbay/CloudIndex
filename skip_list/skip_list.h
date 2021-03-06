/*
 * skip_list.h
 *
 *  Created on: Oct 2, 2013
 *      Author: meibenjin
 */

#ifndef SKIP_LIST_H_
#define SKIP_LIST_H_

#include"utils.h"

int overlaps(interval c[], interval o[]);

data_type get_distance(interval c, interval o);

// compare two torus_node by time interval
int compare(interval cinterval, interval ointerval);

int interval_overlap(interval cinterval, interval ointerval);

// random choose the skip list level
int random_level();

// create skip_list list
skip_list *new_skip_list(int level);

// create a skip list node with LEADER_NUM  torus nodes
skip_list_node *new_skip_list_node(int level, node_info *leaders);

// insert a torus_node into skip_list
int insert_skip_list(skip_list *slist, node_info *node_ptr);

// remove a torus_node from skip_list
int remove_skip_list(skip_list *slist, node_info *node_ptr);

// find out a torus_node from skip_list
//int search_skip_list(skip_list *slist, interval time_interval);

node_info *search_skip_list(skip_list *slist, interval time_interval);

// traverse the skip_list
void print_skip_list(skip_list *slist);

void print_skip_list_node(skip_list *slist);

#endif /* SKIP_LIST_H_ */



