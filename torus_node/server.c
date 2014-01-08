/*
 * server.c
 *
 *  Created on: Sep 24, 2013
 *      Author: meibenjin
 */

__asm__(".symver memcpy,memcpy@GLIBC_2.2.5");

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<time.h>

extern "C" {
#include"server.h"
#include"logs/log.h"
#include"torus_node/torus_node.h"
#include"skip_list/skip_list.h"
#include"socket/socket.h"
};

#include"torus_rtree.h"

//define torus server 
/*****************************************************************************/

torus_node *the_torus;
struct torus_partitions the_partition;

ISpatialIndex* the_torus_rtree;

// mark torus node is active or not 
int should_run;

struct skip_list *the_skip_list;

struct request *req_list;


char result_ip[IP_ADDR_LENGTH] = "172.16.0.83";

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

// list for collect result
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
typedef struct query {
    char stamp[STAMP_SIZE];
    struct timespec start;
    struct query *next;
}query;

struct query *query_list;

query *new_query() {
    query *query_ptr;
    query_ptr = (struct query *) malloc(sizeof(query));
    if (query_ptr == NULL) {
        printf("malloc request list failed.\n");
        return NULL;
    }

    query_ptr->next = NULL;
    return query_ptr;
}

query *find_query(query *list, const char *query_stamp) {
    query *query_ptr = list->next;
    while (query_ptr) {
        if (strcmp(query_ptr->stamp, query_stamp) == 0) {
            return query_ptr;
        }
        query_ptr = query_ptr->next;
    }
    return NULL;
}

query  *insert_query(query *list, const char *query_stamp, struct timespec tspec) {
    struct query *new_q = new_query();
    if (new_q == NULL) {
        printf("insert_query: allocate new query failed.\n");
        return NULL;
    }
    strncpy(new_q->stamp, query_stamp, STAMP_SIZE);
    new_q->start = tspec;

    query *query_ptr = list;
    new_q->next = query_ptr->next;
    query_ptr->next = new_q;

    return new_q;
}

int remove_query(query *list, const char *query_stamp) {
    struct query *pre_ptr = list;
    struct query *query_ptr = list->next;
    while (query_ptr) {
        if (strcmp(query_ptr->stamp, query_stamp) == 0) {
            pre_ptr->next = query_ptr->next;
            free(query_ptr);
            query_ptr = NULL;
            return TRUE;
        }
        pre_ptr = pre_ptr->next;
        query_ptr = query_ptr->next;
    }
    return FALSE;
}
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/


// torus server request list
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

request *new_request() {
	request *req_ptr;
	req_ptr = (struct request *) malloc(sizeof(request));
	if (req_ptr == NULL) {
		printf("malloc request list failed.\n");
		return NULL;
	}
	req_ptr->first_run = TRUE;
	req_ptr->receive_num = 0;
	memset(req_ptr->stamp, 0, STAMP_SIZE);
	req_ptr->next = NULL;
	return req_ptr;
}

request *find_request(request *list, const char *req_stamp) {
	request *req_ptr = list->next;
	while (req_ptr) {
		if (strcmp(req_ptr->stamp, req_stamp) == 0) {
			return req_ptr;
		}
		req_ptr = req_ptr->next;
	}
	return NULL;
}

request *insert_request(request *list, const char *req_stamp) {
	struct request *new_req = new_request();
	if (new_req == NULL) {
		printf("insert_request: allocate new request failed.\n");
		return NULL;
	}
	strncpy(new_req->stamp, req_stamp, STAMP_SIZE);

	request *req_ptr = list;
	new_req->next = req_ptr->next;
	req_ptr->next = new_req;

	return new_req;
}

int remove_request(request *list, const char *req_stamp) {
	struct request *pre_ptr = list;
	struct request *req_ptr = list->next;
	while (req_ptr) {
		if (strcmp(req_ptr->stamp, req_stamp) == 0) {
			pre_ptr->next = req_ptr->next;
			free(req_ptr);
			req_ptr = NULL;
			return TRUE;
		}
		pre_ptr = pre_ptr->next;
		req_ptr = req_ptr->next;
	}
	return FALSE;
}

int gen_request_stamp(char *stamp) {
	if (stamp == NULL) {
		printf("gen_request_stamp: stamp is null pointer.\n");
		return FALSE;
	}
	// TODO automatic generate number stamp
	static long number_stamp = 1;
	char ip_stamp[IP_ADDR_LENGTH];
	if (FALSE == get_local_ip(ip_stamp)) {
		return FALSE;
	}
	snprintf(stamp, STAMP_SIZE, "%s_%ld", ip_stamp, number_stamp++);
	return TRUE;
}

