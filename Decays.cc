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

using crpropa::Vector3d;
using crpropa::GeV;
using crpropa::c_light;
using crpropa::c_squared;

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

const double CTAU_KEEP_MM = 0.1;

// -----------------------------
// PDG database
// -----------------------------
inline Pythia8::Pythia& pdg_db() {
    thread_local Pythia8::Pythia p("", false);
    thread_local bool configured = false;
    if (!configured) {
        p.readString("Print:quiet = on");
        configured = true;
    }
    return p;
}

inline MassTau getMassTauFromPythia(int pdgid) {
    using namespace crpropa;
    Pythia8::Pythia& P = pdg_db();

    const double m_GeV   = P.particleData.m0(pdgid);
    const double ctau_mm = P.particleData.tau0(pdgid);

    const double mass_SI = m_GeV * GeV / c_squared;
    if (ctau_mm <= 0.0) return { mass_SI, 0.0, false };

    const double tau_s   = (ctau_mm * 1e-3) / c_light;
    return { mass_SI, tau_s, true };
}

// -----------------------------
// Thread-local decay engine
// -----------------------------
inline Pythia8::Pythia& decay_engine() {
    using namespace Pythia8;
    thread_local Pythia p("", false);
    thread_local bool inited = false;
    if (!inited) {
        p.readString("Print:quiet = on");
        p.readString("ProcessLevel:all = off");
        p.readString("HadronLevel:Hadronize = off");
        p.readString("ParticleDecays:limitTau0 = off");

        // Allow decays for common particles
        const int allowIds[] = {
            // leptons
            13,-13, 15,-15,
            // pions
            111, 211,-211,
            // kaons
            130, 310, 321,-321, 311,-311,
            // light resonances
            113,213,-213, 223,221,331,333, 313,-313,323,-323,
            // CHARM MESONS
            411,-411,    // D+/D-
            421,-421,    // D0/D0bar
            431,-431,    // Ds+/Ds-
            // CHARM BARYONS
            4122,-4122,  // Lambda_c+
            4132,-4132,  // Xi_c0
            4232,-4232,  // Xi_c+
            4112,-4112,  // Sigma_c0
            4212,-4212,  // Sigma_c+
            4222,-4222,  // Sigma_c++
            // photons, nucleons
            22, 2212, 2112
        };
        for (int id : allowIds) {
            if (p.particleData.isParticle(id))
                p.readString(std::to_string(id) + ":mayDecay = on");
        }

        // Seed once per thread
        crpropa::Random r;
        p.readString("Random:setSeed = on");
        p.readString("Random:seed = " + std::to_string(r.randInt(900000000)));

        p.init();
        inited = true;
    }
    return p;
}

// -----------------------------
// Helpers
// -----------------------------
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
    else if (parentAbsId == 411 || parentAbsId == 421 || parentAbsId == 431) return "CharmD";
    else if (parentAbsId == 4122 || parentAbsId == 4132 || parentAbsId == 4232) return "CharmD";
    else return "HD";
}

} // anonymous namespace

