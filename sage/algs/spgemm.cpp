// Code checked //
// rowbyrow product hash ver4 //

#define weighted

#include <generals.hpp>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <spgemm_types.hpp>

//compare function for intermediate sorting
int compare_y(const void * a, const void * b) {
	return (((adj__ *)a)->y > ((adj__ *)b)->y) - (((adj__*)a)->y < ((adj__*)b)->y);
}


#define INNER 0
#define OUTER 1
#define ROWBA 2
#define ROWAB 3

/* spgemm */

#ifdef USE_NODE_ENGINE
#include <engine-spgemm_node.hpp>
#else
#include <engine-spgemm.hpp>
#endif
#include <minheap.hpp>

#define spgemm_type float
#define deg_type int


class spgemm_algorithm : public algorithm_handler {
private:
	information_handler * information_ptr;

	//	attribute_wrapper * attribute_ptr;
	indicator_handler * indicator_ptr;
	parameter_handler * parameter_ptr;
	meta_handler * meta_a_ptr;
	meta_handler * meta_b_ptr;

	bool set_indicator;

	std::string datapath;

	/* spgemm */
	//boolean flags
	bool merge;
	bool enginerun;

	//constant parameters
	size_t A_max_mb;
	size_t B_max_mb;
	size_t C_max_mb;
	size_t total_mb;
	int cooSize;
	int num_threads;

	//groupmeta
	int num_group;
	int groupmask;

	//output filename
	std::string filename;
	
	//thread-specific
	thread_data ** thread_data_pptr;

	//hash space
	vid_t hashmask;
	vid_t maxHashSize;
	vid_t availHashSize;

	//spgemm-specific
	int round;
	/* spgemm */

	time_entry t;
public:
	spgemm_algorithm() : algorithm_handler(), t("engine") {
	}

	~spgemm_algorithm() {
	}

	std::string output_meta_filename(int t) {
		std::stringstream ss;
		ss << datapath << filename << ".output.meta." << t;
		return ss.str();
	}

	std::string output_intermediate(int t, int r, int i) {
		std::stringstream ss;
		ss << datapath << filename << ".output." << t << "_" << r << "_" << i;
		return ss.str();
	}
	
	std::string output_final_filename(int i) {
		std::stringstream ss;
		ss << datapath << filename << "_result_final_end_" << i;
		return ss.str();
	}

