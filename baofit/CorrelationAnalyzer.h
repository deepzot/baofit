// Created 31-May-2012 by David Kirkby (University of California, Irvine) <dkirkby@uci.edu>

#ifndef BAOFIT_CORRELATION_ANALYZER
#define BAOFIT_CORRELATION_ANALYZER

#include "baofit/types.h"

#include "cosmo/types.h"

#include "likely/BinnedDataResampler.h"
#include "likely/FitParameter.h"

#include <iosfwd>

namespace baofit {
    // Accumulates correlation data and manages its analysis.
	class CorrelationAnalyzer {
	public:
	    // Creates a new analyzer using the specified minimization method.
	    // The range [rmin,rmax] will be used for dumping any model multipoles.
		CorrelationAnalyzer(std::string const &method, double rmin, double rmax, bool verbose = true);
		virtual ~CorrelationAnalyzer();
		// Set the verbose level during analysis.
        void setVerbose(bool value);
		// Adds a new correlation data object to this analyzer.
        void addData(AbsCorrelationDataCPtr data);
        // Returns the number of data objects added to this analyzer.
        int getNData() const;
        // Sets the correlation model to use.
        void setModel(AbsCorrelationModelPtr model);
        // Sets the effective data redshift to use for dumping model predictions.
        void setZData(double zdata);
        // Returns a shared pointer to the combined correlation data added to this
        // analyzer, after it has been finalized. If verbose, prints out the number
        // of bins with data before and after finalizing the data.
        AbsCorrelationDataPtr getCombined(bool verbose = false) const;
        // Fits the combined correlation data aadded to this analyzer and returns
        // the estimated function minimum. Use the optional config script to modify
        // the initial parameter configuration used for the fit (any changes do not
        // propagate back to the model or modify subsequent fits).
        likely::FunctionMinimumPtr fitCombined(std::string const &config = "") const;        
        // Performs a bootstrap analysis and returns the number of fits to bootstrap
        // samples that failed. Specify a non-zero bootstrapSize to generate trials with
        // a number of observations different than getNData(). Specify a refitConfig script
        // to fit each bootstrap sample twice: first with the default model config, then
        // with the refit config script applied. In this case, a trial is only considered
        // successful if both fits succeed. If a saveFile is specified, the parameter
        // values and chi-square value from each fit will be saved to the specified filename.
        // If nsave > 0, then the best-fit model multipoles will be appended to each line
        // using the oneLine option to dumpModel(). In case of refits, the output from both
        // fits will be concatenated on each line. Setting fixCovariance to false means that
        // fits will use a covariance matrix that does not correctly account for double
        // counting. See likely::BinnedDataResampler::bootstrap for details.
	// it ptname is specified, it will calculate the covariance matrix of the resampled
	// *data* realizations and save it to the corresponding file.
        int doBootstrapAnalysis(int bootstrapTrials, int bootstrapSize, bool fixCovariance,
            likely::FunctionMinimumPtr fmin,
            likely::FunctionMinimumPtr fmin2 = likely::FunctionMinimumPtr(),
            std::string const &refitConfig = "", std::string const &saveName = "",
				std::string const &ptname = "" , int nsave = 0) const;
        // Performs a jackknife analysis and returns the number of fits that failed. Set
        // jackknifeDrop to the number of observations to drop from each sample. See
        // doBootstrapAnalysis for a description of the other parameters.
        int doJackknifeAnalysis(int jackknifeDrop, likely::FunctionMinimumPtr fmin, 
            likely::FunctionMinimumPtr fmin2 = likely::FunctionMinimumPtr(),
            std::string const &refitConfig = "", std::string const &saveName = "", int nsave = 0) const;
        // Performs a Markov-chain sampling of the likelihood function for the combined data with
        // the current model, using the specified function minimum to initialize the sampling.
        // Saves nchain samples, using only one per interval trials. See doBootstrapAnalysis for a
        // description of the other parameters.
        void generateMarkovChain(int nchain, int interval, likely::FunctionMinimumCPtr fmin,
            std::string const &saveName = "", int nsave = 0) const;
        // Fits each observation separately and returns the number of fits that failed.
        // See doBootstrapAnalysis for a description of the other parameters.
        int fitEach(likely::FunctionMinimumPtr fmin,
            likely::FunctionMinimumPtr fmin2 = likely::FunctionMinimumPtr(),
            std::string const &refitConfig = "", std::string const &saveName = "", int nsave = 0) const;
        // Generates and fits Monte Carlo samples and returns the number of fits that failed.
        // Samples are generated by calculating the truth corresponding to the best-fit input
        // parameters in fmin and adding noise sampled from the combined dataset covariance matrix.
        // Each sample is fit using the combined dataset covariance matrix. See doBootstrapAnalysis
        // for a description of the other parameters.
        int doMCSampling(int ngen, std::string const &mcConfig,
            likely::FunctionMinimumPtr fmin, likely::FunctionMinimumPtr fmin2,
            std::string const &refitConfig, std::string const &saveName, int nsave) const;
        // Dumps the data, prediction, and diagonal error for each bin of the combined
        // data set to the specified output stream. The fit result is assumed to correspond
        // to model that is currently associated with this analyzer. Use the optional script
        // to modify the parameters used in the model. By default, the gradient of each
        // bin with respect to each floating parameter is append to each output row, unless
        // dumpGradients = false.
        void dumpResiduals(std::ostream &out, likely::FunctionMinimumPtr fmin,
            std::string const &script = "", bool dumpGradients = true) const;
        // Dumps the model predictions for the specified fit parameters to the specified
        // output stream. The input parameters are assumed to correspond to the model that is
        // currently associated with this analyzer. Use the optional script to modify
        // the parameters that will be used to evaluate the model. By default, values are output
        // as "rval mono quad hexa" on separate lines. With oneLine = true, values of
        // "mono quad hexa" are concatenated onto a single line.
        void dumpModel(std::ostream &out, likely::FitParameters parameters,
            int ndump, std::string const &script = "", bool oneLine = false) const;
        // Fills the vector provided with the decorrelated weights of the specified data using
        // the specified parameter values.
        void getDecorrelatedWeights(AbsCorrelationDataCPtr data, likely::Parameters const &params,
            std::vector<double> &dweights) const;
        // Calculates and prints the redshift where the error on the parameter named scaleName
        // has a minimum, assuming that it evolves according to a parameter named "gamma-alpha".
        // Returns true if successful, or false unless both scaleName and "gamma-alpha" are
        // floating parameters of the specified function minimum. Uses the specified zref to
        // calculate the redshift evolution of the scale and its error.
        bool printScaleZEff(likely::FunctionMinimumCPtr fmin, double zref, std::string const &scaleName) const;
	private:
        std::string _method;
        double _rmin, _rmax, _zdata;
        bool _verbose;
        likely::BinnedDataResampler _resampler;
        AbsCorrelationModelPtr _model;
        
        class AbsSampler;
        class JackknifeSampler;
        class BootstrapSampler;
        class EachSampler;
        class MCSampler;
        int doSamplingAnalysis(AbsSampler &sampler, std::string const &method,
            likely::FunctionMinimumPtr fmin, likely::FunctionMinimumPtr fmin2,
            std::string const &refitConfig, std::string const &saveName, int nsave) const;
        
	}; // CorrelationAnalyzer
	
    inline void CorrelationAnalyzer::setVerbose(bool value) { _verbose = value; }
    inline int CorrelationAnalyzer::getNData() const { return _resampler.getNObservations(); }
    inline void CorrelationAnalyzer::setModel(AbsCorrelationModelPtr model) { _model = model; }

} // baofit

#endif // BAOFIT_CORRELATION_ANALYZER
