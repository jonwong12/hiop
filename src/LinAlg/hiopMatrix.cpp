// Copyright (c) 2017, Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory (LLNL).
// Written by Cosmin G. Petra, petra1@llnl.gov.
// LLNL-CODE-742473. All rights reserved.
//
// This file is part of HiOp. For details, see https://github.com/LLNL/hiop. HiOp 
// is released under the BSD 3-clause license (https://opensource.org/licenses/BSD-3-Clause). 
// Please also read “Additional BSD Notice” below.
//
// Redistribution and use in source and binary forms, with or without modification, 
// are permitted provided that the following conditions are met:
// i. Redistributions of source code must retain the above copyright notice, this list 
// of conditions and the disclaimer below.
// ii. Redistributions in binary form must reproduce the above copyright notice, 
// this list of conditions and the disclaimer (as noted below) in the documentation and/or 
// other materials provided with the distribution.
// iii. Neither the name of the LLNS/LLNL nor the names of its contributors may be used to 
// endorse or promote products derived from this software without specific prior written 
// permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY 
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
// SHALL LAWRENCE LIVERMORE NATIONAL SECURITY, LLC, THE U.S. DEPARTMENT OF ENERGY OR 
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED 
// AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, 
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Additional BSD Notice
// 1. This notice is required to be provided under our contract with the U.S. Department 
// of Energy (DOE). This work was produced at Lawrence Livermore National Laboratory under 
// Contract No. DE-AC52-07NA27344 with the DOE.
// 2. Neither the United States Government nor Lawrence Livermore National Security, LLC 
// nor any of their employees, makes any warranty, express or implied, or assumes any 
// liability or responsibility for the accuracy, completeness, or usefulness of any 
// information, apparatus, product, or process disclosed, or represents that its use would
// not infringe privately-owned rights.
// 3. Also, reference herein to any specific commercial products, process, or services by 
// trade name, trademark, manufacturer or otherwise does not necessarily constitute or 
// imply its endorsement, recommendation, or favoring by the United States Government or 
// Lawrence Livermore National Security, LLC. The views and opinions of authors expressed 
// herein do not necessarily state or reflect those of the United States Government or 
// Lawrence Livermore National Security, LLC, and shall not be used for advertising or 
// product endorsement purposes.

#include "hiopMatrix.hpp"

#include <cstdio>
#include <cstring> //for memcpy
#include <cmath>
#include <cassert>

#include "blasdefs.hpp"

#include "hiopVector.hpp"

