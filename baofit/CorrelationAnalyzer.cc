// Created 31-May-2012 by David Kirkby (University of California, Irvine) <dkirkby@uci.edu>

#include "baofit/CorrelationAnalyzer.h"
#include "baofit/RuntimeError.h"
#include "baofit/AbsCorrelationData.h"
#include "baofit/AbsCorrelationModel.h"
#include "baofit/CorrelationFitter.h"

#include "likely/FunctionMinimum.h"
#include "likely/FitParameter.h"
#include "likely/CovarianceMatrix.h"
#include "likely/CovarianceAccumulator.h"
#include "likely/FitParameterStatistics.h"
#include "likely/Random.h"

#include "boost/smart_ptr.hpp"
#include "boost/format.hpp"
#include "boost/foreach.hpp"
#include "boost/utility.hpp"
#include "boost/math/special_functions/gamma.hpp"

#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <iterator>

namespace local = baofit;

local::CorrelationAnalyzer::CorrelationAnalyzer(std::string const &method, double rmin, double rmax,
bool verbose, bool scalarWeights)
: _method(method), _rmin(rmin), _rmax(rmax), _verbose(verbose), _resampler(scalarWeights)
{
    if(rmin >= rmax) {
        throw RuntimeError("CorrelationAnalyzer: expected rmin < rmax.");
    }
}

local::CorrelationAnalyzer::~CorrelationAnalyzer() { }

void local::CorrelationAnalyzer::setZData(double zdata) {
    if(zdata < 0) {
        throw RuntimeError("CorrelationAnalyzer: expected zdata >= 0.");        
    }
    _zdata = zdata;
}

int local::CorrelationAnalyzer::addData(AbsCorrelationDataCPtr data, int reuseCovIndex) {
    return _resampler.addObservation(
        boost::dynamic_pointer_cast<const likely::BinnedData>(data),reuseCovIndex);
}

local::AbsCorrelationDataPtr local::CorrelationAnalyzer::getCombined(bool verbose, bool finalized) const {
    AbsCorrelationDataPtr combined =
        boost::dynamic_pointer_cast<baofit::AbsCorrelationData>(_resampler.combined());
    int nbefore = combined->getNBinsWithData();
    if(finalized) combined->finalize();
    if(verbose && finalized) {
        int nafter = combined->getNBinsWithData();
        std::cout << "Combined data has " << nafter << " (" << nbefore
            << ") bins with data after (before) finalizing." << std::endl;
    }
    return combined;    
}

void local::CorrelationAnalyzer::compareEach(AbsCorrelationDataCPtr refData) const {
    // Check that the reference data is finalized.
    if(!refData->isFinalized()) {
        throw RuntimeError("CorrelationAnalyzer::compareEach: expected finalized reference data.");
    }
    if(_resampler.usesScalarWeights()) {
        std::cerr << "CorrelationAnalyzer::compareEach: not supported with scalar weights." << std::endl;
        return;
    }
    // Load a "theory" vector with the unweighed reference data.
    std::vector<double> theory;
    for(likely::BinnedData::IndexIterator iter = refData->begin(); iter != refData->end(); ++iter) {
        theory.push_back(refData->getData(*iter));
    }
    // Loop over observations.
    int nbins = refData->getNBinsWithData();
    std::cout << "   N     Prob     Chi2  input|C| final|C|" << std::endl;
    for(int obsIndex = 0; obsIndex < _resampler.getNObservations(); ++obsIndex) {
        AbsCorrelationDataPtr observation = boost::dynamic_pointer_cast<baofit::AbsCorrelationData>(
            _resampler.getObservationCopy(obsIndex));
        double logdetBefore = observation->getCovarianceMatrix()->getLogDeterminant();
        observation->finalize();
        double logdetAfter = observation->getCovarianceMatrix()->getLogDeterminant();
        // Calculate the chi-square of this observation relative to the "theory"
        double chi2 = observation->chiSquare(theory);
        double prob = 1 - boost::math::gamma_p(nbins/2.,chi2/2);
        std::cout << boost::format("%4d %.6lf %8.1lf %8.2lf %8.1lf\n")
            % obsIndex % prob % chi2 % logdetBefore % logdetAfter;
    }
}

