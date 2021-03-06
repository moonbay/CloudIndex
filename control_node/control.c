/*
 * control.c
 *
 *  Created on: Sep 20, 2013
 *      Author: meibenjin
 */

#include"control.h"


#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/time.h>
#include<time.h>
#include<unistd.h>
#include<errno.h>

__asm__(".symver memcpy,memcpy@GLIBC_2.2.5");

//define control node 
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

torus_cluster *cluster_list;

// skip list (multiple level linked list for torus cluster)
skip_list *slist;

char torus_ip_list[MAX_NODES_NUM][IP_ADDR_LENGTH];
int torus_nodes_num;

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

torus_cluster *new_torus_cluster() {
	torus_cluster *cluster_list;
	cluster_list = (torus_cluster *) malloc(sizeof(torus_cluster));
	if (cluster_list == NULL) {
		return NULL;
	}
	cluster_list->torus = NULL;
	cluster_list->next = NULL;

	return cluster_list;
}

torus_cluster *find_torus_cluster(torus_cluster *list, int cluster_id) {
	torus_cluster *cluster_ptr = list->next;
	while (cluster_ptr) {
		if (cluster_ptr->torus->cluster_id == cluster_id) {
			return cluster_ptr;
		}
		cluster_ptr = cluster_ptr->next;
	}
	return NULL;
}

torus_cluster *insert_torus_cluster(torus_cluster *list, torus_s *torus_ptr) {
	torus_cluster *new_cluster = new_torus_cluster();
	if (new_cluster == NULL) {
		printf("insert_torus_cluster: allocate new torus cluster failed.\n");
		return NULL;
	}
	new_cluster->torus = torus_ptr;

	torus_cluster *cluster_ptr = list;

	while (cluster_ptr->next) {
		cluster_ptr = cluster_ptr->next;
	}

	new_cluster->next = cluster_ptr->next;
	cluster_ptr->next = new_cluster;

	return new_cluster;
}

int remove_torus_cluster(torus_cluster *list, int cluster_id) {
	torus_cluster *pre_ptr = list;
	torus_cluster *cluster_ptr = list->next;
	while (cluster_ptr) {
		if (cluster_ptr->torus->cluster_id == cluster_id) {
			pre_ptr->next = cluster_ptr->next;
			free(cluster_ptr);
			cluster_ptr = NULL;
			return TRUE;
		}
		pre_ptr = pre_ptr->next;
		cluster_ptr = cluster_ptr->next;
	}
	return FALSE;
}

void print_torus_cluster(torus_cluster *list) {
	torus_cluster *cluster_ptr = list->next;
	while (cluster_ptr) {
		printf("cluster:%d\n", cluster_ptr->torus->cluster_id);
		print_torus(cluster_ptr->torus);
		cluster_ptr = cluster_ptr->next;
	}
}

int set_partitions(torus_partitions *torus_p, int p_x, int p_y, int p_z) {
	if ((p_x < 1) || (p_y < 1) || (p_z < 1)) {
		printf("the number of partitions too small.\n");
		return FALSE;
	}
	if ((p_x > 20) || (p_y > 20) || (p_z > 20)) {
		printf("the number of partitions too large.\n");
		return FALSE;
	}

	torus_p->p_x = p_x;
	torus_p->p_y = p_y;
	torus_p->p_z = p_z;

	return TRUE;
}

int get_nodes_num(struct torus_partitions t_p) {
	return t_p.p_x * t_p.p_y * t_p.p_z;
}

int translate_coordinates(torus_s *torus, int direction) {
	if (torus == NULL) {
		printf("translate_coordinates: torus is null pointer.\n");
		return FALSE;
	}

	int i, nodes_num;

	torus_partitions t_p = torus->partition;
	nodes_num = get_nodes_num(t_p);

	for (i = 0; i < nodes_num; ++i) {
		struct coordinate c = get_node_id(torus->node_list[i].info);
		switch (direction) {
		case X_L:
		case X_R:
			set_node_id(&torus->node_list[i].info, t_p.p_x + c.x, c.y, c.z);
			break;
		case Y_L:
		case Y_R:
			set_node_id(&torus->node_list[i].info, c.x, t_p.p_y + c.y, c.z);
			break;
		case Z_L:
		case Z_R:
			set_node_id(&torus->node_list[i].info, c.x, c.y, t_p.p_z + c.z);
			break;
		}
	}
	return TRUE;
}

