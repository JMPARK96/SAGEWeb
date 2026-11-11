#ifndef __META_HANDLER_HPP__
#define __META_HANDLER_HPP__

#include <generals.hpp>

#define IN_BLOCKS 1
#define OUT_BLOCKS 2
#define ALL_BLOCKS 3

struct graphmeta	{
	int block_id;					
	vid_t first_nodeid;				
	vid_t last_nodeid;				
};

struct algmeta	{
	std::atomic_flag * lock_ptr;
	int nblocks;
	char * lptemp_ptr;
};

class meta_handler {
private:
	
	struct graphmeta * graphmeta;	
	struct algmeta ** algmeta_ptr;
	struct algmeta * algmeta;		
	BLOCK ** bufferindex;			
	BLOCK ** cacheindex;
	std::atomic_flag * lock;
	char * lptemp;
	bool * needed_flag;

	
	std::string datapath;
	std::string filename;
	int size_index;					
	size_t num_nodes;				
	size_t num_edges;				

	int first_index;
	int last_index;

	int size_index_out;
	int size_index_in;

	int edge_direction;

	size_t size_lptemp;
	char * default_lptemp;

public:
	meta_handler()	{
	}

	~meta_handler()	{
	}

	std::string filename_graphinfo()	{
		std::stringstream ss;
		ss << datapath << filename << ".graphinfo";
		return ss.str();
	}

	std::string filename_graphindex()	{
		std::stringstream ss;
		ss << datapath << filename << ".graphindex";
		return ss.str();
	}

	int get_tableindex_with_node(int start, int stop, vid_t node_id) {
		struct graphmeta * graphmeta_ptr = std::lower_bound(&graphmeta[start], &graphmeta[stop], node_id, [](const struct graphmeta& graphmeta, vid_t node_id) {return graphmeta.last_nodeid < node_id;});

		return (graphmeta_ptr - graphmeta);
	}

	void load_graphmeta(std::string _datapath, std::string _filename)	{
		datapath = _datapath;
		filename = _filename;

		FILE * fp = NULL;

		
		fp = fopen(filename_graphinfo().c_str(), "r+b");

		fseek(fp, 0, SEEK_END);
		if (ftell(fp) == 20) {
			fseek(fp, 0, SEEK_SET);

			if (fread(&size_index_out, sizeof(int), 1, fp) <= 0) {
				fprintf(stderr, "reading size_index_out failed\n");
				exit(1);
			}
			if (fread(&size_index_in, sizeof(int), 1, fp) <= 0) {
				fprintf(stderr, "reading size_index_in failed\n");
				exit(1);
			}
			vid_t _num_nodes;
			if (fread(&_num_nodes, sizeof(vid_t), 1, fp) <= 0) {
				fprintf(stderr, "reading num_nodes failed\n");
				exit(1);
			}
			num_nodes = (size_t)_num_nodes;
			if (fread(&num_edges, sizeof(size_t), 1, fp) <= 0) {
				fprintf(stderr, "reading num_edges failed\n");
				exit(1);
			}

			fseek(fp, 0, SEEK_SET);

			if (fwrite(&size_index_out, sizeof(int), 1, fp) <= 0) {
				fprintf(stderr, "writing size_index_out failed\n");
				exit(1);
			}
			if (fwrite(&size_index_in, sizeof(int), 1, fp) <= 0) {
				fprintf(stderr, "writing size_index_in failed\n");
				exit(1);
			}
			if (fwrite(&num_nodes, sizeof(size_t), 1, fp) <= 0) {
				fprintf(stderr, "writing num_nodes failed\n");
				exit(1);
			}
			if (fwrite(&num_edges, sizeof(size_t), 1, fp) <= 0) {
				fprintf(stderr, "writing num_edges failed\n");
				exit(1);
			}
		}
		else {
			fseek(fp, 0, SEEK_SET);
			if (fread(&size_index_out, sizeof(int), 1, fp) <= 0) {
				fprintf(stderr, "reading size_index_out failed\n");
				exit(1);
			}
			if (fread(&size_index_in, sizeof(int), 1, fp) <= 0) {
				fprintf(stderr, "reading size_index_in failed\n");
				exit(1);
			}
			if (fread(&num_nodes, sizeof(size_t), 1, fp) <= 0) {
				fprintf(stderr, "reading num_nodes failed\n");
				exit(1);
			}
			if (fread(&num_edges, sizeof(size_t), 1, fp) <= 0) {
				fprintf(stderr, "reading num_edges failed\n");
				exit(1);
			}
		}

		fclose(fp);

		size_index = size_index_out + size_index_in;

		
		graphmeta = new struct graphmeta[size_index];

		fp = fopen(filename_graphindex().c_str(), "rb");

		if (fread(graphmeta, sizeof(struct graphmeta), size_index, fp) <= 0) {
			fprintf(stderr, "reading graphmeta failed\n");
			exit(1);
		}

		fclose(fp);
	}