int do_traverse_torus(struct message msg) {
	char stamp[STAMP_SIZE];
	memset(stamp, 0, STAMP_SIZE);

	int neighbors_num = get_neighbors_num(the_torus);

	if (strcmp(msg.stamp, "") == 0) {
		if (FALSE == gen_request_stamp(stamp)) {
			return FALSE;
		}
		strncpy(msg.stamp, stamp, STAMP_SIZE);
		neighbors_num += 1;
	} else {
		strncpy(stamp, msg.stamp, STAMP_SIZE);
	}

	request *req_ptr = find_request(req_list, stamp);

	if (NULL == req_ptr) {
		req_ptr = insert_request(req_list, stamp);
		printf("traverse torus: %s -> %s\n", msg.src_ip, msg.dst_ip);

        #ifdef WRITE_LOG
            //write log
            char buf[1024];
            memset(buf, 0, 1024);
            sprintf(buf, "traverse torus: %s -> %s\n", msg.src_ip, msg.dst_ip);
            write_log(TORUS_NODE_LOG, buf);
        #endif

		if (req_ptr && (req_ptr->first_run == TRUE)) {
			forward_to_neighbors(msg);
			req_ptr->first_run = FALSE;
			req_ptr->receive_num = neighbors_num;
		}

	} else {
		req_ptr->receive_num--;
		if (req_ptr->receive_num == 0) {
			remove_request(req_list, stamp);
		}
	}

	return TRUE;
}

int forward_to_neighbors(struct message msg) {
	int d;
	char src_ip[IP_ADDR_LENGTH], dst_ip[IP_ADDR_LENGTH];

	//int neighbors_num = get_neighbors_num(the_torus);

	get_node_ip(the_torus->info, src_ip);
	for (d = 0; d < DIRECTIONS; ++d) {
        if(the_torus->neighbors[d] != NULL) {
            struct neighbor_node *nn_ptr;
            nn_ptr = the_torus->neighbors[d]->next;
            while(nn_ptr != NULL) {
                get_node_ip(*nn_ptr->info, dst_ip);
                strncpy(msg.src_ip, src_ip, IP_ADDR_LENGTH);
                strncpy(msg.dst_ip, dst_ip, IP_ADDR_LENGTH);
                forward_message(msg, 0);
                nn_ptr = nn_ptr->next;
            }
        }

	}
	return TRUE;
}

int do_update_torus(struct message msg) {
	int i, d;
    size_t cpy_len = 0;
	int num, neighbors_num = 0;

    // allocate for the_torus if it's NULL
    if(the_torus == NULL) {
        the_torus = new_torus_node();
    }

    // get torus partitons info from msg
    memcpy(&the_partition, (void *)msg.data, sizeof(struct torus_partitions));
    cpy_len += sizeof(struct torus_partitions);

    #ifdef WRITE_LOG 
        char buf[1024];
        sprintf(buf, "torus partitions:[%d %d %d]\n", the_partition.p_x,
                the_partition.p_y, the_partition.p_z);
        write_log(TORUS_NODE_LOG, buf);
    #endif

    // get local torus node from msg
    memcpy(&the_torus->info, (void *)(msg.data + cpy_len), sizeof(node_info));
    cpy_len += sizeof(node_info);

    // get neighbors info from msg 
    for(d = 0; d < DIRECTIONS; d++) {
        num = 0;
        memcpy(&num, (void *)(msg.data + cpy_len), sizeof(int));
        cpy_len += sizeof(int); 

        for(i = 0; i < num; i++) {
            // allocate space for the_torus's neighbor at direction d
            torus_node *new_node = new_torus_node();
            if (new_node == NULL) {
                return FALSE;
            }
            memcpy(&new_node->info, (void *)(msg.data + cpy_len), sizeof(node_info));
            cpy_len += sizeof(node_info);

            add_neighbor_info(the_torus, d, &new_node->info);
        }
        neighbors_num += num;
    }

    // set the_torus's neighbors_num
	set_neighbors_num(the_torus, neighbors_num);

	return TRUE;
}

int do_update_partition(struct message msg) {

	memcpy(&the_partition, msg.data, sizeof(struct torus_partitions));

    #ifdef WRITE_LOG 
        char buf[1024];
        sprintf(buf, "torus partitions:[%d %d %d]\n", the_partition.p_x,
                the_partition.p_y, the_partition.p_z);
        write_log(TORUS_NODE_LOG, buf);
    #endif

	return TRUE;
}

int do_update_skip_list(struct message msg) {
	int i, index, nodes_num, level;

	nodes_num = 0;
	memcpy(&nodes_num, msg.data, sizeof(int));
	if (nodes_num <= 0) {
		printf("do_update_skip_list: skip_list node number is wrong\n");
		return FALSE;
	}

	skip_list_node *sln_ptr, *new_sln;
	node_info nodes[nodes_num];

	for (i = 0; i < nodes_num; ++i) {
		memcpy(&nodes[i],
				(void*) (msg.data + sizeof(int) + sizeof(node_info) * i),
				sizeof(node_info));
	}

	level = (nodes_num / 2) - 1;
	index = 0;
	sln_ptr = the_skip_list->header;

	for (i = level; i >= 0; i--) {
		if (get_cluster_id(nodes[index]) != -1) {
			new_sln = new_skip_list_node(0, &nodes[index]);
			// free old forward field first
			if (sln_ptr->level[i].forward) {
				free(sln_ptr->level[i].forward);
			}
			sln_ptr->level[i].forward = new_sln;
			index++;
		} else {
			index++;
		}
		if (get_cluster_id(nodes[index]) != -1) {
			new_sln = new_skip_list_node(0, &nodes[index]);
			// free old forward field first
			if (sln_ptr->level[i].backward) {
				free(sln_ptr->level[i].backward);
			}
			sln_ptr->level[i].backward = new_sln;
			index++;
		} else {
			index++;
		}
	}
	return TRUE;
}