int assign_node_ip(torus_node *node_ptr) {
	static int i = 0;
	if (torus_ip_list == NULL) {
		printf("assign_node_ip: torus_ip_list is null pointer.\n");
		return FALSE;
	}

	if (!node_ptr) {
		printf("assign_node_ip: node_ptr is null pointer.\n");
		return FALSE;
	}
	if (i < torus_nodes_num) {
		set_node_ip(&node_ptr->info, torus_ip_list[i++]);
	} else {
		printf("assign_node_ip: no free ip to be assigned.\n");
		return FALSE;
	}

	return TRUE;
}

int assign_cluster_id() {

	static int index = 0;
	//set_cluster_id(&node_ptr->info, index++);
	return index++;
}

int get_node_index(torus_partitions torus_p, int x, int y, int z) {
	return x * torus_p.p_y * torus_p.p_z + y * torus_p.p_z + z;
}

int set_neighbors(torus_s *torus, torus_node *node_ptr) {
	if (!node_ptr) {
		printf("set_neighbors: node_ptr is null pointer.\n");
		return FALSE;
	}
	struct torus_partitions torus_p;
	torus_p = torus->partition;
	struct coordinate c = get_node_id(node_ptr->info);

	// x-coordinate of the right neighbor of node_ptr on x-axis
	int xrc = (c.x + torus_p.p_x + 1) % torus_p.p_x;
	// x-coordinate of the left neighbor of node_ptr on x-axis
	int xlc = (c.x + torus_p.p_x - 1) % torus_p.p_x;

	// y-coordinate of the right neighbor of node_ptr on y-axis
	int yrc = (c.y + torus_p.p_y + 1) % torus_p.p_y;
	// y-coordinate of the left neighbor of node_ptr on y-axis
	int ylc = (c.y + torus_p.p_y - 1) % torus_p.p_y;

	// z-coordinate of the right neighbor of node_ptr on z-axis
	int zrc = (c.z + torus_p.p_z + 1) % torus_p.p_z;
	// z-coordinate of the left neighbor of node_ptr on z-axis
	int zlc = (c.z + torus_p.p_z - 1) % torus_p.p_z;

	// index of the right neighbor of node_ptr on x-axis
	int xri = get_node_index(torus_p, xrc, c.y, c.z);
	// index of the left neighbor of node_ptr on x-axis
	int xli = get_node_index(torus_p, xlc, c.y, c.z);

	// index of the right neighbor of node_ptr on y-axis
	int yri = get_node_index(torus_p, c.x, yrc, c.z);
	// index of the left neighbor of node_ptr on y-axis
	int yli = get_node_index(torus_p, c.x, ylc, c.z);

	// index of the right neighbor of node_ptr on z-axis
	int zri = get_node_index(torus_p, c.x, c.y, zrc);
	// index of the left neighbor of node_ptr on z-axis
	int zli = get_node_index(torus_p, c.x, c.y, zlc);


    int neighbors_num = 0;
	if (xrc != c.x) {
        add_neighbor_info(node_ptr, X_R, &torus->node_list[xri].info);
        neighbors_num++;
	}
	if (xri != xli) {
        add_neighbor_info(node_ptr, X_L, &torus->node_list[xli].info);
        neighbors_num++;
	}

	if (yrc != c.y) {
        add_neighbor_info(node_ptr, Y_R, &torus->node_list[yri].info);
        neighbors_num++;
	}
	if (yri != yli) {
        add_neighbor_info(node_ptr, Y_L, &torus->node_list[yli].info);
        neighbors_num++;
	}

	if (zrc != c.z) {
        add_neighbor_info(node_ptr, Z_R, &torus->node_list[zri].info);
        neighbors_num++;
	}
	if (zri != zli) {
        add_neighbor_info(node_ptr, Z_L, &torus->node_list[zli].info);
        neighbors_num++;
	}

    set_neighbors_num(node_ptr, neighbors_num);

	return TRUE;
}

