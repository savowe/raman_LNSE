/* * ATUS2 - The ATUS2 package is atom interferometer Toolbox developed at ZARM
 * (CENTER OF APPLIED SPACE TECHNOLOGY AND MICROGRAVITY), Germany. This project is
 * founded by the DLR Agentur (Deutsche Luft und Raumfahrt Agentur). Grant numbers:
 * 50WM0942, 50WM1042, 50WM1342.
 * Copyright (C) 2017 Želimir Marojević, Ertan Göklü, Claus Lämmerzahl
 *
 * This file is part of ATUS2.
 *
 * ATUS2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ATUS2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ATUS2.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ostream>
#include <fstream>
#include <string>
#include <cstring>
#include <array>

#include "CRT_Base.h"
#include "ParameterHandler.h"
#include "gsl/gsl_complex_math.h"
#include "gsl/gsl_eigen.h"
#include "gsl/gsl_blas.h"
#include "muParser.h"

using namespace std;

#ifndef __class_CRT_Base_IF__
#define __class_CRT_Base_IF__

/** Template class for interferometry in <B>dim</B> dimensions with <B>no_int_states</B> internal states
  *
  * In this template class functions for the interaction of a BEC with a light field are defined.
  * The three following cases can be computed:
  *   - Bragg beamsplitter with a numerical diagonalisation
  *   - Double Bragg beamsplitter with a numerical diagonalisation
  *   - Raman beamsplitter with a numerical diagonalisation
  */
template <class T, int dim, int no_int_states>
class CRT_Base_IF : public CRT_Base<T,dim,no_int_states>
{
public:
  CRT_Base_IF( ParameterHandler * );
  virtual ~CRT_Base_IF();

  void run_sequence();

protected:
  using CRT_Base<T,dim,no_int_states>::m_header;
  using CRT_Base<T,dim,no_int_states>::m_params;
  using CRT_Base<T,dim,no_int_states>::m_fields;
  using CRT_Base<T,dim,no_int_states>::m_custom_fct;
  using CRT_shared::m_no_of_pts;

  /// Gravitational potential
  CPoint<dim> beta;
  /// Detuning. Energy difference between lasers and the excited state
  std::array<double,no_int_states> DeltaL;
  std::array<double,2> Amp_1_sm,  ///< Amplitude of the light fields \f$ \mu E\f$ */
  	  Phi_1_sm,
  	  Amp_1_sp,///< sm and sp represent left- and right-handed polarization of light
	  Phi_1_sp,
	  Amp_2_sm,
	  Phi_2_sm,
	  Amp_2_sp,
	  Phi_2_sp,
      laser_k, ///< Wave vector of the laser fields
      laser_dk, ///< Difference between wave vectors
      phase, ///< Additional phase (for example phase errors)
      chirp_rate, ///< Chirp rate of the frequency of the laser fields
	  chirp_alpha,
  	  laser_w; ///< angular frequency of the lasers
  std::array<double,no_int_states-2> omega_ij;

  bool amp_is_t;

  static void Do_NL_Step_Wrapper(void *,sequence_item &);
  static void Numerical_Bragg_Wrapper(void *,sequence_item &);
  static void Numerical_Raman_Wrapper(void *,sequence_item &);

  void Do_NL_Step();
  void Numerical_Bragg();
  void Numerical_Raman();

  void UpdateParams();
  void Output_rabi_freq_list(string, const long long);
  void Output_chirps_list(string);

  /// Define custom sequences
  virtual bool run_custom_sequence( const sequence_item & )=0;

  /// Area around momentum states
  double m_rabi_threshold;

  ///< Energy difference between the three internal states: omega_ig, omega_ie
  double omega_ig, omega_ie;

  /// Difference between the frequencies of the laser fields
  double laser_domh;

  /// moving velocity
  double v_0;

  /// constant acceleration
  double g_0;

  /// phase velocity of light
  double c_p;

  /** Contains the position of the momentum states in momentum space */
  vector<CPoint<dim>> m_rabi_momentum_list;
  /** The data for Rabi-oscillations is stored here
    * In this list we store the particle number of each momentum state defined in
    * #m_rabi_momentum_list after each outer loop (after Nk time steps).
    */
  list<list<double>> m_rabi_freq_list;
  /** Contains the phasescan
    * In this list we store the particle number of each momentum state defined in
    * #m_rabi_momentum_list after a Chirp.
    */
  list<list<double>> m_chirps_list;

  void compute_rabi_integrals();

  double Amplitude_at_time();
};


/** Calls UpdateParams() and defines stepfunctions
  *
  * @param Pointer to ParameterHandler object to read from xml files
  */
template <class T, int dim, int no_int_states>
CRT_Base_IF<T,dim,no_int_states>::CRT_Base_IF( ParameterHandler *params ) : CRT_Base<T,dim,no_int_states>(params)
{
  // Map between "freeprop" and Do_NL_Step
  this->m_map_stepfcts["freeprop"] = &Do_NL_Step_Wrapper;
  this->m_map_stepfcts["bragg"] = &Numerical_Bragg_Wrapper;
  this->m_map_stepfcts["raman"] = &Numerical_Raman_Wrapper;

  UpdateParams();
}

