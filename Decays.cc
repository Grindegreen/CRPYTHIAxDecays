#include "Decays.h"

#include "crpropa/Units.h"
#include "crpropa/Random.h"
#include "crpropa/ParticleState.h"
#include "crpropa/Candidate.h"

#include <vector>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "Pythia8/Pythia.h"

namespace {

inline bool shouldKeepId(int id) {
    int a = std::abs(id);
    // Leptons and photons
    if (a==11 || a==13 || a==15 || a==22) return true;
    // Neutrinos
    if (a==12 || a==14 || a==16) return true;
    // Nucleons
    if (a==2212 || a==2112) return true;
    // Pions
    if (a==111 || a==211) return true;
    // Kaons
    if (a==321 || a==311 || a==310 || a==130) return true;
    return false;
}

// If you prefer a purely lifetime-based rule, set a threshold in mm:
inline bool lifetimeLongEnough(const Pythia8::Pythia& p, int id, double ctau_keep_mm) {
    return p.particleData.tau0(id) >= ctau_keep_mm;
}

// Thread-local PYTHIA only for ParticleData queries
inline Pythia8::Pythia& pdg_db() {
    // Pass printBanner=false; keep quiet; no init() needed for particleData
    thread_local Pythia8::Pythia p("", false);
    thread_local bool configured = false;
    if (!configured) {
        p.readString("Print:quiet = on");
        configured = true;
    }
    return p;
}

// Convert PYTHIA’s (m[GeV], c*tau[mm]) to SI
inline MassTau getMassTauFromPythia(int pdgid) {
    using namespace crpropa;
    Pythia8::Pythia& P = pdg_db();

    const double m_GeV   = P.particleData.m0(pdgid);   // GeV/c^2 (PYTHIA units)
    const double ctau_mm = P.particleData.tau0(pdgid); // c*tau in mm (0 => stable)

    const double mass_SI = m_GeV * GeV / c_squared;    // kg
    if (ctau_mm <= 0.0) return { mass_SI, 0.0, false };

    const double tau_s   = (ctau_mm * 1e-3) / c_light; // s (tau = L/c)
    return { mass_SI, tau_s, true };
}

// Small list of particles we generally allow to decay inside event gen
inline const std::vector<int>& default_decay_list() {
    static const std::vector<int> ids = {
        // leptons
        13,-13, 15,-15,
        // pions
        111, 211,-211,
        // kaons
        130, 310, 321,-321, 311,-311,
        // some light resonances
        113,213,-213, 223,221,331,333, 313,-313,323,-323,
        // charm (D’s) and a couple baryons
        411,-411, 421,-421, 431,-431, 4122,
        // photons (stable), nucleons (stable) — included for completeness
        22, 2212, 2112
    };
    return ids;
}

// Momentum magnitude from E and m (GeV)
inline double momentum_from_E_m_GeV(double E_GeV, double m_GeV) {
    const double e2 = E_GeV * E_GeV;
    const double m2 = m_GeV * m_GeV;
    return (e2 > m2) ? std::sqrt(e2 - m2) : 0.0;
}

inline std::string tagForParent(int parentAbsId) {
    if      (parentAbsId == 13)  return "MD";
    else if (parentAbsId == 211) return "CPD";
    else if (parentAbsId == 111) return "NPD";
    else if (parentAbsId == 15)  return "TD";
    else if (parentAbsId == 24)  return "WD";
    else if (parentAbsId == 321 || parentAbsId == 130 || parentAbsId == 310) return "KD";
    else return "HD";
}

} // anonymous namespace

