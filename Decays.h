#ifndef DECAYS_H
#define DECAYS_H

#include "crpropa/Module.h"
#include <string>

/**
 @class Decays
 @brief Particle decays using Pythia8
 
 This module handles decays of unstable particles including:
 - Leptons: muons, taus
 - Mesons: pions, kaons, charm mesons (D+, D0, Ds)
 - Baryons: charm baryons (Lambda_c, Xi_c, Sigma_c)
 
 Decay products are generated using Pythia8 with proper kinematics,
 branching ratios, and angular distributions.
 */
class Decays : public crpropa::Module {
private:
    bool haveOtherSecondaries;   // Output non-neutrino secondaries
    bool haveNeutrinos;          // Output neutrinos
    bool angularCorrection;      // Apply angular correction from Pythia
    double limit;                // Limit factor for next step
    mutable std::string decayTag;  // Tag for decay type
    
public:
    /**
     Constructor
     @param haveOtherSecondaries  Output charged particles, photons, hadrons
     @param haveNeutrinos         Output neutrinos (νe, νμ, ντ)
     @param angularCorrection     Use Pythia angular distributions
     @param limit                 Limit factor for propagation step
     */
    Decays(bool haveOtherSecondaries = true,
           bool haveNeutrinos = true,
           bool angularCorrection = true,
           double limit = 0.1);
    
    // Main processing function
    void process(crpropa::Candidate *candidate) const;
    
    // Perform the actual decay
    void performDecay(crpropa::Candidate *candidate, std::string& decayTag) const;
    
    // Setters
    void setHaveOtherSecondaries(bool haveOtherSecondaries);
    void setHaveNeutrinos(bool haveNeutrinos);
    void setAngularCorrection(bool angularCorrection);
    void setLimit(double limit);
    
    // Decay tag management
    void setDecayTag(std::string tag) const;
    std::string getDecayTag() const;
    std::string getDecayTagForParticle(int pdgId) const;
};

#endif // DECAYS_H