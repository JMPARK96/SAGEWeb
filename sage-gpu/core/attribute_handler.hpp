#ifndef __ATTRIBUTE_HANDLER_HPP__
#define __ATTRIBUTE_HANDLER_HPP__

#include <generals.hpp>
#include <unistd.h>
#include <fcntl.h>

#define BOUNDTYPE_NOD true
#define BOUNDTYPE_ADJ false

#define OFFSET_BITMAP 20
#define NUM_VALUES_IN_PAGE (size_t(1) << OFFSET_BITMAP)

struct attribute_handler_info	{
	std::string filename;
	size_t size_valuetype;
	bool existfile;
	
	size_t num_nodes;
	size_t num_chunks;
	size_t num_pages;
	size_t num_values;

	attribute_handler_info(std::string _filename, size_t _size_valuetype, bool _existfile)	{
		filename = _filename;
		size_valuetype = _size_valuetype;
		existfile = _existfile;
	}
};

class bitattribute_handler {
private:
	
	std::atomic<WORDTYPE> * bitarray;	

	
	size_t size_array;						

	
	size_t num_nodes;						

public:
	bitattribute_handler() : bitarray(NULL) {
	}

	~bitattribute_handler()	{
	}

	
	void create(size_t _num_nodes)	{
		size_array = ((_num_nodes - 1) >> WORDRANGE_LOG2) + 1;

		num_nodes = _num_nodes;

		bitarray = new std::atomic<WORDTYPE>[size_array];

		clear();
	}
	void destroy()	{
		delete bitarray;
	}

	
	void clear()	{
		for (size_t arrayindex = 0; arrayindex < size_array; arrayindex++)	{
			bitarray[arrayindex] = 0;
		}
	}

	
	bool get_bit(size_t node_id)	{
		return (bitarray[node_id >> WORDRANGE_LOG2] & ((WORDTYPE)(1) << (node_id & WORDFILTER))) != 0;
	}

	
	bool set_bit(size_t node_id)	{
		return (atomic_fetch_or(&bitarray[node_id >> WORDRANGE_LOG2], WORDBIT(node_id)) & WORDBIT(node_id)) == (WORDTYPE)(0);
	}

	
	bool unset_bit(size_t node_id)	{
		return (atomic_fetch_and(&bitarray[node_id >> WORDRANGE_LOG2], ~WORDBIT(node_id)) & WORDBIT(node_id)) != (WORDTYPE)(0);
	}

	void lock_bit(size_t node_id)	{
		while (!get_bit(node_id) && set_bit(node_id));
	}

	void unlock_bit(size_t node_id)	{
		unset_bit(node_id);
	}

	void set_pad_bits() {
		bitarray[size_array - 1] |= ~(WORDBIT(num_nodes) - 1);
	}

	size_t get_after_bit_unset(size_t node_id)	{
		size_t array_id = node_id >> WORDRANGE_LOG2;
		size_t first_uppermask = ~(WORDBIT(node_id) - 1);

		if ((~bitarray[array_id] & first_uppermask) != 0) return ((array_id << WORDRANGE_LOG2) + __builtin_ctzll(~bitarray[array_id] & first_uppermask));

		for (size_t index = array_id + 1; index < size_array; index++)	{
			if (~bitarray[index] != 0) return ((index << WORDRANGE_LOG2) + __builtin_ctzll(~bitarray[index]));
		}

		return ULLONG_MAX;
	}

	size_t get_size_array()	{
		return size_array;
	}
};

