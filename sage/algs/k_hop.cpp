#include <cn_utils.hpp>
#include <timer_handler.hpp>
#include <unordered_set>

// -------------------------------------------------------
// 2-hop 경로 열거: A→X→B  (X = A와 B의 공통 이웃)
// -------------------------------------------------------
void write_2hop_paths(
    const std::vector<common_neighbor_pair> & pairs,
    int topk,
    const std::string & datapath,
    const std::string & graph_a,
    const std::string & output_path,
    int max_paths)
{
    block_index index = load_block_index(datapath, graph_a);
    std::string gblock = datapath + graph_a + ".graphblock";

    FILE * fp = fopen(output_path.c_str(), "wt");
    if (!fp) { fprintf(stderr, "cannot open %s\n", output_path.c_str()); return; }

    int limit = (topk < (int)pairs.size()) ? topk : (int)pairs.size();
    for (int idx = 0; idx < limit; idx++) {
        vid_t a = pairs[idx].i, b = pairs[idx].j;
        auto N_a = load_adjacency_list(a, gblock, index);
        auto N_b = load_adjacency_list(b, gblock, index);
        auto common = find_intersection(N_a, N_b);

        fprintf(fp, "%u - %u :", a, b);
        int cnt = 0;
        for (vid_t x : common) {
            if (cnt >= max_paths) { fprintf(fp, ", ..."); break; }
            fprintf(fp, "%s %u - %u - %u", (cnt ? "," : ""), a, x, b);
            cnt++;
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
    printf("wrote 2-hop paths to %s\n", output_path.c_str());
}

// -------------------------------------------------------
// 3-hop 경로 열거: A→X→Y→B
// -------------------------------------------------------
void write_3hop_paths(
    const std::vector<common_neighbor_pair> & pairs,
    int topk,
    const std::string & datapath,
    const std::string & graph_a,
    const std::string & output_path,
    int max_paths)
{
    block_index index = load_block_index(datapath, graph_a);
    std::string gblock = datapath + graph_a + ".graphblock";

    FILE * fp = fopen(output_path.c_str(), "wt");
    if (!fp) { fprintf(stderr, "cannot open %s\n", output_path.c_str()); return; }

    int limit = (topk < (int)pairs.size()) ? topk : (int)pairs.size();
    for (int idx = 0; idx < limit; idx++) {
        vid_t a = pairs[idx].i, b = pairs[idx].j;
        auto N_a = load_adjacency_list(a, gblock, index);
        auto N_b = load_adjacency_list(b, gblock, index);
        std::unordered_set<vid_t> N_b_set(N_b.begin(), N_b.end());

        fprintf(fp, "%u - %u :", a, b);
        int cnt = 0;
        bool done = false;
        for (vid_t x : N_a) {
            if (done) break;
            auto N_x = load_adjacency_list(x, gblock, index);
            for (vid_t y : N_x) {
                if (N_b_set.count(y)) {
                    if (cnt >= max_paths) { fprintf(fp, ", ..."); done = true; break; }
                    fprintf(fp, "%s %u - %u - %u - %u", (cnt ? "," : ""), a, x, y, b);
                    cnt++;
                }
            }
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
    printf("wrote 3-hop paths to %s\n", output_path.c_str());
}

// -------------------------------------------------------
// 4-hop 경로 열거: A→X→Y→Z→B
// -------------------------------------------------------
void write_4hop_paths(
    const std::vector<common_neighbor_pair> & pairs,
    int topk,
    const std::string & datapath,
    const std::string & graph_a,
    const std::string & output_path,
    int max_paths)
{
    block_index index = load_block_index(datapath, graph_a);
    std::string gblock = datapath + graph_a + ".graphblock";

    FILE * fp = fopen(output_path.c_str(), "wt");
    if (!fp) { fprintf(stderr, "cannot open %s\n", output_path.c_str()); return; }

    int limit = (topk < (int)pairs.size()) ? topk : (int)pairs.size();
    for (int idx = 0; idx < limit; idx++) {
        vid_t a = pairs[idx].i, b = pairs[idx].j;
        auto N_a = load_adjacency_list(a, gblock, index);
        auto N_b = load_adjacency_list(b, gblock, index);
        std::unordered_set<vid_t> N_b_set(N_b.begin(), N_b.end());

        fprintf(fp, "%u - %u :", a, b);
        int cnt = 0;
        bool done = false;
        for (vid_t x : N_a) {
            if (done) break;
            auto N_x = load_adjacency_list(x, gblock, index);
            for (vid_t y : N_x) {
                if (done) break;
                auto N_y = load_adjacency_list(y, gblock, index);
                for (vid_t z : N_y) {
                    if (N_b_set.count(z)) {
                        if (cnt >= max_paths) { fprintf(fp, ", ..."); done = true; break; }
                        fprintf(fp, "%s %u - %u - %u - %u - %u", (cnt ? "," : ""), a, x, y, z, b);
                        cnt++;
                    }
                }
            }
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
    printf("wrote 4-hop paths to %s\n", output_path.c_str());
}

// -------------------------------------------------------
// main
// -------------------------------------------------------
int main(int argc, char ** argv) {
    std::string datapath  = "./";
    std::string output    = "Stanford";
    std::string graph_a   = "Stanford_a";
    int k                 = 2;
    int num_threads       = 11;
    int topk              = 100;
    int max_paths         = 20;
    int merge             = 1;
    int partial_agg       = 1;
    bool node_engine      = false;
    std::string product   = "ROWBA";
    int gpu               = 0;

    timer_handler timer;
    time_entry t_total("k_hop_total");
    t_total.start_time();

    for (int i = 1; i < argc - 1; i++) {
        if (std::string(argv[i]) == "datapath")    datapath    = argv[i+1];
        if (std::string(argv[i]) == "output")      output      = argv[i+1];
        if (std::string(argv[i]) == "file_a")      graph_a     = argv[i+1];
        if (std::string(argv[i]) == "k")           k           = atoi(argv[i+1]);
        if (std::string(argv[i]) == "threads")     num_threads = atoi(argv[i+1]);
        if (std::string(argv[i]) == "topk")        topk        = atoi(argv[i+1]);
        if (std::string(argv[i]) == "max_paths")   max_paths   = atoi(argv[i+1]);
        if (std::string(argv[i]) == "merge")       merge       = atoi(argv[i+1]);
        if (std::string(argv[i]) == "partial_agg") partial_agg = atoi(argv[i+1]);
        if (std::string(argv[i]) == "node_engine") node_engine = atoi(argv[i+1]) != 0;
        if (std::string(argv[i]) == "product")     product     = argv[i+1];
        if (std::string(argv[i]) == "gpu")         gpu         = atoi(argv[i+1]);
    }

    if (access((datapath + graph_a + ".graphinfo").c_str(), F_OK) != 0) {
        fprintf(stderr, "missing preprocessed graph files: %s\n",
            (datapath + graph_a + ".graphinfo").c_str());
        fprintf(stderr, "hint: run spgemm preprocess_a 1 preprocess_b 1 enginerun 0 first\n");
        return 1;
    }

    if (k < 2 || k > 4) {
        fprintf(stderr, "k=%d not supported. Use k=2, 3, or 4.\n", k);
        return 1;
    }

    const char* spgemm_bin;
    if      (gpu)                          spgemm_bin = "./spgemm-gpu";
    else if ( node_engine && !partial_agg) spgemm_bin = "./spgemm-plain-node-nonaggre";
    else if ( node_engine &&  partial_agg) spgemm_bin = "./spgemm-plain-node";
    else if (!node_engine && !partial_agg) spgemm_bin = "./spgemm-plain-nonaggre";
    else                                   spgemm_bin = "./spgemm-plain";

    std::string graph_2   = output + "_2";
    std::string graph_2_a = output + "_2_a";
    std::string graph_2_txt = datapath + graph_2 + ".txt";
    std::string graph_3   = output + "_3";
    std::string graph_4   = output + "_4";

    // (1) k=2: A×A SpGEMM (메인 계산)
    if (k == 2) {
        time_entry t_spgemm("k_hop_spgemm_axa");
        t_spgemm.start_time();
        std::stringstream cmd;
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
        if (rc != 0) { fprintf(stderr, "A×A spgemm failed (%d)\n", rc); return 1; }
    }

    // (2) k=3,4: graph_2_a 없으면 A×A → txt 변환 → 전처리
    if (k >= 3 && access((datapath + graph_2_a + ".graphinfo").c_str(), F_OK) != 0) {
        // A×A SpGEMM (전처리용, 항상 spgemm-plain)
        {
            time_entry t_axa("k_hop_preprocess_axa");
            t_axa.start_time();
            std::stringstream cmd;
            cmd << "./spgemm-plain"
                << " datapath "  << datapath
                << " file_a "    << graph_a
                << " file_b "    << graph_a
                << " output "    << graph_2
                << " preprocess_a 0 preprocess_b 0"
                << " enginerun 1 maxiters 1 threads " << num_threads
                << " product ROWBA merge 1";
            int rc = system(cmd.str().c_str());
            t_axa.stop_time();
            timer.add_entry(t_axa);
            if (rc != 0) { fprintf(stderr, "A×A preprocess spgemm failed (%d)\n", rc); return 1; }
        }

        // A×A 결과 → graph_2.txt 변환 (상삼각만)
        {
            time_entry t_conv("k_hop_preprocess_convert");
            t_conv.start_time();
            FILE * fp2 = fopen(graph_2_txt.c_str(), "wt");
            if (!fp2) { fprintf(stderr, "cannot open %s\n", graph_2_txt.c_str()); return 1; }
            for (int t = 0; t < num_threads; t++) {
                std::stringstream ss;
                ss << datapath << graph_2 << "_result_final_end_" << t;
                int fd = open(ss.str().c_str(), O_RDONLY);
                if (fd < 0) { fprintf(stderr, "cannot open %s\n", ss.str().c_str()); continue; }
                stream_result_file(fd, [&](vid_t i, const adj__ & a) {
                    if (i <= a.y) fprintf(fp2, "%u %u %f\n", i, a.y, a.v);
                });
                close(fd);
            }
            fclose(fp2);
            t_conv.stop_time();
            timer.add_entry(t_conv);
        }

        // graph_2_a 전처리
        {
            time_entry t_prep("k_hop_preprocess_2a");
            t_prep.start_time();
            std::stringstream cmd;
            cmd << "./spgemm-plain"
                << " datapath "  << datapath
                << " file_a "    << graph_2_a
                << " file_b "    << graph_a
                << " data_a "    << graph_2 << ".txt"
                << " output "    << graph_3
                << " preprocess_a 1 preprocess_b 0"
                << " enginerun 0 maxiters 0 merge 0";
            int rc = system(cmd.str().c_str());
            t_prep.stop_time();
            timer.add_entry(t_prep);
            if (rc != 0) { fprintf(stderr, "graph_2_a preprocess failed (%d)\n", rc); return 1; }
        }
    }

    // (3) k=3: A²×A SpGEMM (메인 계산)
    if (k == 3) {
        time_entry t_spgemm("k_hop_spgemm_cxa");
        t_spgemm.start_time();
        std::stringstream cmd;
        cmd << spgemm_bin
            << " datapath "  << datapath
            << " file_a "    << graph_2_a
            << " file_b "    << graph_a
            << " output "    << graph_3
            << " preprocess_a 0 preprocess_b 0"
            << " enginerun 1 maxiters 1 threads " << num_threads
            << " a_mb 4 b_mb 1024 c_mb 16 total_mb 1044"
            << " product "   << product
            << " merge "     << merge;
        int rc = system(cmd.str().c_str());
        t_spgemm.stop_time();
        timer.add_entry(t_spgemm);
        if (rc != 0) { fprintf(stderr, "A²×A spgemm failed (%d)\n", rc); return 1; }
    }

    // (4) k=4: C×C SpGEMM (메인 계산)
    if (k == 4) {
        time_entry t_spgemm("k_hop_spgemm_cxc");
        t_spgemm.start_time();
        std::stringstream cmd;
        cmd << spgemm_bin
            << " datapath "  << datapath
            << " file_a "    << graph_2_a
            << " file_b "    << graph_2_a
            << " output "    << graph_4
            << " preprocess_a 0 preprocess_b 0"
            << " enginerun 1 maxiters 1 threads " << num_threads
            << " a_mb 4 b_mb 1024 c_mb 16 total_mb 1044"
            << " product "   << product
            << " merge "     << merge;
        int rc = system(cmd.str().c_str());
        t_spgemm.stop_time();
        timer.add_entry(t_spgemm);
        if (rc != 0) { fprintf(stderr, "C×C spgemm failed (%d)\n", rc); return 1; }
    }

    std::string result_name;
    if      (k == 2) result_name = output;
    else if (k == 3) result_name = graph_3;
    else             result_name = graph_4;

    // (2) 단일 패스: 전체 count 파일 쓰기 + topk heap 동시 수행
    auto cmp_fn = [](const common_neighbor_pair & a, const common_neighbor_pair & b) {
        return a.count > b.count;
    };
    std::priority_queue<common_neighbor_pair,
        std::vector<common_neighbor_pair>,
        decltype(cmp_fn)> heap(cmp_fn);

    std::string count_path = datapath + output + "_" + std::to_string(k) + "hop_count.txt";
    time_entry t_write_counts("k_hop_write_count_file");
    t_write_counts.start_time();
    FILE * fp = fopen(count_path.c_str(), "wt");
    if (fp) {
        int cnt = 0;
        stream_pairs(datapath, result_name, num_threads,
            [&](vid_t i, vid_t j, float count) {
                fprintf(fp, "%u - %u : %.0f\n", i, j, (double)count);
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
        printf("wrote %d-hop counts (%d pairs) to %s\n", k, cnt, count_path.c_str());
    }
    t_write_counts.stop_time();
    timer.add_entry(t_write_counts);

    std::vector<common_neighbor_pair> pairs;
    pairs.reserve(heap.size());
    while (!heap.empty()) { pairs.push_back(heap.top()); heap.pop(); }

    std::string path_out = datapath + output + "_" + std::to_string(k) + "hop_paths.txt";
    time_entry t_write_paths("k_hop_write_paths");
    t_write_paths.start_time();
    if (k == 2)
        write_2hop_paths(pairs, topk, datapath, graph_a, path_out, max_paths);
    else if (k == 3)
        write_3hop_paths(pairs, topk, datapath, graph_a, path_out, max_paths);
    else if (k == 4)
        write_4hop_paths(pairs, topk, datapath, graph_a, path_out, max_paths);
    t_write_paths.stop_time();
    timer.add_entry(t_write_paths);

    t_total.stop_time();
    timer.add_entry(t_total);

    printf("[k_hop timing]\n");
    timer.print_timelist();

    return 0;
}