bool local::CorrelationAnalyzer::printScaleZEff(likely::FunctionMinimumCPtr fmin, double zref,
std::string const &scaleName) const {
    likely::FitParameters params = fmin->getFitParameters();
    std::vector<std::string> pnames;
    bool onlyFloating(true);
    likely::getFitParameterNames(params,pnames,onlyFloating);
    // Is "gamma-scale" a floating parameter of this fit?
    std::vector<std::string>::iterator found = std::find(pnames.begin(),pnames.end(),"gamma-scale");
    if(found == pnames.end()) return false;
    int gammaIndex = std::distance(pnames.begin(),found);
    // Is scaleName a floating parameter of this fit?
    found = std::find(pnames.begin(),pnames.end(),scaleName);
    if(found == pnames.end()) return false;
    int scaleIndex = std::distance(pnames.begin(),found);
    // Look up the fit results for gamma,scale
    likely::Parameters pvalues = fmin->getParameters(onlyFloating);
    likely::Parameters perrors = fmin->getErrors(onlyFloating);
    double scale(pvalues[scaleIndex]), dscale(perrors[scaleIndex]);
    double gamma(pvalues[gammaIndex]), dgamma(perrors[gammaIndex]);
    // Look up the (scale,gamma) covariance.
    likely::CovarianceMatrixCPtr cov = fmin->getCovariance();
    if(!cov) return false;
    double rho(cov->getCovariance(scaleIndex,gammaIndex)/(dscale*dgamma));
    // Calculate zeff
    double a = dscale/(scale*dgamma), b = 1/(2*gamma);
    double logz = -b - a*rho + std::sqrt(b*b - a*a*(1-rho*rho));
    double zEff = std::exp(logz)*(1 + zref) - 1;
    // Calculate the scale at zref.
    double ratio = (1+zEff)/(1+zref);
    double evol = std::pow(ratio,gamma);
    double scaleEff = scale*evol;
    // Calculate the scale error at zref.
    double logRatio = std::log(ratio);
    double tmp = scale*dgamma*logRatio;
    double dscaleEff = evol*std::sqrt(dscale*dscale + 2*rho*scale*dscale*dgamma*logRatio + tmp*tmp);
    // Print results.
    std::vector<double> errors(1,dscaleEff);
    std::cout << boost::format("%18s") % scaleName << "(zeff = " << boost::format("%.3f") % zEff << ") = "
        << likely::roundValueWithError(scaleEff,errors) << std::endl;
    return true;
}

likely::FunctionMinimumPtr local::CorrelationAnalyzer::fitSample(
AbsCorrelationDataCPtr sample, std::string const &config) const {
    CorrelationFitter fitter(sample,_model);
    likely::FunctionMinimumPtr fmin = fitter.fit(_method,config);
    if(_verbose) {
        double chisq = 2*fmin->getMinValue();
        int nbins = sample->getNBinsWithData();
        int npar = fmin->getNParameters(true);
        double prob = 1 - boost::math::gamma_p((nbins-npar)/2.,chisq/2);
        std::cout << std::endl << "Fit results: chiSquare / dof = " << chisq << " / ("
            << nbins << '-' << npar << "), prob = " << prob << ", log(det(Covariance)) = "
            << sample->getCovarianceMatrix()->getLogDeterminant() << std::endl << std::endl;
        fmin->printToStream(std::cout);
    }
    return fmin;
}