/// Destructor
template <class T, int dim, int no_int_states>
CRT_Base_IF<T,dim,no_int_states>::~CRT_Base_IF()
{
}

/** Set values to interferometer variables from xml (m_params)
  *
  * Including:
  *   - the laser beams (amplitude, laser_k, ...)
  *   - gravitation
  *   - rabi_threshold
  */
template <class T, int dim, int no_int_states>
void CRT_Base_IF<T,dim,no_int_states>::UpdateParams()
{
  char s[100];
  for ( int i=0; i<dim; i++)
    beta[i] = m_params->Get_VConstant("Beta",i);

  try
  {
    laser_domh = laser_w[0]-laser_w[1];

    omega_ig = m_params->Get_Constant("omega_ig");
    omega_ie = m_params->Get_Constant("omega_ie");
    for ( int i=0; i<no_int_states-2; i++)
    	omega_ij[i] = m_params->Get_VConstant("omega_ij",i);
    for (int i=2; i<no_int_states; i++)
    {
    	DeltaL[i] = omega_ij[i-2];
    }

    v_0 = m_params->Get_Constant("v_0");
    g_0 = m_params->Get_Constant("g_0");
    c_p = m_params->Get_Constant("c_p");
    m_rabi_threshold = m_params->Get_Constant("rabi_threshold");

  }
  catch (std::string &str )
  {
    cout << str << endl;
    exit(EXIT_FAILURE);
  }
}

template <class T, int dim, int no_int_states>
double CRT_Base_IF<T,dim,no_int_states>::Amplitude_at_time()
{
  if ( amp_is_t )
  {
    double time;
    try
    {
      mu::Parser mup;
      m_params->Setup_muParser( mup );
      mup.SetExpr(m_params->Get_simulation("AMP_T"));
      mup.DefineVar("t",&time);

      time = this->Get_t()+0.5*this->Get_dt();
      return mup.Eval();
    }
    catch (mu::Parser::exception_type &e)
    {
      cout << "Message:  " << e.GetMsg() << "\n";
      cout << "Formula:  " << e.GetExpr() << "\n";
      cout << "Token:    " << e.GetToken() << "\n";
      cout << "Position: " << e.GetPos() << "\n";
      cout << "Errc:     " << e.GetCode() << "\n";
      exit(EXIT_FAILURE);
    }
  }
  else
    return 1;
}


/** Calculate number of particles in the momentum states of the first internal state
  *
  * The momentum states are defined in the list #m_rabi_momentum_list.
  *
  * The particle number is calculated in Fourierspace.
  */
template <class T, int dim, int no_int_states>
void CRT_Base_IF<T,dim,no_int_states>::compute_rabi_integrals()
{
  int n = m_rabi_momentum_list.size();
  if ( n == 0 )
  {
    std::cerr << "WARNING: Rabi momentum list is empty. Cannot compute rabi frquencies." << endl;
    return;
  }

  fftw_complex *psik = m_fields[0]->Getp2In();
  //Fourier transform
  m_fields[0]->ft(-1);

  double res[n];
  memset(res,0,n*sizeof(double));
  double *tmp;
  #pragma omp parallel
  {
    const int nthreads = omp_get_num_threads();
    const int ithread = omp_get_thread_num();

    CPoint<dim> k1, d;

    //size of tmp equals number of threads
    #pragma omp single
    {
      tmp = new double[n*nthreads];
      for (int i=0; i<(n*nthreads); i++) tmp[i] = 0;
    }

    #pragma omp for
    for ( int i=0; i<m_no_of_pts; i++ )
    {
      k1 = m_fields[0]->Get_k(i);
      int j=0;
      //Loop through all momentum states
      for ( auto k0 : m_rabi_momentum_list )
      {
        //Compute distance from momentum state
        d = (k0)-k1;
        //if point lies within area defined by threshold
        if ( sqrt(d*d) < m_rabi_threshold )
        {
          tmp[ithread*n+j] += psik[i][0]*psik[i][0] + psik[i][1]*psik[i][1];
          continue;
        }
        j++;
      }
    }

    //Sum over all threads
    #pragma omp for
    for (int i=0; i<n; i++)
    {
      for (int t=0; t<nthreads; t++)
      {
        res[i] += tmp[n*t + i];
      }
    }
  }
  delete [] tmp;

  //Write number of particles per momentum state
  list<double> tmpvec;
  for ( int i=0; i<n; i++ )
  {
    tmpvec.push_back(this->m_ar_k*res[i]);
  }

  m_rabi_freq_list.push_back(tmpvec);

  //Transform back in real space
  m_fields[0]->ft(1);
}

/** Write Rabi oscillation to file
  *
  * Write #m_rabi_momentum_list to a file called filename
  * @param Nk number of full steps
  */