class attribute_handler {
private:
	int attribute_fd;

	
	std::string filename;

	
	size_t size_valuetype;

	
	size_t num_nodes;
	size_t num_values;				
	size_t num_chunks;				
	size_t size_chunk;				
	size_t size_last_chunk;			
	size_t num_pages;				
	size_t size_page;				
	size_t size_lastpage;			

	
	size_t firstnode_id;			
	size_t lastnode_id;				
	size_t current_chunk_id;		
	size_t next_chunk_id;			

	
	char * default_value;			
	char * attribute_ptr;			

	
	bool * pagelog_ptr;				
	bool * pageflag_ptr;			
	
	
	bool inmemory;					
	bool boundtype;					
	bool existfile;					
	bool writefile;					
	bool nodeID;
public:
	attribute_handler(attribute_handler_info vector_info)	{
		
		filename = vector_info.filename;

		
		size_valuetype = vector_info.size_valuetype;

		
		num_nodes = vector_info.num_nodes;
		num_values = vector_info.num_values;
		num_chunks = vector_info.num_chunks;
		size_chunk = num_values * size_valuetype;
		size_last_chunk = (num_nodes - (num_chunks - 1) * num_values) * size_valuetype;
		num_pages = vector_info.num_pages;
		size_page = NUM_VALUES_IN_PAGE * size_valuetype;
		size_lastpage = (num_nodes - (num_pages - 1) * NUM_VALUES_IN_PAGE) * size_valuetype;

		
		firstnode_id = 0;
		lastnode_id = num_values - 1;
		current_chunk_id = num_chunks == 1 ? 0 : num_chunks;

		
		default_value = NULL;
		attribute_ptr = NULL;

		
		pagelog_ptr = NULL;
		pageflag_ptr = NULL;
		
		
		inmemory = (num_chunks == 1);
		existfile = vector_info.existfile;
		boundtype = BOUNDTYPE_NOD;
		writefile = true;
		nodeID = false;
	}

	~attribute_handler() {
	}

	void set(bool * _pageflag_ptr)	{
		
		pagelog_ptr = (bool *)malloc(num_pages);
		memset(pagelog_ptr, false, num_pages);
		pageflag_ptr = _pageflag_ptr;
		attribute_ptr = (char *)malloc(size_chunk);

		if (existfile && !inmemory) {
			attribute_fd = open(filename.c_str(), O_RDWR);
		}
		else if (existfile && inmemory) {
			attribute_fd = open(filename.c_str(), O_RDWR);

#pragma omp parallel for schedule(dynamic, 1)
			for (size_t page_id = 0; page_id < num_pages - 1; page_id++) {
				if (pread(attribute_fd, attribute_ptr + page_id * size_page, size_page, page_id * size_page) <= 0) {
					fprintf(stderr, "reading an attribute page in set failed\n");
					exit(1);
				}
			}
			if (pread(attribute_fd, attribute_ptr + (num_pages - 1) * size_page, size_lastpage, (num_pages - 1) * size_page) <= 0) {
				fprintf(stderr, "reading last attribute page in set failed\n");
				exit(1);
			}

			close(attribute_fd);
		}
		else if (!existfile && inmemory) {
			if (default_value != NULL) {
#pragma omp parallel for
				for (char * value_ptr = attribute_ptr; value_ptr < attribute_ptr + size_chunk; value_ptr += size_valuetype) {
					memcpy(value_ptr, default_value, size_valuetype);
				}
			}
			if (nodeID) {
				if (size_valuetype == sizeof(unsigned int))	{
					unsigned int * value_ptr = (unsigned int *)attribute_ptr;

					for (size_t index = 0; index < num_nodes; index++) {
						value_ptr[index] = (unsigned int)index;
					}
				}
				else {
					size_t * value_ptr = (size_t *)attribute_ptr;

					for (size_t index = 0; index < num_nodes; index++) {
						value_ptr[index] = index;
					}
				}
			}
		}
		else if (!existfile && !inmemory) {
			attribute_fd = open(filename.c_str(), O_RDWR | O_CREAT | O_TRUNC, 775);
			if (ftruncate(attribute_fd, num_nodes * size_valuetype) < 0) {
				fprintf(stderr, "ftruncate attribute vector in set failed\n");
				exit(1);
			}

			if (default_value != NULL && !writefile)	{
				for (char * value_ptr = attribute_ptr; value_ptr < attribute_ptr + size_page; value_ptr += size_valuetype) {
					memcpy(value_ptr, default_value, size_valuetype);
				}

#pragma omp parallel for schedule(dynamic, 1)
				for (size_t page_id = 0; page_id < num_pages - 1; page_id++) {
					if (pwrite(attribute_fd, attribute_ptr, size_page, page_id * size_page) <= 0) {
						fprintf(stderr, "writing an attribute page in set1 failed\n");
						exit(1);
					}
				}
				if (pwrite(attribute_fd, attribute_ptr, size_lastpage, (num_pages - 1) * size_page) <= 0) {
					fprintf(stderr, "writing last attribute page in set1 failed\n");
					exit(1);
				}
			}
			if (nodeID) {
				if (size_valuetype == sizeof(unsigned int))	{
					unsigned int * value_ptr = (unsigned int *)attribute_ptr;

					for (size_t index = 0; index < NUM_VALUES_IN_PAGE; index++) {
						value_ptr[index] = (unsigned int)index;
					}

					for (size_t page_id = 0; page_id < num_pages - 1; page_id++) {
						if (pwrite(attribute_fd, attribute_ptr, size_page, page_id * size_page) <= 0) {
							fprintf(stderr, "writing an attribute page in set2 failed\n");
							exit(1);
						}

						for (size_t index = 0; index < NUM_VALUES_IN_PAGE; index++) {
							value_ptr[index] += NUM_VALUES_IN_PAGE;
						}
					}

					if (pwrite(attribute_fd, attribute_ptr, size_lastpage, (num_pages - 1) * size_page) <= 0) {
						fprintf(stderr, "writing last attribute page in set2 failed\n");
						exit(1);
					}
				}
				else {
					size_t * value_ptr = (size_t *)attribute_ptr;

					for (size_t index = 0; index < NUM_VALUES_IN_PAGE; index++) {
						value_ptr[index] = index;
					}

					for (size_t page_id = 0; page_id < num_pages - 1; page_id++) {
						if (pwrite(attribute_fd, attribute_ptr, size_page, page_id * size_page) <= 0) {
							fprintf(stderr, "writing an attribute page in set3 failed\n");
							exit(1);
						}

						for (size_t index = 0; index < NUM_VALUES_IN_PAGE; index++) {
							value_ptr[index] += NUM_VALUES_IN_PAGE;
						}
					}

					if (pwrite(attribute_fd, attribute_ptr, size_lastpage, (num_pages - 1) * size_page) <= 0) {
						fprintf(stderr, "writing last attribute page in set3 failed\n");
						exit(1);
					}
				}
			}
		}
	}

