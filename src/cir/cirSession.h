/****************************************************************************
  FileName     [ cirSession.h ]
  PackageName  [ cir ]
  Synopsis     [ CirSession: the circuit the cir commands work on ]
****************************************************************************/

#ifndef CIR_SESSION_H
#define CIR_SESSION_H

// Stage 8 replaced the global "cirMgr" with this object.  main() owns one
// CirSession; every cir command keeps a reference to it (a reference, not a
// pointer: it always exists and outlives the commands, which main() also
// owns through the CmdParser).

#include <memory>
#include <utility>
#include "cirMgr.h"

class CirSession
{
public:
   // Borrowed; nullptr if no circuit has been read.
   CirMgr* circuit() const { return _circuit.get(); }
   // Replaces the current circuit (the old one is freed).
   void setCircuit(std::unique_ptr<CirMgr> c) { _circuit = std::move(c); }
   void clear() { _circuit.reset(); }

private:
   std::unique_ptr<CirMgr> _circuit;
};

#endif // CIR_SESSION_H
