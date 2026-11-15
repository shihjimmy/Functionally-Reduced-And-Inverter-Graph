/****************************************************************************
  FileName     [ cirJson.cpp ]
  PackageName  [ cir ]
  Synopsis     [ Export the circuit as JSON (for the GUI front end) ]
****************************************************************************/

#include <iostream>
#include "cirMgr.h"
#include "cirGate.h"

using namespace std;

/**************************************************************************
   JSON format (one object, written by "CIRJson"):
   {
     "gates": [ { "id": 4, "type": "AIG", "line": 7, "name": "x",
                  "in": [[2, 0], [3, 1]],     // [fanin id, inverted?]
                  "dfs": 2,                   // position in DFS list, -1 if
                                              // unreachable from the POs
                  "fec": 3 }, ... ],          // FEC group index, -1 if none
     "pis": [ids...], "pos": [ids...],
     "fec": [[litIds...], ...],               // the FEC groups
     "floating": [ids...], "unused": [ids...],
     "stats": { "pi": n, "po": n, "aig": n }
   }
   Gates are listed in ascending ID order; removed IDs are absent.
**************************************************************************/

static void
writeJsonString(ostream& os, const char* s)
{
   os << '"';
   for (; *s; ++s) {
      const unsigned char c = *s;
      if (c == '"' || c == '\\') os << '\\' << c;
      else if (c < 0x20) os << ' ';
      else os << c;
   }
   os << '"';
}

static void
writeIdArray(ostream& os, const IdList& ids)
{
   os << '[';
   for (size_t i = 0; i < ids.size(); ++i) os << (i ? "," : "") << ids[i];
   os << ']';
}

void
CirMgr::writeJson(ostream& os) const
{
   vector<int> dfsPos(getNumTots(), -1);
   for (size_t i = 0; i < _dfsList.size(); ++i)
      dfsPos[_dfsList[i]->getGid()] = int(i);

   os << "{\"gates\":[";
   bool first = true;
   for (unsigned id = 0, n = getNumTots(); id < n; ++id) {
      const CirGate* g = getGate(id);
      if (!g) continue;
      os << (first ? "" : ",") << "\n{\"id\":" << id
         << ",\"type\":\"" << g->typeName() << "\",\"line\":"
         << g->getLineNo();
      first = false;
      if (!g->getName().empty()) { os << ",\"name\":"; writeJsonString(os, g->getName().c_str()); }
      os << ",\"in\":[";
      for (unsigned i = 0; i < g->numFanins(); ++i) {
         const CirGateV in = g->fanin(i);
         os << (i ? "," : "") << '[' << in.gateId() << ',' << in.isInv() << ']';
      }
      os << "],\"dfs\":" << dfsPos[id]
         << ",\"fec\":" << (g->isFEC() ? int(g->getFECId() / 2) : -1) << '}';
   }
   os << "],\n\"pis\":[";
   for (unsigned i = 0; i < getNumPIs(); ++i)
      os << (i ? "," : "") << getPi(i)->getGid();
   os << "],\"pos\":[";
   for (unsigned i = 0; i < getNumPOs(); ++i)
      os << (i ? "," : "") << getPo(i)->getGid();
   os << "],\n\"fec\":[";
   for (size_t i = 0; i < _fecGrps.size(); ++i) {
      os << (i ? "," : "");
      writeIdArray(os, _fecGrps[i]);
   }
   os << "],\"floating\":";
   writeIdArray(os, _floatList);
   os << ",\"unused\":";
   writeIdArray(os, _unusedList);
   os << ",\"stats\":{\"pi\":" << getNumPIs() << ",\"po\":" << getNumPOs()
      << ",\"aig\":" << getNumAigs() << "}}";
}