	void unset()	{
		if (!inmemory)	{
			close(attribute_fd);

			if (!existfile)	{
				remove(filename.c_str());
			}
		}

		free(pagelog_ptr);
		if (default_value != NULL) free(default_value);
	}

	void request_chunk(size_t chunk_id)	{
		next_chunk_id = chunk_id;
	}

	void exchange_chunk()	{
		if (inmemory) return;
		if (current_chunk_id == next_chunk_id) return;

		if (is_chunkid_valid(current_chunk_id))	{
			if (writefile) write_chunk(current_chunk_id);
		}

		if (is_chunkid_valid(next_chunk_id))	{
			read_chunk(next_chunk_id);
		}
	}

	void read_chunk(size_t chunk_id)	{
		if (inmemory) return;

		firstnode_id = chunk_id * num_values;
		lastnode_id = (chunk_id + 1 == num_chunks) ? (num_nodes - 1) : (firstnode_id + num_values - 1);
		current_chunk_id = chunk_id;

		size_t fpage_id = firstnode_id >> OFFSET_BITMAP;
		size_t lpage_id = lastnode_id >> OFFSET_BITMAP;
		size_t file_offset = chunk_id * size_chunk;

#pragma omp parallel for schedule(dynamic, 1)
		for (size_t page_id = fpage_id; page_id <= lpage_id; page_id++)	{
			if (boundtype == BOUNDTYPE_ADJ || pageflag_ptr[page_id] == true)	{
				if (default_value == NULL || pagelog_ptr[page_id] == true) {
					if (pread(attribute_fd, attribute_ptr + (page_id - fpage_id) * size_page, (page_id + 1 != num_pages) ? size_page : size_lastpage, file_offset + (page_id - fpage_id) * size_page) <= 0) {
						fprintf(stderr, "reading an attribute page in read_chunk failed\n");
						exit(1);
					}
				}
				else {
					char * value_ptr = attribute_ptr + (page_id - fpage_id) * size_page;
					char * value_ptr_end = value_ptr + ((page_id + 1 != num_pages) ? size_page : size_lastpage);
					while (value_ptr < value_ptr_end) {
						memcpy(value_ptr, default_value, size_valuetype);
						value_ptr += size_valuetype;
					}
				}

				pagelog_ptr[page_id] = true;
			}
		}
	}

