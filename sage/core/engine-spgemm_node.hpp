#include <generals.hpp>
#include <indicator_handler.hpp>
#include <meta_handler.hpp>
#include <buffer_handler.hpp>
#include <preprocess_handler.hpp>
#include <file_handler.hpp>
#include <algorithm_handler.hpp>
#include <timer_handler.hpp>
#include <attribute_handler.hpp>

#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/thread/tss.hpp>

// RealGraph engine header file for block-based workload allocation & hierarchical indicator

//#define ANALYSIS_STATIC_HINDICATOR

#ifdef ANALYSIS_STATIC_HINDICATOR
#define ANALYSIS_STATIC_BASIC
#define ANALYSIS_STATIC_MAIN_BASIC
#define ANALYSIS_STATIC_MAIN_HTIME
#endif

#define ANALYSIS_STATIC_WORKLOAD

#ifdef ANALYSIS_STATIC_WORKLOAD
#define ANALYSIS_STATIC_BASIC
#define ANALYSIS_STATIC_MAIN_BASIC
#define ANALYSIS_STATIC_MAIN_TIME
#define ANALYSIS_STATIC_WORKER_BASIC
#define ANALYSIS_STATIC_WORKER_COUNT
#define ANALYSIS_STATIC_WORKER_TIME
#endif

#ifdef ANALYSIS_STATIC_BASIC
#define NUM_NODES 0
#define NUM_EDGES 1
#define NUM_IOS 2

#define TIME_PROC 0
#define TIME_IO 1
#define TIME_ALLOC 2

#define TIME_SCAN 0
#define TIME_RESET 1
#endif

struct blockmeta {
	BLOCK * block_ptr;
	struct graphmeta graphmeta;
	vid_t lb_offset;
	//node-based;
	vid_t node_id;

	blockmeta() {
	}

	blockmeta(BLOCK * _block_ptr, struct graphmeta _graphmeta) {
		block_ptr = _block_ptr;
		graphmeta = _graphmeta;
		lb_offset = 0;
	}

	void set(BLOCK * _block_ptr, struct graphmeta _graphmeta) {
		block_ptr = _block_ptr;
		graphmeta = _graphmeta;
		lb_offset = 0;
	}
};

/* engine class */
class engine	{
private:
//	meta_handler meta;						/* graph index and buffer index */
	meta_handler meta_a;						/* graph index and buffer index */
	meta_handler meta_b;						/* graph index and buffer index */
	indicator_handler indicator;			/* indicator vector with bit array */
	buffer_handler buffer;					/* buffer functions */
	preprocess_handler preprocess;			/* preprocess functions */
//	file_handler file;						/* file functions */
	file_handler file_a;						/* file functions */
	file_handler file_b;						/* file functions */
	information_handler information;		/* informations about graph processing */
	timer_handler timer;					/* execution time logging */
	algorithm_handler &algorithm;			/* custom graph algorithm */
	parameter_handler &parameter;			/* parameters from shell command */
//	attribute_wrapper attribute;			/* vector space for algorithm */

//	std::vector<int> loaded_blocklist;		/* list of blocks already in the buffer */
//	std::vector<int> unloaded_blocklist;		/* list of blocks not in the buffer */

	std::atomic<int> num_unfinished_tasks;	/* number of unfinished tasks with boost threads */

	boost::asio::io_service io_service;		/* callback thread pool of threads */
	boost::asio::io_service::work work;		/* needed to keep threads waiting instead of exiting when there is no work to do */
	boost::thread_group threads;			/* a thread group which process unloaded blocks (boost implementation) */

	std::string datapath;						/* common path of linux files */
//	std::string filename;					/* wiss relation name : graph data blocks of node adjacency lists which is stored in device */
	std::string filename_a;
	std::string filename_b;
//	std::string dataname;					/* original input filename : raw graph data of edgelists*/
	std::string dataname_a;					/* original input filename : raw graph data of edgelists*/
	std::string dataname_b;					/* original input filename : raw graph data of edgelists*/

	int num_threads;						/* number of threads */
	int cur_iteration;						/* number of current iteration */
	int max_iteration;						/* number of max iteration */

	size_t memory_total_mb;					/* total available memory size in megabytes */
	size_t memory_buffer_mb;				/* memory size for buffer pool in megabytes */
	size_t memory_vector_mb;				/* memory size attribute vector in megabytes */
	size_t memory_indicator_mb;				/* memory size for indicator vector in megabytes */

	size_t hindicator_partition;			/* number of corresponding bits for one bit, power of 2 bits */

//	bool set_preprocess;					/* preprocessing on & off */
	bool set_preprocess_a;					/* preprocessing on & off */
	bool set_preprocess_b;					/* preprocessing on & off */
	bool set_enginerun;						/* just for testing preprocessing chunk */
	bool set_indicator;						/* indicator scheduling on & off */
	
	/* spgemm */
	struct blockmeta * blockmeta_ptr;
	std::atomic<bool> * io_status_a;
	std::atomic<bool> * io_lock_a;
	std::atomic<bool> * io_status_b;
	std::atomic<bool> * io_lock_b;