void local::CorrelationAnalyzer::doScanAnalysis (AbsCorrelationDataCPtr sample,likely::FunctionMinimumPtr fmin, std::string scan1, double scan1min,
		     double scan1max, double scan1step, std::string scan2, double scan2min, double scan2max,
		     double scan2step, std::string saveName) const {

  
  //lets make our own copy of model an update its parameters based on fmin
  boost::format cfg("value[%s]=%g;");
  boost::format cfgfix("fix[%s]=%g;");
  std::string bconfig;
		    
  std::ofstream sstream;
  sstream.open(saveName.c_str());
  
  AbsCorrelationModelPtr model(_model);

  if (scan2.size()==0) {
    // we want exactly one iteration in that loop so
    scan2min=0.0;
    scan2max=1.0;
    scan2step=2.0;
  }

  // Careful scan tries more initial positions to prevent minimizer induced crap
  int pcomax(2);
  if (scan1[0]=='*') {
    scan1.erase(0,1);
    std::cout << "Doing careful scan!" << std::endl;
    pcomax=10;
  }
  

  likely::FunctionMinimumPtr lastmin (fmin);
  likely::Random R;
  for (double scan1val=scan1min; scan1val<scan1max; scan1val+=scan1step) {
      for (double scan2val=scan2min; scan2val<scan2max; scan2val+=scan2step) {

	// use last guys realizations, just to check if this works better.
	{
	  double chisq=1e30;
	  for (int pco=0; pco<pcomax; pco++) {
	    likely::FunctionMinimumPtr prior;
	    if (pco%2==0) prior=fmin;
	    else prior=lastmin;
	    // else if (pco==2) {
	    //   //lets start with no prior
	    //   //prior=NULL;
	    // }
	  
	    bconfig="";
	    if (pco<5) {
	      double pfact(1.0);
	      if (pco>2) pfact=10.0+pco*pco;
	      likely::FitParameters params(prior->getFitParameters());
	      BOOST_FOREACH(likely::FitParameter p, params) {
		double newval(p.getValue() + pfact*R.getNormal()*p.getError());
		std::cout << "setting "<<p.getName() << " to " <<newval <<std::endl;
		model->setParameterValue(p.getName(), newval);
		bconfig+=boost::str( cfg % p.getName() % p.getValue());
	      }
	    }
	  
	    std::string tconfig (bconfig);
	    model->setParameterValue(scan1, scan1val);
	    tconfig+=boost::str(cfgfix % scan1 % scan1val);
	    
	    if (scan2.size()>0) {
	      model->setParameterValue(scan2, scan2val);
	      tconfig+=boost::str(cfgfix % scan2 % scan2val);
	    }
	
	    CorrelationFitter fitter(sample,model);

	    std::cout <<"tconfig="<<tconfig<<std::endl;
	    
	    likely::FunctionMinimumPtr cfmin =  fitter.fit(_method,tconfig);
	    cfmin->printToStream(std::cout);
	    double curchisq = 2 * cfmin->getMinValue();
	    std::cout << "trial " << pco << " " <<curchisq;
	    if (curchisq<chisq) {
	      lastmin = cfmin;
	      chisq=curchisq;
	      std::cout <<" * ";
	    }
	    std::cout << std::endl;
	  }
	  
	  sstream << scan1val << " " <<scan2val << " " <<chisq <<" ";
	  { 
	    likely::FitParameters params(lastmin->getFitParameters());
	    BOOST_FOREACH(likely::FitParameter p, params) {
	      sstream << p.getValue() << " ";
	    }
	  }
	  sstream << std::endl;
	}
      }
  }
  sstream.close();
}