// TODO choose LEADER_NUM leder nodes
// this would be done after torus interval has been assigned;
void assign_torus_leader(torus_s *torus) {
	int i, j;
	int nodes_num = get_nodes_num(torus->partition);

	struct interval total_region[MAX_DIM_NUM];
	for (i = 0; i < MAX_DIM_NUM; ++i) {
        total_region[i] = torus->node_list[0].info.region[i];
    }

	//calculate total interval in a torus cluster
	for (i = 0; i < MAX_DIM_NUM; ++i) {
		for (j = 1; j < nodes_num; ++j) {
			if (torus->node_list[j].info.region[i].low < total_region[i].low) {
				total_region[i].low = torus->node_list[j].info.region[i].low;
			}
			if (torus->node_list[j].info.region[i].high > total_region[i].high) {
				total_region[i].high = torus->node_list[j].info.region[i].high;
			}
		}
	}

    int index[nodes_num];
    // this function rearrange a initial array indexed with 0~n
    shuffle(index, nodes_num);

    // random choose LEADER_NUM torus nodes as leader
    for(i = 0; i < LEADER_NUM; ++i) {
        torus->leaders[i] = torus->node_list[index[i]].info;
        for( j = 0; j < MAX_DIM_NUM; ++j) {
            torus->leaders[i].region[j] = total_region[j];
        } 
        // update leader flag
        torus->node_list[index[i]].is_leader = 1;
    }
}

void shuffle(int array[], int n) {
    int i, tmp;

    for(i = 0; i < n; i++) {
        array[i] = i;
    }

    for(i = n; i > 0; --i) {
        int j = rand() % i;

        //swap
        tmp = array[i - 1];
        array[i - 1] = array[j];
        array[j] = tmp;
    }
}

torus_s *new_torus(struct torus_partitions new_torus_p) {
	int i, j, k, index, nodes_num;

	nodes_num = get_nodes_num(new_torus_p);
	if (nodes_num <= 0) {
		printf("new_torus: incorrect partition.\n");
		return NULL;
	}

	struct torus_s *torus_ptr;

	torus_ptr = (torus_s *) malloc(sizeof(torus_s));
	if (torus_ptr == NULL) {
		printf("new_torus: malloc space for new torus failed.\n");
		return NULL;
	}

	// assign new torus cluster id
	torus_ptr->cluster_id = assign_cluster_id();

	// set new torus's partition info
	torus_ptr->partition = new_torus_p;

	torus_ptr->node_list = (torus_node *) malloc(
			sizeof(torus_node) * nodes_num);

	if (torus_ptr->node_list == NULL) {
		printf("new_torus: malloc space for torus node list failed.\n");
		return NULL;
	}

	index = 0;
	struct torus_node *new_node;
	for (i = 0; i < new_torus_p.p_x; ++i) {
		for (j = 0; j < new_torus_p.p_y; ++j) {
			for (k = 0; k < new_torus_p.p_z; ++k) {
				new_node = &torus_ptr->node_list[index];

				init_torus_node(new_node);
				if (assign_node_ip(new_node) == FALSE) {
					free(torus_ptr->node_list);
					return NULL;
				}

				set_cluster_id(&new_node->info, torus_ptr->cluster_id);
				set_node_id(&new_node->info, i, j, k);
                set_node_capacity(&new_node->info, DEFAULT_CAPACITY);
				index++;
			}
		}
	}

	return torus_ptr;
}

torus_s *create_torus(int p_x, int p_y, int p_z) {
	int i, j, k, index, nodes_num;

	torus_partitions new_torus_p;
	if (FALSE == set_partitions(&new_torus_p, p_x, p_y, p_z)) {
		return NULL;
	}

	nodes_num = get_nodes_num(new_torus_p);
	if (nodes_num <= 0) {
		printf("create_torus: incorrect partition.\n");
		return NULL;
	}

	// create a new torus
	torus_s *torus_ptr;
	torus_ptr = new_torus(new_torus_p);
	if (torus_ptr == NULL) {
		printf("create_torus: create a new torus failed.\n");
		return NULL;
	}


    #ifdef INT_DATA
        interval intvl[MAX_DIM_NUM];
        int d_x[new_torus_p.p_x + 1];
        int d_y[new_torus_p.p_y + 1];
        int d_z[new_torus_p.p_z + 1];

        int range = 100;

        for (i = 1, d_x[0] = 0; i <= new_torus_p.p_x; ++i) {
            if (i == new_torus_p.p_x) {
                d_x[i] = 100;
                break;
            }
            d_x[i] = d_x[i - 1] + range / new_torus_p.p_x;
        }
        for (j = 1, d_y[0] = 0; j <= new_torus_p.p_y; ++j) {
            if (j == new_torus_p.p_y) {
                d_y[j] = 100;
                break;
            }
            d_y[j] = d_y[j - 1] + range / new_torus_p.p_y;
        }
        for (k = 1, d_z[0] = 0; k <= new_torus_p.p_z; ++k) {
            if (k == new_torus_p.p_z) {
                d_z[k] = 100;
                break;
            }
            d_z[k] = d_z[k - 1] + range / new_torus_p.p_z;
        }
    #endif

    // set data interval for each torus node
	index = 0;
	struct torus_node *pnode;
	for (i = 0; i < new_torus_p.p_x; ++i) {
		for (j = 0; j < new_torus_p.p_y; ++j) {
			for (k = 0; k < new_torus_p.p_z; ++k) {
                pnode = &torus_ptr->node_list[index];
                #ifdef INT_DATA
                    intvl[0].low = d_x[i] + 1;
                    intvl[0].high = d_x[i + 1];
                    intvl[1].low = d_y[j] + 1;
                    intvl[1].high = d_y[j + 1];
                    intvl[2].low = d_z[k] + 1;
                    intvl[2].high = d_z[k + 1];

                    int t;
                    for (t = 0; t < MAX_DIM_NUM; ++t) {
                        pnode->info.region[t].low = intvl[t].low;
                        pnode->info.region[t].high = intvl[t].high;
                    }
                #else
                    if(FALSE == set_interval(&pnode->info)){
                       return NULL;
                    }
                #endif
                index++;
            }
        }
    }

    // set neighbors for each torus node
	for (i = 0; i < nodes_num; ++i) {
		set_neighbors(torus_ptr, &torus_ptr->node_list[i]);
	}

    // assign torus leader for new create torus
    assign_torus_leader(torus_ptr);

	return torus_ptr;
}