	int A_max_mb;
	int B_max_mb;
	int C_max_mb;

	int A_first;
	int A_last;
	int B_first;
	int B_last;

	int opt_product;

	boost::thread_specific_ptr<struct thread_data> thread_specific_ptr;
	struct thread_data ** thread_data_pptr;
	std::atomic<int> thread_data_index;
	/* spgemm */

public:
	engine(algorithm_handler & _algorithm, parameter_handler & _parameter, bool _set_indicator) : work(io_service), algorithm(_algorithm), parameter(_parameter), set_indicator(_set_indicator)	{
		/* initialization of parameters */
		set_basic_parameters();

		num_unfinished_tasks = 0;

		omp_set_num_threads(num_threads);

		for (int i = 0; i < num_threads; ++i)
		{
			threads.create_thread(boost::bind(&boost::asio::io_service::run, &io_service));
		}

		/* spgemm */
		thread_data_pptr = new struct thread_data*[num_threads + 1]();
		for (int index = 0; index < num_threads + 1; index++)	{
			thread_data_pptr[index] = new struct thread_data();
		}
		thread_data_index = 0;
		/* spgemm */
	}

	~engine()	{
		io_service.stop();

		threads.join_all();

		timer.print_timelist();

		/* spgemm */
		delete thread_data_pptr;
		/* spgemm */
	}


	/* initializers */

	void set_basic_parameters()	{
		datapath = parameter.get_param_string("datapath", "./");
//		filename = parameter.get_param_string("file", "livejournalrel");
		filename_a = parameter.get_param_string("file_a", "livejournalrel");
		filename_b = parameter.get_param_string("file_b", "livejournalrel");
//		dataname = parameter.get_param_string("data", "livejournal_.txt");
		dataname_a = parameter.get_param_string("data_a", "livejournal_.txt");
		dataname_b = parameter.get_param_string("data_b", "livejournal_.txt");

//		set_preprocess = parameter.get_param_int("preprocess", 0) == 1 ? true : false;
		set_preprocess_a = parameter.get_param_int("preprocess_a", 0) == 1 ? true : false;
		set_preprocess_b = parameter.get_param_int("preprocess_b", 0) == 1 ? true : false;
		set_enginerun = parameter.get_param_int("enginerun", 1) == 1 ? true : false;

		max_iteration = parameter.get_param_int("maxiters", set_indicator ? INT_MAX : 20);

		num_threads = parameter.get_param_int("threads", boost::thread::hardware_concurrency());

		/* spgemm */
		A_max_mb = parameter.get_param_int("a_mb", 1024);
		B_max_mb = parameter.get_param_int("b_mb", 1024);
		C_max_mb = parameter.get_param_int("c_mb", 1024);

		memory_total_mb = parameter.get_param_int("total_mb", A_max_mb + B_max_mb + C_max_mb);

		std::string product_str = parameter.get_param_string("product", "ROWBA");
		if (product_str == "INNER") opt_product = INNER;
		else if (product_str == "ROWBA") opt_product = ROWBA;
		else if (product_str == "ROWAB") opt_product = ROWAB;
		else if (product_str == "OUTER") opt_product = OUTER;
		else {
			printf("error! no such product available!!\n");
			exit(-1);
		}
		/* spgemm */

		hindicator_partition = parameter.get_param_int("hindicator_partition", 10);
	}