int do_traverse_skip_list(struct message msg) {
	//char buf[1024];
	char src_ip[IP_ADDR_LENGTH], dst_ip[IP_ADDR_LENGTH];
	skip_list_node *cur_sln, *forward, *backward;
	cur_sln = the_skip_list->header;
	forward = cur_sln->level[0].forward;
	backward = cur_sln->level[0].backward;

    #ifdef WRITE_LOG
        write_log(TORUS_NODE_LOG, "visit myself:");
    #endif
	print_node_info(cur_sln->leader);

	get_node_ip(cur_sln->leader, src_ip);
	strncpy(msg.src_ip, src_ip, IP_ADDR_LENGTH);
	if (strcmp(msg.data, "") == 0) {
		if (forward) {
			get_node_ip(forward->leader, dst_ip);
			strncpy(msg.dst_ip, dst_ip, IP_ADDR_LENGTH);
			strncpy(msg.data, "forward", DATA_SIZE);
			forward_message(msg, 1);
		}

		if (backward) {
			get_node_ip(backward->leader, dst_ip);
			strncpy(msg.dst_ip, dst_ip, IP_ADDR_LENGTH);
			strncpy(msg.data, "backward", DATA_SIZE);
			forward_message(msg, 1);
		}

	} else if (strcmp(msg.data, "forward") == 0) {
		if (forward) {
			get_node_ip(forward->leader, dst_ip);
			strncpy(msg.dst_ip, dst_ip, IP_ADDR_LENGTH);
			forward_message(msg, 1);
		}

	} else {
		if (backward) {
			get_node_ip(backward->leader, dst_ip);
			strncpy(msg.dst_ip, dst_ip, IP_ADDR_LENGTH);
			forward_message(msg, 1);
		}
	}

	return TRUE;
}

int do_update_skip_list_node(struct message msg) {
	int i;
    size_t cpy_len = 0;
	// analysis node info from message
	memcpy(&i, msg.data, sizeof(int));
    cpy_len += sizeof(int);

	node_info f_node, b_node;
	memcpy(&f_node, (void*) (msg.data + cpy_len), sizeof(node_info));
    cpy_len += sizeof(node_info);

	memcpy(&b_node, (void*) (msg.data + cpy_len), sizeof(node_info));
    cpy_len += sizeof(node_info);

	skip_list_node *sln_ptr, *new_sln;
	sln_ptr = the_skip_list->header;
	if (get_cluster_id(f_node) != -1) {
		new_sln = new_skip_list_node(0, &f_node);

		// free old forward field first
		if (sln_ptr->level[i].forward) {
			free(sln_ptr->level[i].forward);
		}

		sln_ptr->level[i].forward = new_sln;
	}

	if (get_cluster_id(b_node) != -1) {
		new_sln = new_skip_list_node(0, &b_node);

		// free old backward field first
		if (sln_ptr->level[i].backward) {
			free(sln_ptr->level[i].backward);
		}
		sln_ptr->level[i].backward = new_sln;
	}
	return TRUE;
}
int do_update_forward(struct message msg) {
	int i;
    size_t cpy_len = 0;

	// get node info from message
	memcpy(&i, msg.data, sizeof(int));
    cpy_len += sizeof(int);

	node_info f_node;
	memcpy(&f_node, (void*) (msg.data + cpy_len), sizeof(node_info));
    cpy_len += sizeof(node_info);

	skip_list_node *sln_ptr, *new_sln;
	sln_ptr = the_skip_list->header;
	if (get_cluster_id(f_node) != -1) {
		new_sln = new_skip_list_node(0, &f_node);

		// free old forward field first
		if (sln_ptr->level[i].forward) {
			free(sln_ptr->level[i].forward);
		}

		sln_ptr->level[i].forward = new_sln;
	}
	return TRUE;
}

int do_update_backward(struct message msg) {
	int i;
    size_t cpy_len = 0;

	// get node info from message
	memcpy(&i, msg.data, sizeof(int));
    cpy_len += sizeof(int);

	node_info b_node;
	memcpy(&b_node, (void*) (msg.data + cpy_len), sizeof(node_info));
    cpy_len += sizeof(node_info);

	skip_list_node *sln_ptr, *new_sln;
	sln_ptr = the_skip_list->header;
	if (get_cluster_id(b_node) != -1) {
		new_sln = new_skip_list_node(0, &b_node);

		// free old backward field first
		if (sln_ptr->level[i].backward) {
			free(sln_ptr->level[i].backward);
		}

		sln_ptr->level[i].backward = new_sln;
	}
	return TRUE;
}

