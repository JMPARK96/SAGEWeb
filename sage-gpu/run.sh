sudo rm meta.bin test_result.txt_result
sudo rm datasets/poisson3D_a*
sudo rm spgemm
sudo rm result/*
sudo make -f makefile-spgemm spgemm

./spgemm datapath ./datasets/ file_a poisson3Da_a file_b poisson3Da_b output poisson3D preprocess_a 0 preprocess_b 0 enginerun 1 maxiters 1 threads 8 a_mb 8 b_mb 16376 c_mb 16384 total_mb 32768 product ROWBA merge 0