	void write_chunk(size_t chunk_id)	{
		size_t fpage_id = firstnode_id >> OFFSET_BITMAP;
		size_t lpage_id = lastnode_id >> OFFSET_BITMAP;
		size_t file_offset = chunk_id * size_chunk;

#pragma omp parallel for schedule(dynamic, 1)
		for (size_t page_id = fpage_id; page_id <= lpage_id; page_id++)	{
			if (boundtype == BOUNDTYPE_ADJ || pageflag_ptr[page_id] == true)	{
				if (pwrite(attribute_fd, attribute_ptr + (page_id - fpage_id) * size_page, (page_id + 1 != num_pages) ? size_page : size_lastpage, file_offset + (page_id - fpage_id) * size_page) <= 0) {
					fprintf(stderr, "writing an attribute page in write_chunk failed\n");
					exit(1);
				}
			}
		}
	}

	void flush_chunk()	{
		if (is_chunkid_valid(current_chunk_id))	{
			if (writefile) write_chunk(current_chunk_id);
		}

		firstnode_id = 0;
		lastnode_id = num_values - 1;
		current_chunk_id = num_chunks;
	}

	bool is_chunkid_valid(size_t chunk_id)	{
		return (chunk_id < num_chunks);
	}

	bool is_load_needed(size_t lower_pageid, size_t upper_pageid)	{
		if (!is_chunkid_valid(next_chunk_id)) return false;
		if (boundtype == BOUNDTYPE_ADJ) return true;

		size_t fpage_id = (next_chunk_id * num_values) >> OFFSET_BITMAP;
		size_t lpage_id = ((next_chunk_id + 1 == num_chunks) ? (num_nodes - 1) : ((next_chunk_id + 1) * num_values - 1)) >> OFFSET_BITMAP;

		return !(fpage_id > upper_pageid || lpage_id < lower_pageid);
	}


	


	size_t get_firstnode_number()	{
		return firstnode_id;
	}
	
	size_t get_lastnode_number()	{
		return lastnode_id;
	}

	bool is_inmemory()	{
		return inmemory;
	}

	void set_boundtype(bool _boundtype)	{
		boundtype = _boundtype;
	}

	bool get_boundtype()	{
		return boundtype;
	}

	void set_writefile(bool _writefile)	{
		writefile = _writefile;
	}

	void set_nodeID() {
		nodeID = true;
	}

	size_t get_chunk_id()	{
		return current_chunk_id;
	}


	


	template<typename valuetype>
	void get_first20(valuetype * toplist)	{
		valuetype * value_ptr = (valuetype *)attribute_ptr;
		int max20ths = lastnode_id - firstnode_id + 1;
		if (max20ths > 20) max20ths = 20;
		for (int i = 0; i < max20ths; i++)	{
			*(toplist + i) = value_ptr[i];
		}
	}

	template<typename valuetype>
	void sort_chunk()	{
		valuetype * value_ptr = (valuetype *)attribute_ptr;
		std::sort(value_ptr, value_ptr + (lastnode_id - firstnode_id + 1), std::greater<valuetype>());
	}

	template<typename valuetype>
	void initialize_startnode(size_t startnode, valuetype value)	{
		if (inmemory)	{
			valuetype * value_ptr = (valuetype *)attribute_ptr;
			value_ptr[startnode] = value;
		}
		else {
			if (pwrite(attribute_fd, &value, size_valuetype, startnode * size_valuetype) <= 0) {
				fprintf(stderr, "writing an attribute page in initialize_startnode failed\n");
				exit(1);
			}
		}
	}