int do_new_skip_list(struct message msg) {
	int level;
    size_t cpy_len = 0;

	// get node info from message
	skip_list_node *sln_ptr;
	node_info leader;

	memcpy(&level, msg.data, sizeof(int));
    cpy_len += sizeof(int);
	memcpy(&leader, msg.data + cpy_len, sizeof(node_info));
    cpy_len += sizeof(node_info);

    if(the_skip_list == NULL) {
        the_skip_list = new_skip_list(level);
    }
    sln_ptr = the_skip_list->header;
    sln_ptr->leader = leader;
    sln_ptr->height = level;
    the_skip_list->level = level;
	return TRUE;
}

int forward_search(struct interval intval[], struct message msg, int d) {
	char src_ip[IP_ADDR_LENGTH], dst_ip[IP_ADDR_LENGTH];

	struct coordinate lower_id = get_node_id(the_torus->info);
	struct coordinate upper_id = get_node_id(the_torus->info);
	switch (d) {
	case 0:
		lower_id.x = (lower_id.x + the_partition.p_x - 1) % the_partition.p_x;
		upper_id.x = (upper_id.x + the_partition.p_x + 1) % the_partition.p_x;
		break;
	case 1:
		lower_id.y = (lower_id.y + the_partition.p_y - 1) % the_partition.p_y;
		upper_id.y = (upper_id.y + the_partition.p_y + 1) % the_partition.p_y;
		break;
	case 2:
		lower_id.z = (lower_id.z + the_partition.p_z - 1) % the_partition.p_z;
		upper_id.z = (upper_id.z + the_partition.p_z + 1) % the_partition.p_z;
		break;
	}

	node_info *lower_neighbor = get_neighbor_by_id(the_torus, lower_id);
	node_info *upper_neighbor = get_neighbor_by_id(the_torus, upper_id);

    #ifdef WRITE_LOG 
        char buf[1024];
        int len = 0, i;
        len = sprintf(buf, "query:");
        for (i = 0; i < MAX_DIM_NUM; ++i) {
            #ifdef INT_DATA
                len += sprintf(buf + len, "[%d, %d] ", intval[i].low,
                        intval[i].high);
            #else
                len += sprintf(buf + len, "[%.10f, %.10f] ", intval[i].low,
                        intval[i].high);
            #endif
        }
        sprintf(buf + len, "\n");
    #endif

	int lower_overlap = interval_overlap(intval[d], lower_neighbor->dims[d]);
	int upper_overlap = interval_overlap(intval[d], upper_neighbor->dims[d]);

	// choose forward neighbor if query overlap one neighbor at most
	if ((lower_overlap != 0) || (upper_overlap != 0)) {

		//current torus node is the first node on dimension d
		if ((the_torus->info.dims[d].low < lower_neighbor->dims[d].low)
				&& (the_torus->info.dims[d].low
						< upper_neighbor->dims[d].low)) {
			if (get_distance(intval[d], lower_neighbor->dims[d])
					< get_distance(intval[d], upper_neighbor->dims[d])) {
				upper_neighbor = lower_neighbor;
			}
			lower_neighbor = NULL;

		} else if ((the_torus->info.dims[d].high
				> lower_neighbor->dims[d].high)
				&& (the_torus->info.dims[d].high
						> upper_neighbor->dims[d].high)) {
			if (get_distance(intval[d], upper_neighbor->dims[d])
					< get_distance(intval[d], lower_neighbor->dims[d])) {
				lower_neighbor = upper_neighbor;
			}
			upper_neighbor = NULL;
		} else {
			if (lower_overlap != 0) {
				lower_neighbor = NULL;
			}
			if (upper_overlap != 0) {
				upper_neighbor = NULL;
			}
		}
	}

	// the message is from lower_neighbor
	if ((lower_neighbor != NULL) && strcmp(lower_neighbor->ip, msg.src_ip) == 0) {
		lower_neighbor = NULL;
	}
	// the message is from upper_neighbor
	if ((upper_neighbor != NULL) && strcmp(upper_neighbor->ip, msg.src_ip) == 0) {
		upper_neighbor = NULL;
	}

    if(lower_neighbor ==  upper_neighbor) {
        lower_neighbor = NULL;
    }

	if (lower_neighbor != NULL) {
		struct message new_msg;
		get_node_ip(the_torus->info, src_ip);
		get_node_ip(*lower_neighbor, dst_ip);
		fill_message((OP)msg.op, src_ip, dst_ip, msg.stamp, msg.data, &new_msg);

        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, buf);
        #endif
		forward_message(new_msg, 0);
	}
	if (upper_neighbor != NULL) {
		struct message new_msg;
		get_node_ip(the_torus->info, src_ip);
		get_node_ip(*upper_neighbor, dst_ip);
		fill_message((OP)msg.op, src_ip, dst_ip, msg.stamp, msg.data, &new_msg);

        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, buf);
        #endif
		forward_message(new_msg, 0);
	}
	return TRUE;
}