	void initialize(information_handler * _information_ptr, struct thread_data ** _thread_data_pptr)	{
		t.start_time();

		/* setting pointer initialization */
		information_ptr = _information_ptr;

		//		attribute_ptr = information_ptr->attribute_ptr;
		indicator_ptr = information_ptr->indicator_ptr;
		parameter_ptr = information_ptr->parameter_ptr;
		meta_a_ptr = information_ptr->meta_a_ptr;
		meta_b_ptr = information_ptr->meta_b_ptr;

		set_indicator = indicator_ptr->is_indicated();
		datapath = parameter_ptr->get_param_string("datapath", "./");

		/* attribute wrapper initialization */

		/* algorithm specific initialization */

		/* attribute vector initialization */

		meta_a_ptr->set_algmeta();
		meta_b_ptr->set_algmeta();

		/* spgemm */
		merge = parameter_ptr->get_param_int("merge", 1) == 1 ? true : false;
		enginerun = parameter_ptr->get_param_int("enginerun", 1) == 1 ? true : false;
		groupmask = parameter_ptr->get_param_int("groupmask", 10);
		A_max_mb = (size_t)parameter_ptr->get_param_int("a_mb", 1024);
		B_max_mb = (size_t)parameter_ptr->get_param_int("b_mb", 1024);
		C_max_mb = (size_t)parameter_ptr->get_param_int("c_mb", 1024);

		total_mb = parameter_ptr->get_param_int("total_mb", A_max_mb + B_max_mb + C_max_mb);

		num_group = (information_ptr->num_nodes >> groupmask) + 1;

		cooSize = (C_max_mb * 1048576) / (information_ptr->num_threads * (sizeof(adj__) + sizeof(vid_t) + sizeof(vid_t)));

		int i = 10;
		for (vid_t hashSize = 2; (hashSize << i) < cooSize; i++);
		maxHashSize = 2 << (i - 1);
		availHashSize = maxHashSize * 0.5;
		std::cout << "hash size " << availHashSize << " / " << maxHashSize << std::endl;

		filename = parameter_ptr->get_param_string("output", "undefined");
		num_threads = information_ptr->num_threads;
		thread_data_pptr = _thread_data_pptr;
		hashmask = (1 << (i - 1)) - 1;

		std::cout << "hashmask " << hashmask << std::endl;

		for (int index = 1; index < num_threads + 1; index++) {
			thread_data * thread_data_ptr = thread_data_pptr[index];
			thread_data_ptr->output = new adj__[availHashSize];
			thread_data_ptr->outputHash = new vid_t[availHashSize];
			thread_data_ptr->hashBuffer = new vid_t[maxHashSize];
			thread_data_ptr->threadindex = index;

			thread_data_ptr->flops_per_thread = 0;
			thread_data_ptr->compressed_per_thread = 0;
			thread_data_ptr->num_elements_per_thread = 0;

			//Initialise all elements of array as NULL 
			for (vid_t i = 0; i < maxHashSize; i++) {
				thread_data_ptr->hashBuffer[i] = -1;
			}

			thread_data_ptr->groupmeta_list = new meta__[1024];
			thread_data_ptr->group_size = 0;

			unlink(output_meta_filename(index).c_str());
			thread_data_ptr->fd_meta = open(output_meta_filename(index).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
		}
		/* spgemm */

		round = 0;
	}

	void before_iteration()	{
		meta_a_ptr->reset_algmeta();
		meta_b_ptr->reset_algmeta();
	}

	bool request_nextchunk()	{
		return false;
	}

	void rowrowProduct(struct blockmeta & A_blockmeta, struct blockmeta * B_blockmeta_ptr, int loadsize, struct thread_data * thread_data_ptr) {
#ifdef ANALYSIS_STATIC_WORKER_TIME
		//std::chrono::system_clock::time_point appStart = std::chrono::system_clock::now();
#endif	

		/* thread-specific initialization */
		adj__ * output = thread_data_ptr->output;			// edge들을 저장할 메모리 공간을 가리키는 포인터
		vid_t * outputHash = thread_data_ptr->outputHash;
		vid_t * hashBuffer = thread_data_ptr->hashBuffer;
		vid_t & num_elements_per_thread = thread_data_ptr->num_elements_per_thread;
		size_t & flops_per_thread = thread_data_ptr->flops_per_thread;
		size_t & compressed_per_thread = thread_data_ptr->compressed_per_thread;
		std::vector<int> & fd_list = thread_data_ptr->fd_list;
		int& group_idx = thread_data_ptr->group_idx;
		meta__ * groupmeta_list = thread_data_ptr->groupmeta_list;
		int& group_size = thread_data_ptr->group_size;
		vid_t& num_total_fd = thread_data_ptr->num_total_fd;
		int& prev_group_idx = thread_data_ptr->prev_group_idx;
		int& fd_meta = thread_data_ptr->fd_meta;
		size_t& group_counter = thread_data_ptr->group_counter;

		int& threadindex = thread_data_ptr->threadindex;

		struct stat64 buf;

		/* algorithm-specific initialization */
		vid_t B_interval_first_nodeid = B_blockmeta_ptr[0].graphmeta.first_nodeid;
		vid_t B_interval_last_nodeid = B_blockmeta_ptr[loadsize - 1].graphmeta.last_nodeid;

		vid_t * A_adjlist_ptr = NULL;
		val_t * A_vallist_ptr = NULL;
		int A_full_length = 0;
		int A_length = 0;
		vid_t A_first_nodeid = A_blockmeta.graphmeta.first_nodeid;
		vid_t A_last_nodeid = A_blockmeta.graphmeta.last_nodeid;
		BLOCK * A_block_ptr = A_blockmeta.block_ptr;
		int A_slotno = 0;

		vid_t * B_adjlist_ptr = NULL;
		val_t * B_vallist_ptr = NULL;
		int B_full_length = 0;
		int B_length = 0;
		vid_t B_first_nodeid;
		vid_t B_last_nodeid;
		BLOCK * B_block_ptr = NULL;
		int B_slotno = 0;


#ifdef USE_NODE_ENGINE
		// node-based: process only the dispatched node_id row
		vid_t A_proc_node = A_blockmeta.node_id;
		A_slotno = A_proc_node - A_first_nodeid;
		bool ldi_flag = (thread_data_ptr->prev_node_idx == A_proc_node);
		thread_data_ptr->prev_node_idx = A_proc_node;
#else
		bool ldi_flag = (thread_data_ptr->prev_node_idx == A_first_nodeid);
		thread_data_ptr->prev_node_idx = A_first_nodeid;
		A_slotno = 0;
		vid_t A_proc_node = A_first_nodeid;
#endif

		vid_t choice_bound[2];
		vid_t choice_group[2];
		choice_group[0] = 0;

#ifdef USE_NODE_ENGINE
		for (vid_t A_x = A_proc_node; A_x <= A_proc_node; A_x++) {
#else
		for (vid_t A_x = A_first_nodeid; A_x <= A_last_nodeid; A_x++) {
#endif
			A_full_length = A_block_ptr->slot[-A_slotno].length;
			A_adjlist_ptr = (vid_t *)&(A_block_ptr->data[A_block_ptr->slot[-A_slotno].offset]);
			A_slotno++;

			A_vallist_ptr = (val_t *)&A_adjlist_ptr[A_full_length];

			A_length = A_full_length;

			if (prev_group_idx == -1) prev_group_idx = (A_x >> groupmask);
			if (prev_group_idx != (A_x >> groupmask)) {
				if (group_size != 0) {
					write(fd_meta, &prev_group_idx, sizeof(int));
					write(fd_meta, &group_size, sizeof(int));
					write(fd_meta, &groupmeta_list[0], sizeof(meta__) * group_size);
					group_counter++;
					group_size = 0;
				}

				prev_group_idx = (A_x >> groupmask);
			}

			/* if interval of A_adjlist intersects with interval of B_blocks */
			if (A_length > 0 && !(A_adjlist_ptr[0] > B_interval_last_nodeid || A_adjlist_ptr[A_length - 1] < B_interval_first_nodeid)) {
				/* then find the first and last index of intersection between A_adjlist and B_blocks */
				vid_t A_adjlist_first = std::lower_bound(A_adjlist_ptr, A_adjlist_ptr + A_length, B_interval_first_nodeid) - A_adjlist_ptr;

				choice_group[1] = group_idx;
				group_idx = choice_group[ldi_flag];

				int B_index = 0;
				choice_bound[0] = A_adjlist_ptr[A_length - 1];
				for (vid_t A_adj_index = A_adjlist_first; A_adj_index < A_length;) {
					/* find the first intersecting block in the remaining B_blocks */
					for (; B_index < loadsize; B_index++) {
						/* this version only considers all objects are stored consecutively */
						if (A_adjlist_ptr[A_adj_index] <= B_blockmeta_ptr[B_index].graphmeta.last_nodeid) {
							B_first_nodeid = B_blockmeta_ptr[B_index].graphmeta.first_nodeid;
							B_last_nodeid = B_blockmeta_ptr[B_index].graphmeta.last_nodeid;
							B_block_ptr = B_blockmeta_ptr[B_index].block_ptr;
							choice_bound[1] = B_last_nodeid;
							break;
						}
					}

					/* break if out of B_blocks */
					if (B_index == loadsize)
						break;

					/* calculate the lowerbound of maximum nodeid in A_adjlist with the block */
					vid_t B_blockbound_nodeid = choice_bound[(choice_bound[0] > choice_bound[1])];

					B_slotno = 0;
					for (; A_adj_index < A_length && A_adjlist_ptr[A_adj_index] <= B_blockbound_nodeid; A_adj_index++) {
						val_t A_v = A_vallist_ptr[A_adj_index];

						/* find the location of the record on the block */
						B_slotno = A_adjlist_ptr[A_adj_index] - B_first_nodeid;
						B_length = B_block_ptr->slot[-B_slotno].length;
						B_adjlist_ptr = (vid_t*) & (((char*)B_block_ptr)[B_block_ptr->slot[-B_slotno].offset]);

						B_vallist_ptr = (val_t*)& B_adjlist_ptr[B_length];

						//algorithm part (A: A_x, A_y, A_v \\ B: vid_t * adjlist_ptr, val_t * vallist_ptr, vid_t B_length)
						{
							vid_t B_adj_index = 0;

#ifdef NO_PARTIAL_AGGRE
							for (; B_adj_index < B_length; B_adj_index++) {
								vid_t key = B_adjlist_ptr[B_adj_index];
								val_t value = A_v * B_vallist_ptr[B_adj_index];

								output[num_elements_per_thread++] = adj__(key, value);

								if (num_elements_per_thread == availHashSize) { // availHashSize = 50% of maxHashSize, 메모리 버퍼
									if (group_idx == (int)fd_list.size()) {
										std::string ipath = output_intermediate(threadindex, round, group_idx);
										unlink(ipath.c_str());
										fd_list.push_back(open(ipath.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644));
									}

									if (group_idx == group_size) {
										if (fstat64(fd_list[group_idx], &buf) == -1) {
											perror("fstat");
											exit(1);
										}
										groupmeta_list[group_size++] = meta__(num_total_fd + group_idx, num_elements_per_thread, buf.st_size);
									}
									else {
										groupmeta_list[group_idx].len += num_elements_per_thread;
									}

									qsort(output, num_elements_per_thread, sizeof(adj__), compare_y);
									write(fd_list[group_idx], &A_x, sizeof(vid_t));
									write(fd_list[group_idx], &num_elements_per_thread, sizeof(vid_t));
									write(fd_list[group_idx], output, sizeof(adj__) * num_elements_per_thread);

									compressed_per_thread += num_elements_per_thread;
									num_elements_per_thread = 0;

									group_idx++;
								}
							}
#else
							for (; B_adj_index < B_length; B_adj_index++) {
								// Apply hash function to find index for given key
								vid_t key = B_adjlist_ptr[B_adj_index];
								val_t value = A_v * B_vallist_ptr[B_adj_index];

								vid_t hashIndex = key & hashmask;
								vid_t jump = 0;

								vid_t counter = hashBuffer[hashIndex];

								//find next free space (open addressing)
								while (counter != -1 && output[counter].y != key) {
									hashIndex += (++jump);
									hashIndex &= hashmask;
									counter = hashBuffer[hashIndex];
								}

								//if new node to be inserted increase the current size
								if (counter == -1) {
									hashBuffer[hashIndex] = num_elements_per_thread;
									output[num_elements_per_thread] = adj__(key, value);
									outputHash[num_elements_per_thread++] = hashIndex;

									if (num_elements_per_thread == availHashSize) {
										//adding a file if needed
										if (group_idx == (int)fd_list.size()) {
											std::string ipath = output_intermediate(threadindex, round, group_idx);
											unlink(ipath.c_str());
											fd_list.push_back(open(ipath.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644));
										}

										//resetting hashbuffer
										for (vid_t i = 0; i < num_elements_per_thread; i++) {
											hashBuffer[outputHash[i]] = -1;
										}

										//storing metadata
										if (group_idx == group_size) {
											if (fstat64(fd_list[group_idx], &buf) == -1) {
												perror("fstat");
												exit(1);
											}
											groupmeta_list[group_size++] = meta__(num_total_fd + group_idx, num_elements_per_thread, buf.st_size);
										}
										else {
											groupmeta_list[group_idx].len += num_elements_per_thread;
										}

										//sorting & writing intermediate results
										qsort(output, num_elements_per_thread, sizeof(adj__), compare_y);
										write(fd_list[group_idx], &A_x, sizeof(vid_t));
										write(fd_list[group_idx], &num_elements_per_thread, sizeof(vid_t));
										write(fd_list[group_idx], output, sizeof(adj__) * num_elements_per_thread);

										compressed_per_thread += num_elements_per_thread;
										num_elements_per_thread = 0;

										group_idx++;
									}
								}
								else {
									output[counter].v += value;
								}
							}
#endif
						}

						flops_per_thread += (size_t)B_length;

						if (B_blockmeta_ptr[B_index].lb_offset == 1)
							break;
					}
					
					B_index++;
				}

				if (num_elements_per_thread > 0) {
					//adding a file if needed
					if (group_idx == (int)fd_list.size()) {
						std::string ipath = output_intermediate(threadindex, round, group_idx);
						unlink(ipath.c_str());
						fd_list.push_back(open(ipath.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644));
					}

#ifndef NO_PARTIAL_AGGRE
					//resetting hashbuffer
					for (vid_t i = 0; i < num_elements_per_thread; i++) {
						hashBuffer[outputHash[i]] = -1;
					}
#endif

					//storing metadata
					if (group_idx == group_size) {
						if (fstat64(fd_list[group_idx], &buf) == -1) {
							perror("fstat");
							exit(1);
						}
						groupmeta_list[group_size++] = meta__(num_total_fd + group_idx, num_elements_per_thread, buf.st_size);
					}
					else {
						groupmeta_list[group_idx].len += num_elements_per_thread;
					}

					//sorting & writing intermediate results
					qsort(output, num_elements_per_thread, sizeof(adj__), compare_y);
					write(fd_list[group_idx], &A_x, sizeof(vid_t));
					write(fd_list[group_idx], &num_elements_per_thread, sizeof(vid_t));
					write(fd_list[group_idx], output, sizeof(adj__) * num_elements_per_thread);

					compressed_per_thread += num_elements_per_thread;
					num_elements_per_thread = 0;

					group_idx++;
				}
			}
		}

#ifdef ANALYSIS_STATIC_WORKER_TIME
		//std::chrono::duration<double> appEnd = std::chrono::system_clock::now() - appStart;
		//thread_data_ptr->appTime += appEnd.count();
#endif

	}

	void flush_row() {
		round++;

		for (int index = 1; index < num_threads + 1; index++) {
			thread_data* thread_data_ptr = thread_data_pptr[index];

			thread_data_ptr->reset();
		}
	}

	void after_iteration() {
	}

	void finalize() {
		t.stop_time();

		for (int index = 1; index < num_threads + 1; index++) {
			thread_data* thread_data_ptr = thread_data_pptr[index];

			close(thread_data_ptr->fd_meta);

			delete thread_data_ptr->output;
			delete thread_data_ptr->outputHash;
			delete thread_data_ptr->hashBuffer;
		}

		if (parameter_ptr->get_param_int("enginerun", 1) == 1) {
			//calculating flops and compressed
			size_t num_flops = 0;
			size_t num_compressed = 0;
			vid_t num_total_fd = 0;

			for (size_t t = 1; t < num_threads + 1; t++) {
				thread_data* thread_data_ptr = thread_data_pptr[t];

				num_flops += thread_data_ptr->flops_per_thread;
				num_compressed += thread_data_ptr->compressed_per_thread;
				num_total_fd += thread_data_ptr->num_total_fd;
			}
			std::cout << "flops " << num_flops << " compressed " << num_compressed << " totalfd " << num_total_fd << std::endl;
			
			//groupmeta info
			int group_idx;
			int group_size;
			meta__ groupmeta;

			size_t * group_count = new size_t[num_group];
			memset(group_count, 0, sizeof(size_t) * num_group);

			for (size_t t = 1; t < num_threads + 1; t++) {
				thread_data* thread_data_ptr = thread_data_pptr[t];

				int fd_meta = open(output_meta_filename(t).c_str(), O_RDONLY, 775);
				while (read(fd_meta, &group_idx, sizeof(int)) > 0) {					
					read(fd_meta, &group_size, sizeof(int));

					for (size_t g = 0; g < group_size; g++) {
						read(fd_meta, &groupmeta, sizeof(meta__));

						group_count[group_idx] += (size_t)groupmeta.len;
					}
				}
				close(fd_meta);
			}
			size_t counter = 0;
			for (int g = 0; g < num_group; g++) {
				counter += group_count[g];
			}
			std::cout << "total groups " << num_group << " compressed " << counter << std::endl;

			//merge threshold
			size_t tempMsize = num_compressed / num_threads;	//스레드 마다 평균처리해야하는 중간결과의 수
			std::vector<vid_t> merge_threshold;
			size_t sum = 0;
			size_t num_elements = num_compressed;

			merge_threshold.push_back(0);
			for (vid_t i = 0; i < num_group; i++) {
				sum += group_count[i];

				if (sum > tempMsize) {
					num_elements -= (sum - group_count[i]);
					tempMsize = num_elements / (num_threads - merge_threshold.size());
					merge_threshold.push_back(i);
					sum = group_count[i];
				}
			}
			merge_threshold.push_back(num_group);
			// pad to exactly num_threads+1 so all OMP threads get valid ranges
			while ((int)merge_threshold.size() < num_threads + 1) {
				merge_threshold.push_back(merge_threshold.back());
			}
			for (size_t i = 0; i < merge_threshold.size(); i++) {
				std::cout << "merge threshold " << merge_threshold[i] << std::endl;
			}
			std::cout << "total threshold " << merge_threshold.size() << std::endl;

			delete group_count;
			
			//file interval length
			off64_t* off_count = new off64_t[num_total_fd * num_threads];
			for (size_t i = 0; i < num_total_fd * num_threads; i++) {
				off_count[i] = off64_t(-1);
			}
			size_t num_acc_fd = 0;

			for (size_t t = 1; t < num_threads + 1; t++) {
				thread_data* thread_data_ptr = thread_data_pptr[t];

				int fd_meta = open(output_meta_filename(t).c_str(), O_RDONLY, 775);
				for (size_t r = 0; r < round; r++) {
					int merge_threshold_idx = 0;
					off64_t* off_ptr = off_count + num_acc_fd;

					for (size_t g = 0; g < thread_data_ptr->group_round_max[r]; g++) {
						read(fd_meta, &group_idx, sizeof(int));
						read(fd_meta, &group_size, sizeof(int));

						while (group_idx >= merge_threshold[merge_threshold_idx + 1]) {
							merge_threshold_idx++;
							off_ptr += num_total_fd;
						}

						for (size_t f = 0; f < group_size; f++) {
							read(fd_meta, &groupmeta, sizeof(meta__));

							if (off_ptr[f] == off64_t(-1)) off_ptr[f] = (size_t)groupmeta.off;
						}
					}

					num_acc_fd += thread_data_ptr->fd_round_max[r];
				}

				close(fd_meta);
			}
			std::cout << "file interval length completed" << std::endl;

			//storing metadata
			unlink("meta.bin");
			int fd_thres = open("meta.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
			write(fd_thres, &num_total_fd, sizeof(vid_t));
			for (size_t t = 1; t < num_threads + 1; t++) {
				thread_data* thread_data_ptr = thread_data_pptr[t];

				for (size_t r = 0; r < round; r++) {
					for (size_t g = 0; g < thread_data_ptr->fd_round_max[r]; g++) {
						write(fd_thres, &t, sizeof(size_t));
						write(fd_thres, &r, sizeof(size_t));
						write(fd_thres, &g, sizeof(size_t));
					}
				}
			}
			write(fd_thres, off_count, sizeof(off64_t) * num_threads * num_total_fd);
			write(fd_thres, &merge_threshold[0], sizeof(vid_t)* (num_threads + 1));
			close(fd_thres);

			delete off_count;
			std::cout << "storing metadata completed" << std::endl;
			std::cout << "merge initializing completed" << std::endl;
		}
		
		std::stringstream ss;
		ss << parameter_ptr->get_param_string("file", "test_result.txt");
		ss << "_result";

		FILE* fp = fopen(ss.str().c_str(), "wt");

		fprintf(fp, "spgemm result\n");
		fprintf(fp, "time spent %lf\n\n", t.runtime().count());
		std::cout << "time spent " << t.runtime().count() << std::endl << std::endl;

		fclose(fp);
	}
	
	void merge_parts() {
		if (!merge) return;

		std::atomic<size_t> count(0);

		//loading metadata
		int fd_thres = open("meta.bin", O_RDONLY, 775);
		vid_t num_total_fd;
		read(fd_thres, &num_total_fd, sizeof(vid_t));

		std::vector<int> fd_list;
		if (enginerun) {
			//fd initialize
			for (size_t t = 1; t < num_threads + 1; t++) {
				thread_data* thread_data_ptr = thread_data_pptr[t];

				for (size_t f = 0; f < thread_data_ptr->num_total_fd; f++) {
					fd_list.push_back(thread_data_ptr->total_fd_list[f]);
				}
				thread_data_ptr->total_fd_list.clear();
			}
			lseek64(fd_thres, num_total_fd * sizeof(size_t) * 3, SEEK_CUR);
		}
		else {
			size_t t, r, g;

			for (vid_t f = 0; f < num_total_fd; f++) {
				read(fd_thres, &t, sizeof(size_t));
				read(fd_thres, &r, sizeof(size_t));
				read(fd_thres, &g, sizeof(size_t));

				fd_list.push_back(open(output_intermediate(t, r, g).c_str(), O_RDONLY, 775));
			}
		}

		off64_t* off_count = new off64_t[num_total_fd * (num_threads + 1)];
		memset(off_count, 0, sizeof(off64_t) * num_total_fd * (num_threads + 1));
		vid_t* merge_threshold = new vid_t[num_threads + 1];
		read(fd_thres, &off_count[0], sizeof(off64_t) * num_threads * num_total_fd);
		read(fd_thres, merge_threshold, sizeof(vid_t) * (num_threads + 1));
		close(fd_thres);

		//file interval pointer
		off64_t* off_ptr = off_count + num_threads * num_total_fd;
		for (size_t f = 0; f < num_total_fd; f++) {
			struct stat64 buf;
			if (fstat64(fd_list[f], &buf) == -1) {
				perror("fstat");
				exit(1);
			}
			off_ptr[f] = buf.st_size;
		}
		for (size_t t = num_threads; t > 1; t--) {
			off64_t* off_ptr1 = off_count + (t - 1) * num_total_fd;
			off64_t* off_ptr2 = off_count + t * num_total_fd;

			for (size_t f = 0; f < num_total_fd; f++) {
				if (off_ptr1[f] == off64_t(-1)) off_ptr1[f] = off_ptr2[f];
			}
		}
		std::cout << "file interval pointer completed" << std::endl;

		/*
		size_t cc = 0;
		for (vid_t f = 0; f < num_total_fd; f++) {
			vid_t x;
			vid_t l;
			adj__ yv;

			size_t pp = 0;

			vid_t merge_threshold_idx = 1;
			bool flag = true;

			while (read(fd_list[f], &x, sizeof(vid_t)) > 0) {
				read(fd_list[f], &l, sizeof(vid_t));

				while ((merge_threshold[merge_threshold_idx - 1] << groupmask) <= x && (merge_threshold[merge_threshold_idx] << groupmask) > x) {
					if (pp != off_count[(merge_threshold_idx - 1) * num_total_fd + f])
						printf("f %u x %u x_g %u first %u last %u pp %lu off %lu noff %lu\n", f, x, x >> groupmask, merge_threshold[merge_threshold_idx - 1] << groupmask, merge_threshold[merge_threshold_idx] << groupmask, pp, off_count[(merge_threshold_idx - 1) * num_total_fd + f], off_count[(merge_threshold_idx * num_total_fd) + f]);
					merge_threshold_idx++;
				}

				lseek64(fd_list[f], sizeof(adj__) * l, SEEK_CUR);

				cc += l;
				pp += sizeof(vid_t) * 2 + sizeof(adj__) * l;
			}
		}
		printf("stored intermediate results %lu from %u files\n", cc, num_total_fd);
		*/

		omp_set_num_threads(num_threads);

#pragma omp parallel for
		for (size_t tid = 0; tid < num_threads; tid++) {
			//per thread
			vid_t startnode = merge_threshold[tid] << groupmask;
			vid_t endnode = (merge_threshold[tid + 1] << groupmask);
			
			unlink(output_final_filename(tid).c_str());
			int fd_output = open(output_final_filename(tid).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

			vid_t mem_by_file = total_mb * 1024 * 1024 / (num_total_fd * sizeof(adj__) * num_threads);

			std::vector<adj__> row_adjs;
			adj__* buffer = new adj__[num_total_fd * mem_by_file];	//파일수는 아웃풋이 8개지만 엣지파트로 나누면 더 생기니 계산필요.
			adj__** buffer_ptr = new adj__ * [num_total_fd];		//엣지 수는 변경이 있으니 한번 계산해야한다. //노드 수는 동일
			vid_t* buffer_index = new vid_t[num_total_fd];
			vid_t* buffer_maxindex = new vid_t[num_total_fd];
			vid_t* row_id = new vid_t[num_total_fd];
			vid_t* row_remain = new vid_t[num_total_fd];
			size_t* offset = (size_t *)&off_count[tid * num_total_fd];

			for (size_t fidx = 0; fidx < num_total_fd; fidx++) {
				buffer_ptr[fidx] = &buffer[fidx * mem_by_file];
				buffer_index[fidx] = 0;

				row_id[fidx] = endnode;
				row_remain[fidx] = 0;

				offset[fidx] += pread(fd_list[fidx], &row_id[fidx], sizeof(vid_t), offset[fidx]);
				offset[fidx] += pread(fd_list[fidx], &row_remain[fidx], sizeof(vid_t), offset[fidx]);
			}

			MinHeap mh(num_total_fd);

			vid_t choice[2];
			choice[0] = mem_by_file;

			//printf("filenumber %d startnode %d endnode %d\n", file_number,startnode,endnode);
			for (vid_t nodenumber = startnode; nodenumber < endnode; nodenumber++) {
				//read data from storage
				for (size_t fidx = 0; fidx < num_total_fd; fidx++) {
					if (row_id[fidx] == nodenumber) {
						choice[1] = row_remain[fidx];
						size_t read_byte = (vid_t)pread(fd_list[fidx], buffer_ptr[fidx], sizeof(adj__) * choice[(choice[0] > choice[1])], offset[fidx]);
						buffer_maxindex[fidx] = read_byte / sizeof(adj__);
						buffer_index[fidx] = 0;
						offset[fidx] += read_byte;
						row_remain[fidx] -= buffer_maxindex[fidx];

						adj__* ptr = buffer_ptr[fidx];
						int index = buffer_index[fidx];

						mh.insertKey(qData(ptr[index].y, ptr[index].v, fidx));
					}
				}

				//insert first element
				row_adjs.clear();
				if (mh.isEmpty()) {
					qData qdata = mh.extractMin();
					row_adjs.push_back(adj__(qdata.y, qdata.v));
					size_t fidx = qdata.f_num;

					if (++buffer_index[fidx] < buffer_maxindex[fidx]) {
						adj__* ptr = buffer_ptr[fidx];
						int index = buffer_index[fidx];

						mh.insertKey(qData(ptr[index].y, ptr[index].v, fidx));
					}
					else if (row_remain[fidx] != 0) {
						choice[1] = row_remain[fidx];
						size_t read_byte = (vid_t)pread(fd_list[fidx], buffer_ptr[fidx], sizeof(adj__) * choice[(choice[0] > choice[1])], offset[fidx]);
						buffer_maxindex[fidx] = read_byte / sizeof(adj__);
						buffer_index[fidx] = 0;
						offset[fidx] += read_byte;
						row_remain[fidx] -= buffer_maxindex[fidx];

						adj__* ptr = buffer_ptr[fidx];
						int index = buffer_index[fidx];

						mh.insertKey(qData(ptr[index].y, ptr[index].v, fidx));
					}
				}
				else continue;

				//accumulate or insert other elements
				while (mh.isEmpty()) {
					qData qdata = mh.extractMin();

					if (row_adjs.back().y == qdata.y) {
						row_adjs.back().v += qdata.v;
					}
					else {
						row_adjs.push_back(adj__(qdata.y, qdata.v));
					}

					size_t fidx = qdata.f_num;

					if (++buffer_index[fidx] < buffer_maxindex[fidx]) {
						adj__* ptr = buffer_ptr[fidx];
						int index = buffer_index[fidx];

						mh.insertKey(qData(ptr[index].y, ptr[index].v, fidx));
					}
					else if (row_remain[fidx] != 0) {
						choice[1] = row_remain[fidx];
						size_t read_byte = (vid_t)pread(fd_list[fidx], buffer_ptr[fidx], sizeof(adj__) * choice[(choice[0] > choice[1])], offset[fidx]);
						buffer_maxindex[fidx] = read_byte / sizeof(adj__);
						buffer_index[fidx] = 0;
						offset[fidx] += read_byte;
						row_remain[fidx] -= buffer_maxindex[fidx];

						adj__* ptr = buffer_ptr[fidx];
						int index = buffer_index[fidx];

						mh.insertKey(qData(ptr[index].y, ptr[index].v, fidx));
					}
				}

				// write row header + data: [nodenumber][len][adj__ * len]
				vid_t row_len = (vid_t)row_adjs.size();
				write(fd_output, &nodenumber, sizeof(vid_t));
				write(fd_output, &row_len, sizeof(vid_t));
				write(fd_output, row_adjs.data(), sizeof(adj__) * row_len);
				count += row_len;

				//refresh row header
				for (size_t fidx = 0; fidx < num_total_fd; fidx++) {
					if (row_id[fidx] == nodenumber) {
						if (pread(fd_list[fidx], &row_id[fidx], sizeof(vid_t), offset[fidx]) <= 0)
							row_id[fidx] = endnode;
						else {
							offset[fidx] += sizeof(vid_t);
							offset[fidx] += pread(fd_list[fidx], &row_remain[fidx], sizeof(vid_t), offset[fidx]);
						}
					}
				}
			}

			close(fd_output);
		}

		//global
		for (size_t fidx = 0; fidx < fd_list.size(); fidx++) {
			close(fd_list[fidx]);
		}
		fd_list.clear();

		std::cout << "final count " << count.load() << std::endl;
	}
};

int main(int argc, char ** argv)	{
	parameter_handler param(argc, argv);

	spgemm_algorithm alg;
	bool set_indicator = false;
	engine e(alg, param, set_indicator);

	e.run();

	return 0;
}
