#ifndef __GENERALS_HPP__
#define __GENERALS_HPP__

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <atomic>
#include <map>
#include <vector>
#include <chrono>
#include <algorithm>
#include <functional>
#include <climits>
#include <thread>
#include <omp.h>

#define WORDTYPE unsigned long long int
#define WORDRANGE_LOG2 6
#define WORDFILTER 63
#define WORDBIT(node_id) ((WORDTYPE)(1) << ((node_id) & WORDFILTER))

#ifdef vid_32
#define vid_t uint32_t
#define tokenvid(str,endptr,base) strtoul(str,endptr,base)
#else
#define vid_t uint64_t
#define tokenvid(str,endptr,base) strtoull(str,endptr,base)
#endif

#ifdef val_32
#define val_t float
#define tokenval(str,endptr) strtof(str,endptr)
#else
#define val_t double
#define tokenval(str,endptr) strtod(str,endptr)
#endif

#define BLOCKSIZE 	1048576
#define	DPFIXED	(sizeof(int) * 4)

#define BUFFERSIZE 5000
#define MILLION 1048576

typedef struct {
	int length;
	int offset;
} SLOT;

typedef struct {
	char	data[BLOCKSIZE - DPFIXED];
	SLOT	slot[1];					/* slot 0 -- index backwards */
	int	free;			/* pointer to data free area */
	int ridcnt;
} BLOCK;

typedef struct edge {
	vid_t src;
	vid_t dst;

	edge() {
		src = 0;
		dst = 0;
	}

	edge(vid_t _src, vid_t _dst) {
		src = _src;
		dst = _dst;
	}
}edge;

bool less_edge_with_novalue(vid_t i, vid_t j)	{
	return i < j;
}

#define val_t float

typedef struct edge_with_value {
	vid_t src;
	vid_t dst;
	val_t val;

	edge_with_value() {
		src = 0;
		dst = 0;
		val = 0;
	}

	edge_with_value(vid_t _src, vid_t _dst, val_t _val) {
		src = _src;
		dst = _dst;
		val = _val;
	}
}edge_with_value;

typedef struct adj_with_value{
	vid_t vid;
	val_t val;

	adj_with_value(vid_t _vid, val_t _val)	{
		vid = _vid;
		val = _val;
	}
}adj_with_value;

bool less_edge_with_value(adj_with_value i, adj_with_value j)	{
	return i.vid < j.vid;
}



#ifdef weighted

#define adj_t adj_with_value
#define edge_t edge_with_value
#define less_edge less_edge_with_value

#else

#define adj_t vid_t
#define edge_t edge
#define less_edge less_edge_with_novalue

#endif

#endif