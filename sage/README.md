# SAGE
## SAGE: A Storage-Based Approach for Scalable and Efficient Sparse Generalized Matrix-Matrix Multiplication

This directory contains the SAGE CPU engine used by SAGEWeb. For the web application and complete build instructions, see the [main README](../README.md).

This project is written in standard C++. It can be built by running Makefile in the source code directory.

### Build
Build a executable file by:  
  ```
  make -f makefile-spgemm spgemm
  ```  

Then a executable file 'spgemm' is created.

### Usage
The executable file requires several parameters to perform SpGEMM C=AB.  
Each input parameter is a pair of 'key value'.  

  > datapath  : Path where original dataset A/B and preprocessed data is placed.  
  file_a  : Name of preprocessed dataset A.  
  file_b  : Name of preprocessed dataset B.  
  data_a  : Name of original dataset A.  
  data_b  : Name of original dataset B.  
  output  : Name of final result C.  
  preprocess_a  : Binary flag of block-based preprocessing the original dataset A. it should be '1' in the first try, otherwise '0'.  
  preprocess_b  : Binary flag of block-based preprocessing the original dataset B. it should be '1' in the first try, otherwise '0'.  
  enginerun  : Binary flag of running the framework. it should be '1'.  
  maxiters  : The number of iterations. it should be '1'.  
  threads  : The number of threads.  
  a_mb  : Size of input buffer A in MB.  
  b_mb  : Size of input buffer B in MB.  
  c_mb  : Size of output buffer C in MB.  
  total_mb  : Total amount of main memory to use in MB. it should be a sum of a_mb, b_mb, and c_mb.  
  product  : Product methods of SpGEMM (i.e., inner product, outer product, and row-wise product). it should be 'ROWBA'.  
  merge  : Flag of performing merge stage. '1' is required to make final results.  

For example, running the executable file by command like below:  
  ```
  ./spgemm datapath ./data/ file_a Yahoo_a file_b Yahoo_b data_a web_Yahoo_a.txt data_b web_Yahoo_b.txt output Yahoo preprocess_a 1 preprocess_b 1 enginerun 1 maxiters 1 threads 4 a_mb 4 b_mb 1024 c_mb 16 total_mb 1044 product ROWBA merge 1
  ```  
will first block-based preprocess web_Yahoo_a.txt and web_Yahoo_b.txt into Yahoo_a.graphblock and Yahoo_b.graphblock with some metadata files.  
And then perform SpGEMM between two graphblocks Yahoo_a.graphblock and Yahoo_b.graphblock with size of each buffer A/B/C (4MB/1024MB/16MB).  
Finally, it generates one Yahoo_result_final_end_'n' file per thread, where n indicates the partition of rows in ascending order.  
Current build takes a weighted edgelist text file for each input matrix.

### Experiments
Wikipedia: http://konect.cc/networks/wikipedia_link_en/  
Yahoo: https://webscope.sandbox.yahoo.com  
Additional datasets are available from Stanford SNAP and the SuiteSparse Matrix Collection. Datasets are not bundled with this repository.
