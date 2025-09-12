#ifndef DECAYS_H
#define DECAYS_H

#include "crpropa/Module.h"
#include "crpropa/Vector3.h"
#include <string>

// Simple container for mass/lifetime (SI units)
struct MassTau {
    double mass_SI; // kg
    double tau_s;   // seconds
    bool   hasTau;  // false => stable
};

/**
 * Decays
 * ------
 * Propagates unstable particles until decay length is reached,
 * then generates secondaries with PYTHIA and injects them.
 *
 * Options:
 *  - haveOtherSecondaries : keep non-neutrino secondaries
 *  - haveNeutrinos        : keep neutrinos (νe, νμ, ντ)
 *  - angularCorrection    : set daughter directions from PYTHIA 3-momenta
 *  - limit                : step limiter factor (ds ~ limit / decayRate)
 */
class Decays : public crpropa::Module {
public:
    Decays(bool haveOtherSecondaries = true,
           bool haveNeutrinos        = true,
           bool angularCorrection    = true,
           double limit              = 1.0);

    void process(crpropa::Candidate* candidate) const override;

    // Config
    void setHaveOtherSecondaries(bool v);
    void setHaveNeutrinos(bool v);
    void setAngularCorrection(bool v);
    void setLimit(double v);

    // Tag helpers (e.g. "MD","CPD","KD","HD")
    void setDecayTag(std::string tag) const;
    std::string getDecayTag() const;

    std::string getDescription() const override { return description_; }
    void setDescription(const std::string& d) { description_ = d; }

private:
    void performDecay(crpropa::Candidate* candidate) const;

    // user config
    bool   haveOtherSecondaries_;
    bool   haveNeutrinos_;
    bool   angularCorrection_;
    double limit_;
    std::string description_ = "Decay from PYTHIA";

    // mutable because updated during process()
    mutable std::string decayTag_;
};

#endif // DECAYS_H