	void set_algmeta(size_t _size_lptemp = 4)	{
		size_lptemp = _size_lptemp;
		default_lptemp = NULL;

		
		algmeta_ptr = new struct algmeta*[size_index];
		algmeta = new struct algmeta[size_index];

		
		bufferindex = new BLOCK*[size_index];
		cacheindex = new BLOCK*[size_index];
		lock = new std::atomic_flag[size_index];
		lptemp = new char[size_lptemp * size_index];
		needed_flag = new bool[size_index];

		
		for (int index = 0; index < size_index; index++)	{
			algmeta[index].lock_ptr = NULL;;
			algmeta[index].nblocks = 0;
			algmeta[index].lptemp_ptr = NULL;
		}

		
		for (int index = 0; index < size_index; index++)	{
			bufferindex[index] = NULL;
			cacheindex[index] = NULL;
		}

		size_t outlargeblocks = 0;

		
		for (int index = 1; index < size_index_out;)	{
			int start_index = index - 1;
			int stop_index = index;

			for (; stop_index < size_index_out; stop_index++)	{
				if (graphmeta[stop_index].first_nodeid != graphmeta[stop_index - 1].first_nodeid)	{
					break;
				}
			}

			for (int i = start_index; i < stop_index; i++)	{
				algmeta[i].lock_ptr = &lock[start_index];
				algmeta[i].lock_ptr->clear();
				algmeta[i].nblocks = stop_index - start_index;
				algmeta[i].lptemp_ptr = &lptemp[size_lptemp * start_index];
				algmeta_ptr[i] = &algmeta[start_index];
			}

			if (stop_index - start_index > 1) outlargeblocks += stop_index - start_index;

			index = stop_index + 1;
		}

		if (size_index_out > 0 && algmeta[size_index_out - 1].nblocks == 0)		{
			algmeta[size_index_out - 1].lock_ptr = &lock[size_index_out - 1];
			algmeta[size_index_out - 1].lock_ptr->clear();
			algmeta[size_index_out - 1].nblocks = 1;
			algmeta[size_index_out - 1].lptemp_ptr = &lptemp[size_lptemp * (size_index_out - 1)];
			algmeta_ptr[size_index_out - 1] = &algmeta[size_index_out - 1];
		}

		size_t inlargeblocks = 0;

		
		for (int index = size_index_out + 1; index < size_index;)	{
			int start_index = index - 1;
			int stop_index = index;

			for (; stop_index < size_index; stop_index++)	{
				if (graphmeta[stop_index].first_nodeid != graphmeta[stop_index - 1].first_nodeid)	{
					break;
				}
			}

			for (int i = start_index; i < stop_index; i++)	{
				algmeta[i].lock_ptr = &lock[start_index];
				algmeta[i].lock_ptr->clear();
				algmeta[i].nblocks = stop_index - start_index;
				algmeta[i].lptemp_ptr = &lptemp[size_lptemp * start_index];
				algmeta_ptr[i] = &algmeta[start_index];
			}

			if (stop_index - start_index > 1) inlargeblocks += stop_index - start_index;

			index = stop_index + 1;
		}

		if (algmeta[size_index - 1].nblocks == 0)		{
			algmeta[size_index - 1].lock_ptr = &lock[size_index - 1];
			algmeta[size_index - 1].lock_ptr->clear();
			algmeta[size_index - 1].nblocks = 1;
			algmeta[size_index - 1].lptemp_ptr = &lptemp[size_lptemp * (size_index - 1)];
			algmeta_ptr[size_index - 1] = &algmeta[size_index - 1];
		}

		
		first_index = 0;
		last_index = size_index;

		edge_direction = ALL_BLOCKS;

		std::cout << "largeblocks out " << outlargeblocks << " / in " << inlargeblocks << std::endl;
	}