int search_rtree(int op, int id, struct interval intval[]) {
    if(the_torus_rtree == NULL) {
        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, "torus rtree didn't load.\n");
        #endif
        return FALSE;
    }
    #ifdef WRITE_LOG
        write_log(TORUS_NODE_LOG, "begin search torus rtree!\n");
    #endif

    int i;
    double plow[MAX_DIM_NUM], phigh[MAX_DIM_NUM];

    for (i = 0; i < MAX_DIM_NUM; i++) {
        plow[i] = (double)intval[i].low;
        phigh[i] = (double)intval[i].high;
    }
    
    // query rtree of the torus node
    if (FALSE == rtree_query(op, id, plow, phigh, the_torus_rtree)) {
        return FALSE;
    }
    #ifdef WRITE_LOG
        write_log(TORUS_NODE_LOG, "finish search torus rtree!\n");
    #endif
    return TRUE;
}

int do_search_torus_node(struct message msg) {
	int i;
    //char src_ip[IP_ADDR_LENGTH], dst_ip[IP_ADDR_LENGTH];
	char stamp[STAMP_SIZE];

    // get query from message
	int count, op, id, cpy_len = 0;
	struct interval intval[MAX_DIM_NUM];

    // get requst stamp from msg
	strncpy(stamp, msg.stamp, STAMP_SIZE);

    // get forward count from msg
	memcpy(&count, msg.data, sizeof(int));
    cpy_len += sizeof(int);

    // increase count and write back to msg
	count++;
	memcpy(msg.data, &count, sizeof(int));

    // get query string from msg
    // query op
	memcpy(&op, msg.data + cpy_len, sizeof(int));
    cpy_len += sizeof(int);
    // query id
	memcpy(&id, msg.data + cpy_len, sizeof(int));
    cpy_len += sizeof(int);
    // query string
	memcpy(intval, msg.data + cpy_len, sizeof(struct interval) * MAX_DIM_NUM);

	request *req_ptr = find_request(req_list, stamp);

	if (NULL == req_ptr) {
		req_ptr = insert_request(req_list, stamp);

		if (1 == overlaps(intval, the_torus->info.dims)) {
			message new_msg;
			// only for test
			//when receive query and search skip list node finish send message to collect-result node
			fill_message(RECEIVE_RESULT, msg.dst_ip, result_ip, msg.stamp,
					msg.data, &new_msg);

			forward_message(new_msg, 0);			// end test

            #ifdef WRITE_LOG
                char buf[1024];
                write_log(TORUS_NODE_LOG, "search torus success!\n\t");
                print_node_info(the_torus->info);

                int len = 0;
                len = sprintf(buf, "query:");
                for (i = 0; i < MAX_DIM_NUM; ++i) {
                    #ifdef INT_DATA
                        len += sprintf(buf + len, "[%d, %d] ", intval[i].low,
                                intval[i].high);
                    #else
                        len += sprintf(buf + len, "[%.10f, %.10f] ", intval[i].low,
                                intval[i].high);
                    #endif
                }
                sprintf(buf + len, "\n");
                write_log(CTRL_NODE_LOG, buf);

                len = 0;
                len = sprintf(buf, "%s:", the_torus->info.ip);
                for (i = 0; i < MAX_DIM_NUM; ++i) {
                    #ifdef INT_DATA
                        len += sprintf(buf + len, "[%d, %d] ",
                                the_torus->info.dims[i].low,
                                the_torus->info.dims[i].high);
                    #else
                        len += sprintf(buf + len, "[%.10f, %.10f] ",
                                the_torus->info.dims[i].low,
                                the_torus->info.dims[i].high);
                    #endif
                }
                sprintf(buf + len, "\n%s:search torus success. %d\n\n", msg.dst_ip,
                        count);
                write_log(CTRL_NODE_LOG, buf);
            #endif

			for (i = 0; i < MAX_DIM_NUM; i++) {
				forward_search(intval, msg, i);
			}
			//TODO do rtree search

            search_rtree(op, id, intval);

		} else {
			for (i = 0; i < MAX_DIM_NUM; i++) {
				if (interval_overlap(intval[i], the_torus->info.dims[i]) != 0) {
					forward_search(intval, msg, i);
					break;
				}
			}
		}
	}
	/*} else {
	 req_ptr->receive_num--;
	 if (req_ptr->receive_num == 0) {
	 remove_request(req_list, stamp);
	 }*/
	return TRUE;
}