torus_s *append_torus(torus_s *to, torus_s *from, int direction) {

	if (to == NULL || from == NULL) {
		printf("append_torus: torus to be appended is null.\n");
		return to;
	}

	// change torus from's coordinates
	switch (direction) {
	case X_R:
	case Y_R:
	case Z_R:
		translate_coordinates(from, direction);
		break;
	case X_L:
	case Y_L:
	case Z_L:
		translate_coordinates(to, direction);
		break;
	}

	int i, index, to_num, from_num, merged_num;
	struct coordinate c;
	struct torus_s *merged_torus;
	struct torus_partitions merged_partition;

	// calc merged torus partitions info
	merged_partition = to->partition;
	switch (direction) {
	case X_L:
	case X_R:
		merged_partition.p_x += from->partition.p_x;
		break;
	case Y_L:
	case Y_R:
		merged_partition.p_y += from->partition.p_y;
		break;
	case Z_L:
	case Z_R:
		merged_partition.p_z += from->partition.p_z;
		break;
	}

	merged_torus = new_torus(merged_partition);
	if (merged_torus == NULL) {
		printf("append_torus: create a new torus failed.\n");
		return to;
	}

	to_num = get_nodes_num(to->partition);
	from_num = get_nodes_num(from->partition);

	for (i = 0; i < to_num; ++i) {
		c = get_node_id(to->node_list[i].info);
		index = get_node_index(to->partition, c.x, c.y, c.z);
		merged_torus->node_list[index] = to->node_list[i];
	}

	for (i = 0; i < from_num; ++i) {
		c = get_node_id(from->node_list[i].info);
		index = get_node_index(from->partition, c.x, c.y, c.z);
		merged_torus->node_list[index] = from->node_list[i];
	}

	merged_num = get_nodes_num(merged_partition);
	for (i = 0; i < merged_num; ++i) {
		set_neighbors(merged_torus, &merged_torus->node_list[i]);
	}

	// free torus from and to
	free(to->node_list);
	free(from->node_list);

	return merged_torus;
}