	void create_basic_components()	{
		/* load metadata of graph blocks */
//		meta.load_graphmeta(datapath, filename);
		meta_a.load_graphmeta(datapath, filename_a);
		meta_b.load_graphmeta(datapath, filename_b);

		/* create indicator of indicator vector */
		if (set_indicator) {
			vid_t num_max_nodes = meta_a.get_num_nodes() > meta_b.get_num_nodes() ? meta_a.get_num_nodes() : meta_b.get_num_nodes();

			if (num_max_nodes < (size_t(1) << hindicator_partition)) {
				hindicator_partition = 0;
			}

			indicator.create(hindicator_partition, num_max_nodes);

			if (hindicator_partition == 0) {
				memory_indicator_mb = 2 * (num_max_nodes / 8 / 1048576) + 1;
			}
			else {
				memory_indicator_mb = (2 * (num_max_nodes * (1.0 + 1.0 / (pow(2, hindicator_partition) - 1))) / 8 / 1048576) + 1;
			}
		}
		else {
			memory_indicator_mb = 0;
		}

//		attribute.set_wrapper(memory_total_mb - memory_indicator_mb - 5, meta.get_num_nodes());

		/* initialization of information */
		information.num_nodes = meta_a.get_num_nodes() > meta_b.get_num_nodes() ? meta_a.get_num_nodes() : meta_b.get_num_nodes();
		information.num_edges = meta_a.get_num_edges() + meta_b.get_num_edges();
		information.num_threads = num_threads;
		information.max_iteration = max_iteration;
		information.indicator_ptr = &indicator;
		information.parameter_ptr = &parameter;
		//		information.attribute_ptr = &attribute;
		//		information.meta_ptr = &meta;
		information.meta_a_ptr = &meta_a;
		information.meta_b_ptr = &meta_b;

		/* before starting the process */
		algorithm.initialize(&information, thread_data_pptr);

//		memory_vector_mb = (attribute.get_memory_chunk_usage() / 1048576) + 1;
		memory_vector_mb = C_max_mb;
		memory_buffer_mb = memory_total_mb - memory_vector_mb - memory_indicator_mb;
		memory_buffer_mb = BLOCKSIZE > 1048576 ? memory_buffer_mb / (BLOCKSIZE / 1048576) : memory_buffer_mb * (1048576 / BLOCKSIZE);

		std::cout << "-------basic parameters-------" << std::endl;
		std::cout << "datapath: " << datapath << std::endl;
		//		std::cout << "filename: " << filename << std::endl;
		std::cout << "graph_a: " << filename_a << " (preprocessed from " << dataname_a << ")" << std::endl;
		std::cout << "graph_b: " << filename_b << " (preprocessed from " << dataname_b << ")" << std::endl;
		std::cout << "#threads: " << num_threads << std::endl;
		std::cout << "#iters: " << max_iteration << std::endl;
		std::cout << "#total_mb: " << memory_total_mb << "MB" << std::endl;
		std::cout << "#buf_mb: " << memory_buffer_mb << "MB (" << (A_max_mb + B_max_mb) / memory_total_mb << "%)" << std::endl;
		std::cout << "#att_mb: " << memory_vector_mb << "MB (" << C_max_mb / memory_total_mb << "%)" << std::endl;
		if (set_indicator) std::cout << "#hindicator: " << hindicator_partition << " powers of 2" << std::endl;

		//		std::cout << "preprocessing will be " << (set_preprocess ? "done" : "skipped") << std::endl;
		std::cout << "preprocessing A will be " << (set_preprocess_a ? "done" : "skipped") << std::endl;
		std::cout << "preprocessing B will be " << (set_preprocess_b ? "done" : "skipped") << std::endl;
		std::cout << "algorithm will be " << (set_enginerun ? "done" : "skipped") << std::endl;
		std::cout << "------------------------------" << std::endl;

		std::cout << "----------graph A info----------" << std::endl;
		std::cout << "#nodes: " << meta_a.get_num_nodes() << std::endl;
		std::cout << "#edges: " << meta_a.get_num_edges() << std::endl;
		std::cout << "#outblocks: " << meta_a.get_size_out() << std::endl;
		std::cout << "#inblocks: " << meta_a.get_size_in() << std::endl;
		std::cout << "----------graph B info----------" << std::endl;
		std::cout << "#nodes: " << meta_b.get_num_nodes() << std::endl;
		std::cout << "#edges: " << meta_b.get_num_edges() << std::endl;
		std::cout << "#outblocks: " << meta_b.get_size_out() << std::endl;
		std::cout << "#inblocks: " << meta_b.get_size_in() << std::endl;
		std::cout << "--------------------------------" << std::endl;



		/* set buffer */
//		file.set_datapath(datapath);
//		file.open_file(filename);
		file_a.set_datapath(datapath);
		file_a.open_file(filename_a);
		file_b.set_datapath(datapath);
		file_b.open_file(filename_b);
		buffer.cpu_setbuffer(memory_buffer_mb);

	}

	void destroy_basic_components()	{
//		attribute.unset_wrapper();

		/* unset buffer */
		buffer.cpu_unsetbuffer();
//		file.close_file();
		file_a.close_file();
		file_b.close_file();

		/* destroy indicator of indicator vector */
		if (set_indicator)	{
			indicator.destroy();
		}

		/* unload metadata of graph blocks */
//		meta.unload_graphmeta();
		meta_a.unload_graphmeta();
		meta_b.unload_graphmeta();
	}


