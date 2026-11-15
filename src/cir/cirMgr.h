/****************************************************************************
  FileName     [ cirMgr.h ]
  PackageName  [ cir ]
  Synopsis     [ Define circuit manager ]
  Author       [ Chung-Yang (Ric) Huang ]
  Copyright    [ Copyleft(c) 2008-present LaDs(III), GIEE, NTU, Taiwan ]
****************************************************************************/

#ifndef CIR_MGR_H
#define CIR_MGR_H

#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <random>
#include <memory>
#include <optional>
#include <string_view>


// TODO: Feel free to define your own classes, variables, or functions.

#include "cirDef.h"
#include "sat.h"


class CirMgr
{
   enum CirMgrFlag { NO_FEC = 0x1 };
   // The numbers in the AIGER header "aag M I L O A".
   struct AagHeader {
      unsigned maxVar = 0, numPi = 0, numLatch = 0, numPo = 0, numAig = 0;
   };

public:
   // Ownership (docs/OWNERSHIP.md): CirMgr owns every gate through _gates
   // (and _retired); all other CirGate* in the program only borrow.  No
   // destructor and no copy control are written here ("Rule of zero"):
   // unique_ptr members free the gates, and since unique_ptr cannot be
   // copied, CirMgr cannot be copied either (it can be moved).
   // Both are defined in cirMgr.cpp, not here.  The one exception to "Rule
   // of zero" above: _gates holds unique_ptr<CirGate>, and code that only
   // sees this header (main.cpp) has CirGate as an incomplete type, so it
   // cannot generate the code that deletes gates.  The destructor deletes
   // them; the constructor needs it too (if it threw, the members already
   // built would be destroyed).  cirMgr.cpp sees the complete CirGate.
   CirMgr();
   ~CirMgr();

   // --- The spec's flow rules (stage 8: moved here from the command layer).
   // What may be done next depends on what was done to THIS circuit, so the
   // circuit keeps track of it.  whyNot(step) is nullptr if "step" may run
   // now, otherwise the error message the command prints.  optimize(),
   // strash(), randomSim(), fileSim() and fraig() assert it and record it.
   // std::optional (C++17): "maybe an error"; no value = allowed.
   [[nodiscard]] std::optional<std::string_view> whyNot(CirStep next) const;
   const char* stageName() const;        // "READ", "OPT", ... (for the GUI)

   // Access functions
   // Borrowed pointers; nullptr if no gate has this ID.
   CirGate* operator [](unsigned gid) const { return _gates[gid].get(); }
   CirGate* getGate(unsigned gid) const {
      return gid < _gates.size() ? _gates[gid].get() : nullptr; }
   CirGate* const0() const { return _gates[0].get(); }
   CirGate* litId2Gate(unsigned litId) const { return getGate(litId/2); }
   CirGateV litId2GateV(unsigned litId) const;
   bool isFlag(CirMgrFlag f) const { return _flag & f; }
   void setFlag(CirMgrFlag f) const { _flag |= f; }
   void unsetFlag(CirMgrFlag f) const { _flag &= ~f; }
   void resetFlag() const { _flag = 0; }
   unsigned getNumPIs() const { return _header.numPi; }
   unsigned getNumPOs() const { return _header.numPo; }
   unsigned getNumTots() const { return unsigned(_gates.size()); }   // M + O + 1 IDs
   unsigned getNumAigs() const { return _header.numAig; }
   const GateList& getDfsList() const { return _dfsList; }
   CirPiGate* getPi(unsigned i) const { return _piList[i]; }
   CirPoGate* getPo(unsigned i) const { return _poList[i]; }
   GateList& getFanouts(unsigned i) { return _fanoutInfo[i]; }
   const GateList& getFanouts(unsigned i) const { return _fanoutInfo[i]; }

   // Internal consistency (fanin/fanout agree, DFS order, counts...);
   // "" if consistent, otherwise what is wrong.  See cirCheck.cpp.
   [[nodiscard]] std::string checkInvariants() const;

   // Member functions about circuit construction
   [[nodiscard]] bool readCircuit(const std::string&);
   void genDfsList();
//   void updateUndefList();
   void checkFloatList();
   void checkUnusedList();
   CirGateV edgeForLiteral(unsigned litId);

   // Member functions about circuit optimization
   void sweep();
   void optimize();

   // Member functions about simulation
   void randomSim();
   void fileSim(std::ifstream&);
   void setSimLog(std::ofstream *logFile) { _simLog = logFile; }

   // Member functions about fraig
   void strash();
   const IdList& getFECGrps(size_t i) const { return _fecGrps[i]; }
   size_t getNumFECGrps() const { return _fecGrps.size(); }
   void printFEC() const;
   void fraig();