int dispatch_torus(torus_s *torus) {
	int i, j, nodes_num;

	nodes_num = get_nodes_num(torus->partition);
    // send info to all torus nodes
	for (i = 0; i < nodes_num; ++i) {
        int d;
        size_t cpy_len = 0;
        char buf[DATA_SIZE];
        memset(buf, 0, DATA_SIZE);

        //copy torus partitions into buf
        memcpy(buf, (void *)&torus->partition, sizeof(struct torus_partitions));
        cpy_len += sizeof(struct torus_partitions);


        // copy leaders info into buf
        int leaders_num = LEADER_NUM;
        memcpy(buf + cpy_len, &leaders_num, sizeof(int));
        cpy_len += sizeof(int);

        for(j = 0; j < LEADER_NUM; j++) {
            memcpy(buf + cpy_len, &torus->leaders[j], sizeof(node_info));
            cpy_len += sizeof(node_info);
        }

        struct torus_node *node_ptr;
        node_ptr = &torus->node_list[i];

        //copy current torus node's leader flag into buf
        memcpy(buf + cpy_len, &node_ptr->is_leader, sizeof(int));
        cpy_len += sizeof(int);

        //copy current torus node info into buf
		memcpy(buf + cpy_len,(void *) &node_ptr->info, sizeof(node_info));
        cpy_len += sizeof(node_info);

        // copy current torus node's neighbors info into buf
        for(d = 0; d < DIRECTIONS; d++) {
            int num = get_neighbors_num_d(node_ptr, d);
            memcpy(buf + cpy_len, &num, sizeof(int));
            cpy_len += sizeof(int); 

            if(node_ptr->neighbors[d] != NULL) {
                struct neighbor_node *nn_ptr;
                nn_ptr = node_ptr->neighbors[d]->next;
                while(nn_ptr != NULL) {
                    memcpy(buf + cpy_len, nn_ptr->info, sizeof(node_info));
                    cpy_len += sizeof(node_info);
                    nn_ptr = nn_ptr->next;
                }
            }
        }


		char dst_ip[IP_ADDR_LENGTH];
		memset(dst_ip, 0, IP_ADDR_LENGTH);
		get_node_ip(node_ptr->info, dst_ip);

		// send to dst_ip
		if (FALSE == send_data(CREATE_TORUS, dst_ip, buf, cpy_len)) {
			return FALSE;
		}

	}
	return TRUE;
}

int traverse_torus(const char *entry_ip) {
	int socketfd;

	socketfd = new_client_socket(entry_ip, MANUAL_WORKER_PORT);
	if (FALSE == socketfd) {
		return FALSE;
	}
	// get local ip address
	char local_ip[IP_ADDR_LENGTH];
	memset(local_ip, 0, IP_ADDR_LENGTH);
	if (FALSE == get_local_ip(local_ip)) {
		return FALSE;
	}

	struct message msg;
	msg.op = TRAVERSE_TORUS;
	strncpy(msg.src_ip, local_ip, IP_ADDR_LENGTH);
	strncpy(msg.dst_ip, entry_ip, IP_ADDR_LENGTH);
	memset(msg.stamp, 0, STAMP_SIZE);
	memset(msg.data, 0, DATA_SIZE);

	send_message(socketfd, msg);
	close(socketfd);
	return TRUE;
}

void print_torus(torus_s *torus) {
	int i, nodes_num;
	nodes_num = get_nodes_num(torus->partition);

	for (i = 0; i < nodes_num; ++i) {
		print_torus_node(torus->node_list[i]);
	}
}

int read_torus_ip_list() {
	FILE *fp;
	char ip[IP_ADDR_LENGTH];
	int count;
	fp = fopen(TORUS_IP_LIST, "rb");
	if (fp == NULL) {
		printf("read_torus_ip_list: open file %s failed.\n", TORUS_IP_LIST);
		return FALSE;
	}

	count = 0;
	while ((fgets(ip, IP_ADDR_LENGTH, fp)) != NULL) {
		ip[strlen(ip) - 1] = '\0';
		strncpy(torus_ip_list[count++], ip, IP_ADDR_LENGTH);
	}
	torus_nodes_num = count;
	return TRUE;
}

int traverse_skip_list(const char *entry_ip) {
	int socketfd;

	socketfd = new_client_socket(entry_ip, MANUAL_WORKER_PORT);
	if (FALSE == socketfd) {
		return FALSE;
	}

	// get local ip address
	char local_ip[IP_ADDR_LENGTH];
	memset(local_ip, 0, IP_ADDR_LENGTH);
	if (FALSE == get_local_ip(local_ip)) {
		return FALSE;
	}

	struct message msg;
	msg.op = TRAVERSE_SKIP_LIST;
	strncpy(msg.src_ip, local_ip, IP_ADDR_LENGTH);
	strncpy(msg.dst_ip, entry_ip, IP_ADDR_LENGTH);
	memset(msg.stamp, 0, STAMP_SIZE);
	memset(msg.data, 0, DATA_SIZE);

	send_message(socketfd, msg);
	close(socketfd);
	return TRUE;
}