namespace baofit {
    class CorrelationAnalyzer::AbsSampler {
    public:
        virtual AbsCorrelationDataCPtr nextSample() = 0;
    };
    class CorrelationAnalyzer::JackknifeSampler : public CorrelationAnalyzer::AbsSampler {
    public:
        JackknifeSampler(int ndrop, likely::BinnedDataResampler const &resampler)
        : _ndrop(ndrop), _seqno(0), _resampler(resampler) { }
        virtual AbsCorrelationDataCPtr nextSample() {
            AbsCorrelationDataPtr sample = boost::dynamic_pointer_cast<baofit::AbsCorrelationData>(
                _resampler.jackknife(_ndrop,_seqno++));
            if(sample) sample->finalize();
            return sample;
        }
    private:
        int _ndrop, _seqno;
        likely::BinnedDataResampler const &_resampler;
    };
    class CorrelationAnalyzer::BootstrapSampler : public CorrelationAnalyzer::AbsSampler {
    public:
        BootstrapSampler(int trials, int size, bool fix, likely::BinnedDataResampler const &resampler)
        : _trials(trials), _size(size), _fix(fix), _resampler(resampler), _next(0) { }
        virtual AbsCorrelationDataCPtr nextSample() {
            AbsCorrelationDataPtr sample;
            if(++_next <= _trials) {
                sample = boost::dynamic_pointer_cast<baofit::AbsCorrelationData>(
                    _resampler.bootstrap(_size,_fix));
                sample->finalize();
            }
            return sample;
        }
    private:
        int _trials, _size, _next;
        bool _fix;
        likely::BinnedDataResampler const &_resampler;
    };
    class CorrelationAnalyzer::EachSampler : public CorrelationAnalyzer::AbsSampler {
    public:
        EachSampler(likely::BinnedDataResampler const &resampler)
        : _resampler(resampler), _next(0) { }
        virtual AbsCorrelationDataCPtr nextSample() {
            AbsCorrelationDataPtr sample;
            if(++_next <= _resampler.getNObservations()) {
                sample = boost::dynamic_pointer_cast<baofit::AbsCorrelationData>(
                    _resampler.getObservationCopy(_next-1));
                sample->finalize();
            }
            return sample;
        }
    private:
        int _next;
        likely::BinnedDataResampler const &_resampler;
    };
    class CorrelationAnalyzer::ToyMCSampler : public CorrelationAnalyzer::AbsSampler {
    public:
        ToyMCSampler(int ngen, AbsCorrelationDataPtr prototype, std::vector<double> truth,
        std::string const &filename)
        : _remaining(ngen), _prototype(prototype), _truth(truth), _first(true), _filename(filename) { }
        virtual AbsCorrelationDataCPtr nextSample() {
            AbsCorrelationDataPtr sample;
            if(_remaining-- > 0) {
                // Generate a noise vector sampling from the prototype's covariance.
                _prototype->getCovarianceMatrix()->sample(_noise);
                // Clone our prototype (which only copies the covariance smart pointer, not
                // the whole matrix)
                sample.reset((AbsCorrelationData*)_prototype->clone());
                // Overwrite the bin values with truth+noise
                std::vector<double>::const_iterator nextTruth(_truth.begin()), nextNoise(_noise.begin());
                for(likely::BinnedData::IndexIterator iter = _prototype->begin();
                iter != _prototype->end(); ++iter) {
                    sample->setData(*iter,(*nextTruth++) + (*nextNoise++));
                }
                // We don't finalize here because the prototype should already be finalized.
                // Save this file?
                if(_first && _filename.length() > 0) sample->saveData(_filename);
                _first = false;
            }
            return sample;
        }
    private:
        int _remaining;
        bool _first;
        std::string _filename;
        AbsCorrelationDataPtr _prototype;
        std::vector<double> _truth, _noise;
    };
}

int local::CorrelationAnalyzer::doJackknifeAnalysis(int jackknifeDrop, likely::FunctionMinimumPtr fmin,
likely::FunctionMinimumPtr fmin2, std::string const &refitConfig, std::string const &saveName,
int nsave) const {
    if(jackknifeDrop <= 0) {
        throw RuntimeError("CorrelationAnalyzer::doJackknifeAnalysis: expected jackknifeDrop > 0.");
    }
    if(getNData() <= 1) {
        throw RuntimeError("CorrelationAnalyzer::doJackknifeAnalysis: need > 1 observation.");
    }
    CorrelationAnalyzer::JackknifeSampler sampler(jackknifeDrop,_resampler);
    return doSamplingAnalysis(sampler, "Jackknife", fmin, fmin2, refitConfig, saveName, nsave);
}

int local::CorrelationAnalyzer::doBootstrapAnalysis(int bootstrapTrials, int bootstrapSize,
bool fixCovariance, likely::FunctionMinimumPtr fmin, likely::FunctionMinimumPtr fmin2,
std::string const &refitConfig, std::string const &saveName, int nsave) const {
    if(bootstrapTrials <= 0) {
        throw RuntimeError("CorrelationAnalyzer::doBootstrapAnalysis: expected bootstrapTrials > 0.");
    }
    if(bootstrapSize < 0) {
        throw RuntimeError("CorrelationAnalyzer::doBootstrapAnalysis: expected bootstrapSize >= 0.");
    }
    if(getNData() <= 1) {
        throw RuntimeError("CorrelationAnalyzer::doBootstrapAnalysis: need > 1 observation.");
    }
    if(0 == bootstrapSize) bootstrapSize = getNData();
    CorrelationAnalyzer::BootstrapSampler sampler(bootstrapTrials,bootstrapSize,fixCovariance,_resampler);
    return doSamplingAnalysis(sampler, "Bootstrap", fmin, fmin2, refitConfig, saveName, nsave);
}

