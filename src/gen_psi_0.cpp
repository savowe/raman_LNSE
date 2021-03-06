// This file is part of TALISES.
//
// TALISES is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TALISES is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TALISES.  If not, see <http://www.gnu.org/licenses/>.
//
// Copyright (C) 2020 Sascha Vowe
// Copyright (C) 2017 Želimir Marojević, Ertan Göklü, Claus Lämmerzahl - Original implementation in ATUS2

#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <functional>
#include <omp.h>
#include "muParser.h"
#include "ParameterHandler.h"
#include "fftw3.h"

using namespace std;

template<int dim>
class WavefunctionGenerator
{
public:
  WavefunctionGenerator( ParameterHandler &p, std::function<void(const long long, const generic_header &, double (&)[dim])> f ) :
    m_ph(p),
    get_coord(f)
  {
    m_header = {};
    m_header.nDims = dim;
    m_header.nself = sizeof(generic_header);
    m_header.bAtom = 1;
    m_header.bComplex = 1;
    m_header.nDatatyp = sizeof(fftw_complex);
    m_header.xMax = p.Get_xMax();
    m_header.xMin = p.Get_xMin();
    m_header.yMax = p.Get_yMax();
    m_header.yMin = p.Get_yMin();
    m_header.zMax = p.Get_zMax();
    m_header.zMin = p.Get_zMin();
    m_header.M = 1;
    m_header.T_scale = p.Get_t_scale();
    m_header.dt = 0.001;

    switch (dim)
    {
    case 1:
      m_header.nDimX = m_ph.Get_NX();
      m_header.nDimY = 1;
      m_header.nDimZ = 1;
      m_header.dx = fabs(m_header.xMax-m_header.xMin)/double(m_header.nDimX);
      m_header.dkx = 2*M_PI/fabs(m_header.xMax-m_header.xMin);
      m_ar = m_header.dx;
      break;
    case 2:
      m_header.nDimX = m_ph.Get_NX();
      m_header.nDimY = m_ph.Get_NY();
      m_header.nDimZ = 1;
      m_header.dx = fabs(m_header.xMax-m_header.xMin)/double(m_header.nDimX);
      m_header.dy = fabs(m_header.yMax-m_header.yMin)/double(m_header.nDimY);
      m_header.dkx = 2*M_PI/fabs(m_header.xMax-m_header.xMin);
      m_header.dky = 2*M_PI/fabs(m_header.yMax-m_header.yMin);
      m_ar = m_header.dx*m_header.dy;
      break;
    case 3:
      m_header.nDimX = m_ph.Get_NX();
      m_header.nDimY = m_ph.Get_NY();
      m_header.nDimZ = m_ph.Get_NZ();
      m_header.dx = fabs(m_header.xMax-m_header.xMin)/double(m_header.nDimX);
      m_header.dy = fabs(m_header.yMax-m_header.yMin)/double(m_header.nDimY);
      m_header.dz = fabs(m_header.zMax-m_header.zMin)/double(m_header.nDimZ);
      m_header.dkx = 2*M_PI/fabs(m_header.xMax-m_header.xMin);
      m_header.dky = 2*M_PI/fabs(m_header.yMax-m_header.yMin);
      m_header.dkz = 2*M_PI/fabs(m_header.zMax-m_header.zMin);
      m_ar = m_header.dx*m_header.dy*m_header.dz;
      break;
    }

    m_psi = fftw_alloc_complex( m_header.nDimX*m_header.nDimY*m_header.nDimZ );
  }

  ~WavefunctionGenerator()
  {
    fftw_free( m_psi );
  }