	/* main function */
	void run()	{
		if (thread_specific_ptr.get() == NULL)	{
			int thread_index = std::atomic_fetch_add(&thread_data_index, 1);
			thread_specific_ptr.reset(thread_data_pptr[thread_index]);
		}

		/* preprocess original graph data */
		if (set_preprocess_a)	{
			time_entry tp("preprocess_a");
			tp.start_time();
			preprocess.preprocessing(datapath, filename_a, dataname_a, memory_total_mb);
			tp.stop_time();
			timer.add_entry(tp);
		}
		if (set_preprocess_b) {
			time_entry tp("preprocess_b");
			tp.start_time();
			preprocess.preprocessing(datapath, filename_b, dataname_b, memory_total_mb);
			tp.stop_time();
			timer.add_entry(tp);
		}

		/* start engine runtime */
		time_entry t("runtime");
		t.start_time();

		/* initialization of basic components */
		create_basic_components();


		/* just to debug preprocess, no special meaning*/
		if (set_enginerun) {
			for (cur_iteration = 0; cur_iteration < max_iteration; cur_iteration++) {
				information.cur_iteration = cur_iteration;

				/* before starting the iteration */
				algorithm.before_iteration();

				/* start iteration */
				{

#ifdef ANALYSIS_STATIC_MAIN_TIME
					std::chrono::system_clock::time_point start_1 = std::chrono::system_clock::now();
#endif

					switch (opt_product) {
					case INNER:
						break;
					case ROWBA:
						/*
											A_first = meta.get_first_index();
											A_last = meta.get_mid_index();
											B_first = meta.get_mid_index();
											B_last = meta.get_last_index();
						*/
						A_first = meta_a.get_first_index();
						A_last = meta_a.get_mid_index();
						B_first = meta_b.get_first_index();
						B_last = meta_b.get_mid_index();

						taskAlloc_rowBA(B_max_mb);
						break;
					case ROWAB:
						/*
											A_first = meta.get_first_index();
											A_last = meta.get_mid_index();
											B_first = meta.get_mid_index();
											B_last = meta.get_last_index();
						*/
						A_first = meta_a.get_first_index();
						A_last = meta_a.get_mid_index();
						B_first = meta_b.get_first_index();
						B_last = meta_b.get_mid_index();

						taskAlloc_rowAB(B_max_mb, A_max_mb);
						break;
					case OUTER:
						/*
											A_first = meta.get_mid_index();
											A_last = meta.get_last_index();
											B_first = meta.get_first_index();
											B_last = meta.get_mid_index();
						*/
						A_first = meta_a.get_mid_index();
						A_last = meta_a.get_last_index();
						B_first = meta_b.get_first_index();
						B_last = meta_b.get_mid_index();

						taskAlloc_outer(A_max_mb, B_max_mb);
						break;
					default:
						printf("ERROR!!!! Wrong type\n");
						return;
					}

#ifdef ANALYSIS_STATIC_MAIN_TIME
					std::chrono::duration<double> interval_1 = std::chrono::system_clock::now() - start_1;

					thread_specific_ptr->stopwatch[TIME_ALLOC] += interval_1.count();
#endif

				}
				/* stop iteration */

				/* after stopping the iteration */
				algorithm.after_iteration();

				/* one iteration ended, change the current & next iteration indicators */
				if (set_indicator) {

#ifdef ANALYSIS_STATIC_MAIN_HTIME
					std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
#endif

					indicator.exchange();

#ifdef ANALYSIS_STATIC_MAIN_HTIME
					std::chrono::duration<double> interval = std::chrono::system_clock::now() - start;

					thread_specific_ptr->stopwatch[TIME_RESET] += interval.count();
#endif

				}

#ifdef ANALYSIS_STATIC_BASIC
				for (int i = 0; i < num_threads + 1; i++) {
					struct thread_data* thread_data_ptr = thread_data_pptr[i];

					printf("%d %d %lu %lu %lu %lf %lf %lf\n", cur_iteration, i, thread_data_ptr->counter[0], thread_data_ptr->counter[1], thread_data_ptr->counter[2], thread_data_ptr->stopwatch[0], thread_data_ptr->stopwatch[1], thread_data_ptr->stopwatch[2]);

					for (int index = 0; index < 3; index++) {
						thread_data_ptr->counter[index] = 0;
						thread_data_ptr->stopwatch[index] = 0.0f;
					}
				}
#endif

			}
		}

		/* after stopping the process */
		algorithm.finalize();

		/* finalization of basic components */
		destroy_basic_components();

		/* merging phase */
		algorithm.merge_parts();

		/* stop engine runtime */
		t.stop_time();

		timer.add_entry(t);
	}

