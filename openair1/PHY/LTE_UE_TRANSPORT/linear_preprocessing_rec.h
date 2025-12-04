
#include<stdio.h>
#include<math.h>
#include<complex.h>
#include <stdlib.h>
#include "PHY/defs_UE.h"

/* FUNCTIONS FOR LINEAR PREPROCESSING: MMSE, WHITENNING, etc*/
/* mutl_matrix_matrix_col_based performs multiplications matrix is column-oriented H[0], H[2]; H[1], H[3]*/
void mutl_matrix_matrix_col_based(float complex* M0, float complex* M1, int rows_M0, int col_M0, int rows_M1, int col_M1, float complex* Result );

void compute_MMSE(float complex* H, int order_H, float sigma2, float complex* W_MMSE);