int do_search_skip_list_node(struct message msg) {
	int i;
	char src_ip[IP_ADDR_LENGTH], dst_ip[IP_ADDR_LENGTH];
	char stamp[STAMP_SIZE];

    // get query from message
	struct interval intval[MAX_DIM_NUM];
	memcpy(intval, msg.data + sizeof(int) * 3, sizeof(struct interval) * MAX_DIM_NUM);

	message new_msg;

    //get current skip list node  ip address
	get_node_ip(the_skip_list->header->leader, src_ip);

	skip_list_node *sln_ptr;
	sln_ptr = the_skip_list->header;

	if (0 == interval_overlap(sln_ptr->leader.dims[2], intval[2])) { // node searched
		if (FALSE == gen_request_stamp(stamp)) {
			return FALSE;
		}

		// only for test
		//when receive query and search skip list node finish send message to collect-result node
		fill_message(RECEIVE_QUERY, src_ip, result_ip, stamp, "", &new_msg);
		forward_message(new_msg, 0); // end test

        #ifdef WRITE_LOG
            char buf[1024];
            int len = 0;
            len = sprintf(buf, "query:");
            for (i = 0; i < MAX_DIM_NUM; ++i) {
                #ifdef INT_DATA
                    len += sprintf(buf + len, "[%d, %d] ", intval[i].low,
                            intval[i].high);
                #else
                    len += sprintf(buf + len, "[%.10f, %.10f] ", intval[i].low,
                            intval[i].high);
                #endif
            }
            sprintf(buf + len, "\n");
            write_log(TORUS_NODE_LOG, buf);

            write_log(TORUS_NODE_LOG,
                    "search skip list success! turn to search torus\n");
        #endif

		// turn to torus layer
        fill_message((OP)SEARCH_TORUS_NODE, src_ip, msg.dst_ip, stamp, msg.data, &new_msg);
		do_search_torus_node(new_msg);

		//decide whether forward message to it's forward and backward
		if (strcmp(msg.stamp, "") == 0) {
			if ((sln_ptr->level[0].forward != NULL)
					&& (interval_overlap(
							sln_ptr->level[0].forward->leader.dims[2],
							intval[2]) == 0)) {
				get_node_ip(sln_ptr->level[0].forward->leader, dst_ip);
				fill_message((OP)msg.op, src_ip, dst_ip, "forward", msg.data,
						&new_msg);
				forward_message(new_msg, 1);
			}

			if ((sln_ptr->level[0].backward != NULL)
					&& (interval_overlap(
							sln_ptr->level[0].backward->leader.dims[2],
							intval[2]) == 0)) {
				get_node_ip(sln_ptr->level[0].backward->leader, dst_ip);
				fill_message((OP)msg.op, src_ip, dst_ip, "backward", msg.data,
						&new_msg);
				forward_message(new_msg, 1);
			}
		} else if (strcmp(msg.stamp, "forward") == 0) {
			if ((sln_ptr->level[0].forward != NULL)
					&& (interval_overlap(
							sln_ptr->level[0].forward->leader.dims[2],
							intval[2]) == 0)) {
				get_node_ip(sln_ptr->level[0].forward->leader, dst_ip);
				fill_message((OP)msg.op, src_ip, dst_ip, "forward", msg.data,
						&new_msg);
				forward_message(new_msg, 1);
			}
		} else {
			if ((sln_ptr->level[0].backward != NULL)
					&& (interval_overlap(
							sln_ptr->level[0].backward->leader.dims[2],
							intval[2]) == 0)) {
				get_node_ip(sln_ptr->level[0].backward->leader, dst_ip);
				fill_message((OP)msg.op, src_ip, dst_ip, "backward", msg.data,
						&new_msg);
				forward_message(new_msg, 1);
			}
		}

	} else if (-1 == interval_overlap(sln_ptr->leader.dims[2], intval[2])) { // node is on the forward of skip list
		int visit_forward = 0;
		for (i = the_skip_list->level; i >= 0; --i) {
			if ((sln_ptr->level[i].forward != NULL)
					&& (interval_overlap(
							sln_ptr->level[i].forward->leader.dims[2],
							intval[2]) <= 0)) {

				get_node_ip(sln_ptr->level[i].forward->leader, dst_ip);
				fill_message((OP)msg.op, src_ip, dst_ip, "forward", msg.data,
						&new_msg);
				forward_message(new_msg, 1);
				visit_forward = 1;
				break;
			}
		}
		if (visit_forward == 0) {
            #ifdef WRITE_LOG
                write_log(TORUS_NODE_LOG, "search failed!\n");
            #endif
		}

	} else {							// node is on the backward of skip list
		int visit_backward = 0;
		for (i = the_skip_list->level; i >= 0; --i) {
			if ((sln_ptr->level[i].backward != NULL)
					&& (interval_overlap(
							sln_ptr->level[i].backward->leader.dims[2],
							intval[2]) >= 0)) {

				get_node_ip(sln_ptr->level[i].backward->leader, dst_ip);
				fill_message((OP)msg.op, src_ip, dst_ip, "backward", msg.data,
						&new_msg);
				forward_message(new_msg, 1);
				visit_backward = 1;
				break;
			}
		}
		if (visit_backward == 0) {
            #ifdef WRITE_LOG
                write_log(TORUS_NODE_LOG, "search failed!\n");
            #endif
		}
	}

	return TRUE;
}

//return elasped time from start to finish
long get_elasped_time(struct timespec start, struct timespec end) {
	return 1000000000L * (end.tv_sec - start.tv_sec)
			+ (end.tv_nsec - start.tv_nsec);
}

