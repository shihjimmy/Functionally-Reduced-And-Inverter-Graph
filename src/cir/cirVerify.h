/****************************************************************************
  FileName     [ cirVerify.h ]
  PackageName  [ cir ]
  Synopsis     [ Independent combinational equivalence checker ]
****************************************************************************/

#ifndef CIR_VERIFY_H
#define CIR_VERIFY_H

// The checker answers "do two AIGER files compute the same functions?".
//
// It is deliberately independent of the rest of the program: it parses the
// AIGER text itself into a tiny private graph and never touches CirGate or
// CirMgr.  So a bug in the netlist code (sweep / optimize / strash / fraig)
// cannot hide itself by also corrupting the checker.
//
// Method, per output pair (output i of A vs output i of B):
//   1. Structural hashing: both circuits are built into ONE graph that merges
//      identical AND nodes.  Outputs that end up on the same literal are
//      equivalent without further work.
//   2. Random simulation (64-bit parallel): a pattern on which the two
//      outputs differ is a counter-example, found without SAT.
//   3. SAT (MiniSat): "is there an input with out_A != out_B?"
//      UNSAT = proven equivalent; SAT = counter-example.  To keep each SAT
//      call small, internal nodes are first proven equal pairwise from the
//      inputs towards the outputs ("SAT sweeping"), and each proven equality
//      is added to the solver.
// Every counter-example is re-simulated before it is reported.
//
// Inputs are matched by position (PI i of A == PI i of B).  Undefined
// signals (used but never defined) are free variables matched by variable id.

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct VerifyMismatch
{
   size_t      output;    // index of the differing output
   std::string piValues;  // counter-example: '0'/'1' per PI, PI 0 first
   std::string undefs;    // values of undefined signals, e.g. "v7=1 v9=0"
};

struct VerifyResult
{
   bool        ok;          // false: the files could not be compared (see error)
   std::string error;
   size_t      numOutputs;
   size_t      byStructure; // equivalent: same node after structural hashing
   size_t      bySat;       // equivalent: proven by SAT
   size_t      numDiffering;
   std::vector<VerifyMismatch> mismatches;   // the first MAX_LISTED of them

   static const size_t MAX_LISTED = 100;

   VerifyResult(): ok(false), numOutputs(0), byStructure(0), bySat(0), numDiffering(0) {}
   bool equivalent() const { return ok && numDiffering == 0; }
};

// Called now and then: progress(stage, done, total), e.g. ("Sweeping nodes", 2048, 170000).
using VerifyProgress = std::function<void(const char*, size_t, size_t)>;

[[nodiscard]] VerifyResult verifyAag(const std::string& aagA, const std::string& aagB,
                       const VerifyProgress& progress = VerifyProgress());

#endif // CIR_VERIFY_H