// ============================
// Decay event generator
// ============================
namespace {
class PythiaDecay {
public:
    PythiaDecay(int Id, double E_parentTot_GeV, const crpropa::Vector3d& dir,
                bool oneStep)
    {
        using namespace Pythia8;
        Pythia &p = decay_engine();

        // Enable decay for THIS specific particle
        if (!p.particleData.mayDecay(Id)) {
            p.particleData.mayDecay(Id, true);
        }

        // Build one-particle event
        Event& ev = p.event;
        ev.reset();

        const double m0 = p.particleData.m0(Id);
        const double pabs = momentum_from_E_m_GeV(E_parentTot_GeV, m0);

        ev.append(Id, 1, 0,0,0, 0,0,0,
                  pabs*dir.x, pabs*dir.y, pabs*dir.z, E_parentTot_GeV, m0);

        // Decay once
        if (!p.moreDecays()) {
            rows_.clear();
            return;
        }

        std::vector<std::vector<double>> out;
        out.reserve(ev.size());

        if (oneStep) {
            // Collect direct daughters and collapse ultra-short-lived
            const int parentIdx = 1;
            std::vector<int> seeds;
            seeds.reserve(8);
            for (int i = 0; i < ev.size(); ++i)
                if (ev[i].mother1() == parentIdx || ev[i].mother2() == parentIdx)
                    seeds.push_back(i);

            std::vector<int> stack(seeds.begin(), seeds.end());
            while (!stack.empty()) {
                int i = stack.back(); stack.pop_back();
                const int id = ev[i].id();

                const bool keep_by_id  = shouldKeepId(id);
                const bool keep_by_tau = (p.particleData.tau0(id) >= CTAU_KEEP_MM);

                if (keep_by_id || keep_by_tau) {
                    int momIdx = ev[i].mother1();
                    if (momIdx <= 0 || momIdx >= ev.size()) momIdx = ev[i].mother2();
                    if (momIdx <= 0 || momIdx >= ev.size()) momIdx = 0;
                    const int momId = (momIdx > 0) ? ev[momIdx].id() : Id;

                    out.push_back({ (double)id, ev[i].px(), ev[i].py(), ev[i].pz(), 
                                   ev[i].e(), (double)momId });
                    continue;
                }

                // Not keepable: descend one vertex
                const int d1 = ev[i].daughter1();
                const int d2 = ev[i].daughter2();
                if (d1 > 0 && d2 >= d1) {
                    for (int j = d1; j <= d2; ++j) stack.push_back(j);
                } else {
                    // Leaf: keep as fallback
                    int momIdx = ev[i].mother1();
                    if (momIdx <= 0 || momIdx >= ev.size()) momIdx = ev[i].mother2();
                    if (momIdx <= 0 || momIdx >= ev.size()) momIdx = 0;
                    const int momId = (momIdx > 0) ? ev[momIdx].id() : Id;

                    out.push_back({ (double)id, ev[i].px(), ev[i].py(), ev[i].pz(), 
                                   ev[i].e(), (double)momId });
                }
            }
        } else {
            // Full cascade finals
            for (int i = 0; i < ev.size(); ++i) {
                if (!ev[i].isFinal()) continue;
                int momIdx = ev[i].mother1();
                if (momIdx <= 0 || momIdx >= ev.size()) momIdx = ev[i].mother2();
                if (momIdx <= 0 || momIdx >= ev.size()) momIdx = 0;
                const int momId = (momIdx > 0) ? ev[momIdx].id() : Id;

                out.push_back({ (double)ev[i].id(), ev[i].px(), ev[i].py(), ev[i].pz(), 
                               ev[i].e(), (double)momId });
            }
        }
        rows_.swap(out);
    }

    const std::vector<std::vector<double>>& rows() const { return rows_; }
    bool isEmpty() const { return rows_.empty(); }

private:
    std::vector<std::vector<double>> rows_;
};
} // anon

// ============================
// Decays Module
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