int dispatch_skip_list(skip_list *list, node_info leaders[]) {
	int i, j, new_level;
    size_t cpy_len = 0;
	char src_ip[IP_ADDR_LENGTH], dst_ip[IP_ADDR_LENGTH];
	skip_list_node *sln_ptr, *new_sln;
	node_info forwards[LEADER_NUM], backwards[LEADER_NUM], end_node;
    init_node_info(&end_node);


	memset(src_ip, 0, IP_ADDR_LENGTH);
	if (FALSE == get_local_ip(src_ip)) {
		return FALSE;
	}

	// random choose a level and create new skip list node (local create)
	new_level = random_level();
    new_sln = new_skip_list_node(new_level, leaders);

	// create a new skip list node at each leader node (remote create)
	struct message msg;
    msg.op = NEW_SKIP_LIST;
    strncpy(msg.src_ip, src_ip, IP_ADDR_LENGTH);
    strncpy(msg.stamp, "", STAMP_SIZE);
    for(i = 0; i < LEADER_NUM; ++i) {
        cpy_len = 0;
        strncpy(msg.dst_ip, leaders[i].ip, IP_ADDR_LENGTH);
        memset(msg.data, 0, DATA_SIZE);
        memcpy(msg.data, &new_level, sizeof(int));
        cpy_len += sizeof(int);
        memcpy(msg.data + cpy_len, leaders, sizeof(node_info) * LEADER_NUM);

        if (TRUE == forward_message(msg, 0)) {
            printf("%s:\tcreate new skip list node ... success\n", leaders[i].ip);
        } else {
            printf("%s:\tcreate new skip list node ... failed\n", leaders[i].ip);
            return FALSE;
        }
    }

	// update skip list header's level
	if (new_level > list->level) {
		list->level = new_level;
	}


	sln_ptr = list->header;
	for (i = list->level; i >= 0; --i) {
		while ((sln_ptr->level[i].forward != NULL) && \
                (-1 == compare(sln_ptr->level[i].forward->leader[0].region[2], leaders[0].region[2]))) {
			sln_ptr = sln_ptr->level[i].forward;
		}

		if (i <= new_level) {
            int update_forward = 0, update_backword = 0;
			// update new skip list node's forward field (local update)
            new_sln->level[i].forward = sln_ptr->level[i].forward;
            for(j = 0; j < LEADER_NUM; j++) {
                if(sln_ptr->level[i].forward != NULL) {
                    forwards[j] = sln_ptr->level[i].forward->leader[j];
                    update_forward = 1;
                }
            }

			// update new skip list node's backward field (local update)
            new_sln->level[i].backward = (sln_ptr == list->header) ? NULL : sln_ptr;
            for(j = 0; j < LEADER_NUM; j++) {
                if(sln_ptr != list->header) {
                    backwards[j] = sln_ptr->leader[j];
                    update_backword= 1;
                }
            }

            //update new skip list node's forwards and backwards (remote update)
            for(j = 0; j < LEADER_NUM; ++j) {
                msg.op = UPDATE_SKIP_LIST_NODE;
                get_node_ip(leaders[j], dst_ip);
                strncpy(msg.dst_ip, dst_ip, IP_ADDR_LENGTH);
                cpy_len = 0;
                memcpy(msg.data, &i, sizeof(int));
                cpy_len += sizeof(int);
                memcpy(msg.data + cpy_len, &update_forward, sizeof(int));
                cpy_len += sizeof(int);
                memcpy(msg.data + cpy_len, &update_backword, sizeof(int));
                cpy_len += sizeof(int);

                if(update_forward == 1) {
                    memcpy(msg.data + cpy_len, forwards, sizeof(node_info) * LEADER_NUM);
                    cpy_len += sizeof(node_info) * LEADER_NUM;
                }

                if(update_backword == 1) {
                    memcpy(msg.data + cpy_len, backwards, sizeof(node_info) * LEADER_NUM);
                    cpy_len += sizeof(node_info) * LEADER_NUM;
                }

                if (TRUE == forward_message(msg, 0)) {
                    printf("%s:\tupdate new skip list node's forward and backward ... success\n", dst_ip);
                } else {
                    printf("%s:\tupdate new skip list node's forward and backward ... failed\n", dst_ip);
                    return FALSE;
                }
            }

			// if current skip list node's forward field exist(not the tail node)
			// update it's(sln_ptr->level[i].forward) backward field (local update)
			if(sln_ptr->level[i].forward) {
                sln_ptr->level[i].forward->level[i].backward = new_sln;

                //update current skip list node's forward's backward (remote update)
                for( j = 0; j < LEADER_NUM; ++j) {
                    msg.op = UPDATE_SKIP_LIST_NODE;
                    get_node_ip(sln_ptr->level[i].forward->leader[j], dst_ip);
                    strncpy(msg.dst_ip, dst_ip, IP_ADDR_LENGTH);
                    cpy_len = 0;

                    // only need update backward
                    update_forward = 0;
                    update_backword = 1;
                    memcpy(msg.data, &i, sizeof(int));
                    cpy_len += sizeof(int);
                    memcpy(msg.data + cpy_len, &update_forward, sizeof(int));
                    cpy_len += sizeof(int);
                    memcpy(msg.data + cpy_len, &update_backword, sizeof(int));
                    cpy_len += sizeof(int);
                    memcpy(msg.data + cpy_len, leaders,sizeof(node_info) * LEADER_NUM);

                    if (TRUE == forward_message(msg, 0)) {
                        printf("%s:\tupdate skip list node's backward ... success\n", dst_ip);
                    } else {
                        printf("%s:\tupdate skip list node's backward ... failed\n", dst_ip);
                        return FALSE;
                    }
                }
			}

			// update current skip list node's forward field (local update)
            sln_ptr->level[i].forward = new_sln;
            for( j = 0; j < LEADER_NUM; ++j) {

                //update current skip list node's forward (remote update)
                if (get_cluster_id(sln_ptr->leader[j]) != -1) {
                    msg.op = UPDATE_SKIP_LIST_NODE;
                    get_node_ip(sln_ptr->leader[j], dst_ip);
                    strncpy(msg.dst_ip, dst_ip, IP_ADDR_LENGTH);
                    cpy_len = 0;
                    // only need update forward 
                    update_forward = 1;
                    update_backword = 0;
                    memcpy(msg.data, &i, sizeof(int));
                    cpy_len += sizeof(int);
                    memcpy(msg.data + cpy_len, &update_forward, sizeof(int));
                    cpy_len += sizeof(int);
                    memcpy(msg.data + cpy_len, &update_backword, sizeof(int));
                    cpy_len += sizeof(int);
                    memcpy(msg.data + cpy_len, leaders, sizeof(node_info) * LEADER_NUM);

                    if (TRUE == forward_message(msg, 0)) {
                        printf("%s:\tupdate skip list node's forward ... success\n", dst_ip);
                    } else {
                        printf("%s:\tupdate skip list node's forward ... failed\n", dst_ip);
                        return FALSE;
                    }
                }
            }
		}
	}

	return TRUE;
}

