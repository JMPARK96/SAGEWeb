#include <cn_utils.hpp>
#include <timer_handler.hpp>

// -------------------------------------------------------
// degree 파일 읽기
// flat 배열: degree[node_id] = outdegree (vid_t, 4바이트씩)
// -------------------------------------------------------

std::vector<vid_t> load_degrees(const std::string & degree_path)
{
	std::vector<vid_t> degrees;
	FILE * fp = fopen(degree_path.c_str(), "rb");
	if (!fp) {
		fprintf(stderr, "cannot open %s\n", degree_path.c_str());
		return degrees;
	}
	vid_t deg;
	while (fread(&deg, sizeof(vid_t), 1, fp) == 1)
		degrees.push_back(deg);
	fclose(fp);
	return degrees;
}

// -------------------------------------------------------
// Jaccard 점수 계산 및 출력
// Jaccard(i,j) = CN(i,j) / (deg(i) + deg(j) - CN(i,j))
// -------------------------------------------------------

struct jaccard_entry {
	vid_t i, j;
	double score;
};

void write_jaccard_scores(
	const std::string & datapath,
	const std::string & output,
	int num_threads,
	const std::vector<vid_t> & degrees,
	const std::string & output_path)
{
	FILE * fp = fopen(output_path.c_str(), "wt");
	if (!fp) {
		fprintf(stderr, "cannot open %s\n", output_path.c_str());
		return;
	}
	int cnt = 0;
	stream_pairs(datapath, output, num_threads,
		[&](vid_t i, vid_t j, float count) {
			if (i >= (vid_t)degrees.size() || j >= (vid_t)degrees.size()) return;
			double cn    = count;
			double denom = degrees[i] + degrees[j] - cn;
			if (denom <= 0) return;
			fprintf(fp, "%u - %u : %.6f\n", i, j, cn / denom);
			cnt++;
		});
	fclose(fp);
	printf("wrote %d Jaccard scores to %s\n", cnt, output_path.c_str());
}

// -------------------------------------------------------
// main
// -------------------------------------------------------

int main(int argc, char ** argv) {
	std::string datapath    = "./";
	std::string output      = "Stanford";
	std::string degree_file = "Stanford_a";
	int num_threads         = 11;
	int merge               = 1;
	int partial_agg         = 1;
	bool node_engine        = false;
	std::string product     = "ROWBA";
	int gpu                 = 0;

	timer_handler timer;
	time_entry t_total("link_prediction_total");
	t_total.start_time();

	for (int i = 1; i < argc - 1; i++) {
		if (std::string(argv[i]) == "datapath")     datapath    = argv[i+1];
		if (std::string(argv[i]) == "output")       output      = argv[i+1];
		if (std::string(argv[i]) == "degree_file")  degree_file = argv[i+1];
		if (std::string(argv[i]) == "threads")      num_threads = atoi(argv[i+1]);
		if (std::string(argv[i]) == "merge")        merge       = atoi(argv[i+1]);
		if (std::string(argv[i]) == "partial_agg")  partial_agg = atoi(argv[i+1]);
		if (std::string(argv[i]) == "node_engine")  node_engine = atoi(argv[i+1]) != 0;
		if (std::string(argv[i]) == "product")      product     = argv[i+1];
		if (std::string(argv[i]) == "gpu")          gpu         = atoi(argv[i+1]);
	}

	std::string graph_a = degree_file;

	if (access((datapath + graph_a + ".graphinfo").c_str(), F_OK) != 0) {
		fprintf(stderr, "missing preprocessed graph files: %s\n",
			(datapath + graph_a + ".graphinfo").c_str());
		fprintf(stderr, "hint: run spgemm preprocess_a 1 preprocess_b 1 enginerun 0 first\n");
		return 1;
	}

	// A×A SpGEMM → {output}_result_final_end_{t} 생성
	time_entry t_spgemm("link_prediction_spgemm_AxA");
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
		<< " output "    << output
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

	// degree 파일 읽기
	time_entry t_load_degrees("link_prediction_load_degrees");
	t_load_degrees.start_time();
	std::vector<vid_t> degrees =
		load_degrees(datapath + degree_file + ".outdegree");
	t_load_degrees.stop_time();
	timer.add_entry(t_load_degrees);

	// Jaccard 점수 전체 출력 (스트리밍 - 메모리 적재 없음)
	time_entry t_write_jaccard("link_prediction_write_jaccard_scores");
	t_write_jaccard.start_time();
	write_jaccard_scores(
		datapath,
		output,
		num_threads,
		degrees,
		datapath + output + "_jaccard.txt");
	t_write_jaccard.stop_time();
	timer.add_entry(t_write_jaccard);

	t_total.stop_time();
	timer.add_entry(t_total);

	printf("[link_prediction timing]\n");
	timer.print_timelist();

	return 0;
}