int do_receive_result(struct message msg) {
	struct timespec query_start, query_end;
	clock_gettime(CLOCK_REALTIME, &query_end);

	char stamp[STAMP_SIZE];
	char ip[IP_ADDR_LENGTH];
	int i, count;
	long qtime;
	interval intval[MAX_DIM_NUM];
	memcpy(stamp, msg.stamp, STAMP_SIZE);
	memcpy(ip, msg.src_ip, IP_ADDR_LENGTH);
	memcpy(&count, msg.data, sizeof(int));
	memcpy(intval, msg.data + sizeof(int) * 3, sizeof(interval) * MAX_DIM_NUM);

	struct query *query_ptr = find_query(query_list, stamp);
	if(query_ptr == NULL) {
		printf("can't find this query!\n");
		return FALSE;
	}
	query_start = query_ptr->start;
	qtime = get_elasped_time(query_start, query_end);

	char file_name[1024];
	sprintf(file_name, "/root/result/%s", stamp);
	FILE *fp = fopen(file_name, "ab+");
	if (fp == NULL) {
		printf("can't open file %s\n", file_name);
		return FALSE;
	}

	fprintf(fp, "query:");
	for (i = 0; i < MAX_DIM_NUM; i++) {
        #ifdef INT_DATA
            fprintf(fp, "[%d, %d] ", intval[i].low, intval[i].high);
        #else
            fprintf(fp, "[%.10f, %.10f] ", intval[i].low, intval[i].high);
        #endif
	}
	fprintf(fp, "\n");

	fprintf(fp, "query time:%f us\n", (double) qtime / 1000.0);

	fprintf(fp, "%s:%d\n\n", ip, count);
	fclose(fp);

	return TRUE;
}

int do_receive_query(struct message msg) {
	struct timespec query_start;
	clock_gettime(CLOCK_REALTIME, &query_start);

	insert_query(query_list, msg.stamp, query_start);

	return TRUE;
}

int process_message(int socketfd, struct message msg) {

	struct reply_message reply_msg;
	REPLY_CODE reply_code = SUCCESS;
	switch (msg.op) {

	case UPDATE_TORUS:
        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, "receive request update torus.\n");
        #endif

		if (FALSE == do_update_torus(msg)) {
			reply_code = FAILED;
		}

		reply_msg.op = (OP)msg.op;
		reply_msg.reply_code = (REPLY_CODE)reply_code;
		strncpy(reply_msg.stamp, msg.stamp, STAMP_SIZE);

		if (FALSE == send_reply(socketfd, reply_msg)) {
			// TODO handle send reply failed
			return FALSE;
		}
		print_torus_node(*the_torus);
		break;

	case UPDATE_PARTITION:
        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, "receive request update torus.\n");
        #endif
		if (FALSE == do_update_partition(msg)) {
			reply_code = FAILED;
		}

		reply_msg.op = (OP)msg.op;
		reply_msg.reply_code = (REPLY_CODE)reply_code;
		strncpy(reply_msg.stamp, msg.stamp, STAMP_SIZE);

		if (FALSE == send_reply(socketfd, reply_msg)) {
			// TODO handle send reply failed
			return FALSE;
		}
		break;

	case TRAVERSE_TORUS:
        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, "receive request traverse torus.\n");
        #endif

		do_traverse_torus(msg);
		break;

	case SEARCH_TORUS_NODE:
        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, "\nreceive request search torus node.\n");
        #endif

		do_search_torus_node(msg);
		break;

	case UPDATE_SKIP_LIST:
        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, "receive request update skip list.\n");
        #endif

		if (FALSE == do_update_skip_list(msg)) {
			reply_code = FAILED;
		}
		reply_msg.op = (OP)msg.op;
		reply_msg.reply_code = (REPLY_CODE)reply_code;
		strncpy(reply_msg.stamp, msg.stamp, STAMP_SIZE);
		if (FALSE == send_reply(socketfd, reply_msg)) {
			// TODO handle send reply failed
			return FALSE;
		}
		print_skip_list_node(the_skip_list);
		break;

	case UPDATE_SKIP_LIST_NODE:
        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, "receive request update skip list node.\n");
        #endif

		if (FALSE == do_update_skip_list_node(msg)) {
			reply_code = FAILED;
		}
		reply_msg.op = (OP)msg.op;
		reply_msg.reply_code = (REPLY_CODE)reply_code;
		strncpy(reply_msg.stamp, msg.stamp, STAMP_SIZE);
		if (FALSE == send_reply(socketfd, reply_msg)) {
			// TODO handle send reply failed
			return FALSE;
		}
		print_skip_list_node(the_skip_list);
		break;

	case SEARCH_SKIP_LIST_NODE:
        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, "\nreceive request search skip list node.\n");
        #endif

		if( FALSE == do_search_skip_list_node(msg)) {
            reply_code = FAILED;
        }
		reply_msg.op = (OP)msg.op;
		reply_msg.reply_code = (REPLY_CODE)reply_code;
		strncpy(reply_msg.stamp, msg.stamp, STAMP_SIZE);
		if (FALSE == send_reply(socketfd, reply_msg)) {
			// TODO handle send reply failed
			return FALSE;
		}
		break;

	case UPDATE_FORWARD:
        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, "receive request update skip list node's forward field.\n");
        #endif

		if (FALSE == do_update_forward(msg)) {
			reply_code = FAILED;
		}
		reply_msg.op = (OP)msg.op;
		reply_msg.reply_code = (REPLY_CODE)reply_code;
		strncpy(reply_msg.stamp, msg.stamp, STAMP_SIZE);
		if (FALSE == send_reply(socketfd, reply_msg)) {
			// TODO handle send reply failed
			return FALSE;
		}
		print_skip_list_node(the_skip_list);
		break;

	case UPDATE_BACKWARD:
        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, "receive request update skip list node's backward field.\n");
        #endif

		if (FALSE == do_update_backward(msg)) {
			reply_code = FAILED;
		}
		reply_msg.op = (OP)msg.op;
		reply_msg.reply_code = (REPLY_CODE)reply_code;
		strncpy(reply_msg.stamp, msg.stamp, STAMP_SIZE);
		if (FALSE == send_reply(socketfd, reply_msg)) {
			// TODO handle send reply failed
			return FALSE;
		}
		print_skip_list_node(the_skip_list);
		break;

	case NEW_SKIP_LIST:
        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, "receive request new skip list.\n");
        #endif

		if (FALSE == do_new_skip_list(msg)) {
			reply_code = FAILED;
		}
		reply_msg.op = (OP)msg.op;
		reply_msg.reply_code = (REPLY_CODE)reply_code;
		strncpy(reply_msg.stamp, msg.stamp, STAMP_SIZE);
		if (FALSE == send_reply(socketfd, reply_msg)) {
			// TODO handle send reply failed
			return FALSE;
		}
		print_skip_list_node(the_skip_list);
		break;

	case TRAVERSE_SKIP_LIST:
        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, "receive request traverse skip list.\n");
        #endif

		do_traverse_skip_list(msg);
		break;

	case RECEIVE_QUERY:
        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, "receive request receive query.\n");
        #endif

		do_receive_query(msg);
		break;

	case RECEIVE_RESULT:
        #ifdef WRITE_LOG
            write_log(TORUS_NODE_LOG, "receive request receive result.\n");
        #endif

		do_receive_result(msg);
		break;

	default:
		reply_code = (REPLY_CODE)WRONG_OP;

	}

	return TRUE;
}