  void run()
  {
    double n_of_particles=1;
    std::string filename, real_str, imag_str;
    double m_L;
    try
    {
      filename = m_ph.Get_simulation("FILENAME");
      real_str = m_ph.Get_simulation("PSI_REAL_" + std::to_string(dim) + "D");
      imag_str = m_ph.Get_simulation("PSI_IMAG_" + std::to_string(dim) + "D");
      n_of_particles = m_ph.Get_Constant("N");
    }
    catch (const std::string info )
    {
      cout << info << endl;
      throw;
    }

    const long long Ntot = m_header.nDimX*m_header.nDimY*m_header.nDimZ;

    double N=0;

    #pragma omp parallel
    {
      double coord[dim];

      mu::Parser mup_real;
      mu::Parser mup_imag;
      m_ph.Setup_muParser( mup_real );
      m_ph.Setup_muParser( mup_imag );

      switch (dim)
      {
      case 1:
        mup_real.SetExpr(real_str);
        mup_real.DefineVar("x", &coord[0]);
        mup_imag.SetExpr(imag_str);
        mup_imag.DefineVar("x", &coord[0]);
        break;
      case 2:
        mup_real.SetExpr(real_str);
        mup_real.DefineVar("x", &coord[0]);
        mup_real.DefineVar("y", &coord[1]);
        mup_imag.SetExpr(imag_str);
        mup_imag.DefineVar("x", &coord[0]);
        mup_imag.DefineVar("y", &coord[1]);
        break;
      case 3:
        mup_real.SetExpr(real_str);
        mup_real.DefineVar("x", &coord[0]);
        mup_real.DefineVar("y", &coord[1]);
        mup_real.DefineVar("z", &coord[2]);
        mup_imag.SetExpr(imag_str);
        mup_imag.DefineVar("x", &coord[0]);
        mup_imag.DefineVar("y", &coord[1]);
        mup_imag.DefineVar("z", &coord[2]);
        break;
      }

      #pragma omp for reduction(+:N)
      for ( long long l=0; l<Ntot; l++ )
      {
        get_coord( l, m_header, coord );

        try
        {
          m_psi[l][0] = mup_real.Eval();
          m_psi[l][1] = mup_imag.Eval();
        }
        catch ( mu::Parser::exception_type &e )
        {
          cout << "Message:  " << e.GetMsg() << "\n";
          cout << "Formula:  " << e.GetExpr() << "\n";
          cout << "Token:    " << e.GetToken() << "\n";
          cout << "Position: " << e.GetPos() << "\n";
          cout << "Errc:     " << e.GetCode() << "\n";
          throw;
        }

        N += m_psi[l][0]*m_psi[l][0] + m_psi[l][1]*m_psi[l][1];
      }

      #pragma omp single
      N *= m_ar;

      if ( N > 0.0)
      {
        const double f=sqrt(n_of_particles/N);

        #pragma omp for
        for ( long long l=0; l<Ntot; l++ )
        {
          m_psi[l][0] *= f;
          m_psi[l][1] *= f;
        }
      }
    }

    ofstream ofs(filename);
    ofs.write( reinterpret_cast<char *>(&m_header), sizeof(generic_header) );
    ofs.write( reinterpret_cast<char *>(m_psi), sizeof(fftw_complex)*Ntot );
  }

protected:
  double m_ar;
  generic_header m_header;
  ParameterHandler &m_ph;
  fftw_complex *m_psi;

  std::function<void(const long long, const generic_header &, double (&)[dim])> get_coord;
};

void index_to_coordinate_1d( const long long l, const generic_header &header, double (&arr)[1] )
{
  arr[0] = double(l-(header.nDimX>>1))*header.dx;
}

void index_to_coordinate_2d( const long long l, const generic_header &header, double (&arr)[2] )
{
  long long i = l / header.nDimY;
  long long j = l - i * header.nDimY;
  arr[0] = double(i-(header.nDimX>>1))*header.dx;
  arr[1] = double(j-(header.nDimY>>1))*header.dy;
}

void index_to_coordinate_3d( const long long l, const generic_header &header, double (&arr)[3] )
{
  long long i = l / (header.nDimY * header.nDimZ);
  long long j = (l - i * header.nDimY * header.nDimZ)/header.nDimZ;
  long long k = l - i * header.nDimY * header.nDimZ - j * header.nDimZ;
  arr[0] = double(i-(header.nDimX>>1))*header.dx;
  arr[1] = double(j-(header.nDimY>>1))*header.dy;
  arr[2] = double(k-(header.nDimZ>>1))*header.dz;
}

int main( int argc, char *argv[] )
{
  if ( argc != 2 )
  {
    printf( "No xml file specified.\n" );
    return EXIT_FAILURE;
  }

  ParameterHandler params(argv[1]);

  int dim=0;
  try
  {
    string tmp = params.Get_simulation("DIM");
    dim = std::stoi(tmp);
    cout << "DIM = " << dim << endl;
  }
  catch (string str)
  {
    cout << str << endl;
  }

  if ( dim == 1 )
  {
    std::function<void(const long long, const generic_header &, double (&)[1])> f = index_to_coordinate_1d;
    WavefunctionGenerator<1> wg(params,f);
    wg.run();
  }

  if ( dim == 2 )
  {
    std::function<void(const long long, const generic_header &, double (&)[2])> f = index_to_coordinate_2d;
    WavefunctionGenerator<2> wg(params,f);
    wg.run();
  }

  if ( dim == 3 )
  {
    std::function<void(const long long, const generic_header &, double (&)[3])> f = index_to_coordinate_3d;
    WavefunctionGenerator<3> wg(params,f);
    wg.run();
  }

  return EXIT_SUCCESS;
}