	void taskAlloc_rowBA(int max_load) {
		int load_first = B_first;
		int load_last = B_last;
		int post_first = A_first;
		int post_last = A_last;

		blockmeta_ptr = new struct blockmeta[max_load];
				
		std::vector<int> load_blocklist;
		std::vector<int> post_blocklist;

		/* start loading unloaded blocks */
		for (int index = load_first; index < load_last; index++) {
			load_blocklist.push_back(index);
		}
		int num_load = load_last - load_first;
//node-based
		for (int index = post_first; index < post_last; index += meta_a.get_algmeta(index).nblocks) {
			post_blocklist.push_back(index);
		}
		int num_post = post_last - post_first;

		int loadsize = 0;
		int num_alloced = 0;

		for (int num_loaded = 0; num_loaded < num_load; num_loaded += loadsize) {
			/* group blocklist into pinned and unpinned */
			//			for (int tableindex = firstindex; tableindex < lastindex; tableindex++)	{
			//				char * blockptr = NULL;
			//
			//				/* pin the required block if possible */
			//				wiss.pin_block(meta.get_blockid(tableindex), &blockptr);
			//
			//				/* if block is not loaded then get block number into the unloaded list */
			//				(blockptr == NULL) ? unloaded_blocklist.push_back(tableindex) : loaded_blocklist.push_back(tableindex);
			//
			//				/* set address of buffer where block is loaded */
			//				meta.set_bufferindex(tableindex, blockptr);
			//			}

			/* start loading unloaded blocks */
			loadsize = (num_load - num_loaded < max_load) ? (num_load - num_loaded) : max_load;

			/* block load */
			for (int num_processed = 0; num_processed < loadsize; num_processed += num_alloced) {
				/* busy wait until enough free space exists */
				while (buffer.cpu_getfreesize() < 1);

				/* get the free blocks */
				num_alloced = buffer.cpu_allocbufs(file_b.get_fd(), &load_blocklist[num_loaded + num_processed], meta_b.get_bufferindex_ptr(), meta_a.get_bufferindex_ptr(), loadsize - num_processed);

				/* start posting unloaded blocks */
				for (int num_alloced_processing = 0; num_alloced_processing < num_alloced; num_alloced_processing++) {
					int tableindex = load_blocklist[num_loaded + num_processed + num_alloced_processing];

					blockmeta_ptr[num_processed + num_alloced_processing].set(meta_b.get_bufferindex(tableindex), meta_b.get_graphmeta(tableindex));

					num_unfinished_tasks++;

					/* post asynchronous io request to boost handler */
					io_service.post(boost::bind(&engine::handler_load, this, meta_b.get_bufferindex(tableindex), meta_b.get_blockid(tableindex)));
				}
			}

			for (int index = 1; index < loadsize; index++) {
				if (blockmeta_ptr[index - 1].graphmeta.first_nodeid == blockmeta_ptr[index].graphmeta.first_nodeid) {
					blockmeta_ptr[index].lb_offset = 1;
				}
			}

			while (num_unfinished_tasks > 0);

			/* loaded block post */
			//			for (int num_processed = 0; num_processed < (int)loaded_blocklist.size(); num_processed++) {
			//				num_unfinished_tasks++;
			//
			//				/* post asynchronous io request to boost handler */
			//				io_service.post(boost::bind(&engine::handler_unloaded_list_Ac, this, loaded_blocklist[num_processed], first, last));
			//			}

			/* unloaded block post */
			for (int num_processed = 0; num_processed < num_post; num_processed += num_alloced) {
				/* busy wait until enough free space exists */
				while (buffer.cpu_getfreesize() < 1);

				/* get the free blocks */
				num_alloced = buffer.cpu_allocbufs(file_a.get_fd(), &post_blocklist[num_processed], meta_a.get_bufferindex_ptr(), meta_b.get_bufferindex_ptr(), num_post - num_processed);

				for (int num_alloced_processing = 0; num_alloced_processing < num_alloced; num_alloced_processing++) {
					int tableindex = post_blocklist[num_processed + num_alloced_processing];

					num_unfinished_tasks++;

					/* post asynchronous io request to boost handler */
					io_service.post(boost::bind(&engine::handler_iopost_rowBA, this, tableindex));
				}
//node-based
				while (num_unfinished_tasks > 0);

				for (int num_alloced_processing = 0; num_alloced_processing < num_alloced; num_alloced_processing++) {
					int tableindex = post_blocklist[num_processed + num_alloced_processing];

					vid_t first_nodeid = meta_a.get_graphmeta(tableindex).first_nodeid;
					vid_t last_nodeid = meta_a.get_graphmeta(tableindex).last_nodeid;

					num_unfinished_tasks += last_nodeid - first_nodeid + 1;

					for (vid_t node_id = first_nodeid; node_id <= last_nodeid; node_id++) {

						/* post asynchronous io request to boost handler */
						io_service.post(boost::bind(&engine::handler_procpost_rowBA, this, tableindex, loadsize, node_id));
					}
				}

				while (num_unfinished_tasks > 0);

				for (int num_alloced_processing = 0; num_alloced_processing < num_alloced; num_alloced_processing++) {
					buffer.cpu_unpinblock(meta_a.get_bufferindex(post_blocklist[num_processed + num_alloced_processing]));
				}
//node-based
			}

			while (num_unfinished_tasks > 0);

			for (int index = num_loaded; index < num_loaded + loadsize; index++) {	//pin�� ���ϵ� �� ���� ����
				buffer.cpu_unpinblock(meta_b.get_bufferindex(load_blocklist[index]));
			}

			algorithm.flush_row();
		}

		delete blockmeta_ptr;
	}

	//node-based
	/* post handler */
	void handler_iopost_rowBA(int post_index) {	//A block Change Version
		if (thread_specific_ptr.get() == NULL) {
			int thread_index = std::atomic_fetch_add(&thread_data_index, 1);
			thread_specific_ptr.reset(thread_data_pptr[thread_index]);
		}

		/* nodes in the block */
		BLOCK * block_ptr = meta_a.get_bufferindex(post_index);
		struct graphmeta graphmeta = meta_a.get_graphmeta(post_index);

		struct blockmeta A_blockmeta(block_ptr, graphmeta);

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
#endif

		/* file read is performed in this handling function */
		file_a.read_block(block_ptr, graphmeta.block_id);

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::duration<double> interval_1 = std::chrono::system_clock::now() - start;

		thread_specific_ptr->stopwatch[TIME_IO] += interval_1.count();
#endif

#ifdef ANALYSIS_STATIC_WORKER_COUNT
		thread_specific_ptr->counter[NUM_IOS] += 1;
#endif

		num_unfinished_tasks--;

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::duration<double> interval_2 = std::chrono::system_clock::now() - start;

		thread_specific_ptr->stopwatch[TIME_PROC] += interval_2.count();
#endif

	}

