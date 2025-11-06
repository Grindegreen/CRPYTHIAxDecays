#include "Decays.h"

#include "crpropa/Units.h"
#include "crpropa/Random.h"
#include "crpropa/Vector3.h"
#include "crpropa/ParticleState.h"
#include "crpropa/Candidate.h"
#include "crpropa/ParticleID.h"

#include <string>
#include <cmath>
#include <iostream>

#include "Pythia8/Pythia.h"

namespace {

// Shared thread-local Pythia instance for all decays
inline Pythia8::Pythia& decay_engine() {
    using namespace Pythia8;
    thread_local Pythia p("", false);  // No banner
    thread_local bool inited = false;
    
    if (!inited) {
        // Basic settings
        p.readString("Print:quiet = on");
        p.readString("ProcessLevel:all = off");
        p.readString("HadronLevel:Hadronize = off");
        p.readString("ParticleDecays:limitTau0 = off");
        
        // Enable decays for common particles
        const int allowIds[] = {
            // Leptons
            13, -13,      // muons
            15, -15,      // taus
            // Pions
            111,          // pi0
            211, -211,    // pi+/-
            // Kaons
            130, 310,     // K_L, K_S
            321, -321,    // K+/-
            311, -311,    // K0, K0bar
            // Light resonances
            113, 213, -213,  // rho
            223, 221, 331, 333,  // omega, eta, eta', phi
            313, -313, 323, -323,  // K*
            // Charm mesons
            411, -411,    // D+/-
            421, -421,    // D0/D0bar
            431, -431,    // Ds+/-
            // Charm baryons
            4122, -4122,  // Lambda_c
            4132, -4132, 4232, -4232,  // Xi_c
            4112, -4112, 4212, -4212, 4222, -4222,  // Sigma_c
            // Stable particles
            22, 2212, 2112  // gamma, proton, neutron
        };
        
        for (int id : allowIds) {
            if (p.particleData.isParticle(id)) {
                p.readString(std::to_string(id) + ":mayDecay = on");
            }
        }
        
        // Seed RNG
        crpropa::Random r;
        p.readString("Random:setSeed = on");
        p.readString("Random:seed = " + std::to_string(r.randInt(900000000)));
        
        p.init();
        inited = true;
    }
    return p;
}

// Get mass and lifetime from Pythia database
struct MassLifetime {
    double mass_GeV;
    double tau_s;
    bool valid;
};

inline MassLifetime getMassLifetime(int pdgId) {
    Pythia8::Pythia& p = decay_engine();
    if (!p.particleData.isParticle(pdgId)) {
        return {0.0, 0.0, false};
    }
    
    double mass = p.particleData.m0(pdgId);  // GeV
    double tau = p.particleData.tau0(pdgId) * 1e-3 / crpropa::c_light;  // mm/c -> seconds
    
    return {mass, tau, true};
}

} // anonymous namespace

// Constructor
Decays::Decays(bool haveOtherSecondaries, bool haveNeutrinos, 
               bool angularCorrection, double limit) {
    setLimit(limit);
    setHaveOtherSecondaries(haveOtherSecondaries);
    setHaveNeutrinos(haveNeutrinos);
    setAngularCorrection(angularCorrection);
    setDescription("Decay from PYTHIA");
}

void Decays::setHaveOtherSecondaries(bool haveOtherSecondaries) {
    this->haveOtherSecondaries = haveOtherSecondaries;
}

void Decays::setHaveNeutrinos(bool haveNeutrinos) {
    this->haveNeutrinos = haveNeutrinos;
}

void Decays::setAngularCorrection(bool angularCorrection) {
    this->angularCorrection = angularCorrection;
}

void Decays::setLimit(double limit) {
    this->limit = limit;
}

void Decays::setDecayTag(std::string tag) const {
    this->decayTag = tag;
}

std::string Decays::getDecayTag() const {
    return this->decayTag;
}

