/****************************************************************************
  FileName     [ cirDef.h ]
  PackageName  [ cir ]
  Synopsis     [ Define basic data or var for cir package ]
  Author       [ Chung-Yang (Ric) Huang ]
  Copyright    [ Copyleft(c) 2012-present LaDs(III), GIEE, NTU, Taiwan ]
****************************************************************************/

#ifndef CIR_DEF_H
#define CIR_DEF_H

#include <vector>
#include "myHashMap.h"



class CirPValue;
class CirGateV;
class CirGate;
class CirPiGate;
class CirPoGate;
class CirAigGate;
class CirMgr;
class SatSolver;

// "using X = Y" (C++11) instead of "typedef Y X": same meaning, but the
// new name comes first, like a variable, and it also works for templates.
using GateList  = std::vector<CirGate*>;
using GateVList = std::vector<CirGateV>;
using IdList    = std::vector<unsigned>;

// enum class: the names live in GateType:: (no clash with anything else
// called "Pi"), and it does not convert to an integer by accident.
// ": unsigned char" makes it 1 byte.
enum class GateType : unsigned char { Undef, Pi, Po, Aig, Const };

// Simulation works on 64 patterns at a time (one bit per pattern in a word).
// "inline" (C++17): one definition shared by every .cpp that includes this
// header, instead of a separate copy in cirSim.cpp and cirFraig.cpp.
inline constexpr size_t kPatternWidth = 64;

// The steps of the spec's flow (CIRRead, CIROPTimize, ...); see CirMgr::whyNot().
enum class CirStep : unsigned char { Read, Optimize, Strash, Simulate, Fraig };

constexpr const char* gateTypeName(GateType t)
{
   switch (t) {
      case GateType::Undef: return "UNDEF";
      case GateType::Pi:    return "PI";
      case GateType::Po:    return "PO";
      case GateType::Aig:   return "AIG";
      case GateType::Const: return "CONST";
   }
   return "?";
}

#endif // CIR_DEF_H
