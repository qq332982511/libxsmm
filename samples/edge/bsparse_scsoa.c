/******************************************************************************
** Copyright (c) 2014-2018, Intel Corporation                                **
** All rights reserved.                                                      **
**                                                                           **
** Redistribution and use in source and binary forms, with or without        **
** modification, are permitted provided that the following conditions        **
** are met:                                                                  **
** 1. Redistributions of source code must retain the above copyright         **
**    notice, this list of conditions and the following disclaimer.          **
** 2. Redistributions in binary form must reproduce the above copyright      **
**    notice, this list of conditions and the following disclaimer in the    **
**    documentation and/or other materials provided with the distribution.   **
** 3. Neither the name of the copyright holder nor the names of its          **
**    contributors may be used to endorse or promote products derived        **
**    from this software without specific prior written permission.          **
**                                                                           **
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS       **
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT         **
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR     **
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT      **
** HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,    **
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED  **
** TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR    **
** PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF    **
** LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING      **
** NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS        **
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.              **
******************************************************************************/
/* Alexander Heinecke (Intel Corp.)
******************************************************************************/

#include <libxsmm.h>

#include "common_edge_proxy.h"

LIBXSMM_INLINE
void qfma_fill_in( REALTYPE* rm_dense_data, unsigned int m, unsigned int n, unsigned int **colptr, unsigned int **rowidx, REALTYPE **values) {
  REALTYPE* cm_dense = NULL;
  REALTYPE* cm_dense_data = NULL;
  unsigned int  i = 0;
  unsigned int  j = 0;
  unsigned int  l_max_reg_block = 28;
  unsigned int  l_max_cols = 0;
  unsigned int  l_n_chunks = 0;
  unsigned int  l_n_chunksize = 0;
  unsigned int  l_n_limit = 0;
  unsigned int  l_n_processed = 0;
  unsigned int  l_nnz = 0;
  unsigned int* l_colptr = NULL;
  unsigned int* l_rowidx = NULL;
  REALTYPE*     l_values = NULL;
  unsigned int  l_count = 0;
  unsigned int  l_found_qmadd = 0;

  cm_dense      = (REALTYPE*)malloc( m*n*sizeof(REALTYPE) );
  cm_dense_data = (REALTYPE*)malloc( m*n*sizeof(REALTYPE) );

  /* set all values in copy to 1 or 0 */
  for ( j = 0; j < n; ++j ) {
    for ( i = 0; i < m; ++i ) {
      cm_dense[(j*m)+i]      = (REALTYPE)(LIBXSMM_FEQ(rm_dense_data[(i*n)+j], 0) ? 0 : 1);
      cm_dense_data[(j*m)+i] = rm_dense_data[(i*n)+j];
    }
  }

#if 1
  /* finding max. active columns */
  l_max_cols = 0;
  for ( j = 0; j < n; ++j ) {
    for ( i = 0; i < m; ++i ) {
      if (cm_dense[(j*m) + i] > 0.0) {
        l_max_cols = j+1;
      }
    }
  }

  /* calculate n blocking as in the generator */
  l_n_chunks = ( (l_max_cols % l_max_reg_block) == 0 ) ? (l_max_cols / l_max_reg_block) : (l_max_cols / l_max_reg_block) + 1;
  assert(0 != l_n_chunks); /* mute static analysis (division-by-zero); such invalid input must be caught upfront */
  l_n_chunksize = ( (l_max_cols % l_n_chunks) == 0 ) ? (l_max_cols / l_n_chunks) : (l_max_cols / l_n_chunks) + 1;

  /* qmadd padding */
  l_n_processed = 0;
  l_n_limit = l_n_chunksize;
  while ( l_n_processed < l_max_cols ) {
    /* first pass look for qmadds and potential qmadds in the same rows */
    for ( i = 0; i < m; ++i ) {
      if ( i >= m-3 ) continue;
      l_found_qmadd = 0;
      for ( j = l_n_processed; j < l_n_limit - l_n_processed; ++j ) {
        if ( LIBXSMM_FEQ(cm_dense[(j*m)+(i+0)], 1) &&
             LIBXSMM_FEQ(cm_dense[(j*m)+(i+1)], 1) &&
             LIBXSMM_FEQ(cm_dense[(j*m)+(i+2)], 1) &&
             LIBXSMM_FEQ(cm_dense[(j*m)+(i+3)], 1)    ) {
          cm_dense[(j*m)+(i+0)] = (REALTYPE)10.0;
          cm_dense[(j*m)+(i+1)] = (REALTYPE)10.0;
          cm_dense[(j*m)+(i+2)] = (REALTYPE)10.0;
          cm_dense[(j*m)+(i+3)] = (REALTYPE)10.0;
          l_found_qmadd = 1;
        }
      }
      /* if we found qmadd in at least one column, let's check the other columns in the current block for 3 nnz */
      /* -> let's pad them to 4 nnz */
      if (l_found_qmadd == 1) {
        for ( j = l_n_processed; j < l_n_limit - l_n_processed; ++j ) {
          if ( LIBXSMM_FEQ( cm_dense[(j*m)+(i+0)] +
                            cm_dense[(j*m)+(i+1)] +
                            cm_dense[(j*m)+(i+2)] +
                            cm_dense[(j*m)+(i+3)], 3) ) {
            cm_dense[(j*m)+(i+0)] = (REALTYPE)10.0;
            cm_dense[(j*m)+(i+1)] = (REALTYPE)10.0;
            cm_dense[(j*m)+(i+2)] = (REALTYPE)10.0;
            cm_dense[(j*m)+(i+3)] = (REALTYPE)10.0;
          }
        }
        i += 3;
      }
    }
    /* second pass look out for consecutive 4 rows which have 3 nnz in a specifc column */
    for ( i = 0; i < m; ++i ) {
      if ( i >= m-3 ) continue;
      l_found_qmadd = 0;
      /* first check if already a qmadd in that row */
      for ( j = l_n_processed; j < l_n_limit - l_n_processed; ++j ) {
        if ( LIBXSMM_FEQ(cm_dense[(j*m)+(i+0)], 10) ) {
          l_found_qmadd = 1;
        }
      }
      /* we are in a potential candidate row for padding 0 for qmadd */
      if ( l_found_qmadd == 0 ) {
        for ( j = l_n_processed; j < l_n_limit - l_n_processed; ++j ) {
          if ( LIBXSMM_FEQ( cm_dense[(j*m)+(i+0)] +
                            cm_dense[(j*m)+(i+1)] +
                            cm_dense[(j*m)+(i+2)] +
                            cm_dense[(j*m)+(i+3)], 3) ) {
            cm_dense[(j*m)+(i+0)] = (REALTYPE)10.0;
            cm_dense[(j*m)+(i+1)] = (REALTYPE)10.0;
            cm_dense[(j*m)+(i+2)] = (REALTYPE)10.0;
            cm_dense[(j*m)+(i+3)] = (REALTYPE)10.0;
            l_found_qmadd = 1;
          }
        }
      }
      if ( l_found_qmadd > 0 ) {
        i += 3;
      }
    }
    /* adjust n progression */
    l_n_processed += l_n_chunksize;
    l_n_limit = LIBXSMM_MIN(l_n_processed + l_n_chunksize, l_max_cols);
  }
#endif

  /* creating a new CSC matrix */
  /* determining new number of NNZ */
  l_nnz = 0;
  for ( j = 0; j < n; ++j ) {
    for ( i = 0; i < m; ++i ) {
      if (cm_dense[(j*m) + i] > 0.0) {
        l_nnz++;
      }
    }
  }

  (*colptr) = (unsigned int*) malloc( (n+1)*sizeof(unsigned int) );
  (*rowidx) = (unsigned int*) malloc( l_nnz*sizeof(unsigned int) );
  (*values) = (REALTYPE*    ) malloc( l_nnz*sizeof(REALTYPE    ) );

  l_colptr = (*colptr);
  l_rowidx = (*rowidx);
  l_values = (*values);

  /* generating CSC from dense padded structure */
  l_count = 0;
  for ( j = 0; j < n; ++j ) {
    l_colptr[j] = l_count;
    for ( i = 0; i < m; ++i ) {
      if (cm_dense[(j*m) + i] > (REALTYPE)0.0) {
        l_rowidx[l_count] = i;
        l_values[l_count] = (REALTYPE)cm_dense_data[(j*m) + i];
        l_count++;
      }
    }
  }
  l_colptr[n] = l_nnz;

  free ( cm_dense );
  free ( cm_dense_data );
}