template <class T, int dim, int no_int_states>
void CRT_Base_IF<T,dim,no_int_states>::Output_rabi_freq_list( string filename, const long long Nk )
{
  ofstream txtfile( filename );

  double t=0;

  //header
  txtfile << "# time \t";
  for ( auto i : m_rabi_momentum_list )
  {
    txtfile << i << "\t";
  }
  txtfile << endl;

  //data
  for ( auto i : m_rabi_freq_list )
  {
    txtfile << t << "\t";
    for ( auto j : i )
    {
      txtfile << j << "\t";
    }
    txtfile << endl;
    t+=this->m_header.dt*double(Nk);
  }
}
/** Write phasescan due to laser chirp to file
  *
  * Write #m_chirps_list to a file called filename
  */
template <class T, int dim, int no_int_states>
void CRT_Base_IF<T,dim,no_int_states>::Output_chirps_list( string filename )
{
  ofstream txtfile( filename );

  //header
  txtfile << "# Step \t Chirp \t ";
  for ( auto i : m_rabi_momentum_list )
  {
    txtfile << i << "\t";
  }
  txtfile << endl;

  //data
  for ( auto i : m_chirps_list )
  {
    for ( auto j : i )
    {
      txtfile.precision(8);
      txtfile << j << "\t";
    }
    txtfile << endl;
  }
}

/** Wrapper function for Do_NL_Step()
  * @param ptr Function pointer to be set to Do_NL_Step()
  * @param seq Additional information about the sequence (for example file names if a file has to be read)
  */
template <class T, int dim, int no_int_states>
void CRT_Base_IF<T,dim,no_int_states>::Do_NL_Step_Wrapper ( void *ptr, sequence_item &seq )
{
  CRT_Base_IF<T,dim,no_int_states> *self = static_cast<CRT_Base_IF<T,dim,no_int_states>*>(ptr);
  self->Do_NL_Step();
}

/** Wrapper function for Numerical_Bragg()
  * @param ptr Function pointer to be set to Numerical_Bragg()
  * @param seq Additional information about the sequence (for example file names if a file has to be read)
  */
template <class T, int dim, int no_int_states>
void CRT_Base_IF<T,dim,no_int_states>::Numerical_Bragg_Wrapper ( void *ptr, sequence_item &seq )
{
  CRT_Base_IF<T,dim,no_int_states> *self = static_cast<CRT_Base_IF<T,dim,no_int_states>*>(ptr);
  self->Numerical_Bragg();
}

/** Wrapper function for Numerical_Raman()
  * @param ptr Function pointer to be set to Numerical_Raman()
  * @param seq Additional information about the sequence (for example file names if a file has to be read)
  */
template <class T, int dim, int no_int_states>
void CRT_Base_IF<T,dim,no_int_states>::Numerical_Raman_Wrapper ( void *ptr, sequence_item &seq )
{
  CRT_Base_IF<T,dim,no_int_states> *self = static_cast<CRT_Base_IF<T,dim,no_int_states>*>(ptr);
  self->Numerical_Raman();
}

/** Solves the potential part without any external fields but
  * gravity.
  */
template <class T, int dim, int no_int_states>
void CRT_Base_IF<T,dim,no_int_states>::Do_NL_Step()
{
  const double dt = -m_header.dt;
  double re1, im1, tmp1, phi[no_int_states];
  CPoint<dim> x;

  vector<fftw_complex *> Psi;
  //Vector for the components of the wavefunction
  for ( int i=0; i<no_int_states; i++ )
    Psi.push_back(m_fields[i]->Getp2In());

  for ( int l=0; l<this->m_no_of_pts; l++ )
  {
    //Loop through column
    for ( int i=0; i<no_int_states; i++ )
    {
      double tmp_density = Psi[i][l][0]*Psi[i][l][0] + Psi[i][l][1]*Psi[i][l][1];
      if (tmp_density <= 0.0)
      {
    	 phi[i] = 0.0;
      } else
      {
    	  phi[i] = -this->m_b*log( tmp_density );
    	  phi[i] += this->m_gs[i+no_int_states*i]*tmp_density;
      }
      x = m_fields[0]->Get_x(l);
      phi[i] += beta[0]*x[0]-DeltaL[i];
      phi[i] *= dt;
    }

    //Compute exponential: exp(V)*Psi
    for ( int i=0; i<no_int_states; i++ )
    {
      sincos( phi[i], &im1, &re1 );

      tmp1 = Psi[i][l][0];
      Psi[i][l][0] = Psi[i][l][0]*re1 - Psi[i][l][1]*im1;
      Psi[i][l][1] = Psi[i][l][1]*re1 + tmp1*im1;
    }
  }
  //tmp !!
  // absorption of wavefuntion at boundaries
  // can be commented out if necessary
  /*
  for ( int k=0; k<no_int_states; k++ )
  {
	  for ( int m=0; m<1000; m++ )
	  {
		  Psi[k][0+m][0] = Psi[k][0+m][0] * pow(sin(M_PI/2/1000*m),2);
		  Psi[k][0+m][1] = Psi[k][0+m][1] * pow(sin(M_PI/2/1000*m),2);

		  Psi[k][this->m_no_of_pts-m][0] = Psi[k][this->m_no_of_pts-m][0] * pow(sin(M_PI/2/1000*m),2);
		  Psi[k][this->m_no_of_pts-m][1] = Psi[k][this->m_no_of_pts-m][1] * pow(sin(M_PI/2/1000*m),2);
	  }
  } */
}

