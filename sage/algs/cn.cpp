#include <cn_utils.hpp>
#include <timer_handler.hpp>

// top-K 쌍의 공통이웃 노드 ID를 파일에 출력
void write_common_neighbor_ids(
	const std::vector<common_neighbor_pair> & pairs,
	int topk,
	const std::string & datapath,
	const std::string & graph_a,
	const std::string & output_path)
{
	block_index index = load_block_index(datapath, graph_a);
	std::string graphblock_path = datapath + graph_a + ".graphblock";

	FILE * fp = fopen(output_path.c_str(), "wt");
	if (!fp) { fprintf(stderr, "cannot open %s\n", output_path.c_str()); return; }

	int limit = std::min(topk, (int)pairs.size());
	for (int idx = 0; idx < limit; idx++) {
		vid_t i = pairs[idx].i;
		vid_t j = pairs[idx].j;
		auto common = find_intersection(
			load_adjacency_list(i, graphblock_path, index),
			load_adjacency_list(j, graphblock_path, index));

		fprintf(fp, "%u - %u : ", i, j);
		for (int k = 0; k < (int)common.size(); k++) {
			if (k > 0) fprintf(fp, ", ");
			fprintf(fp, "%u", common[k]);
		}
		fprintf(fp, "\n");
	}
	fclose(fp);
	printf("wrote top-%d common neighbor ID lists to %s\n", limit, output_path.c_str());
}

int main(int argc, char ** argv) {
	std::string datapath    = "./";
	std::string graph       = "Stanford";
	int num_threads         = 11;
	int topk                = 100;
	std::string product     = "ROWBA";
	int merge               = 1;
	int partial_agg         = 1;
	bool node_engine        = false;
	int gpu                 = 0;

	timer_handler timer;
	time_entry t_total("cn_total");
	t_total.start_time();

	for (int i = 1; i < argc - 1; i++) {
		if (std::string(argv[i]) == "datapath") datapath    = argv[i+1];
		if (std::string(argv[i]) == "output")   graph       = argv[i+1];
		if (std::string(argv[i]) == "threads")  num_threads = atoi(argv[i+1]);
		if (std::string(argv[i]) == "topk")     topk        = atoi(argv[i+1]);
		if (std::string(argv[i]) == "product")     product     = argv[i+1];
		if (std::string(argv[i]) == "merge")       merge       = atoi(argv[i+1]);
		if (std::string(argv[i]) == "partial_agg") partial_agg = atoi(argv[i+1]);
		if (std::string(argv[i]) == "node_engine") node_engine = atoi(argv[i+1]) != 0;
		if (std::string(argv[i]) == "gpu")         gpu         = atoi(argv[i+1]);
	}

	std::string graph_a = graph + "_a";

	if (access((datapath + graph_a + ".graphinfo").c_str(), F_OK) != 0) {
		fprintf(stderr, "missing preprocessed graph files: %s\n",
			(datapath + graph_a + ".graphinfo").c_str());
		fprintf(stderr, "hint: run spgemm preprocess_a 1 preprocess_b 1 enginerun 0 first\n");
		return 1;
	}

	// A×A SpGEMM → {graph}_result_final_end_{t} 생성
	time_entry t_spgemm("cn_spgemm_AxA");
	t_spgemm.start_time();
	std::stringstream cmd;
	const char* spgemm_bin;
	if      (gpu)                          spgemm_bin = "./spgemm-gpu";
	else if ( node_engine && !partial_agg) spgemm_bin = "./spgemm-plain-node-nonaggre";
	else if ( node_engine &&  partial_agg) spgemm_bin = "./spgemm-plain-node";
	else if (!node_engine && !partial_agg) spgemm_bin = "./spgemm-plain-nonaggre";
	else                                   spgemm_bin = "./spgemm-plain";

	cmd << spgemm_bin
		<< " datapath "  << datapath
		<< " file_a "    << graph_a
		<< " file_b "    << graph_a
		<< " output "    << graph
		<< " preprocess_a 0 preprocess_b 0"
		<< " enginerun 1 maxiters 1 threads " << num_threads
		<< " product "   << product
		<< " merge "     << merge;
	int rc = system(cmd.str().c_str());
	t_spgemm.stop_time();
	timer.add_entry(t_spgemm);
	if (rc != 0) {
		fprintf(stderr, "spgemm failed (%d): %s\n", rc, cmd.str().c_str());
		return 1;
	}

	// 단일 패스: count 파일 쓰기 + top-K heap 동시 수행
	auto cmp_fn = [](const common_neighbor_pair & a, const common_neighbor_pair & b) {
		return a.count > b.count;
	};
	std::priority_queue<common_neighbor_pair,
		std::vector<common_neighbor_pair>,
		decltype(cmp_fn)> heap(cmp_fn);

	time_entry t_counts("cn_write_counts");
	t_counts.start_time();
	std::string count_path = datapath + graph + "_common_neighbor_count.txt";
	FILE * fp = fopen(count_path.c_str(), "wt");
	if (!fp) { fprintf(stderr, "cannot open %s\n", count_path.c_str()); return 1; }
	int cnt = 0;
	stream_pairs(datapath, graph, num_threads,
		[&](vid_t i, vid_t j, float count) {
			fprintf(fp, "%u - %u : %d\n", i, j, (int)count);
			cnt++;
			common_neighbor_pair p{ i, j, count };
			if ((int)heap.size() < topk)
				heap.push(p);
			else if (count > heap.top().count) {
				heap.pop();
				heap.push(p);
			}
		});
	fclose(fp);
	printf("wrote %d common neighbor counts to %s\n", cnt, count_path.c_str());
	t_counts.stop_time();
	timer.add_entry(t_counts);

	std::vector<common_neighbor_pair> pairs;
	pairs.reserve(heap.size());
	while (!heap.empty()) { pairs.push_back(heap.top()); heap.pop(); }
	std::reverse(pairs.begin(), pairs.end());  // min-heap이므로 역순 정렬 필요

	// top-K 쌍의 공통이웃 ID 출력
	time_entry t_ids("cn_write_ids");
	t_ids.start_time();
	write_common_neighbor_ids(
		pairs, topk, datapath, graph_a,
		datapath + graph + "_common_neighbor_ids.txt");
	t_ids.stop_time();
	timer.add_entry(t_ids);

	t_total.stop_time();
	timer.add_entry(t_total);
	printf("[cn timing]\n");
	timer.print_timelist();

	return 0;
}
