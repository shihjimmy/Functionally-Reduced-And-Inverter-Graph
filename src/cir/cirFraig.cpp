/****************************************************************************
  FileName     [ cirFraig.cpp ]
  PackageName  [ cir ]
  Synopsis     [ Define cir FRAIG functions ]
  Author       [ Chung-Yang (Ric) Huang ]
  Copyright    [ Copyleft(c) 2012-present LaDs(III), GIEE, NTU, Taiwan ]
****************************************************************************/

#include <cassert>
#include <algorithm>
#include <unordered_map>
#include "cirMgr.h"
#include "cirGate.h"
#include "sat.h"
#include "util.h"

using namespace std;

/**************************************/
/*   Static varaibles and functions   */
/**************************************/
namespace {

// Structural key of an AND gate: its two fanin edges, ordered so that
// AND(a, b) and AND(b, a) get the same key.  CirGateV::bits() is the edge
// as one integer (gate pointer + phase), meant for exactly this.
struct StrashKey
{
   std::uintptr_t lo, hi;
   StrashKey(const CirGateV& a, const CirGateV& b)
      : lo(std::min(a.bits(), b.bits())), hi(std::max(a.bits(), b.bits())) {}
   bool operator == (const StrashKey& k) const {
      return lo == k.lo && hi == k.hi; }
};

struct StrashKeyHash
{
   size_t operator() (const StrashKey& k) const {
      return k.lo * 0x9E3779B97F4A7C15ULL ^ (k.hi + (k.hi >> 17)); }
};


// Two AND gates with the same fanin edges (in either order) are equal by
// definition, so fraig merges them without asking SAT (stage 8.5).  After
// earlier merges this is common: the merged gate's fanouts now share fanins.
bool sameFanins(const CirGate* g, const CirGate* r)
{
   const CirAigGate* a = g->asAig();
   const CirAigGate* b = r->asAig();
   if (!a || !b) return false;
   return (a->in0() == b->in0() && a->in1() == b->in1()) ||
          (a->in0() == b->in1() && a->in1() == b->in0());
}

}  // namespace

/*******************************************/
/*   Public member functions about fraig   */
/*******************************************/
// Structural hashing: walk the DFS list (fanins first), and look each AND
// gate up by its fanin pair. A hit means an identical gate already exists,
// so this one is merged into it. O(1) expected per gate, O(n) in total.
void
CirMgr::strash()
{
   assert(!whyNot(CirStep::Strash));
   _lastStep = CirStep::Strash;
   strashNetlist();
}

// The work of strash(), without the flow step: fraig() uses it too, and a
// fraig must not count as a CIRSTRash in the flow rules.
void
CirMgr::strashNetlist()
{
   unordered_map<StrashKey, CirGate*, StrashKeyHash> table;
   table.reserve(_dfsList.size());

   const GateList dfs = _dfsList;   // copy: retired gates stay valid in it
   for (CirGate* g : dfs) {
      const CirAigGate* aig = g->asAig();
      if (!aig) continue;
      StrashKey key(aig->in0(), aig->in1());
      // One lookup: inserts g if the key is new, otherwise finds the old gate.
      auto [hit, isNew] = table.try_emplace(key, g);
      if (isNew) continue;

      cout << "Strashing: " << hit->second->getGid() << " merging "
           << g->getGid() << "...\n";
      mergeGate(g, CirGateV(hit->second));
   }
   rebuildDerivedLists();
}

// FRAIG: prove the FEC candidates with SAT and merge the proven ones.
//
// The work is done in rounds. One round:
//   1. Walk the DFS list. The first member of a FEC group met in the walk
//      is the group's representative (CONST 0 if it is in the group).
//      Every later member is proved against its representative only:
//        UNSAT => equivalent: merge it into the representative.
//        SAT   => the model is an input pattern that tells them apart.
//      Since the representative comes earlier in DFS order, merging into
//      it can never create a loop.
//      Every 64 counter-examples are simulated at once to split the groups
//      early; this saves SAT calls for gates later in the walk.
//   2. "Update by SAT": simulate the remaining counter-examples.
//   3. "Update by UNSAT": rebuild the DFS list; drop merged gates, and gates
//      no longer reachable from the POs, from the FEC groups.
// Every round either merges or separates at least one member of every
// group, so the loop ends. Afterwards, gates that became structurally
// identical through the merges are merged by strash.
void
CirMgr::fraig()
{
   assert(!whyNot(CirStep::Fraig));
   _lastStep = CirStep::Fraig;
   SatSolver solver;
   solver.initialize();
   buildSatModel(solver);

   while (!_fecGrps.empty()) {
      vector<size_t> cexWords(getNumPIs() + _undefList.size(), 0);
      size_t nCex = 0;
      proveFECGroups(solver, cexWords, nCex);

      if (nCex > 0) simulatePatterns(cexWords, nCex);
      cout << "Updating by SAT... Total #FEC Group = " << _fecGrps.size()
           << endl;
      purgeFECGroups();
      cout << "Updating by UNSAT... Total #FEC Group = " << _fecGrps.size()
           << endl;
   }
   clearFECGroups();
   rebuildDerivedLists();
   strashNetlist();
}

// Debug helper: dump the current FEC groups
void
CirMgr::printFEC() const
{
   printFECPairs();
}