/** Solves the potential part in the presence of light fields with a numerical method
  *
  * In this function \f$ \exp(V)\Psi \f$ is calculated. The matrix exponential is computed
  * with the help of a numerical diagonalisation which uses the gsl library
  */
template <class T, int dim, int no_int_states>
void CRT_Base_IF<T,dim,no_int_states>::Numerical_Bragg()
{
  #pragma omp parallel
  {
    const double dt = -m_header.dt;
    const double t1 = this->Get_t();

    vector<fftw_complex *> Psi;
    for ( int i=0; i<no_int_states; i++ )
      Psi.push_back(m_fields[i]->Getp2In());

    gsl_matrix_complex *A = gsl_matrix_complex_calloc(no_int_states,no_int_states);
    gsl_matrix_complex *B = gsl_matrix_complex_calloc(no_int_states,no_int_states);
    gsl_eigen_hermv_workspace *w = gsl_eigen_hermv_alloc(no_int_states);
    gsl_vector *eval = gsl_vector_alloc(no_int_states);
    gsl_vector_complex *Psi_1 = gsl_vector_complex_alloc(no_int_states);
    gsl_vector_complex *Psi_2 = gsl_vector_complex_alloc(no_int_states);
    gsl_matrix_complex *evec = gsl_matrix_complex_alloc(no_int_states,no_int_states);

    double phi[no_int_states],re1,im1,eta[2];
    CPoint<dim> x;
    laser_k[1] = -laser_k[0];
    chirp_rate[1] = -chirp_rate[0];

    #pragma omp for
    for ( int l=0; l<this->m_no_of_pts; l++ )
    {
      gsl_matrix_complex_set_zero(A);
      gsl_matrix_complex_set_zero(B);

      //Diagonal elements + Nonlinear part: \Delta+g|\Phi|^2+\beta*x
      //-------------------------------------------------------------
      for ( int i=0; i<no_int_states; i++ )
      {
      	double tmp_density = Psi[i][l][0]*Psi[i][l][0] + Psi[i][l][1]*Psi[i][l][1];
      	if (tmp_density <= 0.0)
      	{
      		phi[i] = 0.0;
      	} else
      	{
      		phi[i] = this->m_b*log( tmp_density );
      	}
        x = m_fields[0]->Get_x(l);
        phi[i] += beta*x-DeltaL[i];
        gsl_matrix_complex_set(A,i,i, {phi[i],0});
      }

      //Off diagonal elements (Bragg + Double Bragg)
      //---------------------------------------------

      for ( int i=0; i<no_int_states-1; i++ )
      {
        sincos(((-laser_domh+chirp_rate[i]*t1)*t1+laser_k[i]*x[0]-0.5*phase[0]), &im1, &re1 );

        eta[0] = Amp_1_sm[0]*re1/2+Amp_1_sm[1]*re1/2;
        eta[1] = Amp_1_sm[0]*im1/2-Amp_1_sm[1]*im1/2;

//      sincos((0.5*laser_dk[0]), &im1, &re1);


        gsl_matrix_complex_set(A,i+1,0, {eta[0],eta[1]});
        gsl_matrix_complex_set(A,0,i+1, {eta[0],-eta[1]});
      }

      //-----------------------------------------

      // Compute Eigenvalues and Eigenvector
      gsl_eigen_hermv(A,eval,evec,w);

      //exp(Eigenvalues)
      for ( int i=0; i<no_int_states; i++ )
      {
        sincos( dt*gsl_vector_get(eval,i), &im1, &re1 );
        gsl_matrix_complex_set(B,i,i, {re1,im1});
      }

      // H_new = Eigenvector * exp(Eigenvalues) * conjugate(Eigenvector)
      gsl_blas_zgemm(CblasNoTrans,CblasConjTrans,GSL_COMPLEX_ONE,B,evec,GSL_COMPLEX_ZERO,A);
      gsl_blas_zgemm(CblasNoTrans,CblasNoTrans,GSL_COMPLEX_ONE,evec,A,GSL_COMPLEX_ZERO,B);

      for ( int i=0; i<no_int_states; i++)
      {
        gsl_vector_complex_set(Psi_1,i, {Psi[i][l][0],Psi[i][l][1]});
      }

      // H_new * Psi
      gsl_blas_zgemv(CblasNoTrans,GSL_COMPLEX_ONE,B,Psi_1,GSL_COMPLEX_ZERO,Psi_2);

      for ( int i=0; i<no_int_states; i++)
      {
        Psi[i][l][0] = gsl_vector_complex_get(Psi_2,i).dat[0];
        Psi[i][l][1] = gsl_vector_complex_get(Psi_2,i).dat[1];
      }
    }
    gsl_matrix_complex_free(A);
    gsl_matrix_complex_free(B);
    gsl_eigen_hermv_free(w);
    gsl_vector_free(eval);
    gsl_vector_complex_free(Psi_1);
    gsl_vector_complex_free(Psi_2);
    gsl_matrix_complex_free(evec);
  }
}