int new_server() {
	int server_socket;
	server_socket = new_server_socket();
	if (server_socket == FALSE) {
		return FALSE;
	}
	return server_socket;
}

int main(int argc, char **argv) {

	printf("start torus node.\n");
	write_log(TORUS_NODE_LOG, "start torus node.\n");

	int server_socket;
	should_run = 1;

	// new a torus node
	the_torus = NULL; // = new_torus_node();

	the_skip_list = NULL; //*new_skip_list_node();

	// create a new request list
	req_list = new_request();

	// create a new query list(collect results)
	query_list = new_query();


	// load rtree
    the_torus_rtree = NULL;
	/*the_torus_rtree = rtree_load();
    if(the_torus_rtree == NULL){
        write_log(TORUS_NODE_LOG, "load rtree failed.\n");
        //exit(1);
    }
	printf("load rtree.\n");
	write_log(TORUS_NODE_LOG, "load rtree.\n");*/


	server_socket = new_server();
	if (server_socket == FALSE) {
        exit(1);
	}
	printf("start server.\n");
	write_log(TORUS_NODE_LOG, "start server.\n");

	// set server socket nonblocking
	set_nonblocking(server_socket);

	// epoll
	int epfd;
	struct epoll_event ev, events[MAX_EVENTS];
	epfd = epoll_create(MAX_EVENTS);

	ev.data.fd = server_socket;

	ev.events = EPOLLIN;

	epoll_ctl(epfd, EPOLL_CTL_ADD, server_socket, &ev);

	int nfds, i;
	while(should_run) {
		nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
		for(i = 0; i < nfds; i++) {
			int conn_socket;
			if(events[i].data.fd == server_socket) {
				while((conn_socket = accept_connection(server_socket)) > 0) {
					set_nonblocking(conn_socket);

					ev.events = EPOLLIN;
					ev.data.fd = conn_socket;
					epoll_ctl(epfd, EPOLL_CTL_ADD, conn_socket, &ev);
				}
				if (conn_socket == FALSE) {
					// TODO: handle accept connection failed
					continue;
				}
			} else {
				conn_socket = events[i].data.fd;
				struct message msg;
				memset(&msg, 0, sizeof(struct message));

				// receive message through the conn_socket
				if (TRUE == receive_message(conn_socket, &msg)) {
					process_message(conn_socket, msg);
				} else {
					//  TODO: handle receive message failed
					printf("receive message failed.\n");
				}
				close(conn_socket);

			}
		}
	}

	return 0;
}