int main(int argc, char* argv[]) {
  int M = ( argc == 7 ) ? atoi(argv[1]) : 9;
  int N = ( argc == 7 ) ? atoi(argv[2]) : 10;
  int K = ( argc == 7 ) ? atoi(argv[3]) : 20;
  unsigned int N_CRUNS = ( argc == 7 ) ? atoi(argv[4]) : 8;
  unsigned int REPS =    ( argc == 7 ) ? atoi(argv[5]) : 1;
  char* l_csc_file =     ( argc == 7 ) ?      argv[6]  : "file.csc";

  const libxsmm_gemm_prefetch_type prefetch = LIBXSMM_GEMM_PREFETCH_NONE;
  const int flags = LIBXSMM_GEMM_FLAGS('N', 'N');
  const REALTYPE alpha = 1, beta = 1;

  edge_mat_desc mat_desc = libxsmm_sparse_csc_reader_desc( l_csc_file );
  unsigned int l_rowcount = mat_desc.row_count;
  unsigned int l_colcount = mat_desc.col_count;
  unsigned int l_elements = mat_desc.num_elements;

  REALTYPE* l_a = (REALTYPE*)libxsmm_aligned_malloc(K * M * N_CRUNS * sizeof(REALTYPE), 64);
  REALTYPE* l_b_de = (REALTYPE*)libxsmm_aligned_malloc(K * N * sizeof(REALTYPE), 64);
  REALTYPE* l_b_sp = NULL;
  unsigned int* l_colptr = NULL;
  unsigned int* l_rowidx = NULL;
  REALTYPE* l_b_sp_padded = NULL;
  unsigned int* l_colptr_padded = NULL;
  unsigned int* l_rowidx_padded = NULL;
  REALTYPE* l_c_gold = (REALTYPE*)libxsmm_aligned_malloc(M * N * N_CRUNS * sizeof(REALTYPE), 64);
  REALTYPE* l_c_asm = (REALTYPE*)libxsmm_aligned_malloc(M * N * N_CRUNS * sizeof(REALTYPE), 64);
  REALTYPE l_max_error = 0.0;
  unsigned int l_k, l_n;
  int l_i, l_j, l_jj;

  LIBXSMM_VLA_DECL(3, REALTYPE, l_p_a, l_a, K, N_CRUNS);
  LIBXSMM_VLA_DECL(3, REALTYPE, l_p_c_asm, l_c_asm, N, N_CRUNS);
  LIBXSMM_VLA_DECL(3, REALTYPE, l_p_c_gold, l_c_gold, N, N_CRUNS);

  libxsmm_descriptor_blob l_xgemm_blob;
  const libxsmm_gemm_descriptor* l_xgemm_desc = 0;
  LIBXSMM_MMFUNCTION_TYPE(REALTYPE) mykernel = NULL;

  unsigned long long l_start, l_end;
  double l_total;

  if (argc != 7) {
    fprintf( stderr, "arguments: M CRUNS #iters csc-file!\n" );
    exit(-1);
  }

  if ((unsigned int)K != l_rowcount) {
    fprintf( stderr, "arguments K needs to match number of rows of the sparse matrix!\n" );
    exit(-1);
  }

  if ((unsigned int)N != l_colcount) {
    fprintf( stderr, "arguments N needs to match number of columns of the sparse matrix!\n" );
    exit(-1);
  }

  if (M != 9) {
    fprintf( stderr, "arguments M needs to match 9!\n" );
    exit(-1);
  }

  /* touch A */
  for ( l_i = 0; l_i < M; l_i++) {
    for ( l_j = 0; l_j < K; l_j++) {
      for ( l_k = 0; l_k < N_CRUNS; l_k++ ) {
        LIBXSMM_VLA_ACCESS(3, l_p_a, l_i, l_j, l_k, K, N_CRUNS) = (REALTYPE)libxsmm_drand();
      }
    }
  }

  /* touch C */
  for ( l_i = 0; l_i < M; l_i++) {
    for ( l_j = 0; l_j < N; l_j++) {
      for ( l_k = 0; l_k < N_CRUNS; l_k++ ) {
        LIBXSMM_VLA_ACCESS(3, l_p_c_gold, l_i, l_j, l_k, N, N_CRUNS) = (REALTYPE)0.0;
        LIBXSMM_VLA_ACCESS(3, l_p_c_asm,  l_i, l_j, l_k, N, N_CRUNS) = (REALTYPE)0.0;
      }
    }
  }

  /* read B, csc */
  libxsmm_sparse_csc_reader(  l_csc_file,
                             &l_colptr,
                             &l_rowidx,
                             &l_b_sp,
                             &l_rowcount, &l_colcount, &l_elements );

  /* copy b to dense */
  printf("csc matrix data structure we just read:\n");
  printf("rows: %u, columns: %u, elements: %u\n", l_rowcount, l_colcount, l_elements);

  for ( l_n = 0; l_n < (((unsigned int)K) * N); l_n++) {
    l_b_de[l_n] = 0.0;
  }

  for ( l_n = 0; l_n < (unsigned int)N; l_n++) {
    const unsigned int l_colelems = l_colptr[l_n+1] - l_colptr[l_n];
    assert(l_colptr[l_n+1] >= l_colptr[l_n]);

    for ( l_k = 0; l_k < l_colelems; l_k++) {
      l_b_de[(l_rowidx[l_colptr[l_n] + l_k] * N) + l_n] = l_b_sp[l_colptr[l_n] + l_k];
    }
  }

  /* pad B to a better qmadd matrix */
  if ( libxsmm_get_target_archid() == LIBXSMM_X86_AVX512_KNM ) {
    qfma_fill_in( l_b_de, K, N, &l_colptr_padded, &l_rowidx_padded, &l_b_sp_padded );
    printf("qfma padded CSC matrix data structure we just read:\n");
    printf("rows: %u, columns: %u, elements: %u\n", l_rowcount, l_colcount, l_colptr_padded[N]);
  }

  /* dense routine */
  l_start = libxsmm_timer_tick();
#if 1
  for ( l_n = 0; l_n < REPS; l_n++) {
    for ( l_i = 0; l_i < M; l_i++) {
      for ( l_j = 0; l_j < N; l_j++) {
        for ( l_jj = 0; l_jj < K; l_jj++) {
          LIBXSMM_PRAGMA_SIMD
          for (l_k = 0; l_k < N_CRUNS; l_k++) {
            LIBXSMM_VLA_ACCESS(3, l_p_c_gold, l_i, l_j, l_k, N, N_CRUNS)
              +=   LIBXSMM_VLA_ACCESS(3, l_p_a, l_i, l_jj, l_k, K, N_CRUNS)
                 * l_b_de[(l_jj*N)+l_j];
          }
        }
      }
    }
  }
#endif
  l_end = libxsmm_timer_tick();
  l_total = libxsmm_timer_duration(l_start, l_end);
  printf("%fs for dense\n", l_total);
  printf("%f GFLOPS for dense\n", ((double)((double)REPS * (double)M * (double)N * (double)K * (double)N_CRUNS) * 2.0) / (l_total * 1.0e9));

  l_xgemm_desc = libxsmm_gemm_descriptor_dinit(&l_xgemm_blob, LIBXSMM_GEMM_PRECISION(REALTYPE),
    M, N, K, K, 0, N, alpha, beta, flags, prefetch);

  /* sparse routine */
#if defined(__EDGE_EXECUTE_F32__)
  if ( libxsmm_get_target_archid() == LIBXSMM_X86_AVX512_KNM ) {
    mykernel = libxsmm_create_xcsc_soa(l_xgemm_desc, l_colptr_padded, l_rowidx_padded, (const void*)l_b_sp).smm;
  } else {
    mykernel = libxsmm_create_xcsc_soa(l_xgemm_desc, l_colptr, l_rowidx, (const void*)l_b_sp).smm;
  }
#else
  mykernel = libxsmm_create_xcsc_soa(l_xgemm_desc, l_colptr, l_rowidx, (const void*)l_b_sp).dmm;
#endif

  if ( libxsmm_get_target_archid() == LIBXSMM_X86_AVX512_KNM ) {
    l_start = libxsmm_timer_tick();
    for ( l_n = 0; l_n < REPS; l_n++) {
#if defined(__EDGE_EXECUTE_F32__)
      mykernel( l_a, l_b_sp_padded, l_c_asm );
#else
      mykernel( l_a, l_b_sp, l_c_asm );
#endif
    }
    l_end = libxsmm_timer_tick();
  } else {
    l_start = libxsmm_timer_tick();
    for ( l_n = 0; l_n < REPS; l_n++) {
      mykernel( l_a, l_b_sp, l_c_asm );
    }
    l_end = libxsmm_timer_tick();
  }
  l_total = libxsmm_timer_duration(l_start, l_end);
  printf("%fs for sparse (asm)\n", l_total);
  printf("%f GFLOPS for sparse (asm)\n", ((double)((double)REPS * (double)M * (double)l_elements * (double)N_CRUNS) * 2.0) / (l_total * 1.0e9));

  /* check for errors */
  l_max_error = (REALTYPE)0.0;
  for ( l_i = 0; l_i < M; l_i++) {
    for ( l_j = 0; l_j < N; l_j++) {
      for ( l_k = 0; l_k < N_CRUNS; l_k++ ) {
        if (fabs( LIBXSMM_VLA_ACCESS(3, l_p_c_gold, l_i, l_j, l_k, N, N_CRUNS)
                    - LIBXSMM_VLA_ACCESS(3, l_p_c_asm, l_i, l_j, l_k, N, N_CRUNS) ) > l_max_error ) {
          l_max_error = (REALTYPE)fabs( LIBXSMM_VLA_ACCESS(3, l_p_c_gold, l_i, l_j, l_k, N, N_CRUNS)
                                       -LIBXSMM_VLA_ACCESS(3, l_p_c_asm, l_i, l_j, l_k, N, N_CRUNS) );
        }
      }
    }
  }
  printf("max error: %f\n", l_max_error);

  printf("PERFDUMP,%s,%u,%i,%i,%i,%u,%u,%f,%f,%f\n", l_csc_file, REPS, M, N, K, l_elements, M * l_elements * N_CRUNS * 2, l_max_error, l_total, ((double)((double)REPS * (double)M * (double)l_elements * (double)N_CRUNS) * 2.0) / (l_total * 1.0e9) );

  /* free */
  libxsmm_free( l_b_de );
  libxsmm_free( l_a );
  libxsmm_free( l_c_gold );
  libxsmm_free( l_c_asm );

  free( l_b_sp );
  free( l_colptr );
  free( l_rowidx );
  if ( l_b_sp_padded != NULL )   free( l_b_sp_padded );
  if ( l_colptr_padded != NULL ) free( l_colptr_padded );
  if ( l_rowidx_padded != NULL ) free( l_rowidx_padded );


  return 0;
}

