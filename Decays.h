#ifndef DECAYS_H
#define DECAYS_H

#include "crpropa/Module.h"

#include <string>

/**
 * Structure to hold mass and lifetime data
 */
struct MassTau {
    double mass_SI;  // kg
    double tau_s;    // seconds
    bool hasTau;     // true if particle is unstable
};

/**
 * @class Decays
 * @brief Particle decay module using Pythia8
 */
class Decays : public crpropa::Module {
private:
    bool haveOtherSecondaries_;
    bool haveNeutrinos_;
    bool angularCorrection_;
    double limit_;
    
    mutable std::string decayTag_;

public:
    /**
     * Constructor
     * @param haveOtherSecondaries  Keep non-neutrino secondaries
     * @param haveNeutrinos         Keep neutrinos
     * @param angularCorrection     Apply angular corrections
     * @param limit                 Step size limit factor
     */
    Decays(bool haveOtherSecondaries = true,
           bool haveNeutrinos = true,
           bool angularCorrection = true,
           double limit = 0.1);

    // Existing setters
    void setHaveOtherSecondaries(bool v);
    void setHaveNeutrinos(bool v);
    void setAngularCorrection(bool v);
    void setLimit(double v);
    void setDecayTag(std::string tag) const;
    std::string getDecayTag() const;

    void process(crpropa::Candidate* candidate) const;
    void performDecay(crpropa::Candidate* candidate, double randDistance) const;
};

#endif // DECAYS_H