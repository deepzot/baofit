// Created 27-Jan-2014 by David Kirkby (University of California, Irvine) <dkirkby@uci.edu>

#ifndef BAOFIT_BAO_K_SPACE_CORRELATION_MODEL
#define BAOFIT_BAO_K_SPACE_CORRELATION_MODEL

#include "baofit/AbsCorrelationModel.h"
#include "baofit/types.h"

#include "cosmo/types.h"

#include <string>

namespace baofit {
	// Represents a two-point correlation model derived from tabulated power spectra (with and
	// without wiggles), a parameterized anisotropic k-space distortion model D(k,mu_k), and
	// anisotropic BAO scale parameters. Optional multiplicative and/or additive broadband
	// distortion can also be added in r space.
	class BaoKSpaceCorrelationModel : public AbsCorrelationModel {
	public:
	    // Creates a new model using the specified tabulated power spectra at the specified
	    // reference redshift. The rmin,rmax parameters specify the range of observed r this
        // model will be evaluated at. The dilmin,dilmax parameters specify the max range of
        // radial dilations that will be explored. The relerr and abserr parameters determine
        // the target accuracy of the numerical transforms from k to r space, using an even
        // multipole expansion up to ellMax. The input tabulated power spectra specified by
        // the model names provided are assumed to be normalized for redshift zref and will
        // be re-normalized appropriately when the model is evaluated at any different z.
		BaoKSpaceCorrelationModel(std::string const &modelrootName,
		    std::string const &fiducialName, std::string const &nowigglesName, double zref,
            double rmin, double rmax, double dilmin, double dilmax,
            double relerr, double abserr, int ellMax, int samplesPerDecade,
            std::string const &distAdd, std::string const &distMul, double distR0,
            bool anisotropic = false, bool decoupled = false, bool nlBroadband = false,
            bool metals = false, bool radiation = false, bool crossCorrelation = false,
            bool verbose = false);
		virtual ~BaoKSpaceCorrelationModel();
        // Prints a multi-line description of this object to the specified output stream.
        virtual void printToStream(std::ostream &out, std::string const &formatSpec = "%12.6f") const;
	protected:
		// Returns the correlation function evaluated in redshift space where (r,mu) is
		// the pair separation and z is their average redshift. The separation r should
		// be provided in Mpc/h.
        virtual double _evaluate(double r, double mu, double z, bool anyChanged) const;
	private:
        double _dilmin, _dilmax;
        AbsCorrelationModelPtr _metalCorr, _radiationAdd, _distortAdd, _distortMul;
        bool _anisotropic, _decoupled, _nlBroadband, _metals, _radiation, _crossCorrelation,
        _verbose;
        int _nlBase, _baoBase, _maxWarnings;
        mutable int _nWarnings;
        cosmo::DistortedPowerCorrelationPtr _Xipk, _Xinw;
        // Evaluates our k-space distortion model D(k,mu_k) using our current parameter values.
        double _evaluateKSpaceDistortion(double k, double mu_k) const;
        // Parameters initialized in _evaluate that are needed by _evaluateKSpaceDistortion
        mutable double _betaz, _beta2z, _snlPar2, _snlPerp2;
	}; // BaoKSpaceCorrelationModel
} // baofit

#endif // BAOFIT_BAO_K_SPACE_CORRELATION_MODEL