/********************************************/
/*   Private member functions about fraig   */
/********************************************/
// CONST 0 gets a variable fixed to false. Every PI gets a variable (also
// PIs outside the DFS list, so reading the model of any PI is safe), and
// every AIG gate of the DFS list gets one plus its AND clauses.
// An UNDEF gate gets a free variable: a merge must be right whatever value
// the undefined signal has. (Simulation uses 0 for it, as the spec says.)
void
CirMgr::buildSatModel(SatSolver& solver) const
{
   // Lazy CNF (stage 8.5): only the constant gets a variable now.  A gate
   // gets one, with its AND clauses, when a proof first needs its fanin
   // cone (satVarOf).  A satisfiable call then only assigns the variables
   // of the cones proven so far, not of the whole circuit.
   for (unsigned id = 0; id < getNumTots(); ++id)
      if (CirGate* g = getGate(id)) g->setSatVar(kNoSatVar);
   const Var constVar = solver.newVar();
   solver.assertProperty(constVar, false);
   const0()->setSatVar(constVar);
}

// The SAT variable of g; creates the variables and AND clauses of g's
// fanin cone that do not exist yet.  Iterative (circuits can be deep).
Var
CirMgr::satVarOf(SatSolver& solver, CirGate* g) const
{
   if (g->getSatVar() != kNoSatVar) return g->getSatVar();
   vector<CirGate*> stack(1, g);
   while (!stack.empty()) {
      CirGate* n = stack.back();
      if (n->getSatVar() != kNoSatVar) { stack.pop_back(); continue; }
      if (const CirAigGate* aig = n->asAig()) {
         CirGate* a = aig->in0().gate();
         CirGate* b = aig->in1().gate();
         if (a->getSatVar() == kNoSatVar || b->getSatVar() == kNoSatVar) {
            if (a->getSatVar() == kNoSatVar) stack.push_back(a);
            if (b->getSatVar() == kNoSatVar) stack.push_back(b);
            continue;
         }
         n->setSatVar(solver.newVar());
         solver.addAigCNF(n->getSatVar(), a->getSatVar(), aig->in0().isInv(),
                          b->getSatVar(), aig->in1().isInv());
      }
      else n->setSatVar(solver.newVar());          // PI or UNDEF: a free variable
      stack.pop_back();
   }
   return g->getSatVar();
}

// Step 1 of a fraig round (see fraig()). On return, cexWords / nCex hold
// the counter-examples that have not been simulated yet (fewer than 64):
// pattern k is bit k of cexWords[i], i = PI index, then UNDEF index.
void
CirMgr::proveFECGroups(SatSolver& solver, vector<size_t>& cexWords,
                       size_t& nCex)
{
   GateList visited;                   // FEC members met so far, in order
   vector<CirGate*> rep;               // group index -> representative
   auto assignReps = [&]() {
      rep.assign(_fecGrps.size(), 0);
      if (const0()->isFEC()) rep[const0()->getFECId() / 2] = const0();
      for (CirGate* v : visited)
         if (getGate(v->getGid()) == v && v->isFEC() && !rep[v->getFECId() / 2])
            rep[v->getFECId() / 2] = v;
   };
   assignReps();

   const GateList dfs = _dfsList;   // copy: retired gates stay valid in it
   for (CirGate* g : dfs) {
      if (!g->isAig() || !g->isFEC()) continue;
      visited.push_back(g);
      CirGate*& r = rep[g->getFECId() / 2];
      if (!r) { r = g; continue; }

      const bool inv = (g->getFECId() & 1) != (r->getFECId() & 1);
      if ((!inv && sameFanins(g, r)) || proveEqual(solver, g, r, inv)) {
         cout << "Fraig: " << r->getGid() << " merging "
              << (inv ? "!" : "") << g->getGid() << "...\n";
         mergeGate(g, CirGateV(r, inv));
         continue;
      }
      const size_t nPi = getNumPIs();
      for (size_t i = 0; i < cexWords.size(); ++i) {
         const CirGate* in = (i < nPi) ? getPi(i) : getGate(_undefList[i - nPi]);
         // An input outside the solved cones does not affect g or r: give it
         // a random value, so the pattern also helps to split other groups.
         const bool bit = in->getSatVar() == kNoSatVar ? (_rng() & 1)
                                                       : solver.getValue(in->getSatVar()) == 1;
         if (bit) cexWords[i] |= size_t(1) << nCex;
      }
      if (++nCex == kPatternWidth) {
         simulatePatterns(cexWords, nCex);   // splits g from r, and more
         fill(cexWords.begin(), cexWords.end(), 0);
         nCex = 0;
         assignReps();                       // group indices have changed
      }
   }
}

// Is g == r ^ inv for every input? Solved as: "g XOR (r ^ inv)" is UNSAT.
// When it is SAT, the solver's model is a distinguishing input pattern.
//
// When it is proven, the equivalence is also added to the solver for good.
// It is already implied by the circuit, so no answer changes, but later
// proofs reach their conclusion with much less propagation (for a CONST 0
// merge, g simply becomes a fixed value).
bool
CirMgr::proveEqual(SatSolver& solver, const CirGate* g, const CirGate* r,
                   bool inv) const
{
   solver.assumeRelease();
   const Var gv = satVarOf(solver, const_cast<CirGate*>(g));
   if (r->isConst()) {                       // g == inv ?  fails iff g can be !inv
      solver.assumeProperty(gv, !inv);
      if (solver.assumpSolve()) return false;
      solver.assertProperty(gv, inv);
      return true;
   }
   const Var rv = satVarOf(solver, const_cast<CirGate*>(r));
   const Var diff = solver.newVar();
   solver.addXorCNF(diff, gv, false, rv, inv);
   solver.assumeProperty(diff, true);
   if (solver.assumpSolve()) return false;
   solver.assertProperty(diff, false);
   return true;
}