	//node-based
	/* post handler */
	void handler_procpost_rowBA(int post_index, int loadsize, vid_t node_id) {	//A block Change Version
		if (thread_specific_ptr.get() == NULL) {
			int thread_index = std::atomic_fetch_add(&thread_data_index, 1);
			thread_specific_ptr.reset(thread_data_pptr[thread_index]);
		}

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
#endif

		for (int block_offset = 0; block_offset < meta_a.get_algmeta(post_index).nblocks; block_offset++) {

			/* nodes in the block */
			BLOCK * block_ptr = meta_a.get_bufferindex(post_index);
			struct graphmeta current_graphmeta = meta_a.get_graphmeta(post_index + block_offset);

			struct blockmeta A_blockmeta(block_ptr, current_graphmeta);
			A_blockmeta.node_id = node_id;

			algorithm.rowrowProduct(A_blockmeta, blockmeta_ptr, loadsize, thread_specific_ptr.get());

			if (block_offset < meta_a.get_algmeta(post_index).nblocks - 1) {
				
				struct graphmeta next_graphmeta = meta_a.get_graphmeta(post_index + block_offset + 1);

#ifdef ANALYSIS_STATIC_WORKER_TIME
				std::chrono::system_clock::time_point start2 = std::chrono::system_clock::now();
#endif

				/* file read is performed in this handling function */
				file_a.read_block(block_ptr, next_graphmeta.block_id);

#ifdef ANALYSIS_STATIC_WORKER_TIME
				std::chrono::duration<double> interval_1 = std::chrono::system_clock::now() - start2;

				thread_specific_ptr->stopwatch[TIME_IO] += interval_1.count();
#endif

#ifdef ANALYSIS_STATIC_WORKER_COUNT
				thread_specific_ptr->counter[NUM_IOS] += 1;
#endif

			}
//node-based
		}

		num_unfinished_tasks--;

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::duration<double> interval_2 = std::chrono::system_clock::now() - start;

		thread_specific_ptr->stopwatch[TIME_PROC] += interval_2.count();
#endif

	}

	void taskAlloc_rowAB(int max_load, int max_post) {
		int load_first = B_first;
		int load_last = B_last;
		int post_first = A_first;
		int post_last = A_last;

		blockmeta_ptr = new struct blockmeta[max_load];

		std::vector<int> load_blocklist;
		std::vector<int> post_blocklist;

		/* start loading unloaded blocks */
		for (int index = load_first; index < load_last; index++) {
			load_blocklist.push_back(index);
		}
		int num_load = load_last - load_first;

		for (int index = post_first; index < post_last; index++) {
			post_blocklist.push_back(index);
		}
		int num_post = post_last - post_first;

		int loadsize = 0;
		int postsize = 0;
		int num_alloced = 0;
		bool read_flag;

		for (int num_posted = 0; num_posted < num_post; num_posted += postsize)	{
			postsize = (num_post - num_posted < max_post) ? (num_post - num_posted) : max_post;

			/* unloaded block post */
			for (int num_processed = 0; num_processed < postsize; num_processed += num_alloced) {
				/* busy wait until enough free space exists */
				while (buffer.cpu_getfreesize() < 1);

				/* get the free blocks */
				num_alloced = buffer.cpu_allocbufs(file_a.get_fd(), &post_blocklist[num_posted + num_processed], meta_a.get_bufferindex_ptr(), postsize - num_processed);
			}

			read_flag = true;

			for (int num_loaded = 0; num_loaded < num_load; num_loaded += loadsize) {
				/* start loading unloaded blocks */
				loadsize = (num_load - num_loaded < max_load) ? (num_load - num_loaded) : max_load;

				/* block load */
				for (int num_processed = 0; num_processed < loadsize; num_processed += num_alloced) {
					/* busy wait until enough free space exists */
					while (buffer.cpu_getfreesize() < 1);

					/* get the free blocks */
					num_alloced = buffer.cpu_allocbufs(file_b.get_fd(), &load_blocklist[num_loaded + num_processed], meta_b.get_bufferindex_ptr(), loadsize - num_processed);

					/* start posting unloaded blocks */
					for (int num_alloced_processing = 0; num_alloced_processing < num_alloced; num_alloced_processing++) {
						int tableindex = load_blocklist[num_loaded + num_processed + num_alloced_processing];

						blockmeta_ptr[num_processed + num_alloced_processing].set(meta_b.get_bufferindex(tableindex), meta_b.get_graphmeta(tableindex));

						num_unfinished_tasks++;

						/* post asynchronous io request to boost handler */
						io_service.post(boost::bind(&engine::handler_load, this, meta_b.get_bufferindex(tableindex), meta_b.get_blockid(tableindex)));
					}
				}

				for (int index = 1; index < loadsize; index++) {
					if (blockmeta_ptr[index - 1].graphmeta.first_nodeid == blockmeta_ptr[index].graphmeta.first_nodeid) {
						blockmeta_ptr[index].lb_offset = 1;
					}
				}

				while (num_unfinished_tasks > 0);

				/* unloaded block post */
				for (int num_processed = 0; num_processed < postsize; num_processed++) {
					int tableindex = post_blocklist[num_posted + num_processed];

					num_unfinished_tasks++;

					/* post asynchronous io request to boost handler */
					io_service.post(boost::bind(&engine::handler_post_rowAB, this, tableindex, loadsize, read_flag));
				}

				while (num_unfinished_tasks > 0);

				for (int index = num_loaded; index < num_loaded + loadsize; index++) {	//pin�� ���ϵ� �� ���� ����
					buffer.cpu_unpinblock(meta_b.get_bufferindex(load_blocklist[index]));
				}

				algorithm.flush_row();

				read_flag = false;
			}

			for (int index = num_posted; index < num_posted + postsize; index++) {	//pin�� ���ϵ� �� ���� ����
				buffer.cpu_unpinblock(meta_a.get_bufferindex(post_blocklist[index]));
			}
		}

		delete blockmeta_ptr;
	}

