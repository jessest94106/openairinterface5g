/* These functions compute linear preprocessing for
the UE using LAPACKE and CBLAS modules of
LAPACK libraries.
MMSE and MMSE whitening filters are available.
Functions are using RowMajor storage of the
matrices, like in conventional C. Traditional
Fortran functions of LAPACK employ ColumnMajor
data storage. */

#include<stdio.h>
#include<math.h>
#include<complex.h>
#include <stdlib.h>
#include <linux/version.h>
#include <cblas.h>
#include <string.h>
#include <linux/version.h>
#include <lapacke_utils.h>
#include <lapacke.h>
//#define DEBUG_PREPROC
#include "common/utils/utils.h"

static void conjugate_transpose(int N, float complex* A, float complex* Result)
{
  // Computes C := alpha*op(A)*op(B) + beta*C,
  float complex alpha = 1.0 + I * 0;
  float complex beta = 0.0+I*0;
  float complex B[N][N];
  for (int i = 0; i < N; i++)
    B[i][i] = 1.0 + I * 0;
  cblas_cgemm(CblasRowMajor, CblasConjTrans, CblasNoTrans, N, N, N, &alpha, A, N, B, N, &beta, Result, N);
}

static void H_hermH_plus_sigma2I(int N, float complex* A, float sigma2, float complex Result[N][N])
{
  // C := alpha*op(A)*op(B) + beta*C,
  float complex alpha = 1.0 + I * 0;
  float complex beta = 1.0 + I * 0;
  for (int i = 0; i < N; i++)
    Result[i][i] = sigma2 * (1.0 + I * 0);
  cblas_cgemm(CblasRowMajor, CblasConjTrans, CblasNoTrans, N, N, N, &alpha, A, N, A, N, &beta, Result, N);
}

static void lin_eq_solver(int N, float complex* A, float complex* B, float complex* Result)
{
  int IPIV[N][N];

  // Compute LU-factorization
  LAPACKE_cgetrf(LAPACK_ROW_MAJOR, N, N, A, N, (int*)IPIV);

  // Solve AX=B
  LAPACKE_cgetrs(LAPACK_ROW_MAJOR, 'N', N, N, A, N, (int*)IPIV, B, N);

  // cgetrs( "N", N, 4, A, lda, IPIV, B, ldb, INFO )

  memcpy(Result, B, N * N * sizeof(float complex));
}

void mutl_matrix_matrix_col_based(float complex* M0,
                                  float complex* M1,
                                  int rows_M0,
                                  int col_M0,
                                  int rows_M1,
                                  int col_M1,
                                  float complex* Result)
{
  float complex alpha = 1.0;
  float complex beta = 0.0;
  cblas_cgemm(CblasColMajor,
              CblasNoTrans,
              CblasNoTrans,
              rows_M0,
              col_M1,
              col_M0,
              &alpha,
              M0,
              col_M0,
              M1,
              rows_M1,
              &beta,
              Result,
              rows_M1);

#ifdef DEBUG_PREPROC
  for (i = 0; i < rows_M0 * col_M1; ++i)
    printf(" result[%d] = (%f + i%f)\n", i , creal(Result[i]), cimag(Result[i]));
#endif
}

/*FILTERS */
void compute_MMSE(float complex* H, int order_H, float sigma2, float complex* W_MMSE)
{
  int N = order_H;
  float complex H_hermH_sigmaI[N][N];
  memset(H_hermH_sigmaI, 0, sizeof(H_hermH_sigmaI));
  H_hermH_plus_sigma2I(N, H, sigma2, H_hermH_sigmaI);

#ifdef DEBUG_PREPROC
  int i =0;
  for(i=0;i<N*N;i++)
    printf(" H_hermH_sigmaI[%d] = (%f + i%f)\n", i , creal(H_hermH_sigmaI[i]), cimag(H_hermH_sigmaI[i]));
#endif

  float complex H_herm[N][N];
  memset(H_herm, 0, sizeof(H_herm));
  conjugate_transpose(N, H, (float complex*)H_herm); // equals H_herm

#ifdef DEBUG_PREPROC
  for(i=0;i<N*N;i++)
    printf(" H_herm[%d] = (%f + i%f)\n", i , creal(H_herm[i]), cimag(H_herm[i]));
#endif

  lin_eq_solver(N, (float complex*)H_hermH_sigmaI, (float complex*)H_herm, W_MMSE);

#ifdef DEBUG_PREPROC
  for(i=0;i<N*N;i++)
    printf(" W_MMSE[%d] = (%f + i%f)\n", i , creal(W_MMSE[i]), cimag(W_MMSE[i]));
#endif
}
