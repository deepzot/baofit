// Created 14-Apr-2014 by Michael Blomqvist (University of California, Irvine) <cblomqvi@uci.edu>

#ifndef BAOFIT_BAO_K_SPACE_FFT_CORRELATION_MODEL
#define BAOFIT_BAO_K_SPACE_FFT_CORRELATION_MODEL

#include "baofit/AbsCorrelationModel.h"
#include "baofit/types.h"

#include "cosmo/types.h"

#include <string>

namespace baofit {
	// Represents a two-point correlation model derived from tabulated power spectra (with and
	// without wiggles), a parameterized anisotropic k-space distortion model D(k,mu_k), and
	// anisotropic BAO scale parameters. Optional multiplicative and/or additive broadband
	// distortion can also be added in r space.
	class BaoKSpaceFftCorrelationModel : public AbsCorrelationModel {
	public:
	    // Creates a new model using the specified tabulated power spectra at the specified
	    // reference redshift. The 3D FFT grid spacing and size are set by the parameters
        // spacing and nx, ny, nz. The input tabulated power spectra specified by the
        // model names provided are assumed to be normalized for redshift zref and will
        // be re-normalized appropriately when the model is evaluated at any different z.
		BaoKSpaceFftCorrelationModel(std::string const &modelrootName,
		    std::string const &fiducialName, std::string const &nowigglesName, double zref,
            double OmegaMatter, double spacing, int nx, int ny, int nz,
            std::string const &distAdd, std::string const &distMul, double distR0,
            double zcorr0, double zcorr1, double zcorr2, double sigma8,
            bool anisotropic = false, bool decoupled = false, bool nlBroadband = false,
            bool nlCorrection = false, bool fitNLCorrection = false, bool nlCorrectionAlt = false,
            bool distortionAlt = false, bool noDistortion = false, bool crossCorrelation = false,
            bool verbose = false);
		virtual ~BaoKSpaceFftCorrelationModel();
        // Prints a multi-line description of this object to the specified output stream.
        virtual void printToStream(std::ostream &out, std::string const &formatSpec = "%12.6f") const;
	protected:
		// Returns the correlation function evaluated in redshift space where (r,mu) is
		// the pair separation and z is their average redshift. The separation r should
		// be provided in Mpc/h.
        virtual double _evaluate(double r, double mu, double z, bool anyChanged, int index) const;
        virtual double _evaluateKSpace(double k, double mu_k, double pk, double z) const;
        virtual int _getIndexBase() const;
	private:
        double _zcorr0, _zcorr1, _zcorr2;
        AbsCorrelationModelPtr _nlCorr, _distortAdd, _distortMul;
        bool _anisotropic, _decoupled, _nlBroadband, _nlCorrection, _fitNLCorrection, _nlCorrectionAlt,
            _distortionAlt, _noDistortion, _crossCorrelation, _verbose;
        int _indexBase, _nlBase, _contBase, _nlcorrBase, _baoBase;
        cosmo::DistortedPowerCorrelationFftPtr _Xipk, _Xinw;
        // Evaluates our k-space distortion model D(k,mu_k) using our current parameter values.
        double _evaluateKSpaceDistortion(double k, double mu_k, double pk) const;
        // Parameters initialized in _evaluate that are needed by _evaluateKSpaceDistortion
        mutable double _betaz, _beta2z, _snlPar2, _snlPerp2, _zeff;
	}; // BaoKSpaceFftCorrelationModel
} // baofit

#endif // BAOFIT_BAO_K_SPACE_FFT_CORRELATION_MODEL