int local::CorrelationAnalyzer::fitEach(likely::FunctionMinimumPtr fmin, likely::FunctionMinimumPtr fmin2,
std::string const &refitConfig, std::string const &saveName, int nsave) const {
    CorrelationAnalyzer::EachSampler sampler(_resampler);
    return doSamplingAnalysis(sampler, "Individual", fmin, fmin2, refitConfig, saveName, nsave);    
}

int local::CorrelationAnalyzer::doToyMCSampling(int ngen, std::string const &mcConfig,
std::string const &mcSaveFile, double varianceScale, likely::FunctionMinimumPtr fmin,
likely::FunctionMinimumPtr fmin2, std::string const &refitConfig,
std::string const &saveName, int nsave) const {
    if(ngen <= 0) {
        throw RuntimeError("CorrelationAnalyzer::doMCSampling: expected ngen > 0.");
    }
    // Get a copy of our (unfinalized!) combined dataset to use as a prototype.
    AbsCorrelationDataPtr prototype = getCombined(false,false);
    if(!prototype->hasCovariance()) {
        throw RuntimeError("CorrelationAnalyzer::doMCSampling: no covariance available.");
    }
    // Scale the prototype covariance, if requested.
    if(varianceScale <= 0) {
        throw RuntimeError("CorrelationAnalyzer::doMCSampling: expected varianceScale > 0.");
    }
    if(varianceScale != 1) {
        likely::CovarianceMatrixPtr covariance(new likely::CovarianceMatrix(*prototype->getCovarianceMatrix()));
        covariance->applyScaleFactor(varianceScale);
        prototype->setCovarianceMatrix(covariance);
    }
    // Finalize now, after any covariance scaling.
    prototype->finalize();
    // Configure the fit parameters for generating the truth vector.
    likely::FitParameters parameters = fmin->getFitParameters();
    likely::modifyFitParameters(parameters,mcConfig);
    std::vector<double> pvalues;
    likely::getFitParameterValues(parameters,pvalues);
    // Build a fitter to calculate the truth vector.
    CorrelationFitter fitter(prototype, _model);
    // Calculate the truth vector.
    std::vector<double> truth;
    fitter.getPrediction(pvalues,truth);
    // Build the sampler for this analysis.
    CorrelationAnalyzer::ToyMCSampler sampler(ngen,prototype,truth,mcSaveFile);
    return doSamplingAnalysis(sampler, "MonteCarlo", fmin, fmin2, refitConfig, saveName, nsave);
}

