#ifndef __INFORMATION_HANDLER_HPP__
#define __INFORMATION_HANDLER_HPP__

#include <generals.hpp>
#include <indicator_handler.hpp>
#include <parameter_handler.hpp>
#include <attribute_handler.hpp>

struct information_handler	{
	indicator_handler * indicator_ptr;		/* indicator vector with bit array */
	parameter_handler * parameter_ptr;		/* parameters from shell command */
	attribute_wrapper * attribute_ptr;		/* attribute space for algorithm */
	meta_handler * meta_ptr;				/* graph metadata management */
	meta_handler * meta_a_ptr;				/* graph metadata management */
	meta_handler * meta_b_ptr;				/* graph metadata management */
	
	size_t num_nodes;						/* total number of nodes */
	size_t num_edges;						/* total number of edges */
	int num_threads;						/* number of worker threads */
	vid_t cur_iteration;						/* number of current iteration */
	vid_t max_iteration;						/* number of max iteration */

	vid_t nod_lowerbound;					/* lowerbound edgenumber to be processed for now */
	vid_t nod_upperbound;					/* upperbound edgenumber to be processed for now */
	vid_t adj_lowerbound;					/* lowerbound nodenumber to be processed for now */
	vid_t adj_upperbound;					/* upperbound nodenumber to be processed for now */
};

#endif