	/* post handler */
	void handler_post_rowAB(int post_index, int loadsize, bool read_flag) {	//B block Change Version
		if (thread_specific_ptr.get() == NULL) {
			int thread_index = std::atomic_fetch_add(&thread_data_index, 1);
			thread_specific_ptr.reset(thread_data_pptr[thread_index]);
		}

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
#endif

		/* nodes in the block */
		BLOCK * block_ptr = meta_a.get_bufferindex(post_index);
		struct graphmeta graphmeta = meta_a.get_graphmeta(post_index);
		struct algmeta algmeta = meta_a.get_algmeta(post_index);

		struct blockmeta A_blockmeta(block_ptr, graphmeta);

		/* file read is performed in this handling function */
		if (read_flag) {
			file_a.read_block(block_ptr, graphmeta.block_id);

#ifdef ANALYSIS_STATIC_WORKER_TIME
			std::chrono::duration<double> interval_1 = std::chrono::system_clock::now() - start;

			thread_specific_ptr->stopwatch[TIME_IO] += interval_1.count();
#endif

#ifdef ANALYSIS_STATIC_WORKER_COUNT
			thread_specific_ptr->counter[NUM_IOS] += 1;
#endif

		}

		algorithm.rowrowProduct(A_blockmeta, blockmeta_ptr, loadsize, thread_specific_ptr.get());

		num_unfinished_tasks--;

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::duration<double> interval_2 = std::chrono::system_clock::now() - start;

		thread_specific_ptr->stopwatch[TIME_PROC] += interval_2.count();
#endif

	}

	/* load handler */
	void handler_load(BLOCK * block_ptr, int block_id) {
		if (thread_specific_ptr.get() == NULL)	{
			int thread_index = std::atomic_fetch_add(&thread_data_index, 1);
			thread_specific_ptr.reset(thread_data_pptr[thread_index]);
		}

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
#endif

		/* file read is performed in this handling function */
		file_b.read_block(block_ptr, block_id);

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::duration<double> interval_1 = std::chrono::system_clock::now() - start;

		thread_specific_ptr->stopwatch[TIME_IO] += interval_1.count();
#endif

#ifdef ANALYSIS_STATIC_WORKER_COUNT
		thread_specific_ptr->counter[NUM_IOS] += 1;
#endif

		num_unfinished_tasks--;

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::duration<double> interval_2 = std::chrono::system_clock::now() - start;

		thread_specific_ptr->stopwatch[TIME_PROC] += interval_2.count();
#endif

	}

	void taskAlloc_outer(int A_max, int B_max) {
		io_status_a = new std::atomic<bool>[meta_a.get_size_index()];
		io_lock_a = new std::atomic<bool>[meta_a.get_size_index()];
		io_status_b = new std::atomic<bool>[meta_b.get_size_index()];
		io_lock_b = new std::atomic<bool>[meta_b.get_size_index()];

		std::vector<int> A_blocklist;
		std::vector<int> B_blocklist;

		/* start loading unloaded blocks */
		for (int index = A_first; index < A_last; index++) {
			A_blocklist.push_back(index);
		}
		int num_A = A_last - A_first;

		for (int index = B_first; index < B_last; index++) {
			B_blocklist.push_back(index);
		}
		int num_B = B_last - B_first;

		for (int index = 0; index < meta_a.get_size_index(); index++) {
			io_status_a[index] = false;
			io_lock_a[index] = false;
		}

		for (int index = 0; index < meta_b.get_size_index(); index++) {
			io_status_b[index] = false;
			io_lock_b[index] = false;
		}

		int num_A_processed = 0;
		int num_B_processed = 0;

		BLOCK * A_block_ptr = NULL;
		BLOCK * B_block_ptr = NULL;

		/* unloaded block post */
		while (num_A_processed < num_A && num_B_processed < num_B) {
			struct graphmeta A_graphmeta = meta_a.get_graphmeta(A_blocklist[num_A_processed]);
			struct graphmeta B_graphmeta = meta_b.get_graphmeta(B_blocklist[num_B_processed]);
			struct algmeta A_algmeta = meta_a.get_algmeta(A_blocklist[num_A_processed]);
			struct algmeta B_algmeta = meta_b.get_algmeta(B_blocklist[num_B_processed]);

			for (int i = 0; i < A_algmeta.nblocks; i++) {
				for (int j = 0; j < B_algmeta.nblocks; j++) {
					/* busy wait until enough free space exists */
					A_block_ptr = meta_a.get_bufferindex(A_blocklist[num_A_processed + i]);

					/* get the free blocks */
					if (A_block_ptr == NULL) {
						while (buffer.cpu_getfreesize() < 1);
						while (buffer.cpu_allocbufs(file_a.get_fd(), &A_blocklist[num_A_processed + i], meta_a.get_bufferindex_ptr(), 1) == 0);
					}
					else
						buffer.cpu_pinblock(A_block_ptr);

					B_block_ptr = meta_b.get_bufferindex(B_blocklist[num_B_processed + j]);

					/* get the free blocks */
					if (B_block_ptr == NULL) {
						while (buffer.cpu_getfreesize() < 1);
						while (buffer.cpu_allocbufs(file_b.get_fd(), &B_blocklist[num_B_processed + j], meta_b.get_bufferindex_ptr(), 1) == 0);
					}
					else
						buffer.cpu_pinblock(B_block_ptr);

					num_unfinished_tasks++;

					/* post asynchronous io request to boost handler */
					io_service.post(boost::bind(&engine::handler_post_outer, this, A_blocklist[num_A_processed + i], B_blocklist[num_B_processed + j]));
				}
			}

			if (A_graphmeta.last_nodeid < B_graphmeta.last_nodeid) {
				num_A_processed += A_algmeta.nblocks;
			}
			else if (A_graphmeta.last_nodeid == B_graphmeta.last_nodeid) {
				num_A_processed += A_algmeta.nblocks;
				num_B_processed += B_algmeta.nblocks;
			}
			else {
				num_B_processed += B_algmeta.nblocks;
			}
		}

		while (num_unfinished_tasks > 0);

		algorithm.flush_outer();

		delete io_status_a;
		delete io_lock_a;
		delete io_status_b;
		delete io_lock_b;
	}