	template<typename valuetype>
	void set_default_value(valuetype value)	{
		valuetype * value_ptr;
		if (default_value == NULL) default_value = (char *)malloc(size_valuetype);
		value_ptr = (valuetype *)default_value;
		*value_ptr = value;

		if (inmemory) {
			if (attribute_ptr != NULL)	{
				value_ptr = (valuetype *)attribute_ptr;

#pragma omp parallel for
				for (size_t value_id = 0; value_id < num_values; value_id++) {
					value_ptr[value_id] = value;
				}
			}
		}
		else {
			if (pagelog_ptr != NULL) memset(pagelog_ptr, false, num_pages);
		}
	}

	char * get_attribute_ptr()	{
		return attribute_ptr;
	}

	template<typename valuetype>
	valuetype * get_value_ptr(size_t node_id) {
		valuetype * value_ptr = (valuetype *)attribute_ptr;
		return &value_ptr[node_id - firstnode_id];
	}

	template<typename valuetype>
	valuetype get_value(size_t node_id)	{
		valuetype * value_ptr = (valuetype *)attribute_ptr;
		return value_ptr[node_id - firstnode_id];
	}

	template<typename valuetype>
	void set_value(size_t node_id, valuetype value)	{
		valuetype * value_ptr = (valuetype *)attribute_ptr;
		value_ptr[node_id - firstnode_id] = value;
	}

	template<typename valuetype>
	void add_value(size_t node_id, valuetype value)	{
		valuetype * value_ptr = (valuetype *)attribute_ptr;
		value_ptr[node_id - firstnode_id] += value;
	}
	
	template<typename valuetype>
	void sub_value(size_t node_id, valuetype value)	{
		valuetype * value_ptr = (valuetype *)attribute_ptr;
		value_ptr[node_id - firstnode_id] -= value;
	}

	template<typename valuetype>
	void mul_value(size_t node_id, valuetype value)	{
		valuetype * value_ptr = (valuetype *)attribute_ptr;
		value_ptr[node_id - firstnode_id] *= value;
	}

	template<typename valuetype>
	void div_value(size_t node_id, valuetype value)	{
		valuetype * value_ptr = (valuetype *)attribute_ptr;
		value_ptr[node_id - firstnode_id] /= value;
	}

	template<typename valuetype>
	void lock_set_unlock_value(size_t node_id, valuetype value, std::atomic_flag * lock)	{
		while (lock->test_and_set());
		valuetype * value_ptr = (valuetype *)attribute_ptr;
		value_ptr[node_id - firstnode_id] = value;
		lock->clear();
	}

	template<typename valuetype>
	void lock_add_unlock_value(size_t node_id, valuetype value, std::atomic_flag * lock)	{
		while (lock->test_and_set());
		valuetype * value_ptr = (valuetype *)attribute_ptr;
		value_ptr[node_id - firstnode_id] += value;
		lock->clear();
	}

	template<typename valuetype>
	void lock_sub_unlock_value(size_t node_id, valuetype value, std::atomic_flag * lock)	{
		while (lock->test_and_set());
		valuetype * value_ptr = (valuetype *)attribute_ptr;
		value_ptr[node_id - firstnode_id] -= value;
		lock->clear();
	}

	template<typename valuetype>
	void lock_mul_unlock_value(size_t node_id, valuetype value, std::atomic_flag * lock)	{
		while (lock->test_and_set());
		valuetype * value_ptr = (valuetype *)attribute_ptr;
		value_ptr[node_id - firstnode_id] *= value;
		lock->clear();
	}

	template<typename valuetype>
	void lock_div_unlock_value(size_t node_id, valuetype value, std::atomic_flag * lock)	{
		while (lock->test_and_set());
		valuetype * value_ptr = (valuetype *)attribute_ptr;
		value_ptr[node_id - firstnode_id] /= value;
		lock->clear();
	}
	
