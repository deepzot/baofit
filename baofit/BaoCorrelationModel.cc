// Created 06-Apr-2012 by David Kirkby (University of California, Irvine) <dkirkby@uci.edu>

#include "baofit/BaoCorrelationModel.h"
#include "baofit/RuntimeError.h"

#include "cosmo/RsdCorrelationFunction.h"
#include "cosmo/TransferFunctionPowerSpectrum.h"
#include "likely/Interpolator.h"
#include "likely/function.h"
#include "likely/RuntimeError.h"

#include "boost/format.hpp"
#include "boost/foreach.hpp"

#include <cmath>

namespace local = baofit;

local::BaoCorrelationModel::BaoCorrelationModel(std::string const &modelrootName,
    std::string const &fiducialName, std::string const &nowigglesName,
    std::string const &broadbandName, double zref, int bb3rpmin, int bb3rpmax,
    int bb3mupmax, int bb3zpmax, bool bb3muodd, bool anisotropic)
  : AbsCorrelationModel("BAO Correlation Model"), _zref(zref), _anisotropic(anisotropic),
    _bband3(zref, bb3rpmin, bb3rpmax, bb3mupmax, bb3zpmax, bb3muodd)

{
    if(zref < 0) {
        throw RuntimeError("BaoCorrelationModel: expected zref >= 0.");
    }
    // Linear bias parameters
    defineParameter("beta",1.4,0.1);
    defineParameter("(1+beta)*bias",-0.336,0.03);
    // BAO peak parameters
    defineParameter("BAO amplitude",1,0.15);
    defineParameter("BAO alpha-iso",1,0.02);
    defineParameter("BAO alpha-parallel",1,0.1);
    defineParameter("BAO alpha-perp",1,0.1);
    // Redshift evolution parameters
    defineParameter("gamma-bias",3.8,0.3);
    defineParameter("gamma-beta",0,0.1);
    defineParameter("gamma-scale",0,0.5);    
    // Broadband Model 1 parameters
    defineParameter("BBand1 xio",0,0.001);
    defineParameter("BBand1 a0",0,0.2);
    defineParameter("BBand1 a1",0,2);
    defineParameter("BBand1 a2",0,2);
    // Broadband Model 2 parameters
    defineParameter("BBand2 mono const",0,1e-4);
    defineParameter("BBand2 quad const",0,1e-4);
    defineParameter("BBand2 hexa const",0,1e-4);
    defineParameter("BBand2 mono 1/r",0,0.01);
    defineParameter("BBand2 quad 1/r",0,0.02);
    defineParameter("BBand2 hexa 1/r",0,0.04);
    defineParameter("BBand2 mono 1/(r*r)",0,0.6);
    defineParameter("BBand2 quad 1/(r*r)",0,1.2);
    defineParameter("BBand2 hexa 1/(r*r)",0,2.4);
    // Brodband Model 3 parameters
    // cannot do this in _bband3.defineparamter(this), because
    // defineParamter is protected.
    BOOST_FOREACH(std::string s, _bband3.parameterNames()) {
	defineParameter (s,0.0, 1.0);
      }

    // Load the interpolation data we will use for each multipole of each model.
    std::string root(modelrootName);
    if(0 < root.size() && root[root.size()-1] != '/') root += '/';
    boost::format fileName("%s%s.%d.dat"),bbandName("%s%s%c.%d.dat");
    std::string method("cspline");
    try {
        cosmo::CorrelationFunctionPtr
            fid0 = likely::createFunctionPtr(likely::createInterpolator(
                boost::str(fileName % root % fiducialName % 0),method)),
            fid2 = likely::createFunctionPtr(likely::createInterpolator(
                boost::str(fileName % root % fiducialName % 2),method)),
            fid4 = likely::createFunctionPtr(likely::createInterpolator(
                boost::str(fileName % root % fiducialName % 4),method)),
            nw0 = likely::createFunctionPtr(likely::createInterpolator(
                boost::str(fileName % root % nowigglesName % 0),method)),
            nw2 = likely::createFunctionPtr(likely::createInterpolator(
                boost::str(fileName % root % nowigglesName % 2),method)),
            nw4 = likely::createFunctionPtr(likely::createInterpolator(
                boost::str(fileName % root % nowigglesName % 4),method)),
            bbc0 = likely::createFunctionPtr(likely::createInterpolator(
                boost::str(bbandName % root % broadbandName % 'c' % 0),method)),
            bbc2 = likely::createFunctionPtr(likely::createInterpolator(
                boost::str(bbandName % root % broadbandName % 'c' % 2),method)),
            bbc4 = likely::createFunctionPtr(likely::createInterpolator(
                boost::str(bbandName % root % broadbandName % 'c' % 4),method)),
            bb10 = likely::createFunctionPtr(likely::createInterpolator(
                boost::str(bbandName % root % broadbandName % '1' % 0),method)),
            bb12 = likely::createFunctionPtr(likely::createInterpolator(
                boost::str(bbandName % root % broadbandName % '1' % 2),method)),
            bb14 = likely::createFunctionPtr(likely::createInterpolator(
                boost::str(bbandName % root % broadbandName % '1' % 4),method)),
            bb20 = likely::createFunctionPtr(likely::createInterpolator(
                boost::str(bbandName % root % broadbandName % '2' % 0),method)),
            bb22 = likely::createFunctionPtr(likely::createInterpolator(
                boost::str(bbandName % root % broadbandName % '2' % 2),method)),
            bb24 = likely::createFunctionPtr(likely::createInterpolator(
                boost::str(bbandName % root % broadbandName % '2' % 4),method));
        // Create redshift-space distorted correlation function models from the multipole interpolators.
        _fid.reset(new cosmo::RsdCorrelationFunction(fid0,fid2,fid4));
        _nw.reset(new cosmo::RsdCorrelationFunction(nw0,nw2,nw4));
        _bbc.reset(new cosmo::RsdCorrelationFunction(bbc0,bbc2,bbc4));
        _bb1.reset(new cosmo::RsdCorrelationFunction(bb10,bb12,bb14));
        _bb2.reset(new cosmo::RsdCorrelationFunction(bb20,bb22,bb24));
    }
    catch(likely::RuntimeError const &e) {
        throw RuntimeError("BaoCorrelationModel: error while reading model interpolation data.");
    }
}

