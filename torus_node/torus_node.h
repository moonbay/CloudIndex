/*
 * torus_node.h
 *
 *  Created on: Sep 16, 2013
 *      Author: meibenjin
 */

#ifndef TORUS_NODE_H_
#define TORUS_NODE_H_

#include"../utils.h"

// torus node info 
typedef struct node_info
{
	// torus node ip address
	char ip[MAX_IP_ADDR_LENGTH];

	// torus node coordinate
    struct coordinate id;

    // number of neighbors
    int neighbors_num;

}node_info;

typedef struct torus_node
{
    // torus node info
    struct node_info info;

	// torus node's neighbors 
	struct torus_node *neighbors[MAX_NEIGHBORS];
}torus_node;

struct torus_node local_torus_node;

// set torus node ip
void set_node_ip(torus_node *node_ptr, const char *ip);

void get_node_ip(torus_node node, char *ip);

// set the coordinate of a torus node
void set_coordinate(torus_node *node_ptr, int x, int y, int z);

// set the neighbors num of a torus node
void set_neighbors_num(torus_node *node_ptr, int neighbors_num);

int get_neighbors_num(torus_node node);

void print_coordinate(torus_node node);

void init_torus_node(torus_node *node_ptr);


#endif /* TORUS_NODE_H_ */