int query_torus(struct query_struct query, const char *entry_ip) {

	// get local ip address
	char local_ip[IP_ADDR_LENGTH];
	memset(local_ip, 0, IP_ADDR_LENGTH);
	if (FALSE == get_local_ip(local_ip)) {
		return FALSE;
	}

	struct message msg;
	msg.op = QUERY_TORUS_CLUSTER;
	strncpy(msg.src_ip, local_ip, IP_ADDR_LENGTH);
	strncpy(msg.dst_ip, entry_ip, IP_ADDR_LENGTH);
	strncpy(msg.stamp, "", STAMP_SIZE);

    size_t cpy_len = 0; 
	int count = 0;
	memcpy(msg.data, &count, sizeof(int));
    cpy_len += sizeof(int);

    memcpy(msg.data + cpy_len, (void *)&query, sizeof(struct query_struct));
    cpy_len += sizeof(struct query_struct);

    forward_message(msg, 0);

	return TRUE;
}

int send_file(char *entry_ip) {
	int socketfd, ret = TRUE;

	// get local ip address
	char local_ip[IP_ADDR_LENGTH];
	memset(local_ip, 0, IP_ADDR_LENGTH);
	if (FALSE == get_local_ip(local_ip)) {
		ret = FALSE;
	}

	socketfd = new_client_socket(entry_ip, COMPUTE_WORKER_PORT);
	if (FALSE == socketfd) {
		ret = FALSE;
	}

    struct message msg;
    msg.op = RECEIVE_DATA;
	strncpy(msg.src_ip, local_ip, IP_ADDR_LENGTH);
	strncpy(msg.dst_ip, entry_ip, IP_ADDR_LENGTH);
	strncpy(msg.stamp, "", STAMP_SIZE);
    strncpy(msg.data, "", DATA_SIZE);
    send_message(socketfd, msg);

    FILE *fp = fopen("/root/mbj/send_test", "r");
    if(fp == NULL) {
        printf("open range_query failed.\n");
        ret = FALSE;
    } else {
        int block_len = 0;
        char buf[SOCKET_BUF_SIZE];
        memset(buf, 0, SOCKET_BUF_SIZE);
        int len = 0;
        int count = 0;
        
        while((block_len = fread(buf, sizeof(char), SOCKET_BUF_SIZE, fp)) > 0) {
            count++;
            len = send(socketfd, (void *) buf, block_len, 0);
            if(len < 0) {
                printf("%d, %d\n", errno, count);
                break;
            }
        }
        fclose(fp);
    }

	close(socketfd);

    return ret;
}