// ============================
// Decay event generator (per call)
// ============================
namespace {
class PythiaDecay {
public:
    PythiaDecay(int Id, double E_parent_GeV, const crpropa::Vector3d& dir,
                bool hadronize, bool oneStep)
    : oneStep_(oneStep) {
        using namespace Pythia8;

        p_ = new Pythia("", false);
        p_->readString("Print:quiet = on");
        p_->readString("ProcessLevel:all = off");
        p_->readString(std::string("HadronLevel:Hadronize = ") + (hadronize ? "on" : "off"));
        p_->readString("ParticleDecays:limitTau0 = off"); // allow all decays

        // Seed
        crpropa::Random r;
        const int seed = r.randInt(900000000);
        p_->readString("Random:setSeed = on");
        p_->readString("Random:seed = " + std::to_string(seed));

        // Allow common decays + the parent
        const std::vector<int>& ids = default_decay_list();
        for (size_t i = 0; i < ids.size(); ++i)
            p_->readString(std::to_string(ids[i]) + ":mayDecay = on");
        p_->readString(std::to_string(Id) + ":mayDecay = on");

        p_->init();

        // Build the one-particle event at index 1
        const double m0   = p_->particleData.m0(Id);

        // Now compute |p| from total E
        const double pabs = momentum_from_E_m_GeV(E_parent_GeV, m0);
        Event& ev = p_->event;
        ev.reset();
        ev.append(Id, 1, 0, 0, 0, 0, 0, 0,
                  pabs*dir.x, pabs*dir.y, pabs*dir.z, E_parent_GeV, m0);

        // IMPORTANT: process user-supplied event
        // - moreDecays(): do decays of particles in the current event
        // - forceHadronLevel(): optional hadronization after decays (if enabled)
        if (!p_->moreDecays()) return;
        if (!oneStep_ && hadronize) p_->forceHadronLevel();

        const int parentIdx = 1;
        std::vector<std::vector<double>> out; // {id, px, py, pz, e, mother_id}

        // Helper to push a particle with its *immediate mother PDG* captured
        auto push_particle = [&](int i) {
            const int id = ev[i].id();
            int momIdx = ev[i].mother1();            // 0 if none
            if (momIdx < 0 || momIdx >= ev.size())   // guard
                momIdx = 0;
            const int momId = (momIdx > 0) ? ev[momIdx].id() : 0;
            out.push_back({ (double)id, ev[i].px(), ev[i].py(), ev[i].pz(), ev[i].e(), (double)momId });
        };

        if (oneStep_) {
            // Collect direct daughters of the parent
            std::vector<int> seeds;
            for (int i = 0; i < ev.size(); ++i) {
                if (ev[i].mother1() == parentIdx || ev[i].mother2() == parentIdx)
                    seeds.push_back(i);
            }

            // Walk down only through very short-lived resonances until we hit "keepable" species
            const double CTAU_KEEP_MM = 0.1; // keep anything with c*tau >= 0.1 mm; collapses ρ, K*, ϕ, a1, … 

            std::vector<int> stack(seeds.begin(), seeds.end());
            while (!stack.empty()) {
                int i = stack.back(); stack.pop_back();
                const int id = ev[i].id();

                const bool keep_by_id  = shouldKeepId(id);
                const bool keep_by_tau = (p_->particleData.tau0(id) >= CTAU_KEEP_MM);

                if (keep_by_id || keep_by_tau) {
                    out.push_back({ (double)id, ev[i].px(), ev[i].py(), ev[i].pz(), ev[i].e() });
                    continue; // IMPORTANT: do NOT descend further from a kept node
                }

                // Not keepable → try to replace by its daughters (one vertex deeper)
                const int d1 = ev[i].daughter1();
                const int d2 = ev[i].daughter2();
                if (d1 > 0 && d2 >= d1) {
                    for (int j = d1; j <= d2; ++j) stack.push_back(j);
                } else {
                    // Leaf without recorded daughters → keep as fallback
                    out.push_back({ (double)id, ev[i].px(), ev[i].py(), ev[i].pz(), ev[i].e() });
                }
            }
        } else {
            // Full cascade: keep finals
            for (int i = 0; i < ev.size(); ++i) {
                if (!ev[i].isFinal()) continue;
                out.push_back({ (double)ev[i].id(), ev[i].px(), ev[i].py(), ev[i].pz(), ev[i].e() });
            }
        }
        rows_.swap(out);
    }

    ~PythiaDecay() { delete p_; }

    const std::vector<std::vector<double>>& rows() const { return rows_; }

private:
    Pythia8::Pythia* p_;
    bool oneStep_;
    std::vector<std::vector<double>> rows_; // {id, px, py, pz, e, mother_id} (GeV)
};
} // anon

// ============================
// Decays (CRPropa Module)
// ============================

Decays::Decays(bool haveOtherSecondaries,
               bool haveNeutrinos,
               bool angularCorrection,
               double limit)
: haveOtherSecondaries_(haveOtherSecondaries)
, haveNeutrinos_(haveNeutrinos)
, angularCorrection_(angularCorrection)
, limit_(limit) {
    setDescription("Decay from PYTHIA");
}

void Decays::setHaveOtherSecondaries(bool v) { haveOtherSecondaries_ = v; }
void Decays::setHaveNeutrinos(bool v)        { haveNeutrinos_ = v; }
void Decays::setAngularCorrection(bool v)    { angularCorrection_ = v; }
void Decays::setLimit(double v)              { limit_ = v; }

void Decays::setDecayTag(std::string tag) const { decayTag_ = tag; }
std::string Decays::getDecayTag() const { return decayTag_; }

