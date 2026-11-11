#include <generals.hpp>
#include <spgemm_types.hpp>
#include <timer_handler.hpp>
#include <fcntl.h>
#include <unistd.h>
#include <cn_utils.hpp>

// A²(=C) SpGEMM 결과 파일들을 txt edgelist로 변환
// 이후 spgemm preprocess 입력으로 사용
void convert_c_to_txt(
    const std::string & datapath,
    const std::string & graph_2,   // A² 결과 이름 (e.g. "Stanford_2")
    int num_threads,
    const std::string & txt_path)
{
    FILE * fp = fopen(txt_path.c_str(), "wt");
    if (!fp) { fprintf(stderr, "cannot open %s\n", txt_path.c_str()); return; }

    for (int t = 0; t < num_threads; t++) {
        std::stringstream ss;
        ss << datapath << graph_2 << "_result_final_end_" << t;

        int fd = open(ss.str().c_str(), O_RDONLY);
        if (fd < 0) { fprintf(stderr, "cannot open %s\n", ss.str().c_str()); continue; }

        stream_result_file(fd, [&](vid_t i, const adj__ & a) {
            // symmetric이 역방향 추가하므로 i<=j - 대각 & 상삼각만 출력
            if (i <= a.y) fprintf(fp, "%u %u %f\n", i, a.y, a.v); 
        });

        close(fd);
    }

    fclose(fp);
}

// A³(=D=C×A) SpGEMM 결과에서 대각원소 D[i][i] 읽어 삼각형 수 계산
// 반환값: 노드별 삼각형 수 (내림차순 정렬), top-K 출력에 재사용
std::vector<std::pair<vid_t, double>> count_triangles(
    const std::string & datapath,
    const std::string & graph_3,   // A³ 결과 이름 (e.g. "Stanford_3")
    int num_threads,
    const std::string & result_path)
{
    val_t sum = 0;
    std::vector<std::pair<vid_t, double>> triangles;

    for (int t = 0; t < num_threads; t++) {
        std::stringstream ss;
        ss << datapath << graph_3 << "_result_final_end_" << t;

        int fd = open(ss.str().c_str(), O_RDONLY);
        if (fd < 0) { fprintf(stderr, "cannot open %s\n", ss.str().c_str()); continue; }

        stream_result_file(fd, [&](vid_t i, const adj__ & a) {
            if (i == a.y) {
                // 따라서 실제 삼각형 수 = (D[i][i] / 2), 방향별 2번 카운트되므로 (i->j->k->i, i->k->j->i)
                triangles.push_back({i, (double)a.v / 2});
                sum += a.v;
            }
        });

        close(fd);
    }

    FILE * fp = fopen(result_path.c_str(), "wt");
    if (!fp) { fprintf(stderr, "cannot open %s\n", result_path.c_str()); return {}; }

    // 전체 합산 후 /6:
    // 삼각형 하나가 세 꼭짓점 각각에서 2번씩 = 총 6번 카운트
    fprintf(fp, "Total Triangle Count: %.0f\n", sum / 6);

    std::sort(triangles.begin(), triangles.end(),
        [](const std::pair<vid_t, double> & a, const std::pair<vid_t, double> & b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });

    for (std::pair<vid_t, double> p : triangles) {
        fprintf(fp, "%u : %.0f\n", p.first, p.second);
    }

    fclose(fp);
    return triangles;
}