void Decays::performDecay(crpropa::Candidate *candidate) const {
    using namespace crpropa;
    
    int Id = candidate->current.getId();
    double Ekin_J = candidate->current.getEnergy();
    double Ekin_GeV = Ekin_J / GeV;
    
    // Get particle properties from Pythia
    MassLifetime ml = getMassLifetime(Id);
    if (!ml.valid) return;
    
    double m0_GeV = ml.mass_GeV;
    double Etot_GeV = Ekin_GeV + m0_GeV;
    
    Vector3d dir = candidate->current.getDirection();
    
    // Generate decay using Pythia
    Pythia8::Pythia& p = decay_engine();
    
    // Enable decay for this particle if not already enabled
    if (!p.particleData.mayDecay(Id)) {
        p.particleData.mayDecay(Id, true);
    }
    
    // Build event
    Pythia8::Event& ev = p.event;
    ev.reset();
    
    // Calculate momentum
    double pabs_GeV = (Etot_GeV > m0_GeV) ? 
        std::sqrt(Etot_GeV * Etot_GeV - m0_GeV * m0_GeV) : 0.0;
    
    // Special handling for tau polarization
    if (std::abs(Id) == 15) {
        Random random;
        double rand = random.randUniform(0, 1);
        int helicity = (rand < 1./3.) ? 0 : (rand < 2./3.) ? 1 : -1;
        double scale = 1.0;
        
        ev.append(Id, 1, 0, 0, 0, 0, 0, 0,
                  pabs_GeV * dir.x, pabs_GeV * dir.y, pabs_GeV * dir.z,
                  Etot_GeV, m0_GeV, helicity, scale);
    } else {
        ev.append(Id, 1, 0, 0, 0, 0, 0, 0,
                  pabs_GeV * dir.x, pabs_GeV * dir.y, pabs_GeV * dir.z,
                  Etot_GeV, m0_GeV);
    }

    const int motherIdx = 1; // first appended particle
    bool ok = p.moreDecays();
    if (!ok) {
        std::cerr << "Warning: single-step decay failed for PDG " << Id
                  << " at E=" << Etot_GeV << " GeV" << std::endl;
        return;
    }    
    
    // Deactivate parent
    candidate->setActive(false);
    
    // Sample decay position along step
    Random &random = Random::instance();
    Vector3d pos = random.randomInterpolatedPosition(
        candidate->previous.getPosition(),
        candidate->current.getPosition()
    );
    
    // Get candidate properties
    Vector3d pos0 = candidate->current.getPosition();
    double z = candidate->getRedshift();
    double w = candidate->getWeight();
    double trajectoryLength = candidate->getTrajectoryLength();
    const Candidate::PropertyMap& properties = candidate->properties;
    Candidate* parent = candidate;
    
    std::string decayTag = getDecayTag();
    
    // Create secondaries
    for (int i = 0; i < ev.size(); ++i) {
        if (i==motherIdx) continue;  // Skip mother

        // Keep only direct daughters of the mother
        if (ev[i].mother1() != motherIdx && ev[i].mother2() != motherIdx) continue;
        
        int secId = ev[i].id();
        int absSecId = std::abs(secId);
        
        // Check if neutrino
        bool isNeutrino = (absSecId == 12 || absSecId == 14 || absSecId == 16);
        
        // Filter based on settings
        if (isNeutrino && !haveNeutrinos) continue;
        if (!isNeutrino && !haveOtherSecondaries) continue;
        
        // Create secondary candidate
        Candidate* c = new Candidate();
        
        c->setRedshift(z);
        c->setTrajectoryLength(trajectoryLength - (pos0 - pos).getR());
        c->setWeight(w);
        c->setTagOrigin(decayTag);
        
        // Copy properties
        for (Candidate::PropertyMap::const_iterator it = properties.begin();
             it != properties.end(); ++it) {
            c->setProperty(it->first, it->second);
        }
        
        // Set particle states
        c->source = candidate->source;
        c->previous = candidate->previous;
        c->created = candidate->current;
        c->current = candidate->current;
        
        c->current.setId(secId);
        c->current.setPosition(pos);
        c->created.setPosition(pos);
        
        // Set energy (convert from GeV to Joules)
        double Etot_sec_GeV = ev[i].e();
        double m0_sec_GeV = p.particleData.m0(secId);
        double Ekin_sec_GeV = Etot_sec_GeV - m0_sec_GeV;
        
        // Ensure kinetic energy is non-negative
        if (Ekin_sec_GeV < 0.0) {
            Ekin_sec_GeV = 0.0;
        }
        
        c->current.setEnergy(Ekin_sec_GeV * GeV);
        
        // Set momentum direction
        if (angularCorrection) {
            Vector3d pdir(ev[i].px(), ev[i].py(), ev[i].pz());
            double pr = pdir.getR();
            if (pr > 0) {
                c->current.setDirection(pdir / pr);
            }
        }
        
        c->parent = parent;
        candidate->addSecondary(c);
    }
}

void Decays::process(crpropa::Candidate *candidate) const {
    using namespace crpropa;
    
    int Id = candidate->current.getId();
    double Ekin_J = candidate->current.getEnergy();
    double d = candidate->getTrajectoryLength();
    
    // Get mass and lifetime from Pythia
    MassLifetime ml = getMassLifetime(Id);
    if (!ml.valid || ml.tau_s <= 0.0) return;
    
    // Calculate decay probability
    double m_SI = ml.mass_GeV * GeV / c_squared;
    double gamma = 1.0 + (Ekin_J / (m_SI * c_squared));
    
    double beta2 = (gamma > 1.0) ? (1.0 - 1.0/(gamma*gamma)) : 0.0;
    double beta = (beta2 > 0.0) ? std::sqrt(beta2) : 0.0;
    
    double L_mean = c_light * beta * (gamma * ml.tau_s);
    double rate = (L_mean > 0.0) ? 1.0 / L_mean : 0.0;
    
    // Sample decay distance
    Random &random = Random::instance();
    double randDistance = (rate > 0.0) ? -std::log(random.rand()) / rate : 1e300;
    
    if (d <= randDistance) {
        // Not decaying yet
        if (rate > 0.0) {
            candidate->limitNextStep(limit / rate);
        }
        return;
    }
    
    // Decay now
    setDecayTag(getDecayTagForParticle(Id));
    performDecay(candidate);
}

std::string Decays::getDecayTagForParticle(int pdgId) const {
    int absPdg = std::abs(pdgId);
    
    // Map PDG codes to decay tags
    if (absPdg == 13) return "MD";      // Muon decay
    if (absPdg == 15) return "TD";      // Tau decay
    if (absPdg == 211) return "CPD";    // Charged pion decay
    if (pdgId == 111) return "NPD";     // Neutral pion decay
    if (absPdg == 24) return "WD";      // W boson decay
    if (absPdg == 321) return "KD";     // Charged kaon decay
    if (absPdg == 130 || absPdg == 310) return "KD";  // Neutral kaon decay
    if (absPdg == 411) return "D+D";    // D+ decay
    if (absPdg == 421) return "D0D";    // D0 decay
    if (absPdg == 431) return "DsD";    // Ds decay
    if (absPdg == 4122) return "LcD";   // Lambda_c decay
    
    return "DECAY";  // Generic decay tag
}