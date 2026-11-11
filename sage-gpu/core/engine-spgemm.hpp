#include <generals.hpp>
#include <GPU_indicator_handler.hpp>
#include <GPU_meta_handler.hpp>
#include <GPU_buffer_handler.hpp>
#include <preprocess_handler.hpp>
#include <file_handler.hpp>
#include <algorithm_handler.hpp>
#include <timer_handler.hpp>
#include <attribute_handler.hpp>


#include <GPU_stream_handler.cuh>
#include <GPU_attribute_handler.cuh>
#include <GPU_kernel_handler.cuh>


#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/thread/tss.hpp>



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

std::atomic<uint32_t> StreamFlag;


struct realgraph_task{
	int Streamindex;
	BLOCK *block_ptr;
	BLOCK *gpu_block_ptr;
	void * engine;
	void * buffer;
};

CUDART_CB void MyCallback(void *data); 
CUDART_CB void MyCallback2(void *data); 


struct blockmeta {
	BLOCK * block_ptr;
	BLOCK * gpu_block_ptr;
	struct graphmeta graphmeta;
	vid_t lb_offset;

	blockmeta() {
	}

	blockmeta(BLOCK * _block_ptr, BLOCK * _gpu_block_ptr, struct graphmeta _graphmeta) {
		block_ptr = _block_ptr;
		gpu_block_ptr = _gpu_block_ptr;
		graphmeta = _graphmeta;
		lb_offset = 0;
	}

	void set(BLOCK * _block_ptr, BLOCK * _gpu_block_ptr, struct graphmeta _graphmeta) {
		block_ptr = _block_ptr;
		gpu_block_ptr = _gpu_block_ptr;
		graphmeta = _graphmeta;
		lb_offset = 0;
	}
};


class engine	{
private:

	gpu_meta_handler meta_a;						
	gpu_meta_handler meta_b;						
	gpu_indicator_handler indicator;			
	gpu_buffer_handler buffer;					
	preprocess_handler preprocess;			

	file_handler file_a;						
	file_handler file_b;						
	information_handler information;		
	timer_handler timer;					
	algorithm_handler &algorithm;			
	parameter_handler &parameter;			


	
	gpu_stream_handler gpu;

	size_t gpu_memory_total_mb;
	size_t gpu_memory_cache_mb;
	size_t gpu_memory_vector_mb;

	int num_streams;
	int centric;

	int type = 0;
	size_t RASIZE = 0;

	
	





	std::atomic<int> num_unfinished_tasks;	

	boost::asio::io_service io_service, io_service_gpu;		
	boost::asio::io_service::work work, work_gpu;		
	boost::thread_group threads, gpu_threads;			

	std::string datapath;						

	std::string filename_a;
	std::string filename_b;

	std::string dataname_a;					
	std::string dataname_b;					

	int num_threads;						
	int cur_iteration;						
	int max_iteration;						
	int running_threads;					

	size_t memory_total_mb;					
	size_t memory_buffer_mb;				
	size_t memory_vector_mb;				
	size_t memory_indicator_mb;				

	size_t hindicator_partition;			


	bool set_preprocess_a;					
	bool set_preprocess_b;					
	bool set_enginerun;						
	bool set_indicator;						
	
	
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
	struct thread_data * gpu_thread_data_ptr;
	std::atomic<int> thread_data_index;
	

	
	std::atomic<bool> * gpu_io_status_a;
	std::atomic<bool> * gpu_io_lock_a;
	std::atomic<bool> * gpu_io_status_b;
	std::atomic<bool> * gpu_io_lock_b;

	int gpu_A_max_mb;
	int gpu_B_max_mb;
	int gpu_C_max_mb;