void write_triangle_compositions(
    const std::vector<std::pair<vid_t, double>> & triangles,
    int topk,
    const std::string & datapath,
	const std::string & graph_a,
    const std::string & result_path,
    int max_paths) 
{
    block_index index = load_block_index(datapath, graph_a);
    std::string graphblock_path = datapath + graph_a + ".graphblock";

    FILE * fp = fopen(result_path.c_str(), "wt");
	if (!fp) {
		fprintf(stderr, "cannot open %s\n", result_path.c_str());
		return;
	}

    int limit = (topk < (int)triangles.size()) ? topk : (int)triangles.size();
    for (int idx = 0; idx < limit; idx++) {
        vid_t i = triangles[idx].first;
        fprintf(fp, "%u : ", i);
        auto N_i = load_adjacency_list(i, graphblock_path, index);
        
        bool first = true;
        int cnt = 0;
        bool done = false;
        for (vid_t j : N_i) {
            if (done) break;
            auto N_j = load_adjacency_list(j, graphblock_path, index);
            auto common = find_intersection(N_i, N_j);
            
            for (vid_t k : common) {
                if (k > j) {// 중복 제거
                    if (cnt >= max_paths) {
                        fprintf(fp, ", ...");
                        done = true;
                        break;
                    }
                    if (!first) fprintf(fp, ", ");
                    fprintf(fp, "%u - %u - %u", i, j, k);
                    first = false;
                    cnt++;
                }    
            }
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}

int main(int argc, char ** argv) {
    std::string datapath    = "./";
    std::string graph       = "Stanford";
    int num_threads         = 11;
    int topk                = 100;
    int max_paths           = 20;
    bool preprocess_only    = false;
    std::string product     = "ROWBA";
    int merge               = 1;
    int partial_agg         = 1;
    bool node_engine        = false;
    int gpu                 = 0;

    timer_handler timer;
    time_entry t_total("triangle_total");
    t_total.start_time();

    for (int i = 1; i < argc - 1; i++) {
        if (std::string(argv[i]) == "datapath")        datapath        = argv[i+1];
        if (std::string(argv[i]) == "output")          graph           = argv[i+1];
        if (std::string(argv[i]) == "threads")         num_threads     = atoi(argv[i+1]);
        if (std::string(argv[i]) == "topk")            topk            = atoi(argv[i+1]);
        if (std::string(argv[i]) == "max_paths")       max_paths       = atoi(argv[i+1]);
        if (std::string(argv[i]) == "preprocess_only") preprocess_only = atoi(argv[i+1]) != 0;
        if (std::string(argv[i]) == "product")         product         = argv[i+1];
        if (std::string(argv[i]) == "merge")           merge           = atoi(argv[i+1]);
        if (std::string(argv[i]) == "partial_agg")    partial_agg     = atoi(argv[i+1]);
        if (std::string(argv[i]) == "node_engine")     node_engine     = atoi(argv[i+1]) != 0;
        if (std::string(argv[i]) == "gpu")             gpu             = atoi(argv[i+1]);
    }

    std::string graph_a     = graph + "_a";
    std::string graph_b     = graph + "_b";
    std::string graph_2     = graph + "_2";
    std::string graph_2_a   = graph + "_2_a";
    std::string graph_2_txt = datapath + graph_2 + ".txt";
    std::string graph_3     = graph + "_3";

    if (preprocess_only) {
        // A×A SpGEMM → {graph_2}_result_final_end_*
        time_entry t_axa("triangle_preprocess_AxA");
        t_axa.start_time();
        std::stringstream cmd_axa;
        cmd_axa << "./spgemm-plain"
            << " datapath "  << datapath
            << " file_a "    << graph_a
            << " file_b "    << graph_b
            << " output "    << graph_2
            << " preprocess_a 0 preprocess_b 0"
            << " enginerun 1 maxiters 1 threads " << num_threads
            << " product ROWBA merge 1";
        int rc = system(cmd_axa.str().c_str());
        t_axa.stop_time();
        timer.add_entry(t_axa);
        if (rc != 0) { fprintf(stderr, "AxA spgemm failed (%d)\n", rc); return 1; }

        // {graph_2}_result_final_end_* → {graph}_2.txt
        time_entry t_conv("triangle_preprocess_convert");
        t_conv.start_time();
        convert_c_to_txt(datapath, graph_2, num_threads, graph_2_txt);
        t_conv.stop_time();
        timer.add_entry(t_conv);

        // {graph}_2_a 블록 전처리
        time_entry t_prep("triangle_preprocess_2a");
        t_prep.start_time();
        std::stringstream cmd_prep;
        cmd_prep << "./spgemm-plain"
            << " datapath "  << datapath
            << " file_a "    << graph_2_a
            << " file_b "    << graph_a
            << " data_a "    << graph_2 << ".txt"
            << " output "    << graph_3
            << " preprocess_a 1 preprocess_b 0"
            << " enginerun 0 maxiters 0 merge 0";
        rc = system(cmd_prep.str().c_str());
        t_prep.stop_time();
        timer.add_entry(t_prep);
        if (rc != 0) { fprintf(stderr, "_2_a preprocess failed (%d)\n", rc); return 1; }

        t_total.stop_time();
        timer.add_entry(t_total);
        printf("[triangle preprocess timing]\n");
        timer.print_timelist();
        return 0;
    }

    // 일반 실행: _2_a가 업로드 시 전처리되어 있어야 함
    if (access((datapath + graph_2_a + ".graphinfo").c_str(), F_OK) != 0) {
        fprintf(stderr, "missing %s\nhint: upload preprocessing must complete first\n",
            (datapath + graph_2_a + ".graphinfo").c_str());
        return 1;
    }

    // A³ = A²×A SpGEMM
    std::stringstream cmd;
    const char* spgemm_bin;
    if      (gpu)                          spgemm_bin = "./spgemm-gpu";
    else if ( node_engine && !partial_agg) spgemm_bin = "./spgemm-plain-node-nonaggre";
    else if ( node_engine &&  partial_agg) spgemm_bin = "./spgemm-plain-node";
    else if (!node_engine && !partial_agg) spgemm_bin = "./spgemm-plain-nonaggre";
    else                                   spgemm_bin = "./spgemm-plain";

    cmd << spgemm_bin
        << " datapath "    << datapath
        << " file_a "      << graph_2_a
        << " file_b "      << graph_a
        << " output "      << graph_3
        << " preprocess_a 0 preprocess_b 0"
        << " enginerun 1 maxiters 1 threads " << num_threads
        << " a_mb 4 b_mb 1024 c_mb 16 total_mb 1044"
        << " product "     << product
        << " merge "       << merge;
    time_entry t_spgemm("triangle_spgemm_CxA");
    t_spgemm.start_time();
    int rc = system(cmd.str().c_str());
    t_spgemm.stop_time();
    timer.add_entry(t_spgemm);
    if (rc != 0) {
        fprintf(stderr, "spgemm failed (%d): %s\n", rc, cmd.str().c_str());
        return 1;
    }

    // D[i][i] 읽어서 삼각형 수 계산
    time_entry t_count("triangle_count_triangles");
    t_count.start_time();
    auto triangles = count_triangles(datapath, graph_3, num_threads, datapath + graph + "_triangle_count.txt");
    t_count.stop_time();
    timer.add_entry(t_count);

    // topk 노드의 삼각형 구성 출력
    time_entry t_components("triangle_write_components");
    t_components.start_time();
    write_triangle_compositions(triangles, topk, datapath, graph_a, datapath + graph + "_triangle_components.txt", max_paths);
    t_components.stop_time();
    timer.add_entry(t_components);

    t_total.stop_time();
    timer.add_entry(t_total);

    printf("[triangle timing]\n");
    timer.print_timelist();
}