	void lock_value(std::atomic_flag * lock)	{
		while (lock->test_and_set());
	}

	void unlock_value(std::atomic_flag * lock)	{
		lock->clear();
	}

	void read_chunks(size_t chunk_id)	{
		if (inmemory) return;

		firstnode_id = chunk_id * num_values;
		lastnode_id = (chunk_id + 1 == num_chunks) ? (num_nodes - 1) : (firstnode_id + num_values - 1);
		current_chunk_id = chunk_id;

		size_t fpage_id = firstnode_id >> OFFSET_BITMAP;
		size_t lpage_id = lastnode_id >> OFFSET_BITMAP;
		size_t file_offset = chunk_id * size_chunk;

#pragma omp parallel for schedule(dynamic, 1)
		for (size_t page_id = fpage_id; page_id <= lpage_id; page_id++) {
			if (pread(attribute_fd, attribute_ptr + (page_id - fpage_id) * size_page, size_page, file_offset + (page_id - fpage_id) * size_page) <= 0) {
				fprintf(stderr, "reading an attribute page in read_chunks failed\n");
				exit(1);
			}
		}
	}
};

class attribute_wrapper	{
private:
	size_t max_memory_mb;
	size_t memory_chunk_usage_b = 0;
	size_t num_nodes;
	size_t num_values;
	size_t num_chunks;
	size_t num_pages;

	
	std::vector<attribute_handler *> vectors;
	std::vector<bitattribute_handler *> bitvectors;
	std::vector<attribute_handler_info> vector_infos;

	size_t size_total_valuetype;

	size_t nod_lowerbound;
	size_t nod_upperbound;
	size_t adj_lowerbound;
	size_t adj_upperbound;

	
	bool * pageflag_ptr;			

	size_t lower_pageid;
	size_t upper_pageid;

	bool inmemory;
public:
	attribute_wrapper() {
	}

	~attribute_wrapper() {
	}

	void set_wrapper(size_t _max_memory_mb, size_t _num_nodes)	{
		inmemory = true;

		max_memory_mb = _max_memory_mb;
		num_nodes = _num_nodes;

		size_total_valuetype = 0;
	}

	void unset_wrapper()	{
		for (size_t i = 0; i < vectors.size(); i++)	{
			vectors[i]->unset();
		}

		for (size_t i = 0; i < bitvectors.size(); i++)	{
			bitvectors[i]->destroy();
		}

		if (!inmemory) free(pageflag_ptr);
		vectors.clear();
		bitvectors.clear();
	}


	


	attribute_handler * get_vector(size_t vector_index)	{
		return vectors[vector_index];
	}

	void set_vector(std::string filename, size_t size_valuetype, bool existfile)	{
		vector_infos.push_back(attribute_handler_info(filename, size_valuetype, existfile));

		size_total_valuetype += size_valuetype;
	}

	
	size_t build_vectors()	{
		size_t memory_total_usage_b = num_nodes * size_total_valuetype;
		size_t memory_remain_mb = max_memory_mb - (num_nodes / 8 / 1024 / 1024 + 1) * bitvectors.size();

		for (num_chunks = 1; true; num_chunks++) {
			memory_chunk_usage_b = ((memory_total_usage_b / num_chunks) / size_total_valuetype + 1) * size_total_valuetype;

			if (memory_chunk_usage_b < (memory_remain_mb * 1024 * 1024)) break;
		}

		num_values = (((memory_chunk_usage_b / size_total_valuetype) >> OFFSET_BITMAP) + 1) << OFFSET_BITMAP;
		num_values = num_values >= num_nodes ? num_nodes : num_values;
		
		inmemory = (num_chunks == 1);

		num_pages = (num_nodes >> OFFSET_BITMAP) + 1;

		for (size_t vector_index = 0; vector_index < vector_infos.size(); vector_index++)	{
			vector_infos[vector_index].num_nodes = num_nodes;
			vector_infos[vector_index].num_chunks = num_chunks;
			vector_infos[vector_index].num_pages = num_pages;
			vector_infos[vector_index].num_values = num_values;

			vectors.push_back(new attribute_handler(vector_infos[vector_index]));
		}

		return num_chunks;
	}

	
	void initialize_vectors() {
		pageflag_ptr = (bool *)malloc(num_pages);
		memset(pageflag_ptr, false, num_pages);
		lower_pageid = num_pages;
		upper_pageid = 0;

		for (size_t vector_index = 0; vector_index < vector_infos.size(); vector_index++) {
			vectors[vector_index]->set(pageflag_ptr);
		}
	}