local::BaoCorrelationModel::~BaoCorrelationModel() { }

namespace baofit {
    // Define a function object class that simply returns a constant. This could also be done
    // with boost::lambda using (_1 = value), but I don't know how to create a lambda functor
    // on the heap so it can be used with the likely::createFunctionPtr machinery.
    class BaoCorrelationModel::BBand2 {
    public:
        BBand2(double c, double r1, double r2) : _c(c), _r1(r1), _r2(r2) { }
        double operator()(double r) { return _c + _r1/r + _r2*(r-100.0); }
    private:
        double _c,_r1,_r2;
    };
}

baofit::BBand3::BBand3(int zref, int rpmin, int rpmax, int mupmax, int zpmax, bool muodd) :
  _zref(zref), _rpmin(rpmin), _rpmax(rpmax), _mupmax(mupmax), _zpmax(zpmax)
{
  if (muodd) _mupstep=1; else _mupstep=2;
  boost::format bb3name("BBand3 r%i mu%i z%i");

  for (int rp=_rpmin; rp<=_rpmax; rp++) 
    for (int mp=0; mp<=mupmax; mp+=_mupstep)
      for (int zp=0; zp<=_zpmax; zp++)
	_pnames.push_back(boost::str(bb3name%rp%mp%zp));
  _vals.resize(_pnames.size());
  
}

std::vector<std::string> baofit::BBand3::parameterNames() {
  return _pnames;
}
      