void Decays::performDecay(crpropa::Candidate* candidate, double randDistance) const {
    using namespace crpropa;

    const int    Id         = candidate->current.getId();
    const double Ekin_J     = candidate->current.getEnergy();
    const double Ekin_GeV   = Ekin_J / GeV;
    const double m0_GeV     = pdg_db().particleData.m0(Id);
    const double Etot_GeV   = Ekin_GeV + m0_GeV;

    if (Etot_GeV < m0_GeV * 0.99) {  // Allow 1% numerical tolerance
        std::cerr << "WARNING: Particle " << Id << " has E_total (" << Etot_GeV 
                  << " GeV) < mass (" << m0_GeV << " GeV). "
                  << "E_kinetic = " << Ekin_GeV << " GeV" << std::endl;
    }

    const Vector3d dir = candidate->current.getDirection();
    const bool oneStep = true;

    PythiaDecay decay(Id, Etot_GeV, dir, oneStep);
    const auto& secs = decay.rows();

    // Place vertex at sampled distance
    const Vector3d x0 = candidate->previous.getPosition();
    const Vector3d x1 = candidate->current.getPosition();
    const Vector3d seg = x1 - x0;
    const double   segLen = seg.getR();
    const Vector3d pos = x0 + (segLen > 0 ? seg * (std::min(randDistance, segLen)/segLen)
                                          : Vector3d(0,0,0));
    const double   distBack = (x1 - pos).getR();
    const double   trajAtDecay = candidate->getTrajectoryLength() - distBack;

    const double z    = candidate->getRedshift();
    const double w    = candidate->getWeight();
    const Candidate::PropertyMap& props = candidate->properties;
    Candidate* parent = candidate;

    for (size_t i = 0; i < secs.size(); ++i) {
        const std::vector<double>& row = secs[i];
        const int    id    = static_cast<int>(row[0]);
        const int    absid = std::abs(id);
        const bool   isNu  = (absid == 12 || absid == 14 || absid == 16);
        const int    momId = (row.size() >= 6) ? static_cast<int>(row[5]) : Id;
        const int    absMom= std::abs(momId);

        if (isNu && !haveNeutrinos_)         continue;
        if (!isNu && !haveOtherSecondaries_)  continue;

        Candidate* c = new Candidate();
        c->setRedshift(z);
        c->setWeight(w);
        c->setTagOrigin(tagForParent(absMom));

        for (Candidate::PropertyMap::const_iterator it = props.begin(); 
             it != props.end(); ++it)
            c->setProperty(it->first, it->second);

        c->source   = candidate->source;
        c->created  = candidate->current;
        c->created.setPosition(pos);
        c->previous = c->created;
        c->current  = c->created;

        c->current.setId(id);
        {
            const double Etot_child_GeV = row[4];
            const double m0_child_GeV   = pdg_db().particleData.m0(id);
            double Ekin_child_GeV = Etot_child_GeV - m0_child_GeV;

            const double Ekin_child_J = Ekin_child_GeV * GeV;

            if (Ekin_child_GeV < 0.0) {
                // Clamp to zero rather than keeping negative
                Ekin_child_GeV = 0.0;
            }

            c->current.setEnergy(Ekin_child_J);
        }

        if (angularCorrection_) {
            Vector3d pdir(row[1], row[2], row[3]);
            const double pr = pdir.getR();
            if (pr > 0) c->current.setDirection(pdir / pr);
        }

        c->setTrajectoryLength(trajAtDecay);
        c->parent = parent;
        candidate->addSecondary(c);
    }

    candidate->setActive(false);
}

void Decays::process(crpropa::Candidate* candidate) const {
    using namespace crpropa;

    const int    Id = candidate->current.getId();
    const double Ekin_J  = candidate->current.getEnergy();

    const Vector3d x0 = candidate->previous.getPosition();
    const Vector3d x1 = candidate->current.getPosition();
    const double stepLen = (x1 - x0).getR();

    const MassTau mt = getMassTauFromPythia(Id);
    if (!mt.hasTau || mt.tau_s <= 0.0) return;

    const double gamma = 1.0 + (Ekin_J / (mt.mass_SI * c_squared));
    const double beta2 = (gamma > 1.0) ? (1.0 - 1.0/(gamma*gamma)) : 0.0;
    const double beta  = (beta2 > 0.0) ? std::sqrt(beta2) : 0.0;

    const double L_mean = c_light * beta * (gamma * mt.tau_s);
    const double rate   = (L_mean > 0.0) ? 1.0 / L_mean : 0.0;

    crpropa::Random& rng = crpropa::Random::instance();
    const double randDistance = (rate > 0.0) ? -std::log(rng.rand()) / rate : 1e300;

    if (stepLen <= randDistance) {
        if (rate > 0.0) candidate->limitNextStep(limit_ / rate);
        return;
    }

    performDecay(candidate, randDistance);
}