   // Member functions about circuit reporting
   void printSummary() const;
   void printNetlist() const;
   void printPIs() const;
   void printPOs() const;
   void printFloatGates() const;
   void printFECPairs() const;
   void writeAag(std::ostream&) const;

   // The file given to CIRRead, as it was when it was read.  CIRVerify
   // compares the current (optimized) circuit against this snapshot.
   const std::string& sourceName() const { return _sourceName; }
   const std::string& sourceText() const { return _sourceText; }
   void writeGate(std::ostream&, CirGate*) const;
   void writeJson(std::ostream&) const;   // cirJson.cpp, used by the GUI

private:
   friend struct CirMgrTestPeer;   // unit tests only: corrupts a netlist on purpose
   // Reading a file: fanin literals are kept here until every gate exists,
   // then connectFanins() turns them into edges.  (A CirGateV is always a
   // real edge; it never holds a literal number.)
   struct PendingFanins {
      CirGate* gate;
      unsigned lit[2];
      unsigned n;       // 1 for a PO, 2 for an AIG
   };
   std::vector<PendingFanins> _pending;
   void connectFanins();
   // The parser's state while one file is read (defined in cirMgr.cpp).
   struct AagLexer;

   void strashNetlist();   // the work of strash(), without the flow step (fraig uses it)

   AagHeader           _header;
   CirStep             _lastStep = CirStep::Read;   // see whyNot()
   mutable unsigned    _flag;
   // --- owners ---
   std::vector<std::unique_ptr<CirGate>> _gates;    // index = gate ID; [0] is CONST 0
   std::vector<std::unique_ptr<CirGate>> _retired;  // removed but not yet freed, see retireGate()
   // --- borrowers (never delete through these) ---
   std::vector<CirPiGate*>  _piList;
   std::vector<CirPoGate*>  _poList;
   // IDs in _undefList are NOT sorted!!
   IdList              _undefList;
   // Make sure the IDs of the following lists are sorted!!
   IdList              _floatList;  // gates with fanin(s) undefined
   IdList              _unusedList; // gates defined but not used
   GateList            _dfsList;
   std::vector<GateList>    _fanoutInfo;  // index = gate ID
   // FEC groups. Each group stores litIds (gateId*2 + phase) in ascending
   // gate-ID order; the phase is relative to the group's first member, so
   // the first member is always positive. A gate's _fecId = groupIdx*2+phase.
   std::vector<IdList>      _fecGrps;
   bool                _fecReady;   // groups initialized by a simulation
   std::ofstream           *_simLog;     // borrowed from CIRSimulate, only while it runs
   std::mt19937_64          _rng;        // fixed seed => reproducible random sim
   std::string              _sourceName; // see sourceText()
   std::string              _sourceText;

   // private member functions for circuit parsing
   bool parseHeader(std::ifstream&, AagLexer&);
   bool parseInput(std::ifstream&, AagLexer&);
   bool parseLatch(std::ifstream&, AagLexer&);
   bool parseOutput(std::ifstream&, AagLexer&);
   bool parseAig(std::ifstream&, AagLexer&);
   bool parseSymbol(std::ifstream&, AagLexer&);
   bool parseComment(std::ifstream&, AagLexer&);
   bool checkId(unsigned&, const std::string&, AagLexer&);
   CirGate* checkGate(unsigned&, GateType, const std::string&, AagLexer&);

   // private member functions for netlist editing (cirOpt.cpp)
   // These are shared by sweep, optimize, strash and fraig.
   void mergeGate(CirGate* oldGate, const CirGateV& newDriver);
   void retireGate(CirGate* g);
   void removeFanout(CirGate* driver, CirGate* fanout);
   void freeRetired();
   void markDfsCone() const;
   void rebuildDerivedLists();
   bool simplifyAig(const CirAigGate* g, CirGateV& result) const;

   // private member functions about simulation (cirSim.cpp)
   void simulatePatterns(const std::vector<size_t>& piWords, size_t nPatterns);
   void writeSimLog(const std::vector<size_t>& piWords, size_t nPatterns) const;
   void initFECGroups();
   void refineFECGroups();
   void splitByValue(const IdList& members, bool allowInverse,
                     std::vector<IdList>& out) const;
   void purgeFECGroups();
   void setFECGroups(std::vector<IdList>& groups);
   void clearFECGroups();

   // private member functions about FRAIG (cirFraig.cpp)
   void buildSatModel(SatSolver& solver) const;
   Var satVarOf(SatSolver&, CirGate*) const;   // lazy CNF, see buildSatModel()
   static constexpr Var kNoSatVar = -1;
   void proveFECGroups(SatSolver& solver, std::vector<size_t>& cexWords,
                       size_t& nCex);
   bool proveEqual(SatSolver& solver, const CirGate* g, const CirGate* r,
                   bool inv) const;
};

#endif // CIR_MGR_H