/** Solves the potential part in the presence of light fields with a numerical method
  *
  * In this function \f$ \exp(V)\Psi \f$ is calculated. The matrix exponential is computed
  * with the help of a numerical diagonalisation which uses the gsl library
  */
template <class T, int dim, int no_int_states>
void CRT_Base_IF<T,dim,no_int_states>::Numerical_Raman()
{
  #pragma omp parallel
  {
    const double dt = -m_header.dt;
    const double t1 = this->Get_t();

    std::array<double,2> laser_k_tmp, laser_w_tmp, z_R, waist_0, z_0, GaussAmp, R_c, Phase_c, waist, gouy;

    vector<fftw_complex *> Psi;
    for ( int i=0; i<no_int_states; i++ )
      Psi.push_back(m_fields[i]->Getp2In());

    gsl_matrix_complex *A = gsl_matrix_complex_calloc(no_int_states,no_int_states);
    gsl_matrix_complex *B = gsl_matrix_complex_calloc(no_int_states,no_int_states);
    gsl_eigen_hermv_workspace *w = gsl_eigen_hermv_alloc(no_int_states);
    gsl_vector *eval = gsl_vector_alloc(no_int_states);
    gsl_vector_complex *Psi_1 = gsl_vector_complex_alloc(no_int_states);
    gsl_vector_complex *Psi_2 = gsl_vector_complex_alloc(no_int_states);
    gsl_matrix_complex *evec = gsl_matrix_complex_alloc(no_int_states,no_int_states);

    double phi[no_int_states], re1, re2, im1, im2, eta[2];
    CPoint<dim> x;
    std::array<double,2> chirp_alpha = this->chirp_alpha;
    double doppler_beta = v_0/c_p + (g_0*t1)/c_p;

    // Correction for chirp
    laser_w_tmp[0] = laser_w[0]+chirp_alpha[0]*t1;
    laser_k_tmp[0] = laser_w[0]/c_p;
    laser_w_tmp[1] = laser_w[1]+chirp_alpha[1]*t1;
    laser_k_tmp[1] = laser_w[1]/c_p;
    // -------------------

    // Gauss Beam Parameter
    z_0[0] = 0;
    z_0[1] = 0;
    waist_0[0] = 7; // std::numeric_limits<float>::max() for plane waves
    waist_0[1] = 7;
    z_R[0] = laser_k_tmp[0]/2 * pow(waist_0[0],2);
    z_R[1] = laser_k_tmp[1]/2 * pow(waist_0[1],2);
    // -------------------

    // Main Diagonal of Hamiltonian in Rotating Frame
    DeltaL[0] = laser_w_tmp[0]-omega_ig;
    DeltaL[1] = laser_w_tmp[1]-omega_ie;
    // -------------------

    #pragma omp for
    for ( int l=0; l<this->m_no_of_pts; l++ )
    {
      gsl_matrix_complex_set_zero(A);
      gsl_matrix_complex_set_zero(B);

      //Diagonal elements + Nonlinear part: \Delta+g|\Phi|^2
      for ( int i=0; i<no_int_states; i++ )
      {
      	double tmp_density = Psi[i][l][0]*Psi[i][l][0] + Psi[i][l][1]*Psi[i][l][1];
      	if (tmp_density <= 0.0)
      	{
      		phi[i] = 0.0;
      	} else
      	{
      		phi[i] = -this->m_b*log( tmp_density );
      	}
        x = this->m_fields[0]->Get_x(l);
        phi[i] += DeltaL[i];
        gsl_matrix_complex_set(A,i,i, {phi[i],0});
      }

      // 2D Gauss Amplitude x[0] is z-axis and x[1] is y-axis also called radius
      waist[0] = waist_0[0] * sqrt( 1 + pow((x[0]-z_0[0])/z_R[0], 2) );
      waist[1] = waist_0[1] * sqrt( 1 + pow((x[0]-z_0[0])/z_R[1], 2) );
      R_c[0] = (x[0]-z_0[0]) * ( 1 + pow(z_R[0]/(x[0]-z_0[0]),2) );
      R_c[1] = (x[0]-z_0[1]) * ( 1 + pow(z_R[1]/(x[0]-z_0[1]),2) );
      // The Radius of curvature can be infinite. This is catched here
      if (R_c[0] != R_c[0]) {
    	  R_c[0] = std::numeric_limits<float>::max();
      }
      if (R_c[1] != R_c[1]) {
    	  R_c[1] = std::numeric_limits<float>::max();
      }
      //-----------------------------------
      Phase_c[0] = pow(x[1],2)/(2*R_c[0]);
      Phase_c[1] = pow(x[1],2)/(2*R_c[1]);

      gouy[0] = atan( (x[0]-z_0[0])/z_R[0] );
      gouy[1] = atan( (x[0]-z_0[0])/z_R[1] );

      GaussAmp[0] = waist_0[0] / waist[0] * exp( - pow(x[1]/waist[0],2) );
      GaussAmp[1] = waist_0[1] / waist[1] * exp( - pow(x[1]/waist[1],2) );

      //---------------------------------------------

      //Raman
      //---------------------------------------------

      sincos(laser_k_tmp[0] * (x[0] + Phase_c[0] ) * (doppler_beta - 1) - doppler_beta * laser_w_tmp[0] * t1 - Phi_1_sm[0] - gouy[0], &im1, &re1 ); // For right going light
      sincos(laser_k_tmp[0] * (x[0] + Phase_c[0] ) * (doppler_beta + 1) + doppler_beta * laser_w_tmp[0] * t1 - Phi_1_sm[1] - gouy[0], &im2, &re2 ); // For left going light

      //---------------------------------------------

      eta[0] = re1 * Amp_1_sm[0] + re2 * Amp_1_sm[1];
      eta[0] *= GaussAmp[0];
      eta[1] = im1 * Amp_1_sm[0] + im2 * Amp_1_sm[1];
      eta[1] *= GaussAmp[0];

      gsl_matrix_complex_set(A,0,2, {eta[0],eta[1]});
      gsl_matrix_complex_set(A,2,0, {eta[0],-eta[1]});

      //---------------------------------------------

      sincos(laser_k_tmp[1] * (x[0] + Phase_c[1] ) * (doppler_beta - 1) - doppler_beta * laser_w_tmp[1] * t1 - Phi_2_sm[0] - gouy[1], &im1, &re1 );
      sincos(laser_k_tmp[1] * (x[0] + Phase_c[1] ) * (doppler_beta + 1) + doppler_beta * laser_w_tmp[1] * t1 - Phi_2_sm[1] - gouy[1], &im2, &re2 );

      //---------------------------------------------

      eta[0] = re1 * Amp_2_sm[0] + re2 * Amp_2_sm[1];
      eta[0] *= GaussAmp[1];
      eta[1] = im1 * Amp_2_sm[0] + im2 * Amp_2_sm[1];
      eta[0] *= GaussAmp[1];

      gsl_matrix_complex_set(A,1,2, {eta[0],eta[1]});
      gsl_matrix_complex_set(A,2,1, {eta[0],-eta[1]});

      //--------------------------------------------

      //Compute Eigenvalues + Eigenvector
      gsl_eigen_hermv(A,eval,evec,w);

      // exp(Eigenvalues)
      for ( int i=0; i<no_int_states; i++ )
      {
        sincos( dt*gsl_vector_get(eval,i), &im1, &re1 );
        gsl_matrix_complex_set(B,i,i, {re1,im1});
      }

      // H_new = Eigenvector * exp(Eigenvalues) * conjugate(Eigenvector)
      gsl_blas_zgemm(CblasNoTrans,CblasConjTrans,GSL_COMPLEX_ONE,B,evec,GSL_COMPLEX_ZERO,A);
      gsl_blas_zgemm(CblasNoTrans,CblasNoTrans,GSL_COMPLEX_ONE,evec,A,GSL_COMPLEX_ZERO,B);

      for ( int i=0; i<no_int_states; i++)
      {
        gsl_vector_complex_set(Psi_1,i, {Psi[i][l][0],Psi[i][l][1]});
      }

      // H_new * Psi
      gsl_blas_zgemv(CblasNoTrans,GSL_COMPLEX_ONE,B,Psi_1,GSL_COMPLEX_ZERO,Psi_2);

      for ( int i=0; i<no_int_states; i++)
      {
        Psi[i][l][0] = gsl_vector_complex_get(Psi_2,i).dat[0];
        Psi[i][l][1] = gsl_vector_complex_get(Psi_2,i).dat[1];
      }
    }
    gsl_matrix_complex_free(A);
    gsl_matrix_complex_free(B);
    gsl_eigen_hermv_free(w);
    gsl_vector_free(eval);
    gsl_vector_complex_free(Psi_1);
    gsl_vector_complex_free(Psi_2);
    gsl_matrix_complex_free(evec);
  }
}