namespace baofit {
    // An implementation class to save the results of a sampling analysis in a standard format.
    class SamplingOutput : public boost::noncopyable {
    public:
        SamplingOutput(likely::FunctionMinimumCPtr fmin, likely::FunctionMinimumCPtr fmin2,
        std::string const &saveName, int nsave, CorrelationAnalyzer const &parent)
        : _nsave(nsave), _parent(parent) {
            if(0 < saveName.length()) {
                _save.reset(new std::ofstream(saveName.c_str()));
                // Print a header consisting of the number of parameters, the number of dump points,
                // and the number of fits (1 = no-refit, 2 = with refit)
                *_save << fmin->getNParameters() << ' ' << _nsave << ' ' << (fmin2 ? 2:1) << std::endl;
                // Print the errors in fmin,fmin2.
                BOOST_FOREACH(double pvalue, fmin->getErrors()) {
                    *_save << pvalue << ' ';
                }
                if(fmin2) {
                    BOOST_FOREACH(double pvalue, fmin2->getErrors()) {
                        *_save << pvalue << ' ';
                    }
                }
                *_save << std::endl;
                // The first line encodes the inputs fmin,fmin2 just like each sample below, for reference.
                BOOST_FOREACH(double pvalue, fmin->getParameters()) {
                    *_save << pvalue << ' ';
                }
                *_save << 2*fmin->getMinValue() << ' ';
                if(fmin2) {
                    BOOST_FOREACH(double pvalue, fmin2->getParameters()) {
                        *_save << pvalue << ' ';
                    }
                    *_save << 2*fmin2->getMinValue() << ' ';
                }
                if(_nsave > 0) {
                    _parent.dumpModel(*_save,fmin->getFitParameters(),_nsave,"",true);
                    if(fmin2) _parent.dumpModel(*_save,fmin2->getFitParameters(),_nsave,"",true);
                }
                *_save << std::endl;
            }            
        }
        ~SamplingOutput() {
            if(_save) _save->close();
        }
        void saveSample(likely::FitParameters parameters, double fval,
        likely::FitParameters parameters2 = likely::FitParameters(), double fval2 = 0) {
            if(!_save) return;
            // Save fit parameter values and chisq.
            likely::Parameters pvalues;
            likely::getFitParameterValues(parameters,pvalues);
            BOOST_FOREACH(double pvalue, pvalues) {
                *_save << pvalue << ' ';
            }
            // Factor of 2 converts -logL to chiSquare.
            *_save << 2*fval << ' ';
            // Save alternate fit parameter values and chisq, if any.
            if(parameters2.size() > 0) {
                likely::getFitParameterValues(parameters2,pvalues);
                BOOST_FOREACH(double pvalue, pvalues) {
                    *_save << pvalue << ' ';
                }
                *_save << 2*fval2 << ' ';
            }
            // Save best-fit model multipoles, if requested.
            if(_nsave > 0) {
                _parent.dumpModel(*_save,parameters,_nsave,"",true);
                if(parameters2.size() > 0) _parent.dumpModel(*_save,parameters2,_nsave,"",true);
            }
            *_save << std::endl;            
       }
    private:
        int _nsave;
        CorrelationAnalyzer const &_parent;
        boost::scoped_ptr<std::ofstream> _save;
    };
}

int local::CorrelationAnalyzer::doSamplingAnalysis(CorrelationAnalyzer::AbsSampler &sampler,
std::string const &method, likely::FunctionMinimumPtr fmin, likely::FunctionMinimumPtr fmin2,
std::string const &refitConfig, std::string const &saveName, int nsave) const {
    if(nsave < 0) {
        throw RuntimeError("CorrelationAnalyzer::doSamplingAnalysis: expected nsave >= 0.");
    }
    if((!fmin2 && 0 < refitConfig.size()) || !!fmin2 && 0 == refitConfig.size()) {
        throw RuntimeError("CorrelationAnalyzer::doSamplingAnalysis: inconsistent refit parameters.");
    }
    SamplingOutput output(fmin,fmin2,saveName,nsave,*this);
    baofit::AbsCorrelationDataCPtr sample;
    // Initialize the parameter value statistics accumulators we will need.
    likely::FitParameterStatisticsPtr refitStats,
        fitStats(new likely::FitParameterStatistics(fmin->getFitParameters()));
    if(fmin2) {
        refitStats.reset(new likely::FitParameterStatistics(fmin2->getFitParameters()));
    }
    int nInvalid(0);
    // Loop over samples.
    int nsamples(0);
    while(sample = sampler.nextSample()) {
        // Fit the sample.
        baofit::CorrelationFitter fitEngine(sample,_model);
        likely::FunctionMinimumPtr sampleMin = fitEngine.fit(_method);
        bool ok = (sampleMin->getStatus() == likely::FunctionMinimum::OK);
        // Refit the sample if requested and the first fit succeeded.
        likely::FunctionMinimumPtr sampleMinRefit;
        if(ok && fmin2) {
            sampleMinRefit = fitEngine.fit(_method,refitConfig);
            // Did this fit succeed also?
            if(sampleMinRefit->getStatus() != likely::FunctionMinimum::OK) ok = false;
        }
        if(ok) {
            // Accumulate the fit results if the fit was successful.
            bool onlyFloating(true);
            fitStats->update(sampleMin->getParameters(onlyFloating),sampleMin->getMinValue());
            if(refitStats) refitStats->update(sampleMinRefit->getParameters(onlyFloating),sampleMinRefit->getMinValue());
            // Save the fit results, if requested.
            output.saveSample(sampleMin->getFitParameters(),sampleMin->getMinValue(),
                sampleMinRefit ? sampleMinRefit->getFitParameters() : likely::FitParameters(),
                sampleMinRefit ? sampleMinRefit->getMinValue() : 0);
        }
        else {
            nInvalid++;
        }
        // Print periodic updates while the analysis is running.
        nsamples++;
        if(_verbose && (0 == nsamples%10)) {
            std::cout << "Analyzed " << nsamples << " samples (" << nInvalid << " invalid)" << std::endl;
        }
    }
    // Print a summary of the analysis results.
    std::cout << std::endl << "== " << method << " Fit Results:" << std::endl;
    fitStats->printToStream(std::cout);
    if(refitStats) {
        std::cout << std::endl << "== " << method << " Re-Fit Results:" << std::endl;
        refitStats->printToStream(std::cout);        
    }
    return nInvalid;
}