int main(int argc, char **argv) {
	/*if (argc < 4) {
	 printf("usage: %s x y z\n", argv[0]);
	 exit(1);
	 }*/

    // read ip pool from file 
	if (FALSE == read_torus_ip_list()) {
		exit(1);
	}

	cluster_list = new_torus_cluster();

	if (NULL == cluster_list) {
		exit(1);
	}

	// create a new skip list
	slist = new_skip_list(MAXLEVEL);
	if (NULL == slist) {
		exit(1);
	}

	char entry_ip[IP_ADDR_LENGTH];
    //int cnt = 5;
    int cnt = 0;
    while(cnt < 1) {

        // create a new torus by torus partition info
        struct torus_s *torus_ptr;
        torus_ptr = create_torus(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
        if (torus_ptr == NULL) {
            exit(1);
        }

        //strncpy(entry_ip, torus_ptr->leaders[0].ip, IP_ADDR_LENGTH);

        // insert newly created torus cluster into cluster_list
        insert_torus_cluster(cluster_list, torus_ptr);

        //print_torus_cluster(cluster_list);

        if (TRUE == dispatch_torus(torus_ptr)) {
            if (TRUE == dispatch_skip_list(slist, torus_ptr->leaders)) {
                print_skip_list(slist);
                //printf("traverse skip list success!\n");
            }
        }

        printf("\n\n");
        //print_torus_cluster(cluster_list);
        printf("\n\n");
        cnt++;
    }

    strncpy(entry_ip, "172.16.0.166", IP_ADDR_LENGTH);
	int count = 0, i;
    struct query_struct query;
    FILE *fp;

    char data_file[MAX_FILE_NAME];
    snprintf(data_file, MAX_FILE_NAME, "%s/data", TMP_DATA_DIR);
	fp = fopen(data_file, "rb");
	if (fp == NULL) {
		printf("can't open file\n");
		exit(1);
	}

    printf("begin read.\n");
	while (!feof(fp)) {

        fscanf(fp, "%d %d %d", &query.op, &query.trajectory_id, &query.data_id);
        count++;
        //printf("%d %d %d ", ++count, op, id);
        for (i = 0; i < MAX_DIM_NUM; i++) {
            #ifdef INT_DATA
                fscanf(fp, "%d", &query.intval[i].low);
                //printf("%d ", query.intval[i].low);
            #else
                fscanf(fp, "%lf", &query.intval[i].low);
                //printf("%lf ", query.intval[i].low);
            #endif
        }

        for (i = 0; i < MAX_DIM_NUM; i++) {
            #ifdef INT_DATA
                fscanf(fp, "%d", &query.intval[i].high);
                //printf("%d ", query.intval[i].high);
            #else
                fscanf(fp, "%lf", &query.intval[i].high);
                //printf("%lf ", query.intval[i].high);
            #endif
        }
        fscanf(fp, "\n");
        //printf("\n");

        query_torus(query, entry_ip);
        //printf("\n");
        if(count % 1000 == 0) {
            printf("%d\n", count);
        }
	}
    printf("finish read.\n");
	fclose(fp);

	fp = fopen("./range_query", "rb");
	if (fp == NULL) {
		printf("can't open file\n");
		exit(1);
	}

    count = 0;
    printf("begin query.\n");
	while (!feof(fp)) {
        fscanf(fp, "%d %d", &query.op, &query.data_id);
        printf("%d %d %d ", ++count, query.op, query.data_id);
        fscanf(fp, "%d %d", &query.op, &query.data_id);
        printf("%d %d %d ", ++count, query.op, query.data_id);
        for (i = 0; i < MAX_DIM_NUM; i++) {
            #ifdef INT_DATA
                fscanf(fp, "%d %d ", &query.intval[i].low, &query.intval[i].high);
                printf("%d %d", query.intval[i].low, query.intval[i].high);
            #else
                fscanf(fp, "%lf %lf ", &query.intval[i].low, &query.intval[i].high);
                printf("%lf %lf", query.intval[i].low, query.intval[i].high);
            #endif
        }
        fscanf(fp, "\n");
        printf("\n");

        query_torus(query, entry_ip);
        printf("\n");
        //usleep(1000);
	}
    printf("finish query.\n");
	fclose(fp);

    //performance_test(entry_ip);
    //send_file(entry_ip);

	return 0;
}