namespace hiop
{

hiopMatrixDense::hiopMatrixDense(const long long& m, 
				 const long long& glob_n, 
				 long long* col_part/*=NULL*/, 
				 MPI_Comm comm_/*=MPI_COMM_SELF*/, 
				 const long long& m_max_alloc/*=-1*/)
{
  m_local=m; n_global=glob_n;
  comm=comm_;
  int P=0;
  if(col_part) {
#ifdef WITH_MPI
    int ierr=MPI_Comm_rank(comm, &P); assert(ierr==MPI_SUCCESS);
#endif
    glob_jl=col_part[P]; glob_ju=col_part[P+1];
  } else {
    glob_jl=0; glob_ju=n_global;
  }
  n_local=glob_ju-glob_jl;
  
  max_rows=m_max_alloc;
  if(max_rows==-1) max_rows=m_local;
  assert(max_rows>=m_local && "the requested extra allocation is smaller than the allocation needed by the matrix");

  //M=new double*[m_local==0?1:m_local];
  M=new double*[max_rows==0?1:max_rows];
  M[0] = max_rows==0?NULL:new double[max_rows*n_local];
  for(int i=1; i<max_rows; i++)
    M[i]=M[0]+i*n_local;

  //! valgrind reports a shit load of errors without this; check this
  for(int i=0; i<max_rows*n_local; i++) M[0][i]=0.0;

  //internal temporary buffers to follow
}
hiopMatrixDense::~hiopMatrixDense()
{
  if(M) {
    if(M[0]) delete[] M[0];
    delete[] M;
  }
}

hiopMatrixDense::hiopMatrixDense(const hiopMatrixDense& dm)
{
  n_local=dm.n_local; m_local=dm.m_local; n_global=dm.n_global;
  glob_jl=dm.glob_jl; glob_ju=dm.glob_ju;
  comm=dm.comm;

  //M=new double*[m_local==0?1:m_local];
  max_rows = dm.max_rows;
  M=new double*[max_rows==0?1:max_rows];
  //M[0] = m_local==0?NULL:new double[m_local*n_local];
  M[0] = max_rows==0?NULL:new double[max_rows*n_local];
  //for(int i=1; i<m_local; i++)
  for(int i=1; i<max_rows; i++)
    M[i]=M[0]+i*n_local;
}

void hiopMatrixDense::appendRow(const hiopVectorPar& row)
{
#ifdef DEEP_CHECKING  
  assert(row.get_local_size()==n_local);
  assert(m_local<max_rows && "no more space to append rows ... should have preallocated more rows.");
#endif
  memcpy(M[m_local], row.local_data_const(), n_local*sizeof(double));
  m_local++;
}

void hiopMatrixDense::copyFrom(const hiopMatrixDense& dm)
{
  assert(n_local==dm.n_local); assert(m_local==dm.m_local); assert(n_global==dm.n_global);
  assert(glob_jl==dm.glob_jl); assert(glob_ju==dm.glob_ju);
  memcpy(M[0], dm.M[0], m_local*n_local*sizeof(double));
  //for(int i=1; i<m_local; i++)
  //  M[i]=M[0]+i*n_local;
}

void hiopMatrixDense::copyFrom(const double* buffer)
{
  memcpy(M[0], buffer, m_local*n_local*sizeof(double));
}

void hiopMatrixDense::copyRowsFrom(const hiopMatrixDense& src, int num_rows, int row_dest)
{
#ifdef DEEP_CHECKING
  assert(row_dest>=0);
  assert(n_global==src.n_global);
  assert(n_local==src.n_local);
  assert(row_dest+num_rows<=m_local);
  assert(num_rows<=src.m_local);
#endif
  if(num_rows>0)
    memcpy(M[row_dest], src.M[0], n_local*num_rows*sizeof(double));
}
void hiopMatrixDense::copyBlockFromMatrix(const long i_start, const long j_start,
					  const hiopMatrixDense& src)
{
  assert(n_local==n_global && "this method should be used only in 'serial' mode");
  assert(src.n_local==src.n_global && "this method should be used only in 'serial' mode");
  assert(m_local>=i_start+src.m_local && "the matrix does not fit as a sublock in 'this' at specified coordinates");
  assert(n_local>=j_start+src.n_local && "the matrix does not fit as a sublock in 'this' at specified coordinates");

  //quick returns for empty source matrices
  if(src.n()==0) return;
  if(src.m()==0) return;
#ifdef DEEP_CHECKING
  assert(i_start<m_local || !m_local);
  assert(j_start<n_local || !n_local);
  assert(i_start>=0); assert(j_start>=0);
#endif
  const size_t buffsize=src.n_local*sizeof(double);
  for(long ii=0; ii<src.m_local; ii++)
    memcpy(M[ii+i_start]+j_start, src.M[ii], buffsize);
}

void hiopMatrixDense::copyFromMatrixBlock(const hiopMatrixDense& src, const int i_block, const int j_block)
{
  assert(n_local==n_global && "this method should be used only in 'serial' mode");
  assert(src.n_local==src.n_global && "this method should be used only in 'serial' mode");
  assert(m_local+i_block<=src.m_local && "the source does not enough rows to fill 'this'");
  assert(n_local+j_block<=src.n_local && "the source does not enough cols to fill 'this'");

  if(n_local==src.n_local) //and j_block=0
    memcpy(M[0], src.M[i_block], n_local*m_local*sizeof(double));
  else {
    for(int i=0; i<m_local; i++)
      memcpy(M[i], src.M[i+i_block]+j_block, n_local*sizeof(double));
  }
}

void hiopMatrixDense::shiftRows(long long shift)
{
  if(shift==0) return;
  if(-shift==m_local) return; //nothing to shift
  assert(fabs(shift)<m_local);
#ifdef DEEP_CHECKING
  //not sure if memcpy is copying sequentially on all systems. we check this.
  //let's at least check it
  double test1=shift<0 ? M[-shift][0] : M[m_local-shift][0];
  double test2=shift<0 ? M[-shift][n_local-1] : M[m_local-shift][n_local-1];
#endif

  //shift < 0 -> up; shift > 0 -> down
  //if(shift<0) memcpy(M[0], M[-shift], n_local*(m_local+shift)*sizeof(double));
  //else        memcpy(M[shift], M[0],  n_local*(m_local-shift)*sizeof(double));
  if(shift<0) {
    for(int row=0; row<m_local+shift; row++)
      memcpy(M[row], M[row-shift], n_local*sizeof(double));
  } else {
    for(int row=m_local-1; row>=shift; row--) {
      memcpy(M[row], M[row-shift], n_local*sizeof(double));
    }
  }

#ifdef DEEP_CHECKING
  assert(test1==M[shift<0?0:m_local][0] && "a different copy technique than memcpy is needed on this system");
  assert(test2==M[shift<0?0:m_local][n_local-1] && "a different copy technique than memcpy is needed on this system");
#endif
}
void hiopMatrixDense::replaceRow(long long row, const hiopVectorPar& vec)
{
  assert(row>=0); assert(row<m_local);
  long long vec_size=vec.get_local_size();
  memcpy(M[row], vec.local_data_const(), (vec_size>=n_local?n_local:vec_size)*sizeof(double));
}

void hiopMatrixDense::getRow(long long irow, hiopVector& row_vec)
{
  assert(irow>=0); assert(irow<m_local);
  hiopVectorPar& vec=dynamic_cast<hiopVectorPar&>(row_vec);
  assert(n_local==vec.get_local_size());
  memcpy(vec.local_data(), M[irow], n_local*sizeof(double));
}

#ifdef DEEP_CHECKING
void hiopMatrixDense::overwriteUpperTriangleWithLower()
{
  assert(n_local==n_global && "Use only with local, non-distributed matrices");
  for(int i=0; i<m_local; i++)
    for(int j=i+1; j<n_local; j++)
      M[i][j] = M[j][i];
}
void hiopMatrixDense::overwriteLowerTriangleWithUpper()
{
  assert(n_local==n_global && "Use only with local, non-distributed matrices");
  for(int i=1; i<m_local; i++)
    for(int j=0; j<i; j++)
      M[i][j] = M[j][i];
}
#endif

hiopMatrixDense* hiopMatrixDense::alloc_clone() const
{
  hiopMatrixDense* c = new hiopMatrixDense(*this);
  return c;
}

hiopMatrixDense* hiopMatrixDense::new_copy() const
{
  hiopMatrixDense* c = new hiopMatrixDense(*this);
  c->copyFrom(*this);
  return c;
}

void hiopMatrixDense::setToZero()
{
  //for(int i=0; i<m_local; i++)
  //  for(int j=0; j<n_local; j++)
  //    M[i][j]=0.0;
  setToConstant(0.0);
}
void hiopMatrixDense::setToConstant(double c)
{
  double* buf=M[0]; 
  for(int j=0; j<n_local; j++) *(buf++)=c;
  
  buf=M[0]; int inc=1;
  for(int i=1; i<m_local; i++)
   DCOPY(&n_local, buf, &inc, M[i], &inc);
  
  //memcpy(M[i], buf, sizeof(double)*n_local); 
  //memcpy has similar performance as dcopy_; both faster than a loop
}

void hiopMatrixDense::print(FILE* f, 
			    const char* msg/*=NULL*/, 
			    int maxRows/*=-1*/, 
			    int maxCols/*=-1*/, 
			    int rank/*=-1*/) const
{
  int myrank=0; 
#ifdef WITH_MPI
  if(rank>=0) assert(MPI_Comm_rank(comm, &myrank)==MPI_SUCCESS);
#endif
  if(myrank==rank || rank==-1) {
    if(NULL==f) f=stdout;
    if(maxRows>m_local) maxRows=m_local;
    if(maxCols>n_local) maxCols=n_local;

    if(msg) {
      fprintf(f, "%s (local_dims=[%d,%d])\n", msg, m_local,n_local);
    } else { 
      fprintf(f, "hiopMatrixDense::printing max=[%d,%d] (local_dims=[%d,%d], on rank=%d)\n", 
	      maxRows, maxCols, m_local,n_local,myrank);
    }
    maxRows = maxRows>=0?maxRows:m_local;
    maxCols = maxCols>=0?maxCols:n_local;
    fprintf(f, "[");
    for(int i=0; i<maxRows; i++) {
      fprintf(f, " ");
      for(int j=0; j<maxCols; j++) 
	fprintf(f, "%22.16e ", M[i][j]);
      if(i<maxRows-1)
	fprintf(f, "; ...\n");
      else
	fprintf(f, "];\n");
    }
  }
}

#include <unistd.h>

/* y = beta * y + alpha * this * x */
void hiopMatrixDense::timesVec(double beta, hiopVector& y_,
			       double alpha, const hiopVector& x_) const
{
  hiopVectorPar& y = dynamic_cast<hiopVectorPar&>(y_);
  const hiopVectorPar& x = dynamic_cast<const hiopVectorPar&>(x_);
#ifdef DEEP_CHECKING
  assert(y.get_local_size() == m_local);
  assert(y.get_size() == m_local); //y should not be distributed
  assert(x.get_local_size() == n_local);
  assert(x.get_size() == n_global);
#endif
  char fortranTrans='T';
  int MM=m_local, NN=n_local, incx_y=1;

#ifdef WITH_MPI
  //only add beta*y on one processor (rank 0)
  int myrank;
  int ierr=MPI_Comm_rank(comm, &myrank); assert(MPI_SUCCESS==ierr);

  if(myrank!=0) beta=0.0; 
#endif

  if( MM != 0 && NN != 0 ) {
    // the arguments seem reversed but so is trans='T' 
    // required since we keep the matrix row-wise, while the Fortran/BLAS expects them column-wise
    DGEMV( &fortranTrans, &NN, &MM, &alpha, &M[0][0], &NN,
	    x.local_data_const(), &incx_y, &beta, y.local_data(), &incx_y );
  } else {
    if( MM != 0 ) y.scale( beta );
  }
#ifdef WITH_MPI
  double* yglob=new double[m_local]; //shouldn't be any performance issue here since m_local is small
  ierr=MPI_Allreduce(y.local_data(), yglob, m_local, MPI_DOUBLE, MPI_SUM, comm); assert(MPI_SUCCESS==ierr);
  memcpy(y.local_data(), yglob, m_local*sizeof(double));
  //usleep(100000);
  delete[] yglob;
#endif
  
}

/* y = beta * y + alpha * transpose(this) * x */
void hiopMatrixDense::transTimesVec(double beta, hiopVector& y_,
				    double alpha, const hiopVector& x_) const
{
  hiopVectorPar& y = dynamic_cast<hiopVectorPar&>(y_);
  const hiopVectorPar& x = dynamic_cast<const hiopVectorPar&>(x_);
#ifdef DEEP_CHECKING
  assert(x.get_local_size() == m_local);
  assert(x.get_size() == m_local); //x should not be distributed
  assert(y.get_local_size() == n_local);
  assert(y.get_size() == n_global);
#endif
  char fortranTrans='N';
  int MM=m_local, NN=n_local, incx_y=1;

  if( MM!=0 && NN!=0 ) {
    // the arguments seem reversed but so is trans='T' 
    // required since we keep the matrix row-wise, while the Fortran/BLAS expects them column-wise
    DGEMV( &fortranTrans, &NN, &MM, &alpha, &M[0][0], &NN,
	    x.local_data_const(), &incx_y, &beta, y.local_data(), &incx_y );
  } else {
    if( NN != 0 ) y.scale( beta );
  }
}

/* W = beta*W + alpha*this*X 
 * -- this is 'M' mxn, X is nxk, W is mxk
 */
void hiopMatrixDense::timesMat(double beta, hiopMatrix& W_, double alpha, const hiopMatrix& X_) const
{
#ifndef WITH_MPI
  timesMat_local(beta,W_,alpha,X_);
#else
  hiopMatrixDense& W = dynamic_cast<hiopMatrixDense&>(W_); double** WM=W.local_data();
  
  int myrank, ierr;
  ierr=MPI_Comm_rank(comm,&myrank); assert(ierr==MPI_SUCCESS);
  if(0==myrank) timesMat_local(beta,W_,alpha,X_);
  else          timesMat_local(0.,  W_,alpha,X_);

  int n2Red=W.m()*W.n(); double* Wglob=new double[n2Red]; //!opt
  ierr = MPI_Allreduce(WM[0], Wglob, n2Red, MPI_DOUBLE, MPI_SUM,comm); assert(ierr==MPI_SUCCESS);
  memcpy(WM[0], Wglob, n2Red*sizeof(double));
  delete[] Wglob;
#endif

}

/* W = beta*W + alpha*this*X 
 * -- this is 'M' mxn, X is nxk, W is mxk
 */
void hiopMatrixDense::timesMat_local(double beta, hiopMatrix& W_, double alpha, const hiopMatrix& X_) const
{
  const hiopMatrixDense& X = dynamic_cast<const hiopMatrixDense&>(X_);
  hiopMatrixDense& W = dynamic_cast<hiopMatrixDense&>(W_);
#ifdef DEEP_CHECKING  
  assert(W.m()==this->m());
  assert(X.m()==this->n());
  assert(W.n()==X.n());
#endif
  assert(W.n_local==W.n_global && "requested multiplication should be done in parallel using timesMat");
  if(W.m()==0 || X.m()==0 || W.n()==0) return;

  /* C = alpha*op(A)*op(B) + beta*C in our case is
     Wt= alpha* Xt  *Mt    + beta*Wt */
  char trans='N'; 
  int M=X.n(), N=m_local, K=X.m();
  int ldx=X.n(), ldm=n_local, ldw=X.n();

  double** XM=X.local_data(); double** WM=W.local_data();
  DGEMM(&trans,&trans, &M,&N,&K, &alpha,XM[0],&ldx, this->M[0],&ldm, &beta,WM[0],&ldw);

  /* C = alpha*op(A)*op(B) + beta*C in our case is
     Wt= alpha* Xt  *Mt    + beta*Wt */

  //char trans='T';
  //int lda=X.m(), ldb=n_local, ldc=W.n();
  //int M=X.n(), N=this->m(), K=this->n_local;

  //DGEMM(&trans,&trans, &M,&N,&K, &alpha,XM[0],&lda, this->M[0],&ldb, &beta,WM[0],&ldc);
  
}

/* W = beta*W + alpha*this^T*X 
 * -- this is mxn, X is mxk, W is nxk
 */
void hiopMatrixDense::transTimesMat(double beta, hiopMatrix& W_, double alpha, const hiopMatrix& X_) const
{
  const hiopMatrixDense& X = dynamic_cast<const hiopMatrixDense&>(X_);
  hiopMatrixDense& W = dynamic_cast<hiopMatrixDense&>(W_);
#ifdef DEEP_CHECKING
  assert(W.m()==n_local);
  assert(X.m()==m_local);
  assert(W.n()==X.n());
#endif
  if(W.m()==0) return;

  /* C = alpha*op(A)*op(B) + beta*C in our case is Wt= alpha* Xt  *M    + beta*Wt */
  char transX='N', transM='T';
  int ldx=X.n(), ldm=n_local, ldw=W.n();
  int M=X.n(), N=n_local, K=X.m();
  double** XM=X.local_data(); double** WM=W.local_data();
  
  DGEMM(&transX, &transM, &M,&N,&K, &alpha,XM[0],&ldx, this->M[0],&ldm, &beta,WM[0],&ldw);
}

/* W = beta*W + alpha*this*X^T
 * -- this is mxn, X is kxn, W is mxk
 */
void hiopMatrixDense::timesMatTrans_local(double beta, hiopMatrix& W_, double alpha, const hiopMatrix& X_) const
{
  const hiopMatrixDense& X = dynamic_cast<const hiopMatrixDense&>(X_);
  hiopMatrixDense& W = dynamic_cast<hiopMatrixDense&>(W_);
#ifdef DEEP_CHECKING
  assert(W.m()==m_local);
  //assert(X.n()==n_local);
  assert(W.n()==X.m());
#endif
  assert(W.n_local==W.n_global && "not intended for the case when the result matrix is distributed.");
  if(W.m()==0) return;
  if(W.n()==0) return;
  if(n_local==0) {
    if(beta!=1.0) {
      int one=1; int mn=W.m()*W.n();
      DSCAL(&mn, &beta, W.M[0], &one);
    }
    return;
  }

  /* C = alpha*op(A)*op(B) + beta*C in our case is Wt= alpha* X  *Mt    + beta*Wt */
  char transX='T', transM='N';
  int ldx=n_local;//=X.n(); (modified to support the parallel case)
  int ldm=n_local, ldw=W.n();
  int M=X.m(), N=m_local, K=n_local;
  double** XM=X.local_data(); double** WM=W.local_data();

  DGEMM(&transX, &transM, &M,&N,&K, &alpha,XM[0],&ldx, this->M[0],&ldm, &beta,WM[0],&ldw);
}
void hiopMatrixDense::timesMatTrans(double beta, hiopMatrix& W_, double alpha, const hiopMatrix& X_) const
{
  hiopMatrixDense& W = dynamic_cast<hiopMatrixDense&>(W_); 
#ifdef DEEP_CHECKING
  const hiopMatrixDense& X = dynamic_cast<const hiopMatrixDense&>(X_);
  assert(W.n_local==W.n_global && "not intended for the case when the result matrix is distributed.");
#endif

  int myrank=0;
#ifdef WITH_MPI
  int ierr=MPI_Comm_rank(comm,&myrank); assert(ierr==MPI_SUCCESS);
#endif

  if(0==myrank) timesMatTrans_local(beta,W_,alpha,X_);
  else          timesMatTrans_local(0.,  W_,alpha,X_);

#ifdef WITH_MPI
  double** WM=W.local_data();
  int n2Red=W.m()*W.n(); double* Wglob=new double[n2Red]; //!opt
  ierr = MPI_Allreduce(WM[0], Wglob, n2Red, MPI_DOUBLE, MPI_SUM, comm); assert(ierr==MPI_SUCCESS);
  memcpy(WM[0], Wglob, n2Red*sizeof(double));
  delete[] Wglob;
#endif
}
void hiopMatrixDense::addDiagonal(const hiopVector& d_)
{
  const hiopVectorPar& d = dynamic_cast<const hiopVectorPar&>(d_);
#ifdef DEEP_CHECKING
  assert(d.get_size()==n());
  assert(d.get_size()==m());
  assert(d.get_local_size()==m_local);
  assert(d.get_local_size()==n_local);
#endif
  const double* dd=d.local_data_const();
  for(int i=0; i<n_local; i++) M[i][i] += dd[i];
}
void hiopMatrixDense::addDiagonal(const double& value)
{
  for(int i=0; i<n_local; i++) M[i][i] += value;
}
void hiopMatrixDense::addSubDiagonal(long long start, const hiopVector& d_)
{
  const hiopVectorPar& d = dynamic_cast<const hiopVectorPar&>(d_);
  long long dlen=d.get_size();
#ifdef DEEP_CHECKING
  assert(start>=0);
  assert(start+dlen<=n_local);
#endif

  const double* dd=d.local_data_const();
  for(int i=start; i<start+dlen; i++) M[i][i] += dd[i-start];
}

void hiopMatrixDense::addMatrix(double alpha, const hiopMatrix& X_)
{
  const hiopMatrixDense& X = dynamic_cast<const hiopMatrixDense&>(X_); 
#ifdef DEEP_CHECKING
  assert(m_local==X.m_local);
  assert(n_local==X.n_local);
#endif
  //  extern "C" void   daxpy_(int* n, double* da, double* dx, int* incx, double* dy, int* incy );
  int N=m_local*n_local, inc=1;
  DAXPY(&N, &alpha, X.M[0], &inc, M[0], &inc);
}

double hiopMatrixDense::max_abs_value()
{
  char norm='M';
  double maxv = DLANGE(&norm, &n_local, &m_local, M[0], &n_local, NULL);
#ifdef WITH_MPI
  double maxvg;
  int ierr=MPI_Allreduce(&maxv,&maxvg,1,MPI_DOUBLE,MPI_MAX,comm); assert(ierr==MPI_SUCCESS);
  return maxvg;
#endif
  return maxv;
}

#ifdef DEEP_CHECKING
bool hiopMatrixDense::assertSymmetry(double tol) const
{
  //must be square
  if(m_local!=n_global) assert(false);

  //symmetry
  for(int i=0; i<n_local; i++)
    for(int j=0; j<n_local; j++) {
      double ij=M[i][j], ji=M[j][i];
      double relerr= fabs(ij-ji)/(1+fabs(ij));
      assert(relerr<tol);
    }
  return true;
}
#endif
};

