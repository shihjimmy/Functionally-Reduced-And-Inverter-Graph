/****************************************************************************
  FileName     [ cirGate.cpp ]
  PackageName  [ cir ]
  Synopsis     [ Define class CirAigGate member functions ]
  Author       [ Chung-Yang (Ric) Huang ]
  Copyright    [ Copyleft(c) 2008-present LaDs(III), GIEE, NTU, Taiwan ]
****************************************************************************/

#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdarg.h>
#include <cassert>
#include "cirGate.h"
#include "cirMgr.h"
#include "util.h"

using namespace std;

// The report functions print what CIRGate shows.


unsigned CirGate::_globalRef_s = 0;

/*
#if 1 // 1 for opt mode
#define NDEBUG
#endif
*/

/***************************************/
/*   class CirGateV member functions   */
/***************************************/
unsigned CirGateV::gateId() const { return gate()->getGid(); }
unsigned CirGateV::litId() const { return gate()->getGid() * 2 + (isInv() ? 1 : 0); }

ostream& operator << (ostream& os, const CirGateV& gv)
{
   if (gv.isFloat()) os << "*";
   if (gv.isInv()) os << "!";
   os << gv.gateId();
   return os;
}

// Print as 8 groups of 8 bits (MSB first), separated by '_'
ostream& operator << (ostream& os, const CirPValue& v)
{
   const size_t nBits = sizeof(size_t) * 8;
   for (size_t i = nBits; i > 0; --i) {
      os << ((v._pValue >> (i - 1)) & 0x1);
      if ((i - 1) % 8 == 0 && i != 1) os << '_';
   }
   return os;
}

/**************************************/
/*   class CirGate member functions   */
/**************************************/
void
CirPiGate::printGate() const
{
   cout << setw(4) << left << typeName() << _gid;
   if (!_name.empty()) cout << " (" << _name << ")";
   cout << endl;
}

void
CirPoGate::printGate() const
{
   cout << setw(4) << left << typeName() << _gid << " " << _in0;
   if (!_name.empty()) cout << " (" << _name << ")";
   cout << endl;
}

void
CirAigGate::printGate() const
{
   cout << setw(4) << left << typeName() << _gid << " " << _in0 << " "
        << _in1 << endl;
}

void
CirConstGate::printGate() const
{
   cout << setw(4) << left << typeName() << _gid << endl;
}

void
CirGate::reportGate(const CirMgr& circuit) const
{
   unsigned w = 80;
   for (unsigned i = 0; i < w; ++i) cout << '=';
   cout << endl;

   // Printing gate info
   ostringstream osstr;
   osstr << "= " << typeName() << "(" << _gid << ")";
   if (!getName().empty()) osstr << "\"" << getName() << "\"";
   osstr << ", line " << _lineNo;
   cout << osstr.str() << endl;

   // Printing FEC Pairs
   osstr.str("");
   osstr << "= FECs:";
   if (isFEC()) {
      const IdList& fecs = circuit.getFECGrps(_fecId/2);
      bool isInv = _fecId & 1;
      for (unsigned lit : fecs) {
         if (lit / 2 == _gid) continue;
         osstr << ((isInv ^ (lit % 2 != 0))?" !":" ") << lit / 2;
      }
   }
   cout << osstr.str() << endl;

   // Printing sim value
   osstr.str("");
   osstr << "= Value: " << _pValue;
   cout << osstr.str() << endl;

   for (unsigned i = 0; i < w; ++i) cout << '=';
   cout << endl;
}

void
CirGate::reportFanin(int level) const
{
   assert (level >= 0);
   setGlobalRef();
   reportFaninRecur(level, 0, false);
}

void
CirGate::reportFaninRecur(int level, int repLevel, bool isInv) const
{
   if (repLevel > 0)  cout << setw(repLevel*2) << ' ';
   cout << (isInv?"!":"") << typeName() << " " << _gid;
/*
   #ifndef NDEBUG
   cout << " " << (_pValue()&0x1);
   #endif
*/
   if (level == repLevel) { cout << endl; return; }
   assert (level > repLevel);
   if (isGlobalRef() && numFanins() != 0) {
      cout << " (*)" << endl; return; }
   cout << endl;
   setToGlobalRef();
   for (unsigned i = 0; i < numFanins(); ++i)
      fanin(i).gate()->reportFaninRecur(level, repLevel + 1, fanin(i).isInv());
}

void
CirGate::reportFanout(const CirMgr& circuit, int level) const
{
   assert (level >= 0);
   setGlobalRef();
   reportFanoutRecur(circuit, level, 0, false);
}

void
CirGate::reportFanoutRecur(const CirMgr& circuit, int level, int repLevel, bool isInv) const
{
   if (repLevel > 0)  cout << setw(repLevel*2) << ' ';
   cout << (isInv?"!":"") << typeName() << " " << _gid;
   if (level == repLevel) { cout << endl; return; }
   assert (level > repLevel);
   const GateList& fanouts = circuit.getFanouts(_gid);
   size_t nFanouts = fanouts.size();
   if (isGlobalRef() && nFanouts != 0) { cout << " (*)" << endl; return; }
   cout << endl;
   setToGlobalRef();
   for (size_t i = 0; i < nFanouts; ++i) {
      // The phase of the edge from this gate into the fanout (its first
      // fanin that is this gate).
      const CirGate *fanout = fanouts[i];
      bool fanoutInv = false;
      for (unsigned k = 0; k < fanout->numFanins(); ++k)
         if (fanout->fanin(k).gate() == this) { fanoutInv = fanout->fanin(k).isInv(); break; }
      fanouts[i]->reportFanoutRecur(circuit, level, repLevel + 1, fanoutInv);
   }
}

