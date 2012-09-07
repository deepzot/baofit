// Created 07-Apr-2012 by David Kirkby (University of California, Irvine) <dkirkby@uci.edu>

#include "baofit/AbsCorrelationModel.h"
#include "baofit/RuntimeError.h"

#include <cmath>

namespace local = baofit;

local::AbsCorrelationModel::AbsCorrelationModel(std::string const &name)
: FitModel(name), _indexBase(-1)
{ }

local::AbsCorrelationModel::~AbsCorrelationModel() { }

double local::AbsCorrelationModel::evaluate(double r, double mu, double z,
likely::Parameters const &params) {
    bool anyChanged = updateParameterValues(params);
    double result = _evaluate(r,mu,z,anyChanged);
    resetParameterValuesChanged();
    return result;
}

double local::AbsCorrelationModel::evaluate(double r, cosmo::Multipole multipole, double z,
likely::Parameters const &params) {
    bool anyChanged = updateParameterValues(params);
    double result = _evaluate(r,multipole,z,anyChanged);
    resetParameterValuesChanged();
    return result;
}

int local::AbsCorrelationModel::_defineLinearBiasParameters(double zref) {
    if(_indexBase >= 0) throw RuntimeError("AbsCorrelationModel: linear bias parameters already defined.");
    if(_zref < 0) throw RuntimeError("AbsCorrelationModel: expected zref >= 0.");
    _zref = zref;
    // Linear bias parameters
    _indexBase = defineParameter("beta",1.4,0.1);
    defineParameter("(1+beta)*bias",-0.336,0.03);
    // Redshift evolution parameters
    defineParameter("gamma-bias",3.8,0.3);
    return defineParameter("gamma-beta",0,0.1);
}

double local::AbsCorrelationModel::_redshiftEvolution(double p0, double gamma, double z) const {
    return p0*std::pow((1+z)/(1+_zref),gamma);
}

double local::AbsCorrelationModel::_getNormFactor(cosmo::Multipole multipole, double z) const {
    if(_indexBase < 0) throw RuntimeError("AbsCorrelationModel: no linear bias parameters defined.");
    // Lookup the linear bias parameters.
    double beta = getParameterValue("beta");
    double bb = getParameterValue("(1+beta)*bias");
    // Calculate bias from beta and bb.
    double bias = bb/(1+beta);
    double biasSq = bias*bias;
    // Calculate redshift evolution of bias and beta.
    biasSq = _redshiftEvolution(biasSq,getParameterValue("gamma-bias"),z);
    beta = _redshiftEvolution(beta,getParameterValue("gamma-beta"),z);
    // Calculate the linear bias normalization factors.
    _normFactor0 = biasSq*(1 + beta*(2./3. + (1./5.)*beta));
    _normFactor2 = biasSq*beta*(4./3. + (4./7.)*beta);
    _normFactor4 = biasSq*beta*beta*(8./35.);
    // Return the requested normalization factor.
    switch(multipole) {
    case cosmo::Hexadecapole:
        return _normFactor4;
    case cosmo::Quadrupole:
        return _normFactor2;
    default:
        return _normFactor0;
    }
}
