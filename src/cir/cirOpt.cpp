/****************************************************************************
  FileName     [ cirOpt.cpp ]
  PackageName  [ cir ]
  Synopsis     [ Define cir optimization functions ]
  Author       [ Chung-Yang (Ric) Huang ]
  Copyright    [ Copyleft(c) 2008-present LaDs(III), GIEE, NTU, Taiwan ]
****************************************************************************/

#include <cassert>
#include "cirMgr.h"
#include "cirGate.h"
#include "util.h"

using namespace std;

/**************************************************************************
   How the netlist is edited
   -------------------------
   Every command that changes the circuit (sweep, optimize, strash, fraig)
   uses the same three primitives, so the bookkeeping lives in one place:

   mergeGate(old, driver)  every fanout of "old" is re-pointed to "driver"
                           (with phase), then "old" is retired.
   retireGate(g)           g is unlinked from its fanins' fanout lists, and
                           its ownership moves from _gates to _retired: it
                           is not freed until freeRetired(). Borrowed
                           pointers to it (e.g. in a copy of _dfsList that
                           we are still walking) stay valid until then.
   rebuildDerivedLists()   recomputes everything that is derived from the
                           netlist: DFS list, UNDEF / floating / unused lists.

   Cost: each fanout entry is moved once, and removing an entry from a
   fanout list costs O(fanout degree), so every command is linear in the
   number of edges.
**************************************************************************/

/***************************************************/
/*   Private member functions for netlist editing  */
/***************************************************/
// Remove ONE occurrence of "fanout" from the fanout list of "driver",
// keeping the order of the remaining entries (it is visible in "cirg -fo").
void
CirMgr::removeFanout(CirGate* driver, CirGate* fanout)
{
   GateList& fanouts = _fanoutInfo[driver->getGid()];
   for (size_t i = 0; i < fanouts.size(); ++i)
      if (fanouts[i] == fanout) {
         fanouts.erase(fanouts.begin() + i);
         return;
      }
}

void
CirMgr::retireGate(CirGate* g)
{
   for (unsigned i = 0; i < g->numFanins(); ++i)
      removeFanout(g->fanin(i).gate(), g);
   const unsigned id = g->getGid();
   clearList(_fanoutInfo[id]);
   if (g->isAig()) --_header.numAig;
   assert(_gates[id].get() == g);
   _retired.push_back(std::move(_gates[id]));   // _gates[id] is now empty
}

// Replace "oldGate" by "newDriver" everywhere, then retire "oldGate".
void
CirMgr::mergeGate(CirGate* oldGate, const CirGateV& newDriver)
{
   CirGate* newGate = newDriver.gate();
   const bool inv = newDriver.isInv();
   assert(newGate != oldGate);

   // Detach first, so a fanout list never contains "oldGate" after we
   // move its fanouts (newGate may be one of oldGate's own fanins).
   GateList movedFanouts;
   movedFanouts.swap(_fanoutInfo[oldGate->getGid()]);
   retireGate(oldGate);

   GateList& newFanouts = _fanoutInfo[newGate->getGid()];
   for (CirGate* fo : movedFanouts) {
      fo->replaceFanin(oldGate, newGate, inv);
      newFanouts.push_back(fo);
   }
}

void
CirMgr::freeRetired()
{
   _retired.clear();   // the unique_ptrs free the gates
   _retired.shrink_to_fit();
}

// Mark (with the global ref) every gate reachable from the POs, including
// UNDEF gates, which are reachable but never stored in _dfsList.
void
CirMgr::markDfsCone() const
{
   CirGate::setGlobalRef();
   for (CirGate* g : _dfsList) {
      g->setToGlobalRef();
      for (unsigned i = 0; i < g->numFanins(); ++i)
         g->fanin(i).gate()->setToGlobalRef();
   }
}

// An UNDEF gate exists only because some gate referred to it; when nobody
// refers to it any more it is deleted. Then all derived lists are rebuilt.
void
CirMgr::rebuildDerivedLists()
{
   clearList(_undefList);
   for (unsigned id = 1, n = getNumTots(); id < n; ++id) {
      CirGate* g = getGate(id);
      if (!g || !g->isUndef()) continue;
      if (_fanoutInfo[id].empty()) retireGate(g);
      else _undefList.push_back(id);
   }
   freeRetired();
   genDfsList();
   checkFloatList();
   checkUnusedList();
}

/**************************************************/
/*   Public member functions about optimization   */
/**************************************************/
// Remove every AIG / UNDEF gate that cannot be reached from the POs.
// PIs, POs and CONST are never removed. The DFS list does not change.
void
CirMgr::sweep()
{
   markDfsCone();
   for (unsigned id = 1, n = getNumTots(); id < n; ++id) {
      CirGate* g = getGate(id);
      if (!g || g->isGlobalRef()) continue;
      if (!g->isAig() && !g->isUndef()) continue;
      cout << "Sweeping: " << g->typeName() << "(" << id << ") removed...\n";
      retireGate(g);
   }
   rebuildDerivedLists();
}

// Constant propagation and trivial AND simplification, gate by gate in
// DFS order. Because fanins come before fanouts in DFS order, a gate's
// fanins are already in their final form when the gate is visited, so one
// pass is enough.
void
CirMgr::optimize()
{
   assert(!whyNot(CirStep::Optimize));
   _lastStep = CirStep::Optimize;
   const GateList dfs = _dfsList;   // copy: retired gates stay valid in it
   for (CirGate* g : dfs) {
      const CirAigGate* aig = g->asAig();
      if (!aig) continue;
      CirGateV result;
      if (!simplifyAig(aig, result)) continue;
      cout << "Simplifying: " << (result.isInv() ? "!" : "")
           << result.gate()->getGid() << " merging " << g->getGid()
           << "...\n";
      mergeGate(g, result);
   }
   rebuildDerivedLists();
}

/***************************************************/
/*   Private member functions about optimization   */
/***************************************************/
// The four rules of CIROPTimize, for g = AND(a, b):
//   a == 1   =>  g = b          a == 0   =>  g = 0
//   a == b   =>  g = a          a == !b  =>  g = 0
// (and the same with a and b swapped). Returns false if none applies.
bool
CirMgr::simplifyAig(const CirAigGate* g, CirGateV& result) const
{
   const CirGateV a = g->in0(), b = g->in1();
   const CirGateV zero(const0());

   if (a.gate()->isConst()) { result = a.isInv() ? b : zero; return true; }
   if (b.gate()->isConst()) { result = b.isInv() ? a : zero; return true; }
   if (a.gate() == b.gate()) {
      result = (a.isInv() == b.isInv()) ? a : zero;
      return true;
   }
   return false;
}