	struct realgraph_task *RT =(struct realgraph_task *)malloc(sizeof(struct realgraph_task)*32);

	

public:
	engine(algorithm_handler & _algorithm, parameter_handler & _parameter, bool _set_indicator) : work(io_service), work_gpu(io_service_gpu), algorithm(_algorithm), parameter(_parameter), set_indicator(_set_indicator)	{
		
		set_basic_parameters();
		StreamFlag=0;
		StreamFlag=~StreamFlag;
		running_threads = 0;
		num_unfinished_tasks = 0;

		omp_set_num_threads(num_threads);

		for (int i = 0; i < num_threads; ++i)
		{
			threads.create_thread(boost::bind(&boost::asio::io_service::run, &io_service));
		}
		gpu_threads.create_thread(boost::bind(&boost::asio::io_service::run, &io_service_gpu));

		 
		thread_data_pptr = new struct thread_data*[num_threads + 1]();
		gpu_thread_data_ptr = new struct thread_data();
		for (int index = 0; index < num_threads + 1; index++)	{
			thread_data_pptr[index] = new struct thread_data();
		}
		thread_data_index = 0;
		
	}

	~engine()	{
		io_service.stop();
		io_service_gpu.stop();

		gpu_threads.join_all();
		threads.join_all();

		timer.print_timelist();

		
		delete thread_data_pptr;
		delete gpu_thread_data_ptr;
		
	}


	

	void set_basic_parameters()	{
		datapath = parameter.get_param_string("datapath", "./");

		filename_a = parameter.get_param_string("file_a", "livejournalrel");
		filename_b = parameter.get_param_string("file_b", "livejournalrel");

		dataname_a = parameter.get_param_string("data_a", "livejournal_.txt");
		dataname_b = parameter.get_param_string("data_b", "livejournal_.txt");


		set_preprocess_a = parameter.get_param_int("preprocess_a", 0) == 1 ? true : false;
		set_preprocess_b = parameter.get_param_int("preprocess_b", 0) == 1 ? true : false;
		set_enginerun = parameter.get_param_int("enginerun", 1) == 1 ? true : false;

		max_iteration = parameter.get_param_int("maxiters", set_indicator ? INT_MAX : 20);

		num_threads = parameter.get_param_int("threads", boost::thread::hardware_concurrency());

		
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
		

		
		gpu_A_max_mb = parameter.get_param_int("a_mb", 1024);
		gpu_B_max_mb = parameter.get_param_int("b_mb", 1024);
		gpu_C_max_mb = parameter.get_param_int("c_mb", 1024);

		num_streams = parameter.get_param_int("streams", 32);
		centric = parameter.get_param_int("centric", 0);
		gpu_memory_total_mb = parameter.get_param_int("total_mb", A_max_mb + B_max_mb + C_max_mb);
		


		hindicator_partition = parameter.get_param_int("hindicator_partition", 10);
	}

