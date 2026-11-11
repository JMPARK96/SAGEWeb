#ifndef __INFORMATION_HANDLER_HPP__
#define __INFORMATION_HANDLER_HPP__

#include <generals.hpp>
#include <indicator_handler.hpp>
#include <parameter_handler.hpp>
#include <attribute_handler.hpp>

struct information_handler	{
	indicator_handler * indicator_ptr;		
	parameter_handler * parameter_ptr;		
	attribute_wrapper * attribute_ptr;		
	meta_handler * meta_ptr;				
	meta_handler * meta_a_ptr;				
	meta_handler * meta_b_ptr;				
	
	size_t num_nodes;						
	size_t num_edges;						
	int num_threads;						
	vid_t cur_iteration;						
	vid_t max_iteration;						

	vid_t nod_lowerbound;					
	vid_t nod_upperbound;					
	vid_t adj_lowerbound;					
	vid_t adj_upperbound;					
};

#endif