double baofit::BBand3::operator()(const likely::FitModel* m, double r,double mu, double z, bool anyChanged) const {
  double bband3(0.0);
  double zr (z-_zref);
  

  std::vector<double>::iterator  vit=_vals.begin();
  std::vector<std::string>::const_iterator pit(_pnames.begin());
  
  for (int rp=_rpmin; rp<=_rpmax; rp++) {
    // if rp==0: 1
    // if rp<0 : (100/r)^rp
    // if rp>0:  ((r-100)/100)^rp
    double rfact(rp==0 ? 1 : ( (rp>0) ? pow ((r-100.)/100.,rp) : pow((r/100.),rp) )  ); 
    //double rfact(rp==0 ? 1 : pow((r),+rp)  ); 
      for (int mp=0; mp<=_mupmax; mp+=_mupstep) {
	double mufact(mp==0 ? 1 : pow(mu,mp));
	for (int zp=0; zp<=_zpmax; zp++) {
	  double zfact(zp==0? 1 : pow (zr,zp));
	  double v;
	  if (anyChanged) {
	    v=m->getParameterValue(*pit);
	    *vit=v;
	  } else v=*vit;
	  
	  pit++; vit++;
	  bband3+=rfact*mufact*zfact*v;
	}
      }
  }
  return bband3;
}





#include "likely/function_impl.h"

template cosmo::CorrelationFunctionPtr likely::createFunctionPtr<local::BaoCorrelationModel::BBand2>
    (local::BaoCorrelationModel::BBand2Ptr pimpl);

double local::BaoCorrelationModel::_evaluate(double r, double mu, double z, bool anyChanged) const {
  static double beta, bb, gamma_bias, gamma_beta, ampl, scale, scale_parallel, scale_perp, gamma_scale,
    xio, a0, a1, a2;
  
  if (anyChanged) {
   
    beta = getParameterValue("beta");
     bb = getParameterValue("(1+beta)*bias");
     gamma_bias = getParameterValue("gamma-bias");
     gamma_beta = getParameterValue("gamma-beta");
     ampl = getParameterValue("BAO amplitude");
     scale = getParameterValue("BAO alpha-iso");
     scale_parallel = getParameterValue("BAO alpha-parallel");
     scale_perp = getParameterValue("BAO alpha-perp");
     gamma_scale = getParameterValue("gamma-scale");
     xio = getParameterValue("BBand1 xio");
     a0 = getParameterValue("BBand1 a0");
     a1 = getParameterValue("BBand1 a1");
     a2 = getParameterValue("BBand1 a2");
  } 
    // Calculate bias(zref) from beta(zref) and bb(zref).
    double bias = bb/(1+beta);
    // Calculate redshift evolution.
    double zratio((1+z)/(1+_zref));
    double zfactor = std::pow(zratio,gamma_bias);
    double scaleFactor = std::pow(zratio,gamma_scale);
    scale *= scaleFactor;
    scale_parallel *= scaleFactor;
    scale_perp *= scaleFactor;
    beta *= std::pow(zratio,gamma_beta);
    // Build a model with xi(ell=0,2,4) = c(ell).
    cosmo::RsdCorrelationFunction bband2Model(
        likely::createFunctionPtr(BBand2Ptr(new BBand2(
            getParameterValue("BBand2 mono const"),
            getParameterValue("BBand2 mono 1/r"),
            getParameterValue("BBand2 mono 1/(r*r)")))),
        likely::createFunctionPtr(BBand2Ptr(new BBand2(
            getParameterValue("BBand2 quad const"),
            getParameterValue("BBand2 quad 1/r"),
            getParameterValue("BBand2 quad 1/(r*r)")))),
        likely::createFunctionPtr(BBand2Ptr(new BBand2(
            getParameterValue("BBand2 hexa const"),
            getParameterValue("BBand2 hexa 1/r"),
            getParameterValue("BBand2 hexa 1/(r*r)")))));
    // Apply redshift-space distortion to each model component.
    _fid->setDistortion(beta);
    _nw->setDistortion(beta);
    _bbc->setDistortion(beta);
    _bb1->setDistortion(beta);
    _bb2->setDistortion(beta);
    bband2Model.setDistortion(beta);
    // Calculate the peak contribution with scaled radius.
    double cosmoxi(0);
    if(ampl != 0) {
        double rPeak, muPeak;
        if(_anisotropic) {
            double ap1(scale_parallel);
            double bp1(scale_perp);
            double musq(mu*mu);
            // Exact (r,mu) transformation
            double rscale = std::sqrt(ap1*ap1*musq + (1-musq)*bp1*bp1);
            rPeak = r*rscale;
            muPeak = mu*ap1/rscale;
            // Linear approximation, equivalent to multipole model below
            /*
            rPeak = r*(1 + (ap1-1)*musq + (bp1-1)*(1-musq));
            muPeak = mu*(1 + (ap1-bp1)*(1-musq));
            */
        }
        else {
	        rPeak = r*scale;
            muPeak = mu;
        }
        double fid((*_fid)(rPeak,muPeak)), nw((*_nw)(rPeak,muPeak));
	cosmoxi = nw+ampl*(fid-nw);
    } else cosmoxi=(*_nw)(r,mu);

    // Calculate the additional broadband contributions with no radius scaling.
    double bband1(0);
    if(xio != 0) bband1 += xio*(*_bbc)(r,mu);
    if(a0 != 0) bband1 += a0*cosmoxi;
    if(a1 != 0) bband1 += a1*(*_bb1)(r,mu);
    if(a2 != 0) bband1 += a2*(*_bb2)(r,mu);
    double bband2 = bband2Model(r,mu);
    // Combine the peak and broadband components, with bias and redshift evolution.
    double bband3(_bband3(this, r,mu,z, anyChanged));

    // cosmo::RsdCorrelationFunction bbandXModel(
    //     likely::createFunctionPtr(BBand2Ptr(new BBand2(
    //         getParameterValue("BBand3 r0 mu0 z0"),
    //         getParameterValue("BBand3 r1 mu0 z0"),
    //         getParameterValue("BBand3 r2 mu0 z0")))),
    //     likely::createFunctionPtr(BBand2Ptr(new BBand2(
    // 						       0,0,0))),
    //     likely::createFunctionPtr(BBand2Ptr(new BBand2(
    // 						       0,0,0))));

    // std::cout <<"debg:" << bbandXModel(r,mu) << " " <<bband3<<std::endl;
    
    return bias*bias*zfactor*(cosmoxi *(1+bband2) + bband1 + bband3);
   
}