	/* post handler */
	void handler_post_outer(int A_tableindex, int B_tableindex) {
		if (thread_specific_ptr.get() == NULL) {
			int thread_index = std::atomic_fetch_add(&thread_data_index, 1);
			thread_specific_ptr.reset(thread_data_pptr[thread_index]);
		}

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
#endif

		/* nodes in the block */
		BLOCK * A_block_ptr = meta_a.get_bufferindex(A_tableindex);
		struct graphmeta A_graphmeta = meta_a.get_graphmeta(A_tableindex);
		struct algmeta A_algmeta = meta_a.get_algmeta(A_tableindex);

		struct blockmeta A_blockmeta(A_block_ptr, A_graphmeta);

		BLOCK * B_block_ptr = meta_b.get_bufferindex(B_tableindex);
		struct graphmeta B_graphmeta = meta_b.get_graphmeta(B_tableindex);
		struct algmeta B_algmeta = meta_b.get_algmeta(B_tableindex);

		struct blockmeta B_blockmeta(B_block_ptr, B_graphmeta);

		/* file read is performed in this handling function */
		if (std::atomic_exchange(&io_lock_a[A_tableindex], true) == false) {

#ifdef ANALYSIS_STATIC_WORKER_TIME
			std::chrono::system_clock::time_point blockIOstart = std::chrono::system_clock::now();
#endif

			file_a.read_block(A_block_ptr, A_graphmeta.block_id);

#ifdef ANALYSIS_STATIC_WORKER_TIME
			std::chrono::duration<double> blockIO = std::chrono::system_clock::now() - blockIOstart;

			thread_specific_ptr->stopwatch[TIME_IO] += blockIO.count();
#endif

#ifdef ANALYSIS_STATIC_WORKER_COUNT
			thread_specific_ptr->counter[NUM_IOS] += 1;
#endif

			std::atomic_exchange(&io_status_a[A_tableindex], true);
		}
		if (std::atomic_exchange(&io_lock_b[B_tableindex], true) == false) {

#ifdef ANALYSIS_STATIC_WORKER_TIME
			std::chrono::system_clock::time_point blockIOstart = std::chrono::system_clock::now();
#endif

			file_b.read_block(B_block_ptr, B_graphmeta.block_id);

#ifdef ANALYSIS_STATIC_WORKER_TIME
			std::chrono::duration<double> blockIO = std::chrono::system_clock::now() - blockIOstart;

			thread_specific_ptr->stopwatch[TIME_IO] += blockIO.count();
#endif

#ifdef ANALYSIS_STATIC_WORKER_COUNT
			thread_specific_ptr->counter[NUM_IOS] += 1;
#endif

			std::atomic_exchange(&io_status_b[B_tableindex], true);
		}
		while (io_status_a[A_tableindex] == false || io_status_b[B_tableindex] == false);

		algorithm.outerProduct(A_blockmeta, B_blockmeta, thread_specific_ptr.get());

		buffer.cpu_unpinblock(A_block_ptr);
		buffer.cpu_unpinblock(B_block_ptr);

		num_unfinished_tasks--;

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::duration<double> interval_2 = std::chrono::system_clock::now() - start;

		thread_specific_ptr->stopwatch[TIME_PROC] += interval_2.count();
#endif

	}
};