void local::CorrelationAnalyzer::generateMarkovChain(int nchain, int interval, likely::FunctionMinimumCPtr fmin,
std::string const &saveName, int nsave) const {
    if(nchain <= 0) {
        throw RuntimeError("CorrelationAnalyzer::generateMarkovChain: expected nchain > 0.");
    }
    if(interval < 0) {
        throw RuntimeError("CorrelationAnalyzer::generateMarkovChain: expected interval >= 0.");        
    }
    // Create a fitter to calculate the likelihood.
    AbsCorrelationDataCPtr combined = getCombined(true);
    CorrelationFitter fitter(combined,_model);
    // Generate the MCMC chains, saving the results in a vector.
    std::vector<double> samples;
    fitter.mcmc(fmin, nchain, interval, samples);
    // Output the results and accumulate statistics.
    SamplingOutput output(fmin,likely::FunctionMinimumCPtr(),saveName,nsave,*this);
    likely::FitParameters parameters(fmin->getFitParameters());
    likely::FitParameterStatistics paramStats(parameters);
    int npar = parameters.size();
    bool onlyFloating(true);
    std::vector<double>::const_iterator iter(samples.begin()), next;
    for(int i = 0; i < nchain; ++i) {
        // Copy the parameter values for this MCMC sample into pvalues.
        next = iter+npar;
        likely::Parameters pvalues(iter,next);
        // Get the chi-square corresponding to these parameter values.
        double fval = *next++;
        iter = next;
        // Output this sample.
        likely::setFitParameterValues(parameters,pvalues);
        output.saveSample(parameters,fval);
        // Accumulate stats on floating parameters.
        likely::Parameters pfloating;
        likely::getFitParameterValues(parameters,pfloating,onlyFloating);
        paramStats.update(pfloating,fval);
    }
    paramStats.printToStream(std::cout);
}

void local::CorrelationAnalyzer::dumpResiduals(std::ostream &out, likely::FunctionMinimumPtr fmin,
std::string const &script, bool dumpGradients) const {
    if(getNData() == 0) {
        throw RuntimeError("CorrelationAnalyzer::dumpResiduals: no observations have been added.");
    }
    AbsCorrelationDataCPtr combined = getCombined();
    AbsCorrelationData::TransverseBinningType type = combined->getTransverseBinningType();
    // Get a copy of the the parameters at this minimum.
    likely::FitParameters parameters(fmin->getFitParameters());
    // Should check that these parameters are "congruent" (have same names?) with model params.
    // assert(parameters.isCongruent(model...))
    // Modify the parameters using the specified script, if any.
    if(0 < script.length()) likely::modifyFitParameters(parameters, script);
    // Get the parameter values (floating + fixed)
    likely::Parameters parameterValues,parameterErrors;
    likely::getFitParameterValues(parameters,parameterValues);
    int npar = fmin->getNParameters();
    if(dumpGradients) likely::getFitParameterErrors(parameters,parameterErrors);
    // Loop over 3D bins in the combined dataset.
    std::vector<double> centers;
    for(likely::BinnedData::IndexIterator iter = combined->begin(); iter != combined->end(); ++iter) {
        int index(*iter);
        out << index;
        combined->getBinCenters(index,centers);
        BOOST_FOREACH(double center, centers) {
            out << ' ' << center;
        }
        double data = combined->getData(index);
        double error = combined->hasCovariance() ? std::sqrt(combined->getCovariance(index,index)) : 0;
        double z = combined->getRedshift(index);
        double r = combined->getRadius(index);
        double predicted,mu;
        cosmo::Multipole multipole;
        if(type == AbsCorrelationData::Coordinate) {
            mu = combined->getCosAngle(index);
            predicted = _model->evaluate(r,mu,z,parameterValues);
            out  << ' ' << r << ' ' << mu << ' ' << z;
        }
        else {
            multipole = combined->getMultipole(index);
            predicted = _model->evaluate(r,multipole,z,parameterValues);
            out  << ' ' << r << ' ' << (int)multipole << ' ' << z;
        }
        out << ' ' << predicted << ' ' << data << ' ' << error;
        if(dumpGradients) {
            for(int ipar = 0; ipar < npar; ++ipar) {
                double gradient(0), dpar(0.1*parameterErrors[ipar]);
                if(dpar > 0) {
                    double p0 = parameterValues[ipar];
                    parameterValues[ipar] = p0 + 0.5*dpar;
                    double predHi = (type == AbsCorrelationData::Coordinate) ?
                        _model->evaluate(r,mu,z,parameterValues) :
                        _model->evaluate(r,multipole,z,parameterValues);
                    parameterValues[ipar] = p0 - 0.5*dpar;
                    double predLo = (type == AbsCorrelationData::Coordinate) ?
                        _model->evaluate(r,mu,z,parameterValues) :
                        _model->evaluate(r,multipole,z,parameterValues);
                    gradient = (predHi - predLo)/dpar;
                    parameterValues[ipar] = p0;
                }
                out << ' ' << gradient;
            }
        }
        out << std::endl;
    }
}