void Decays::performDecay(crpropa::Candidate* candidate) const {
    using namespace crpropa;

    const int    Id   = candidate->current.getId();
    const double E_parent_J = candidate->current.getEnergy();   // CRPropa (J)
    double       E_parent_GeV = E_parent_J / GeV;               // convert to GeV

    // If your CRPropa energy is *kinetic*, promote to total here:
    const double m0_GeV = pdg_db().particleData.m0(Id);         // Pythia mass
    const bool   energyIsKinetic = true;
    if (energyIsKinetic) E_parent_GeV += m0_GeV;

    const Vector3d dir = candidate->current.getDirection();

    // Decide one-step mode
    const bool oneStep = true;

    // If one-step decay, turn hadronization OFF even for τ/W,
    // otherwise their quark daughters will hadronize (a second step).
    const bool hadronize =
        (!oneStep) && ((std::abs(Id) == 15) || (std::abs(Id) == 24));

    PythiaDecay decay(Id, E_parent_GeV, dir, hadronize, oneStep);
    const auto& secs = decay.rows();

    // Sample decay position along the current step
    Random& rng = Random::instance();
    const Vector3d pos0 = candidate->current.getPosition();
    const Vector3d pos  = rng.randomInterpolatedPosition(candidate->previous.getPosition(), pos0);

    const double z    = candidate->getRedshift();
    const double w    = candidate->getWeight();
    const double traj = candidate->getTrajectoryLength();
    const Candidate::PropertyMap& props = candidate->properties;
    Candidate* parent = candidate;

    for (size_t i = 0; i < secs.size(); ++i) {
        const std::vector<double>& row = secs[i];
        const int    id    = static_cast<int>(row[0]);
        const int    absid = std::abs(id);
        const bool   isNu  = (absid == 12 || absid == 14 || absid == 16);
        const int    momId   = (row.size() >= 6) ? static_cast<int>(row[5]) : candidate->current.getId();
        const int    absMom  = std::abs(momId);

        if (isNu && !haveNeutrinos_)         continue; // skip neutrinos if disabled
        if (!isNu && !haveOtherSecondaries_)  continue; // skip others if disabled

        Candidate* c = new Candidate();
        c->setRedshift(z);
        c->setTrajectoryLength(traj - (pos0 - pos).getR());
        c->setWeight(w);
        
        c->setTagOrigin(tagForParent(absMom));

        // propagate existing properties
        for (Candidate::PropertyMap::const_iterator it = props.begin(); it != props.end(); ++it)
            c->setProperty(it->first, it->second);

        // copy states & set identity/kinematics
        c->source   = candidate->source;
        c->previous = candidate->current;
        c->created  = parent->current;
        c->current  = candidate->current;

        c->current.setId(id);
        c->current.setEnergy(row[4] * GeV);
        c->current.setPosition(pos);
        c->created.setPosition(pos);

        if (angularCorrection_) {
            Vector3d pdir(row[1], row[2], row[3]);
            const double pr = pdir.getR();
            if (pr > 0) c->current.setDirection(pdir / pr);
        }

        c->parent = parent;
        candidate->addSecondary(c);
    }

    // deactivate parent after generating decay products
    candidate->setActive(false);
}

void Decays::process(crpropa::Candidate* candidate) const {
    using namespace crpropa;

    const int    Id = candidate->current.getId();
    const double E  = candidate->current.getEnergy();   // J
    const double d  = candidate->getTrajectoryLength(); // m

    const MassTau mt = getMassTauFromPythia(Id);
    if (!mt.hasTau || mt.tau_s <= 0.0) return; // stable -> nothing to do

    // Relativistic factors
    double gamma = E / (mt.mass_SI * c_squared);
    if (gamma < 1.0) gamma = 1.0;
    const double beta = std::sqrt(1.0 - 1.0 / (gamma * gamma));

    // Mean decay distance in lab frame and rate
    const double t_lab  = gamma * mt.tau_s;
    const double L_mean = c_light * beta * t_lab; // meters
    const double rate   = (L_mean > 0.0) ? 1.0 / L_mean : 0.0;

    // const int aId = std::abs(Id);
    // if      (aId == 13)  setDecayTag("MD");
    // else if (aId == 211) setDecayTag("CPD");
    // else if (aId  == 111) setDecayTag("NPD");
    // else if (aId == 15)  setDecayTag("TD");
    // else if (aId == 24)  setDecayTag("WD");
    // else if (aId == 321 || Id == 130 || Id == 310) setDecayTag("KD");
    // else setDecayTag("HD");

    // Exponential decay sampling along the current step
    Random& rng = Random::instance();
    const double randDistance = (rate > 0.0) ? -std::log(rng.rand()) / rate : 1e300;

    if (d <= randDistance) {
        // Not yet decayed: reduce step to resolve decay soon
        if (rate > 0.0) candidate->limitNextStep(limit_ / rate);
        return;
    }

    // Decay occurs in this step
    performDecay(candidate);
}