	void default_algmeta(char * _default_lptemp)	{
		default_lptemp = new char[size_lptemp];
		memcpy(default_lptemp, _default_lptemp, size_lptemp);
	}

	void reset_algmeta()	{
		for (int index = first_index; index < last_index; index++)	{
			struct lpmeta * lpmeta_ptr = (struct lpmeta *)algmeta[index].lptemp_ptr;
			lpmeta_ptr->reset();
		}
	}

	void unload_graphmeta()	{
		delete algmeta_ptr;
		delete algmeta;
		delete graphmeta;
		delete cacheindex;
		delete bufferindex;
		delete lock;
		delete lptemp;
		delete needed_flag;
	}

	int get_size_in() {
		return size_index_in;
	}

	int get_size_out() {
		return size_index_out;
	}

	int get_size_index()	{
		return size_index;
	}

	vid_t get_num_nodes()	{
		return num_nodes;
	}

	size_t get_num_edges()	{
		return num_edges;
	}

	vid_t get_first_nodeid(int tableindex)	{
		return graphmeta[tableindex].first_nodeid;
	}

	vid_t get_last_nodeid(int tableindex)	{
		return graphmeta[tableindex].last_nodeid;
	}

	int get_blockid(int tableindex)	{
		return graphmeta[tableindex].block_id;
	}

	struct graphmeta get_graphmeta(int tableindex)	{
		return graphmeta[tableindex];
	}

	struct graphmeta * get_graphmeta_ptr() {
		return graphmeta;
	}

	std::atomic_flag * get_lock_ptr(int tableindex)	{
		return algmeta_ptr[tableindex]->lock_ptr;
	}

	int get_nblocks(int tableindex)	{
		return algmeta_ptr[tableindex]->nblocks;
	}

	char * get_lptemp_ptr(int tableindex)	{
		return algmeta_ptr[tableindex]->lptemp_ptr;
	}

	bool * get_needed_flag_ptr() {
		return needed_flag;
	}

	bool get_needed_flag(int tableindex) {
		return needed_flag[tableindex];
	}

	void set_needed_flag(int tableindex, bool flag) {
		needed_flag[tableindex] = flag;
	}

	struct algmeta get_algmeta(int tableindex)	{
		return *algmeta_ptr[tableindex];
	}

	BLOCK * get_bufferindex(int tableindex)	{
		return bufferindex[tableindex];
	}

	void set_bufferindex(int tableindex, BLOCK * ptr)	{
		bufferindex[tableindex] = ptr;
	}

	BLOCK ** get_bufferindex_ptr()	{
		return bufferindex;
	}

	BLOCK * get_cacheindex(int tableindex)	{
		return cacheindex[tableindex];
	}

	void set_cacheindex(int tableindex, BLOCK * ptr)	{
		cacheindex[tableindex] = ptr;
	}

	BLOCK ** get_cacheindex_ptr()	{
		return cacheindex;
	}

	int get_first_index()	{
		return first_index;
	}

	int get_mid_index() {
		return size_index_out;
	}

	int get_last_index()	{
		return last_index;
	}

	void set_blocks_in()	{
		first_index = size_index_out;
		last_index = size_index;

		edge_direction = IN_BLOCKS;
	}

	void set_blocks_out()	{
		first_index = 0;
		last_index = size_index_out;

		edge_direction = OUT_BLOCKS;
	}

	void set_blocks_all()	{
		first_index = 0;
		last_index = size_index;

		edge_direction = ALL_BLOCKS;
	}

	int get_edge_direction()	{
		return edge_direction;
	}
};

#endif