	void create_basic_components()	{
		

		meta_a.load_graphmeta(datapath, filename_a);
		meta_b.load_graphmeta(datapath, filename_b);

		
		meta_a.load_graphmeta_gpu();
		meta_b.load_graphmeta_gpu();
		

		
		if (set_indicator) {
			vid_t num_max_nodes = meta_a.get_num_nodes() > meta_b.get_num_nodes() ? meta_a.get_num_nodes() : meta_b.get_num_nodes();

			if (num_max_nodes < (size_t(1) << hindicator_partition)) {
				hindicator_partition = 0;
			}

			indicator.create(hindicator_partition, num_max_nodes);
			indicator.create_gpu();

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



		
		information.num_nodes = meta_a.get_num_nodes() > meta_b.get_num_nodes() ? meta_a.get_num_nodes() : meta_b.get_num_nodes();
		information.num_edges = meta_a.get_num_edges() + meta_b.get_num_edges();
		information.num_threads = num_threads;
		information.max_iteration = max_iteration;
		information.indicator_ptr = &indicator;
		information.parameter_ptr = &parameter;
		
		
		information.meta_a_ptr = &meta_a;
		information.meta_b_ptr = &meta_b;

		

		
		algorithm.initialize(&information, &timer, thread_data_pptr, gpu_thread_data_ptr);

		
		gpu_memory_vector_mb = gpu_C_max_mb; 
		gpu_memory_cache_mb = gpu_memory_total_mb - gpu_memory_vector_mb;
		


		memory_vector_mb = C_max_mb;
		memory_buffer_mb = memory_total_mb - memory_vector_mb - memory_indicator_mb;
		memory_buffer_mb = BLOCKSIZE > 1048576 ? memory_buffer_mb / (BLOCKSIZE / 1048576) : memory_buffer_mb * (1048576 / BLOCKSIZE);

		

		memory_buffer_mb -= memory_indicator_mb;
		gpu_memory_cache_mb -= memory_indicator_mb;

		std::cout << "-------basic parameters-------" << std::endl;
		std::cout << "datapath: " << datapath << std::endl;
		
		std::cout << "graph_a: " << filename_a << " (preprocessed from " << dataname_a << ")" << std::endl;
		std::cout << "graph_b: " << filename_b << " (preprocessed from " << dataname_b << ")" << std::endl;
		std::cout << "#threads: " << num_threads << std::endl;
		std::cout << "#iters: " << max_iteration << std::endl;
		std::cout << "#total_mb: " << memory_total_mb << "MB" << std::endl;
		std::cout << "#buf_mb: " << memory_buffer_mb << "MB (" << ((A_max_mb + B_max_mb) * 100)/ memory_total_mb  << "%)" << std::endl;
		std::cout << "#att_mb: " << memory_vector_mb << "MB (" << (C_max_mb * 100)/ memory_total_mb << "%)" << std::endl;
		if (set_indicator) std::cout << "#hindicator: " << hindicator_partition << " powers of 2" << std::endl;

		
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

		
		std::cout << "------ GPU basic parameters ------" << std::endl;
		std::cout << "#gpu_total_mb: " << gpu_memory_total_mb << "MB" << std::endl;
		std::cout << "#gpu_cac_mb: " << gpu_memory_cache_mb << "MB (" << gpu_memory_cache_mb / (float)gpu_memory_total_mb * 100 << "%)" << std::endl;
		std::cout << "#gpu_att_mb: " << gpu_memory_vector_mb << "MB (" << gpu_memory_vector_mb / (float)gpu_memory_total_mb * 100 << "%)" << std::endl;
		std::cout << "#streams: " << num_streams << std::endl;

		if(centric == 0) std::cout << "VWC" << std::endl;
		else std::cout << "Vertex-centric" << std::endl;
		



		


		file_a.set_datapath(datapath);
		file_a.open_file(filename_a);
		file_b.set_datapath(datapath);
		file_b.open_file(filename_b);
		buffer.cpu_setbuffer(memory_buffer_mb);

		
		buffer.gpu_setcache(gpu_memory_cache_mb);
		std::cout << "GPU memory cache size: " << gpu_memory_cache_mb << "MB" << std::endl;
		std::cout << "CPU memory buffer size: " << memory_buffer_mb << "MB" << std::endl;
		gpu.createStream(num_streams);
		

	}

	void destroy_basic_components()	{


		
		buffer.gpu_unsetcache();
		buffer.cpu_unsetbuffer();
		

		file_a.close_file();
		file_b.close_file();

		
		if (set_indicator)	{
			indicator.destroy();
			indicator.destroy_gpu();
		}

		

		
		meta_a.unload_graphmeta_gpu();
		meta_b.unload_graphmeta_gpu();
		
		meta_a.unload_graphmeta();
		meta_b.unload_graphmeta();

		
		gpu.destroyStream();
		free(RT);
		
	}


	
	void run()	{
		if (thread_specific_ptr.get() == NULL)	{ 
			int thread_index = std::atomic_fetch_add(&thread_data_index, 1);
			thread_specific_ptr.reset(thread_data_pptr[thread_index]);
		}

		
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

		
		time_entry t("runtime");
		t.start_time();

		
		create_basic_components();


		
		if (set_enginerun) {
			for (cur_iteration = 0; cur_iteration < max_iteration; cur_iteration++) {
				information.cur_iteration = cur_iteration;

				
				algorithm.before_iteration();

				
				{

#ifdef ANALYSIS_STATIC_MAIN_TIME
					std::chrono::system_clock::time_point start_1 = std::chrono::system_clock::now();
#endif

					switch (opt_product) {
					case INNER:
						break;
					case ROWBA:
						
						A_first = meta_a.get_first_index();
						A_last = meta_a.get_mid_index();
						B_first = meta_b.get_first_index();
						B_last = meta_b.get_mid_index();

						taskAlloc_rowBA(B_max_mb);
						break;
					case ROWAB:
						
						A_first = meta_a.get_first_index();
						A_last = meta_a.get_mid_index();
						B_first = meta_b.get_first_index();
						B_last = meta_b.get_mid_index();

						
						break;
					case OUTER:
						
						A_first = meta_a.get_mid_index();
						A_last = meta_a.get_last_index();
						B_first = meta_b.get_first_index();
						B_last = meta_b.get_mid_index();

						
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
				

				
				algorithm.after_iteration();

				
				if (set_indicator) {

#ifdef ANALYSIS_STATIC_MAIN_HTIME
					std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
#endif

					indicator.exchange();
					
					indicator.exchange_gpu();

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

		
		algorithm.finalize();
		std::cout << "algorithm finalized" << std::endl;
		
		destroy_basic_components();
		time_entry merge_t("merge_time");
		merge_t.start_time();
		
		algorithm.merge_parts();
		std::cout << "merging phase done" << std::endl;
		merge_t.stop_time();
		timer.add_entry(merge_t);

		
		t.stop_time();

		timer.add_entry(t);
		timer.print_timelist();
	}

	void taskAlloc_rowBA(int max_load) {
		int load_first = B_first;
		int load_last = B_last;
		int post_first = A_first;
		int post_last = A_last;
		time_entry tp("processing");
		time_entry loadb("load_b");
		

		blockmeta_ptr = new struct blockmeta[max_load];
				
		std::vector<int> load_blocklist;
		std::vector<int> post_blocklist;

		
		for (int index = load_first; index < load_last; index++) { 
			load_blocklist.push_back(index);
		}
		int num_load = load_last - load_first; 
		

		for (int index = post_first; index < post_last; index++) { 
			post_blocklist.push_back(index);
		}
		int num_post = post_last - post_first;  


		int loadsize = 0;
		int num_alloced = 0;
		int StreamIndex;

		for (int num_loaded = 0; num_loaded < num_load; num_loaded += loadsize) {

			
			loadsize = (num_load - num_loaded < max_load) ? (num_load - num_loaded) : max_load;

			loadb.start_time();
			
			for (int num_processed = 0; num_processed < loadsize; num_processed += num_alloced) {
				
				while (buffer.cpu_getfreesize() < 1);

				 
				num_alloced = buffer.cpu_allocbufs(file_b.get_fd(), &load_blocklist[num_loaded + num_processed], meta_b.get_bufferindex_ptr(), meta_a.get_bufferindex_ptr(), loadsize - num_processed);
				
				
				int count_gpu_num_allocated = 0;
				int num_gpu_processed_unloaded = num_loaded + num_processed;
				
				while (num_alloced > count_gpu_num_allocated) {
					while (buffer.gpu_getfreesize() < 1);

					int gpu_num_alloced = buffer.gpu_allocchs(file_b.get_fd(), &load_blocklist[num_gpu_processed_unloaded], meta_b.get_cacheindex_ptr(), meta_a.get_cacheindex_ptr(), num_alloced - count_gpu_num_allocated);

				

					
					for (int num_alloced_processing = 0; num_alloced_processing < gpu_num_alloced; num_alloced_processing++) {
						int tableindex = load_blocklist[num_gpu_processed_unloaded + num_alloced_processing];

						blockmeta_ptr[num_processed + num_alloced_processing].set(meta_b.get_bufferindex(tableindex), meta_b.get_cacheindex(tableindex), meta_b.get_graphmeta(tableindex));

						num_unfinished_tasks++;
						
						while(1){
							StreamIndex = __builtin_ctz(StreamFlag);
							if(StreamIndex!=32) break;
						}
						std::atomic_fetch_xor(&StreamFlag, (uint)(1) << (StreamIndex));
						

						 
						io_service.post(boost::bind(&engine::handler_load, this, tableindex, StreamIndex));
					}
					num_gpu_processed_unloaded += gpu_num_alloced;
					count_gpu_num_allocated += gpu_num_alloced;
				}
			}

			for (int index = 1; index < loadsize; index++) { 
				if (blockmeta_ptr[index - 1].graphmeta.first_nodeid == blockmeta_ptr[index].graphmeta.first_nodeid) {
					blockmeta_ptr[index].lb_offset = 1;
				}
			}

			while (num_unfinished_tasks > 0); 
			loadb.stop_time();
			timer.add_entry(loadb);

			
			for (int num_processed = 0; num_processed < num_post; num_processed += num_alloced) {
				std::cout << "num_processed =" << num_processed << std::endl;
				std::cout << "num_post = " << num_post << std::endl;
				
				while (buffer.cpu_getfreesize() < 1);
				 
				num_alloced = buffer.cpu_allocbufs(file_a.get_fd(), &post_blocklist[num_processed], meta_a.get_bufferindex_ptr(), meta_b.get_bufferindex_ptr(), num_post - num_processed);
				std::cout << "num_alloced = " << num_alloced << std::endl;
				
				
				std::vector<int> num_calc(num_alloced);
				
				
				
				#pragma omp parallel for num_threads(64)
				for (int i = 0; i < num_alloced; ++i) {
					int post_index = post_blocklist[num_processed + i];

					
					BLOCK* A_block_ptr = meta_a.get_bufferindex(post_index);
					graphmeta graphmeta = meta_a.get_graphmeta(post_index);
					vid_t A_first_nodeid = graphmeta.first_nodeid;
					vid_t A_last_nodeid = graphmeta.last_nodeid;

					time_entry loada("load_a");
					loada.start_time();
					
					
					file_a.read_block(A_block_ptr, graphmeta.block_id);

					loada.stop_time();
					timer.add_entry(loada);


					int num_node = A_last_nodeid - A_first_nodeid + 1;
					
					num_calc[i] = static_cast<int>(num_node);
				}
				long long total = 0;
				for (int i = 0; i < num_alloced; ++i) {
					total += num_calc[i];
				}
				double avg = static_cast<double>(total) / num_alloced;
				int trill = 1000000000;
				
				
				std::vector<int> post_blocklist_cpu;
				std::vector<int> post_blocklist_gpu;
				std::vector<int> post_blocklist_nutral;
				//static version workload allocation
				for (int i = 0; i < num_alloced; ++i) {
					int post_index = post_blocklist[num_processed + i];
					if (num_calc[i] > avg*1.8f ) { 
						post_blocklist_cpu.push_back(post_index);
					}
					else if (num_calc[i] < avg*0.2f) { 
						post_blocklist_gpu.push_back(post_index);
					}
					else { 
						post_blocklist_nutral.push_back(post_index);
					}
				}

				
				
				int num_gpu_alloced = buffer.gpu_allocchs(
					file_a.get_fd(),
					&post_blocklist[num_processed],
					meta_a.get_cacheindex_ptr(),
					meta_b.get_cacheindex_ptr(),
					num_alloced
				);

				std::cout << "num_gpu_alloced = " << num_gpu_alloced << std::endl;
				
				int posted_gpu = 0;
				int posted_cpu = 0;
				int posted_nutral = 0;
				
				while (posted_gpu < post_blocklist_gpu.size() || posted_cpu < post_blocklist_cpu.size()) {
					if (posted_gpu < post_blocklist_gpu.size() && (StreamFlag & 1u)) {  
						std::atomic_fetch_xor(&StreamFlag, 1u);
						int tableindex = post_blocklist_gpu[posted_gpu];
						num_unfinished_tasks++;
						io_service_gpu.post(boost::bind(&engine::handler_post_rowBA, this, tableindex, loadsize, 0));
						posted_gpu++;
					}

					if (posted_cpu < post_blocklist_cpu.size()) {
						running_threads++;
						int tableindex = post_blocklist_cpu[posted_cpu];
						num_unfinished_tasks++;
						io_service.post(boost::bind(&engine::handler_post_rowBA_cpu, this, tableindex, loadsize));
						posted_cpu++;
					} 
				}

				
				
				while (posted_nutral < post_blocklist_nutral.size()) {
					if (StreamFlag & 1u) {  
						std::atomic_fetch_xor(&StreamFlag, 1u);
						int tableindex = post_blocklist_nutral[posted_nutral];
						num_unfinished_tasks++;
						io_service_gpu.post(boost::bind(&engine::handler_post_rowBA, this, tableindex, loadsize, 0));
						posted_nutral++;
					}
					else if (running_threads < 8) { 
						running_threads++;
						int tableindex = post_blocklist_nutral[posted_nutral];
						num_unfinished_tasks++;
						io_service.post(boost::bind(&engine::handler_post_rowBA_cpu, this, tableindex, loadsize));
						posted_nutral++;
					}
				}

				while (num_unfinished_tasks > 0);
				for (int index = num_processed; index < num_processed + num_alloced; index++) {	
					buffer.cpu_unpinblock(meta_a.get_bufferindex(post_blocklist[index]));
					
					buffer.gpu_unpinblock(meta_a.get_cacheindex(post_blocklist[index]));
					
				}
				std::cout << "[INFO] Average flops per block = " << avg << std::endl;
				std::cout <<"post_blocklist_cpu.size() = " << post_blocklist_cpu.size() << std::endl;
				std::cout <<"post_blocklist_gpu.size() = " << post_blocklist_gpu.size() << std::endl;
				std::cout <<"post_blocklist_nutral.size() = " << post_blocklist_nutral.size() << std::endl;
			}
			


			for (int index = num_loaded; index < num_loaded + loadsize; index++) {	
				buffer.cpu_unpinblock(meta_b.get_bufferindex(load_blocklist[index]));
				
				buffer.gpu_unpinblock(meta_b.get_cacheindex(load_blocklist[index]));
				
			}
			

			algorithm.flush_row(); 
		}
		
		delete blockmeta_ptr;
	}

	void handler_post_rowBA_cpu(int post_index, int loadsize) {	
		if (thread_specific_ptr.get() == NULL) {
			int thread_index = std::atomic_fetch_add(&thread_data_index, 1);
			thread_specific_ptr.reset(thread_data_pptr[thread_index]);
		}

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
#endif

		
		BLOCK * block_ptr = meta_a.get_bufferindex(post_index);
		struct graphmeta graphmeta = meta_a.get_graphmeta(post_index);

		struct blockmeta A_blockmeta(block_ptr,0,graphmeta);
		
		
		
		

		
		

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::duration<double> interval_1 = std::chrono::system_clock::now() - start;

		thread_specific_ptr->stopwatch[TIME_IO] += interval_1.count();
#endif

#ifdef ANALYSIS_STATIC_WORKER_COUNT
		thread_specific_ptr->counter[NUM_IOS] += 1;
#endif
		time_entry tp("processing");
		tp.start_time();
		algorithm.rowrowProduct(A_blockmeta, blockmeta_ptr, loadsize, thread_specific_ptr.get());

		tp.stop_time();
		timer.add_entry(tp);

		

		num_unfinished_tasks--;
		running_threads--;

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::duration<double> interval_2 = std::chrono::system_clock::now() - start;

		thread_specific_ptr->stopwatch[TIME_PROC] += interval_2.count();
#endif

	}

	
	void handler_post_rowBA(int post_index, int loadsize, int streamIndex) {	
		if (thread_specific_ptr.get() == NULL) {
			int thread_index = std::atomic_fetch_add(&thread_data_index, 1);
			thread_specific_ptr.reset(thread_data_pptr[thread_index]);
		}

		
		
		BLOCK * block_ptr = meta_a.get_bufferindex(post_index);
		BLOCK* gpu_block_ptr = meta_a.get_cacheindex(post_index);
		struct graphmeta graphmeta = meta_a.get_graphmeta(post_index);
		RT[streamIndex].Streamindex=streamIndex;
		RT[streamIndex].block_ptr = block_ptr;
		RT[streamIndex].gpu_block_ptr= gpu_block_ptr;
		RT[streamIndex].buffer = &buffer;
		RT[streamIndex].engine= this;

		struct blockmeta A_blockmeta(block_ptr, gpu_block_ptr, graphmeta);
		
		
		
		
		
		gpu.transferAsyncH2D(block_ptr, gpu_block_ptr, streamIndex);
		

		
		


		time_entry tp("processing");
		tp.start_time();

		algorithm.rowrowProduct_GPU(A_blockmeta, blockmeta_ptr, loadsize, thread_specific_ptr.get(), gpu.getStream(streamIndex));

		tp.stop_time();
		timer.add_entry(tp);

		
		
		
		cudaLaunchHostFunc(gpu.getStream(streamIndex), MyCallback, (void*)&RT[streamIndex]);

	}

	
	void handler_load(int tableindex, int streamIndex) {
		if (thread_specific_ptr.get() == NULL)	{
			int thread_index = std::atomic_fetch_add(&thread_data_index, 1);
			thread_specific_ptr.reset(thread_data_pptr[thread_index]);
		}

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
#endif

		
		BLOCK* block_ptr = meta_b.get_bufferindex(tableindex);
		int block_id = meta_b.get_blockid(tableindex);
		BLOCK* gpu_block_ptr = meta_b.get_cacheindex(tableindex);
		RT[streamIndex].Streamindex=streamIndex;
		RT[streamIndex].block_ptr = block_ptr;
		RT[streamIndex].gpu_block_ptr= gpu_block_ptr;
		RT[streamIndex].buffer = &buffer;
		RT[streamIndex].engine= this;
		
		



		
		file_b.read_block(block_ptr, block_id); 
		gpu.transferAsyncH2D(block_ptr, gpu_block_ptr, streamIndex);

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::duration<double> interval_1 = std::chrono::system_clock::now() - start;

		thread_specific_ptr->stopwatch[TIME_IO] += interval_1.count();
#endif

#ifdef ANALYSIS_STATIC_WORKER_COUNT
		thread_specific_ptr->counter[NUM_IOS] += 1;
#endif

		cudaLaunchHostFunc(gpu.getStream(streamIndex), MyCallback2, (void*)&RT[streamIndex]);

#ifdef ANALYSIS_STATIC_WORKER_TIME
		std::chrono::duration<double> interval_2 = std::chrono::system_clock::now() - start;

		thread_specific_ptr->stopwatch[TIME_PROC] += interval_2.count();
#endif

	}

	void temp(){
		num_unfinished_tasks--;
	}
};


CUDART_CB void MyCallback(void *data){
	struct realgraph_task *task=(struct realgraph_task*)data;
	gpu_buffer_handler *gpu=(gpu_buffer_handler*)task->buffer;
	
	
	
	
	static_cast<class engine *>(task->engine)->temp();		
	std::atomic_fetch_or(&StreamFlag, (uint)(1) << (task->Streamindex));
	}

CUDART_CB void MyCallback2(void *data){
	struct realgraph_task *task=(struct realgraph_task*)data;
	gpu_buffer_handler *gpu=(gpu_buffer_handler*)task->buffer;
	static_cast<class engine *>(task->engine)->temp();		
	std::atomic_fetch_or(&StreamFlag, (uint)(1) << (task->Streamindex));
	}