double local::BaoCorrelationModel::_evaluate(double r, cosmo::Multipole multipole, double z,
bool anyChanged) const {
  static double beta, bb, gamma_bias, gamma_beta, ampl, scale, scale_parallel, scale_perp, gamma_scale,
    xio, a0, a1, a2;
  
  if (anyChanged) {
    std::cout << "T";
    beta = getParameterValue("beta");
     bb = getParameterValue("(1+beta)*bias");
     gamma_bias = getParameterValue("gamma-bias");
     gamma_beta = getParameterValue("gamma-beta");
     ampl = getParameterValue("BAO amplitude");
     scale = getParameterValue("BAO alpha-iso");
     scale_parallel = getParameterValue("BAO alpha-parallel");
     scale_perp = getParameterValue("BAO alpha-perp");
     gamma_scale = getParameterValue("gamma-scale");
     xio = getParameterValue("BBand1 xio");
     a0 = getParameterValue("BBand1 a0");
     a1 = getParameterValue("BBand1 a1");
     a2 = getParameterValue("BBand1 a2");
  } else std::cout <<"F";
  
    // Calculate bias(zref) from beta(zref) and bb(zref).
    double bias = bb/(1+beta);
    // Calculate redshift evolution.
    double zratio((1+z)/(1+_zref));
    double zfactor = std::pow(zratio,gamma_bias);
    double scaleFactor = std::pow(zratio,gamma_scale);
    scale *= scaleFactor;
    scale_parallel *= scaleFactor;
    scale_perp *= scaleFactor;
    beta *= std::pow(zratio,gamma_beta);
    // Calculate the redshift-space distortion scale factor for this multipole.
    double rsdScale, bband2, rsq(r*r);
    if(multipole == cosmo::Hexadecapole) {
        rsdScale = (8./35.)*beta*beta;
        bband2 = getParameterValue("BBand2 hexa const") + getParameterValue("BBand2 hexa 1/r")/r
            + getParameterValue("BBand2 hexa 1/(r*r)")/(r*r);
    }
    else if(multipole == cosmo::Quadrupole) {
        rsdScale = 4*beta*((1./3.) + beta/7.);
        bband2 = getParameterValue("BBand2 quad const") + getParameterValue("BBand2 quad 1/r")/r
            + getParameterValue("BBand2 quad 1/(r*r)")/(r*r);
    }
    else {
        rsdScale = 1 + beta*((2./3.) + beta/5.);
        bband2 = getParameterValue("BBand2 mono const") + getParameterValue("BBand2 mono 1/r")/r
            + getParameterValue("BBand2 mono 1/(r*r)")/(r*r);
    }
    // Calculate the peak contribution with scaled radius.
    double peak(0);
    if(ampl != 0) {
        double fid, nw;
        if(_anisotropic) {
            double fid0 = (*_fid)(r,cosmo::Monopole), fid2 = (*_fid)(r,cosmo::Quadrupole), fid4 = (*_fid)(r,cosmo::Hexadecapole);
            double nw0 = (*_nw)(r,cosmo::Monopole), nw2 = (*_nw)(r,cosmo::Quadrupole), nw4 = (*_nw)(r,cosmo::Hexadecapole);

            double dr = 1;
            double fid0p = ((*_fid)(r+dr,cosmo::Monopole) - (*_fid)(r-dr,cosmo::Monopole))/(2*dr);
            double fid2p = ((*_fid)(r+dr,cosmo::Quadrupole) - (*_fid)(r-dr,cosmo::Quadrupole))/(2*dr);
            double fid4p = ((*_fid)(r+dr,cosmo::Hexadecapole) - (*_fid)(r-dr,cosmo::Hexadecapole))/(2*dr);
            double nw0p = ((*_nw)(r+dr,cosmo::Monopole) - (*_nw)(r-dr,cosmo::Monopole))/(2*dr);
            double nw2p = ((*_nw)(r+dr,cosmo::Quadrupole) - (*_nw)(r-dr,cosmo::Quadrupole))/(2*dr);
            double nw4p = ((*_nw)(r+dr,cosmo::Hexadecapole) - (*_nw)(r-dr,cosmo::Hexadecapole))/(2*dr);
        
            double a(scale_parallel-1);
            double b(scale_perp-1);

            // !! TODO: add hexadecapole terms below
            switch(multipole) {
            case cosmo::Monopole:
                fid = fid0 + (2./5.)*(a-b)*fid2 + (a+2*b)/3*r*fid0p + (2./15.)*(a-b)*r*fid2p;
                nw = nw0 + (2./5.)*(a-b)*nw2 + (a+2*b)/3*r*nw0p + (2./15.)*(a-b)*r*nw2p;
                break;
            case cosmo::Quadrupole:
                fid = fid2*(1 + (2./7.)*(a-b)) + (2./3.)*(a-b)*r*fid0p + (11*a+10*b)/21*r*fid2p;
                nw = nw2*(1 + (2./7.)*(a-b)) + (2./3.)*(a-b)*r*nw0p + (11*a+10*b)/21*r*nw2p;
                break;
            case cosmo::Hexadecapole:
                //throw RuntimeError("BaoCorrelationModel: anisotropic hexadecapole not implemented yet.");
                fid = nw = 0;
                break;
            }
        }
        else {
            fid = (*_fid)(r*scale,multipole);
            nw = (*_nw)(r*scale,multipole);
        }
        peak = ampl*(fid-nw);
    }
    // Calculate the additional broadband contribution with no radius scaling.
    double bband1(0);
    if(xio != 0) bband1 += xio*(*_bbc)(r,multipole);
    if(1+a0 != 0) bband1 += (1+a0)*(*_nw)(r,multipole);
    if(a1 != 0) bband1 += a1*(*_bb1)(r,multipole);
    if(a2 != 0) bband1 += a2*(*_bb2)(r,multipole);
    // Combine the peak and broadband components, with bias and redshift evolution.

    return bias*bias*zfactor*rsdScale*(peak + bband1 + bband2);
}

void  local::BaoCorrelationModel::printToStream(std::ostream &out, std::string const &formatSpec) const {
    AbsCorrelationModel::printToStream(out,formatSpec);
    out << std::endl << "Reference redshift = " << _zref << std::endl;
    out << "Using " << (_anisotropic ? "anisotropic":"isotropic") << " BAO scales." << std::endl;
}