	bitattribute_handler * get_bitvector(size_t vector_index)	{
		return bitvectors[vector_index];
	}

	void build_bitvector()	{
		bitattribute_handler * bitarray_ptr = new bitattribute_handler();
		bitarray_ptr->create(num_nodes);

		bitvectors.push_back(bitarray_ptr);
	}


	


	void set_pageflag(size_t first_nodeid, size_t last_nodeid)	{
		if (inmemory) return;

		size_t fpage_id = first_nodeid >> OFFSET_BITMAP;
		size_t lpage_id = last_nodeid >> OFFSET_BITMAP;

		for (size_t index_pageflag = fpage_id; index_pageflag <= lpage_id; index_pageflag++)	{
			pageflag_ptr[index_pageflag] = true;
		}

		if (lower_pageid > fpage_id) lower_pageid = fpage_id;
		if (upper_pageid < lpage_id) upper_pageid = lpage_id;
	}

	bool is_load_needed()	{
		for (size_t vector_index = 0; vector_index < vectors.size(); vector_index++)	{
			if (vectors[vector_index]->is_load_needed(lower_pageid, upper_pageid) == false) return false;
		}

		return true;
	}

	
	void exchange_chunks(bool alledge)	{
		for (size_t vector_index = 0; vector_index < vectors.size(); vector_index++)	{
			vectors[vector_index]->exchange_chunk();
		}

		nod_lowerbound = 0;
		nod_upperbound = num_nodes;
		adj_lowerbound = 0;
		adj_upperbound = num_nodes;

		for (size_t vector_index = 0; vector_index < vectors.size(); vector_index++)	{
			attribute_handler * attribute_ptr = vectors[vector_index];
			size_t lower = attribute_ptr->get_firstnode_number();
			size_t upper = attribute_ptr->get_lastnode_number();

			if (!alledge)	{
				if (attribute_ptr->get_boundtype() == BOUNDTYPE_NOD)	{
					nod_lowerbound = nod_lowerbound < lower ? lower : nod_lowerbound;
					nod_upperbound = nod_upperbound > upper ? upper : nod_upperbound;
				}
				else {
					adj_lowerbound = adj_lowerbound < lower ? lower : adj_lowerbound;
					adj_upperbound = adj_upperbound > upper ? upper : adj_upperbound;
				}
			}
			else {
				nod_lowerbound = nod_lowerbound < lower ? lower : nod_lowerbound;
				nod_upperbound = nod_upperbound > upper ? upper : nod_upperbound;
				adj_lowerbound = adj_lowerbound < lower ? lower : adj_lowerbound;
				adj_upperbound = adj_upperbound > upper ? upper : adj_upperbound;
			}
		}
	}

	
	void flush_chunks()	{
		for (size_t vector_index = 0; vector_index < vectors.size(); vector_index++)	{
			if (!vectors[vector_index]->is_inmemory())	{
				vectors[vector_index]->flush_chunk();
			}
		}

		memset(pageflag_ptr, false, num_pages);
		lower_pageid = num_pages;
		upper_pageid = 0;
	}


	


	bool is_inmemory()	{
		return inmemory;
	}

	size_t get_nod_lowerbound()	{
		return nod_lowerbound;
	}

	size_t get_nod_upperbound()	{
		return nod_upperbound;
	}

	size_t get_adj_lowerbound()	{
		return adj_lowerbound;
	}

	size_t get_adj_upperbound()	{
		return adj_upperbound;
	}

	size_t get_memory_chunk_usage() {
		return memory_chunk_usage_b;
	}
};

#endif