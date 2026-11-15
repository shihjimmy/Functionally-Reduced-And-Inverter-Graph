/****************************************************************************
  FileName     [ cirCheck.cpp ]
  PackageName  [ cir ]
  Synopsis     [ CirMgr::checkInvariants(): internal consistency of a netlist ]
****************************************************************************/

// The golden-output tests only see what the program prints.  The netlist
// also has internal relations that nothing prints, and every editing
// command (sweep, optimize, strash, fraig) must keep them.  The unit tests
// call checkInvariants() after each command.  O(gates + edges).

#include <sstream>
#include <unordered_map>
#include "cirMgr.h"
#include "cirGate.h"

using std::string;

namespace {

string describe(const CirGate* g)
{
   std::ostringstream os;
   os << g->typeName() << "(" << g->getGid() << ")";
   return os.str();
}

// Number of times "g" appears in "list".
size_t countIn(const GateList& list, const CirGate* g)
{
   size_t n = 0;
   for (const CirGate* x : list) n += (x == g);
   return n;
}

} // namespace

// Returns "" if the netlist is consistent, otherwise what is wrong.
string
CirMgr::checkInvariants() const
{
   std::ostringstream err;
   const unsigned M = _header.maxVar, O = _header.numPo;

   // 1. ID table: one slot per ID, each gate stored under its own ID.
   if (_gates.size() != M + O + 1) {
      err << "_gates has " << _gates.size() << " slots, expected M+O+1 = " << M + O + 1;
      return err.str();
   }
   if (!_gates[0] || !_gates[0]->isConst()) return "ID 0 is not the CONST gate";
   if (!_retired.empty()) return "retired gates were not freed";

   unsigned numAig = 0;
   for (unsigned id = 0; id < _gates.size(); ++id) {
      const CirGate* g = _gates[id].get();
      if (!g) continue;
      if (g->getGid() != id) { err << describe(g) << " is stored under ID " << id; return err.str(); }
      if ((id > M) != g->isPo()) { err << describe(g) << " has an ID in the wrong range"; return err.str(); }
      if (g->isAig()) ++numAig;
   }
   if (numAig != _header.numAig) {
      err << "AIG count is " << _header.numAig << " but there are " << numAig << " AIG gates";
      return err.str();
   }

   // 2. PI / PO lists point to live gates of the right type.
   for (unsigned i = 0; i < _piList.size(); ++i)
      if (!_piList[i] || !_piList[i]->isPi() || getGate(_piList[i]->getGid()) != _piList[i]) {
         err << "PI list entry " << i << " is wrong"; return err.str(); }
   for (unsigned i = 0; i < _poList.size(); ++i)
      if (!_poList[i] || _poList[i]->getGid() != M + 1 + i || getGate(M + 1 + i) != _poList[i]) {
         err << "PO list entry " << i << " is wrong"; return err.str(); }

   // 3. Fanin and fanout agree: if g has k fanin edges from f, then g
   //    appears exactly k times in f's fanout list, and nothing else does.
   for (unsigned id = 0; id < _gates.size(); ++id) {
      const CirGate* g = _gates[id].get();
      if (!g) continue;
      for (unsigned k = 0; k < g->numFanins(); ++k) {
         const CirGateV e = g->fanin(k);
         if (!e) { err << describe(g) << " has a null fanin"; return err.str(); }
         const CirGate* f = e.gate();
         if (getGate(f->getGid()) != f) {
            err << describe(g) << " has a fanin that is not in the netlist (freed or retired?)";
            return err.str();
         }
         size_t edges = 0;
         for (unsigned j = 0; j < g->numFanins(); ++j) edges += (g->fanin(j).gate() == f);
         if (countIn(_fanoutInfo[f->getGid()], g) != edges) {
            err << describe(g) << " uses " << describe(f) << " " << edges << " time(s), but "
                << describe(f) << "'s fanout list has it "
                << countIn(_fanoutInfo[f->getGid()], g) << " time(s)";
            return err.str();
         }
      }
   }
   for (unsigned id = 0; id < _fanoutInfo.size(); ++id) {
      for (const CirGate* fo : _fanoutInfo[id]) {
         if (!getGate(id)) { err << "ID " << id << " has no gate but has fanouts"; return err.str(); }
         if (getGate(fo->getGid()) != fo) {
            err << describe(getGate(id)) << " has a fanout that is not in the netlist";
            return err.str();
         }
         bool uses = false;
         for (unsigned k = 0; k < fo->numFanins(); ++k) uses |= (fo->fanin(k).gate() == getGate(id));
         if (!uses) {
            err << describe(fo) << " is in " << describe(getGate(id))
                << "'s fanout list but does not use it";
            return err.str();
         }
      }
   }

   // 4. DFS list: live gates, no UNDEF, each fanin (except UNDEF) earlier,
   //    every PO present.
   std::unordered_map<const CirGate*, size_t> pos;
   for (size_t i = 0; i < _dfsList.size(); ++i) {
      const CirGate* g = _dfsList[i];
      if (getGate(g->getGid()) != g) { err << "DFS list has a gate that is not in the netlist"; return err.str(); }
      if (g->isUndef()) { err << "DFS list contains " << describe(g); return err.str(); }
      if (!pos.emplace(g, i).second) { err << "DFS list has " << describe(g) << " twice"; return err.str(); }
      for (unsigned k = 0; k < g->numFanins(); ++k) {
         const CirGate* f = g->fanin(k).gate();
         if (f->isUndef()) continue;
         if (!pos.count(f)) {
            err << "in the DFS list, " << describe(g) << " comes before its fanin " << describe(f);
            return err.str();
         }
      }
   }
   for (const CirPoGate* po : _poList)
      if (!pos.count(po)) { err << describe(po) << " is not in the DFS list"; return err.str(); }

   // 5. UNDEF list: exactly the UNDEF gates.
   size_t numUndef = 0;
   for (unsigned id = 0; id < _gates.size(); ++id)
      numUndef += (_gates[id] && _gates[id]->isUndef());
   if (numUndef != _undefList.size()) {
      err << "UNDEF list has " << _undefList.size() << " entries but there are " << numUndef << " UNDEF gates";
      return err.str();
   }
   for (unsigned id : _undefList)
      if (!getGate(id) || !getGate(id)->isUndef()) { err << "UNDEF list entry " << id << " is wrong"; return err.str(); }

   return "";
}