void local::CorrelationAnalyzer::dumpModel(std::ostream &out, likely::FitParameters parameters,
int ndump, std::string const &script, bool oneLine) const {
    if(ndump <= 1) {
        throw RuntimeError("CorrelationAnalyzer::dump: expected ndump > 1.");
    }
    // Should check that parameters are "congruent" (have same names?) with model params.
    // assert(parameters.isCongruent(model...))
    // Modify the parameters using the specified script, if any.
    if(0 < script.length()) likely::modifyFitParameters(parameters, script);
    // Get the parameter values (floating + fixed)
    likely::Parameters parameterValues;
    likely::getFitParameterValues(parameters,parameterValues);
    // Loop over the specified radial grid.
    double dr((_rmax - _rmin)/(ndump-1));
    for(int rIndex = 0; rIndex < ndump; ++rIndex) {
        double rval(_rmin + dr*rIndex);
        double mono = _model->evaluate(rval,cosmo::Monopole,_zdata,parameterValues);
        double quad = _model->evaluate(rval,cosmo::Quadrupole,_zdata,parameterValues);
        double hexa = _model->evaluate(rval,cosmo::Hexadecapole,_zdata,parameterValues);
        // Output the model predictions for this radius in the requested format.
        if(!oneLine) out << rval;
        out << ' ' << mono << ' ' << quad << ' ' << hexa;
        if(!oneLine) out << std::endl;
    }
}

void local::CorrelationAnalyzer::getDecorrelatedWeights(AbsCorrelationDataCPtr data,
likely::Parameters const &params, std::vector<double> &dweights) const {
    CorrelationFitter fitter(data, _model);
    std::vector<double> prediction;
    fitter.getPrediction(params,prediction);
    data->getDecorrelatedWeights(prediction,dweights);
}

namespace baofit {
    bool accumulationCallback(likely::CovarianceAccumulatorCPtr accumulator) {
        std::cout << "accumulated " << accumulator->count() << " samples." << std::endl;
        return true;
    }
}

likely::CovarianceMatrixPtr
local::CorrelationAnalyzer::estimateCombinedCovariance(int nSamples, std::string const &filename) const {
    // Print a message every 10 samples during accumulation.
    likely::CovarianceAccumulatorPtr accumulator = _resampler.estimateCombinedCovariance(nSamples,
        likely::BinnedDataResampler::AccumulationCallback(accumulationCallback),10);
    // Save the work in progress if a filename was specified.
    if(filename.length() > 0) {
        std::cout << "saving work in progress to " << filename << std::endl;
        std::ofstream out(filename.c_str());
        accumulator->dump(out);
        out.close();
    }
    // Return the estimate covariance (which might not be positive definite)
    return accumulator->getCovariance();
}