/** Run all the sequences defined in the xml file
  *
  * For furher information about the sequences see sequence_item
  */
template <class T, int dim, int no_int_states>
void CRT_Base_IF<T,dim,no_int_states>::run_sequence()
{
  if ( m_fields.size() != no_int_states )
  {
    std::cerr << "Critical Error: m_fields.size() != no_int_states\n";
    exit(EXIT_FAILURE);
  }

  StepFunction step_fct=nullptr;
  StepFunction half_step_fct=nullptr;
  StepFunction full_step_fct=nullptr;
  char filename[1024];

  std::cout << "FYI: Found " << m_params->m_sequence.size() << " sequences." << std::endl;
  int nrm = m_rabi_momentum_list.size();
  if ( nrm == 0 )
  {
    std::cerr << "WARNING: Rabi momentum list is empty. Cannot compute rabi frquencies." << endl;
  }

  try
  {
    half_step_fct = this->m_map_stepfcts.at("half_step");
    full_step_fct = this->m_map_stepfcts.at("full_step");
  }
  catch (const std::out_of_range &oor)
  {
    std::cerr << "Critical Error: Invalid fct ptr to ft_half_step or ft_full_step ()" << oor.what() << ")\n";
    exit(EXIT_FAILURE);
  }

  int seq_counter=1;

  for ( auto seq : m_params->m_sequence )
  {
    if ( run_custom_sequence(seq) )
    {
      seq_counter++;
      continue;
    }

    if ( seq.name == "set_momentum" )
    {
      std::vector<std::string> vec;
      strtk::parse(seq.content,",",vec);
      CPoint<dim> P;

      assert( vec.size() > dim );
      assert( seq.comp <= dim );

      for ( int i=0; i<dim; i++ )
        P[i] = stod(vec[i]);

      this->Setup_Momentum( P, seq.comp );

      std::cout << "FYI: started new sequence " << seq.name << "\n";
      std::cout << "FYI: momentum set for component " << seq.comp << "\n";
      continue;
    }


    double max_duration = 0;
    for ( int i = 0; i < seq.duration.size(); i++)
      if (seq.duration[i] > max_duration )
        max_duration = seq.duration[i];

    int subN = int(max_duration / seq.dt);
    int Nk = seq.Nk;
    int Na = subN / seq.Nk;

	this->laser_w[0] = seq.laser_w1;
	this->laser_w[1] = seq.laser_w2;
	this->chirp_alpha[0] = seq.chirp_w1;
	this->chirp_alpha[1] = seq.chirp_w2;
	this->laser_k[0] = this->laser_w[0]/this->c_p;
	this->laser_k[1] = this->laser_w[1]/this->c_p;
    this->Amp_1_sm[0] = seq.Amp_1_sm_r;
    this->Phi_1_sm[0] = seq.Phi_1_sm_r;
    this->Amp_1_sm[1] = seq.Amp_1_sm_l;
    this->Phi_1_sm[1] = seq.Phi_1_sm_l;
    this->Amp_2_sm[0] = seq.Amp_2_sm_r;
    this->Phi_2_sm[0] = seq.Phi_2_sm_r;
    this->Amp_2_sm[1] = seq.Amp_2_sm_l;
    this->Phi_2_sm[1] = seq.Phi_2_sm_l;

    this->Amp_1_sp[0] = seq.Amp_1_sp_r;
    this->Phi_1_sp[0] = seq.Phi_1_sp_r;
    this->Amp_1_sp[1] = seq.Amp_1_sp_l;
    this->Phi_1_sp[1] = seq.Phi_1_sp_l;
    this->Amp_2_sp[0] = seq.Amp_2_sp_r;
    this->Phi_2_sp[0] = seq.Phi_2_sp_r;
    this->Amp_2_sp[1] = seq.Amp_2_sp_l;
    this->Phi_2_sp[1] = seq.Phi_2_sp_l;

    std::cout << "FYI: started new sequence " << seq.name << "\n";
    std::cout << "FYI: sequence no : " << seq_counter << "\n";
    std::cout << "FYI: duration    : " << max_duration << "\n";
    std::cout << "FYI: dt          : " << seq.dt << "\n";
    std::cout << "FYI: Na          : " << Na << "\n";
    std::cout << "FYI: Nk          : " << Nk << "\n";
    std::cout << "FYI: Na*Nk*dt    : " << double(Na*Nk)*seq.dt << "\n";

    try
    {
      std::cout << "FYI: Amp is      : " << m_params->Get_simulation("AMP_T") << "\n";
      amp_is_t = true;
    }
    catch (std::string &str )
    {
      std::cout << "FYI: Amp is      : 1" << std::endl;
      amp_is_t = false;
    }

    if ( double(Na*Nk)*seq.dt != max_duration )
      std::cout << "FYI: double(Na*Nk)*seq.dt != max_duration\n";

    if ( this->Get_dt() != seq.dt )
      this->Set_dt(seq.dt);

    try
    {
      step_fct = this->m_map_stepfcts.at(seq.name);
    }
    catch (const std::out_of_range &oor)
    {
      std::cerr << "Critical Error: Invalid squence name " << seq.name << "\n(" << oor.what() << ")\n";
      exit(EXIT_FAILURE);
    }

    if ( seq.name == "freeprop" ) seq.no_of_chirps=1;
    double backup_t = m_header.t;
    double backup_end_t = m_header.t;
    for ( int k=0; k<no_int_states; k++ ) // Delete old packed Sequence
    {
      sprintf( filename, "Seq_%d_%d.bin", seq_counter, k+1 );
      std::remove(filename);
    }

    m_chirps_list.clear();

    double dw[seq.no_of_chirps], dphi[seq.no_of_chirps];

    if (seq.no_of_chirps > 1)
    {
      for ( int k=0; k<no_int_states; k++ ) // Save Wavefunction to reset per chirp
      {
        sprintf( filename, "%.3f_%d.bin", this->Get_t(), k+1 );
        this->Save_Phi( filename, k );
      }
      dw[0] = seq.chirp_min;
      dw[1] = seq.chirp_max;
    }
    else
      dw[0] = 0;

    for ( int s=0; s<seq.no_of_chirps; ++s )
    {
      m_rabi_freq_list.clear();
      chirp_rate[0] = dw[s];
      if (seq.chirp_mode == 1)
      {
        phase[0] = s*2.0*M_PI/(1.0*seq.no_of_chirps);
      }

      for ( int i=1; i<=Na; i++ )
      {
        (*half_step_fct)(this,seq);
        for ( int j=2; j<=Nk; j++ )
        {
          (*step_fct)(this,seq);
          (*full_step_fct)(this,seq);
        }
        (*step_fct)(this,seq);
        (*half_step_fct)(this,seq);

        std::cout << "t = " << to_string(m_header.t) << std::endl;

        if ( (seq.output_freq == freq::each) and (s == 0) )
        {
          for ( int k=0; k<no_int_states; k++ )
          {
            sprintf( filename, "%.3f_%d.bin", this->Get_t(), k+1 );
            this->Save_Phi( filename, k );
          }
        }

        if ( (seq.output_freq == freq::packed) and (s == 0) )
        {
          for ( int k=0; k<no_int_states; k++ )
          {
            sprintf( filename, "Seq_%d_%d.bin", seq_counter, k+1 );
            this->Append_Phi( filename, k );
          }
        }

        if ( seq.compute_pn_freq == freq::each )
        {
          for ( int c=0; c<no_int_states; c++ )
            std::cout << "N[" << c << "] = " << this->Get_Particle_Number(c) << std::endl;
        }

        if ( seq.rabi_output_freq == freq::each )
        {
          compute_rabi_integrals();
        }

        if ( seq.custom_freq == freq::each && m_custom_fct != nullptr )
        {
          (*m_custom_fct)(this,seq);
        }
      }

      if ( (seq.output_freq == freq::last) and (s == 0) )
      {
        for ( int k=0; k<no_int_states; k++ )
        {
          sprintf( filename, "%.3f_%d.bin", this->Get_t(), k+1 );
          this->Save_Phi( filename, k );
        }
      }

      if ( (seq.no_of_chirps > 1) and (s == 0) )
      {
        backup_end_t = this->Get_t();
        for ( int k=0; k<no_int_states; k++ )
        {
          sprintf( filename, "%.3f_%d.bin", this->Get_t(), k+1 );
          this->Save_Phi( filename, k );
        }
      }


      if ( seq.compute_pn_freq == freq::last )
      {
        for ( int c=0; c<no_int_states; c++ )
          std::cout << "N[" << c << "] = " << this->Get_Particle_Number(c) << std::endl;
      }

      if ( seq.custom_freq == freq::last && m_custom_fct != nullptr )
      {
        (*m_custom_fct)(this,seq);
      }

      if ( seq.rabi_output_freq == freq::last )
      {
        compute_rabi_integrals();

        cout << "Rabi output for timestep ";
        for ( auto i : m_rabi_freq_list )
        {
          cout << this->Get_t() << "\n";
          for ( auto j : i ) cout << j << "\t";
          cout << endl;
        }
      }

      if ( seq.rabi_output_freq == freq::each )
      {
        sprintf(filename, "Rabi_%d_%d.txt", seq_counter, s );
        Output_rabi_freq_list(filename,seq.Nk);
      }

      if ( seq.no_of_chirps > 1 )
      {
        //Calculate number of particles of each momentum state (for chirps)
        if (  nrm > 0)
        {
          compute_rabi_integrals();
          list<double> tmp = m_rabi_freq_list.back();
          if (seq.chirp_mode == 1)
          {
            tmp.push_front(phase[0]);
          }
          else
          {
            dphi[s] = tmp.front();
            tmp.push_front(chirp_rate[0]);
          }
          tmp.push_front(s);
          m_chirps_list.push_back(tmp);
        }

        // Loading files from last sequence
        if ( s<seq.no_of_chirps-1 )
        {
          for ( int k=0; k<no_int_states; k++ )
          {
            sprintf( filename, "%.3f_%d.bin", backup_t, k+1 );
            ifstream file1( filename, ifstream::binary );
            file1.seekg( sizeof(generic_header), ifstream::beg );
            file1.read( (char *)m_fields[k]->Getp2In(), sizeof(fftw_complex)*m_no_of_pts );
          }
          this->m_header.t = backup_t;
        }

        //Calculate new chirp
        if ( s>0 && s<seq.no_of_chirps-1)
        {
          dw[s+1] = (dw[s]+dw[s-1])/2;
          if (dphi[s]-dphi[s-1]<0)
          {
            dw[s] = dw[s-1];
            dphi[s] = dphi[s-1];
          }
        }
      }
    } // end of phase scan loop

    //Output number of particles dependent on chirp
    if ( seq.no_of_chirps > 1 )
    {
      sprintf(filename, "Chirp_%d.txt", seq_counter );
      Output_chirps_list(filename);
    }

    // Loading files after initial chirp sequence
    if ( seq.no_of_chirps > 1 )
    {
      for ( int k=0; k<no_int_states; k++ )
      {
        sprintf( filename, "%.3f_%d.bin", backup_end_t, k+1 );
        ifstream file1( filename, ifstream::binary );
        file1.seekg( sizeof(generic_header), ifstream::beg );
        file1.read( (char *)m_fields[k]->Getp2In(), sizeof(fftw_complex)*m_no_of_pts );
      }
      assert(this->m_header.t = backup_end_t);
      this->m_header.t = backup_end_t;
    }

    seq_counter++;
  } // end of sequence loop
}
#endif
