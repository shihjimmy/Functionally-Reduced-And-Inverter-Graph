/****************************************************************************
  FileName     [ cirVerify.cpp ]
  PackageName  [ cir ]
  Synopsis     [ Independent combinational equivalence checker ]
****************************************************************************/

// See cirVerify.h for what this checks and why it does not use CirGate.
//
// Complexity: parsing and building are O(size of both files); simulation is
// O(nodes) per 64 patterns; each SAT call is NP-hard in the worst case, but
// structural hashing and simulation decide most outputs before SAT is called.

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <unordered_map>
#include "cirVerify.h"
#include "sat.h"

using std::string;
using std::vector;

namespace {

// ---------------------------------------------------------------------------
// 1. A parsed AIGER file (ASCII "aag", combinational only)
// ---------------------------------------------------------------------------
struct AagFile
{
   unsigned         maxVar = 0;
   vector<unsigned> inputs;   // variable ids, in file order
   vector<unsigned> outputs;  // literals, in file order
   vector<unsigned> andLhs;   // AND i defines variable andLhs[i] ...
   vector<unsigned> andIn0;   // ... = literal andIn0[i] & literal andIn1[i]
   vector<unsigned> andIn1;
};

bool readUnsigned(std::istream& in, unsigned& v)
{
   long long x;
   if (!(in >> x) || x < 0 || x > 0x7fffffffLL) return false;
   v = unsigned(x);
   return true;
}

// Returns "" on success, otherwise an error message.
string parseAag(const string& text, AagFile& f)
{
   std::istringstream in(text);
   string magic;
   unsigned numIn, numLatch, numOut, numAnd;
   if (!(in >> magic) || magic != "aag" || !readUnsigned(in, f.maxVar) ||
       !readUnsigned(in, numIn) || !readUnsigned(in, numLatch) ||
       !readUnsigned(in, numOut) || !readUnsigned(in, numAnd))
      return "bad header";
   if (numLatch != 0) return "latches are not supported";

   const unsigned maxLit = 2 * f.maxVar + 1;
   f.inputs.resize(numIn);
   for (unsigned& v : f.inputs) {
      unsigned lit;
      if (!readUnsigned(in, lit) || lit > maxLit || lit < 2 || lit % 2)
         return "bad input literal";
      v = lit / 2;
   }
   f.outputs.resize(numOut);
   for (unsigned& lit : f.outputs)
      if (!readUnsigned(in, lit) || lit > maxLit) return "bad output literal";
   f.andLhs.resize(numAnd); f.andIn0.resize(numAnd); f.andIn1.resize(numAnd);
   for (unsigned i = 0; i < numAnd; ++i) {
      unsigned lhs;
      if (!readUnsigned(in, lhs) || !readUnsigned(in, f.andIn0[i]) ||
          !readUnsigned(in, f.andIn1[i]) || lhs > maxLit || lhs < 2 || lhs % 2 ||
          f.andIn0[i] > maxLit || f.andIn1[i] > maxLit)
         return "bad AND gate";
      f.andLhs[i] = lhs / 2;
   }
   return "";   // symbol table and comments are ignored
}

// ---------------------------------------------------------------------------
// 2. One structurally hashed graph holding both circuits
//
// Node 0 is constant 0.  A literal is 2 * node + (1 if inverted).  Nodes are
// created fanins-first, so increasing node order is a topological order.
// ---------------------------------------------------------------------------
class Graph
{
public:
   enum Kind { CONST0, INPUT, FREE, AND };
   static const unsigned NONE = ~0u;

   Graph() { addNode(CONST0, 0, 0); }

   size_t   size() const           { return _kind.size(); }
   Kind     kind(unsigned n) const { return _kind[n]; }
   unsigned in0(unsigned n) const  { return _in0[n]; }
   unsigned in1(unsigned n) const  { return _in1[n]; }

   unsigned newInput() { return 2 * addNode(INPUT, 0, 0); }

   // Undefined signal with AIGER variable id "var": one free node per id,
   // shared by both circuits.
   unsigned freeVar(unsigned var) {
      if (auto it = _free.find(var); it != _free.end()) return it->second;
      const unsigned lit = 2 * addNode(FREE, 0, 0);
      _free[var] = lit;
      _freeVar.push_back(var);
      return lit;
   }
   // AIGER variable id of the i-th FREE node (for printing counter-examples)
   unsigned freeVarOf(size_t i) const { return _freeVar[i]; }

   // a & b, with trivial simplification and structural hashing: O(1) average
   unsigned mkAnd(unsigned a, unsigned b) {
      if (a > b) std::swap(a, b);
      if (a == 0 || a == (b ^ 1)) return 0;   // 0 & x, x & ~x
      if (a == 1 || a == b) return b;         // 1 & x, x & x
      const uint64_t key = (uint64_t(a) << 32) | b;
      if (auto it = _strash.find(key); it != _strash.end()) return it->second;
      const unsigned lit = 2 * addNode(AND, a, b);
      _strash[key] = lit;
      return lit;
   }

private:
   unsigned addNode(Kind k, unsigned a, unsigned b) {
      _kind.push_back(k); _in0.push_back(a); _in1.push_back(b);
      return unsigned(_kind.size() - 1);
   }

   vector<Kind>     _kind;
   vector<unsigned> _in0, _in1;
   std::unordered_map<uint64_t, unsigned> _strash;
   std::unordered_map<unsigned, unsigned> _free;   // AIGER var -> literal
   vector<unsigned> _freeVar;                      // FREE nodes, creation order
};

// Builds one parsed file into the graph.  "inputs" are the graph literals of
// the shared PIs.  Fills "outputs" with the graph literal of every output.
// Iterative DFS (no recursion: real circuits can be very deep).
string buildInto(const AagFile& f, const vector<unsigned>& inputs, Graph& g,
                 vector<unsigned>& outputs)
{
   const unsigned UNSET = Graph::NONE;
   vector<unsigned> lit(f.maxVar + 1, UNSET);     // AIGER var -> graph literal
   vector<unsigned> andOf(f.maxVar + 1, UNSET);   // AIGER var -> AND index
   vector<char>     onStack(f.maxVar + 1, 0);

   lit[0] = 0;
   for (size_t i = 0; i < f.inputs.size(); ++i) {
      if (lit[f.inputs[i]] != UNSET) return "input defined twice";
      lit[f.inputs[i]] = inputs[i];
   }
   for (unsigned i = 0; i < f.andLhs.size(); ++i) {
      const unsigned v = f.andLhs[i];
      if (lit[v] != UNSET || andOf[v] != UNSET) return "variable defined twice";
      andOf[v] = i;
   }

   vector<unsigned> stack;
   for (unsigned out : f.outputs) {
      stack.push_back(out / 2);
      while (!stack.empty()) {
         const unsigned v = stack.back();
         if (lit[v] != UNSET) { stack.pop_back(); continue; }
         if (andOf[v] == UNSET) {                 // used but never defined
            lit[v] = g.freeVar(v); stack.pop_back(); continue;
         }
         const unsigned a = f.andIn0[andOf[v]], b = f.andIn1[andOf[v]];
         const bool readyA = lit[a / 2] != UNSET, readyB = lit[b / 2] != UNSET;
         if (readyA && readyB) {
            lit[v] = g.mkAnd(lit[a / 2] ^ (a & 1), lit[b / 2] ^ (b & 1));
            onStack[v] = 0;
            stack.pop_back();
            continue;
         }
         if (onStack[v]) return "combinational loop";   // revisited before done
         onStack[v] = 1;
         if (!readyA) { if (onStack[a / 2]) return "combinational loop"; stack.push_back(a / 2); }
         if (!readyB) { if (onStack[b / 2]) return "combinational loop"; stack.push_back(b / 2); }
      }
      outputs.push_back(lit[out / 2] ^ (out & 1));
   }
   return "";
}

// ---------------------------------------------------------------------------
// 3. Simulation
// ---------------------------------------------------------------------------
class Simulator
{
public:
   explicit Simulator(const Graph& g): _g(g), _val(g.size(), 0) {}

   // Simulates 64 patterns; the source values come from "sourceWord(node)".
   template <class Source>
   void run(Source sourceWord) {
      for (unsigned n = 0; n < _g.size(); ++n) {
         switch (_g.kind(n)) {
            case Graph::CONST0: _val[n] = 0; break;
            case Graph::INPUT:
            case Graph::FREE:   _val[n] = sourceWord(n); break;
            case Graph::AND:    _val[n] = value(_g.in0(n)) & value(_g.in1(n)); break;
         }
      }
   }
   uint64_t value(unsigned lit) const {
      return (lit & 1) ? ~_val[lit / 2] : _val[lit / 2];
   }
   bool bit(unsigned node, unsigned b) const { return (_val[node] >> b) & 1; }

private:
   const Graph&     _g;
   vector<uint64_t> _val;
};

// Deterministic pseudo-random words (xorshift64*), so results are repeatable.
class Random64
{
public:
   uint64_t next() {
      _s ^= _s >> 12; _s ^= _s << 25; _s ^= _s >> 27;
      return _s * 2685821657736338717ULL;
   }
private:
   uint64_t _s = 0x9E3779B97F4A7C15ULL;
};

// Counter-example recorded from simulation bit "b", or from a SAT model.
template <class BitOf>
VerifyMismatch makeMismatch(size_t output, const Graph& g, BitOf bitOf)
{
   VerifyMismatch m;
   m.output = output;
   size_t freeIdx = 0;
   for (unsigned n = 0; n < g.size(); ++n) {
      if (g.kind(n) == Graph::INPUT) m.piValues += bitOf(n) ? '1' : '0';
      else if (g.kind(n) == Graph::FREE) {
         if (!m.undefs.empty()) m.undefs += ' ';
         m.undefs += "v" + std::to_string(g.freeVarOf(freeIdx++)) + "=" + (bitOf(n) ? "1" : "0");
      }
   }
   return m;
}

// Re-simulates a counter-example: true iff outputs a and b really differ.
bool confirms(const Graph& g, const VerifyMismatch& m, unsigned a, unsigned b)
{
   // Inputs and free nodes are created in the same order as the strings list them.
   vector<uint64_t> word(g.size(), 0);
   size_t pi = 0, fr = 0;
   std::istringstream undefs(m.undefs);
   vector<char> freeVals;
   string tok;
   while (undefs >> tok) freeVals.push_back(tok.back());
   for (unsigned n = 0; n < g.size(); ++n) {
      if (g.kind(n) == Graph::INPUT) word[n] = m.piValues[pi++] == '1' ? ~0ULL : 0;
      else if (g.kind(n) == Graph::FREE) word[n] = freeVals[fr++] == '1' ? ~0ULL : 0;
   }
   Simulator sim(g);
   sim.run([&](unsigned n) { return word[n]; });
   return (sim.value(a) ^ sim.value(b)) != 0;
}


// ---------------------------------------------------------------------------
// 4. SAT sweeping
//
// Proving a big output pair with one SAT call can take minutes.  Instead we
// walk the graph in topological order and, for every AND node, look for an
// earlier node with the same simulation signature (up to inversion).  If SAT
// proves them equal, the equality is added to the solver as a clause, so
// every later proof only has to reason about the small region between
// already-proven points.  A failed proof yields a counter-example, which is
// simulated (64 at a time) to split the candidate classes further.
// ---------------------------------------------------------------------------
class SatSweeper
{
public:
   explicit SatSweeper(const Graph& g)
      : _g(g), _var(g.size(), Var(NO_VAR)), _rep(g.size()), _pending(g.size(), 0), _seen(g.size(), 0) {
      _solver.initialize();
      for (unsigned n = 0; n < g.size(); ++n) {
         _rep[n] = 2 * n;
         if (g.kind(n) == Graph::INPUT || g.kind(n) == Graph::FREE) _sources.push_back(n);
      }
      _var[0] = _solver.newVar();
      _solver.assertProperty(_var[0], false);           // node 0 is constant 0
      for (int w = 0; w < RANDOM_WORDS; ++w) addWord([&](unsigned) { return _rnd.next(); });
   }

   // Sweeps every node; progress(done, total) is called now and then.
   template <class Progress>
   void sweep(Progress progress) {
      vector<unsigned> failed;            // candidates already refuted for this node
      for (unsigned n = 0; n < _g.size(); ++n) {
         if ((n & 1023) == 0) progress(n, _g.size());
         if (_g.kind(n) != Graph::AND) { addRep(n); continue; }
         if (mergeByStructure(n)) continue;
         failed.clear();
         for (int tries = 0; ; ++tries) {
            const unsigned cand = findCandidate(n, failed);
            if (cand == Graph::NONE || tries == MAX_TRIES) { addRep(n); break; }
            const bool inv = phase(n) != phase(cand);
            if (proveLits(2 * n, 2 * cand + inv)) { _rep[n] = 2 * cand + inv; ++_merged; break; }
            failed.push_back(cand);
            if (addCounterExample(n, cand)) failed.clear();   // new signatures separate them
         }
      }
      progress(_g.size(), _g.size());
   }

   // True iff literals a and b are proven equal; otherwise the solver holds a
   // model (see modelBit) that distinguishes them.
   bool proveEqual(unsigned a, unsigned b) {
      if (repLit(a) == repLit(b)) return true;
      return proveLits(a, b);
   }
   // Value of a node in the last SAT model (nodes outside the solved cones
   // do not influence the result and read as 0).
   bool modelBit(unsigned node) const {
      return _var[node] != NO_VAR && _solver.getValue(_var[node]) == 1; }

private:
   static const int      RANDOM_WORDS = 16;   // 1024 random patterns
   static const size_t   MAX_WORDS    = 128;  // signature length cap: 64 saves memory on sim13 (232 -> 168 MB) but is 50% slower
   static const int      MAX_TRIES    = 16;   // SAT calls per node, at most
   enum { NO_VAR = -1 };   // an enum, so it can be passed by value anywhere (C++11)

   // Lazy CNF: a node gets a SAT variable (and its AND clauses) only when a
   // proof first needs its fanin cone.  A satisfiable call then only has to
   // assign the variables of the cones proven so far, not the whole graph.
   Var satVar(unsigned node) {
      if (_var[node] != NO_VAR) return _var[node];
      _stack.push_back(node);
      while (!_stack.empty()) {
         const unsigned n = _stack.back();
         if (_var[n] != NO_VAR) { _stack.pop_back(); continue; }
         if (_g.kind(n) == Graph::AND) {
            const unsigned a = _g.in0(n) / 2, b = _g.in1(n) / 2;
            if (_var[a] == NO_VAR || _var[b] == NO_VAR) {
               if (_var[a] == NO_VAR) _stack.push_back(a);
               if (_var[b] == NO_VAR) _stack.push_back(b);
               continue;
            }
            _var[n] = _solver.newVar();
            _solver.addAigCNF(_var[n], _var[a], _g.in0(n) & 1, _var[b], _g.in1(n) & 1);
         }
         else {
            _var[n] = _solver.newVar();                   // an input or free node
         }
         _stack.pop_back();
      }
      return _var[node];
   }

   unsigned repLit(unsigned lit) const { return _rep[lit / 2] ^ (lit & 1); }

   // If the fanins of n, replaced by their representatives, are trivially
   // related or form an AND that already exists, n is equal to it: no SAT
   // needed.  Most nodes of an optimized copy are merged this way.
   bool mergeByStructure(unsigned n) {
      unsigned a = repLit(_g.in0(n)), b = repLit(_g.in1(n));
      if (a > b) std::swap(a, b);
      unsigned same;
      if (a == 0 || a == (b ^ 1)) same = 0;
      else if (a == 1 || a == b)  same = b;
      else {
         const uint64_t k = (uint64_t(a) << 32) | b;
         auto [it, isNew] = _byFanins.try_emplace(k, 2 * n);   // one lookup
         if (isNew) return false;
         same = repLit(it->second);
      }
      _rep[n] = same;
      assertEqual(2 * n, same);
      ++_merged;
      return true;
   }
   // Tell the solver a == b (already proven by the caller).
   void assertEqual(unsigned a, unsigned b) {
      const Var va = satVar(a / 2), vb = satVar(b / 2);
      const Var d = _solver.newVar();
      _solver.addXorCNF(d, va, a & 1, vb, b & 1);
      _solver.assertProperty(d, false);
   }

   // Signatures are compared up to inversion: "phase" is the value of the
   // node under the first pattern, and a node is normalized by it.
   bool phase(unsigned n) const { return _sig[0][n] & 1; }
   uint64_t key(unsigned n) const {
      const uint64_t mask = phase(n) ? ~0ULL : 0;
      uint64_t h = 1469598103934665603ULL;
      for (size_t w = 0; w < _sig.size(); ++w) h = (h ^ (_sig[w][n] ^ mask)) * 1099511628211ULL;
      return h;
   }
   bool sameSig(unsigned a, unsigned b) const {
      const uint64_t mask = phase(a) != phase(b) ? ~0ULL : 0;
      for (size_t w = 0; w < _sig.size(); ++w)
         if (_sig[w][a] != (_sig[w][b] ^ mask)) return false;
      return true;
   }

   unsigned findCandidate(unsigned n, const vector<unsigned>& failed) const {
      const auto it = _classes.find(key(n));
      if (it == _classes.end()) return Graph::NONE;
      for (unsigned r : it->second)
         if (sameSig(n, r) && std::find(failed.begin(), failed.end(), r) == failed.end())
            return r;
      return Graph::NONE;
   }
   void addRep(unsigned n) { _reps.push_back(n); _classes[key(n)].push_back(n); }

   bool proveLits(unsigned a, unsigned b) {
      const Var va = satVar(a / 2), vb = satVar(b / 2);
      const Var d = _solver.newVar();                     // d = a xor b
      _solver.addXorCNF(d, va, a & 1, vb, b & 1);
      _solver.assumeRelease();
      _solver.assumeProperty(d, true);
      if (_solver.assumpSolve()) return false;
      _solver.assertProperty(d, false);                   // proven: keep it
      return true;
   }

   // Stores the current SAT model, which tells nodes a and b apart, as one
   // pattern.  Inputs in the fanin cones of a and b take their model values;
   // all other inputs are random, so the pattern also helps elsewhere.
   // When 64 are stored they are simulated as a new signature word.
   // Returns true if it did so.
   bool addCounterExample(unsigned a, unsigned b) {
      ++_stamp;
      _stack.push_back(a); _stack.push_back(b);
      while (!_stack.empty()) {
         const unsigned n = _stack.back(); _stack.pop_back();
         if (_seen[n] == _stamp) continue;
         _seen[n] = _stamp;
         if (_g.kind(n) == Graph::AND) { _stack.push_back(_g.in0(n) / 2); _stack.push_back(_g.in1(n) / 2); }
      }
      const uint64_t bit = 1ULL << _numPending;
      for (unsigned n : _sources) {
         const bool v = _seen[n] == _stamp ? modelBit(n) : (_rnd.next() >> 63);
         if (v) _pending[n] |= bit;
      }
      if (++_numPending < 64) return false;
      addWord([&](unsigned n) { return _pending[n]; });
      std::fill(_pending.begin(), _pending.end(), 0);
      _numPending = 0;
      _classes.clear();                                   // re-key the classes
      for (unsigned r : _reps) _classes[key(r)].push_back(r);
      return true;
   }

   template <class Source>
   void addWord(Source sourceWord) {
      vector<uint64_t> v(_g.size(), 0);
      for (unsigned n = 0; n < _g.size(); ++n) {
         switch (_g.kind(n)) {
            case Graph::CONST0: v[n] = 0; break;
            case Graph::INPUT:
            case Graph::FREE:   v[n] = sourceWord(n); break;
            case Graph::AND: {
               const unsigned a = _g.in0(n), b = _g.in1(n);
               v[n] = (v[a / 2] ^ ((a & 1) ? ~0ULL : 0)) & (v[b / 2] ^ ((b & 1) ? ~0ULL : 0));
               break;
            }
         }
      }
      if (_sig.size() < MAX_WORDS) _sig.push_back(std::move(v));
      else { _sig[_nextReplace] = std::move(v);              // keep the first random words
             _nextReplace = _nextReplace + 1 < MAX_WORDS ? _nextReplace + 1 : RANDOM_WORDS; }
   }

   const Graph&             _g;
   SatSolver                _solver;
   vector<Var>              _var;      // graph node -> SAT variable, or NO_VAR
   vector<unsigned>         _sources;  // all input and free nodes
   vector<unsigned>         _stack;    // scratch for satVar()
   vector<unsigned>         _rep;      // node -> literal of its proven representative
   vector<vector<uint64_t> > _sig;     // _sig[word][node]
   vector<unsigned>         _reps;     // nodes that are representatives
   std::unordered_map<uint64_t, vector<unsigned> > _classes;   // signature key -> reps
   std::unordered_map<uint64_t, unsigned> _byFanins;   // (rep fanin, rep fanin) -> AND literal
   vector<uint64_t>         _pending;  // counter-examples not yet simulated
   vector<unsigned>         _seen;     // scratch for addCounterExample(): cone marks
   unsigned                 _stamp = 0;
   Random64                 _rnd;
   unsigned                 _numPending = 0;
   size_t                   _nextReplace = RANDOM_WORDS;
   size_t                   _merged = 0;
};

} // namespace

// ---------------------------------------------------------------------------
// 5. The checker
// ---------------------------------------------------------------------------
VerifyResult verifyAag(const string& aagA, const string& aagB,
                       const VerifyProgress& progress)
{
   VerifyResult r;
   AagFile fa, fb;
   string err = parseAag(aagA, fa);
   if (err.empty()) { err = parseAag(aagB, fb); if (!err.empty()) err = "second circuit: " + err; }
   else err = "first circuit: " + err;
   if (!err.empty()) { r.error = err; return r; }
   if (fa.inputs.size() != fb.inputs.size()) { r.error = "different number of inputs"; return r; }
   if (fa.outputs.size() != fb.outputs.size()) { r.error = "different number of outputs"; return r; }

   Graph g;
   vector<unsigned> inputs;
   for (size_t i = 0; i < fa.inputs.size(); ++i) inputs.push_back(g.newInput());
   vector<unsigned> outA, outB;
   if (!(err = buildInto(fa, inputs, g, outA)).empty()) { r.error = "first circuit: " + err; return r; }
   if (!(err = buildInto(fb, inputs, g, outB)).empty()) { r.error = "second circuit: " + err; return r; }

   const size_t n = outA.size();
   r.ok = true;
   r.numOutputs = n;
   enum State { OPEN, SAME, DIFF };
   vector<State> state(n, OPEN);
   size_t done = 0;
   auto report = [&]() { ++done; if (progress) progress("Checking outputs", done, n); };

   // (1) structural hashing
   for (size_t i = 0; i < n; ++i)
      if (outA[i] == outB[i]) { state[i] = SAME; ++r.byStructure; report(); }

   // (2) random simulation: 16 x 64 patterns
   Simulator sim(g);
   Random64 rnd;
   for (int round = 0; round < 16 && done < n; ++round) {
      sim.run([&](unsigned) { return rnd.next(); });
      for (size_t i = 0; i < n; ++i) {
         if (state[i] != OPEN) continue;
         const uint64_t diff = sim.value(outA[i]) ^ sim.value(outB[i]);
         if (!diff) continue;
         unsigned b = 0;
         while (!((diff >> b) & 1)) ++b;
         if (r.mismatches.size() < VerifyResult::MAX_LISTED)
            r.mismatches.push_back(makeMismatch(i, g, [&](unsigned node) { return sim.bit(node, b); }));
         ++r.numDiffering;
         state[i] = DIFF;
         report();
      }
   }

   // (3) SAT sweeping, then SAT on the outputs that are still open
   if (done < n) {
      SatSweeper sw(g);
      sw.sweep([&](size_t k, size_t total) { if (progress) progress("Sweeping nodes", k, total); });
      for (size_t i = 0; i < n; ++i) {
         if (state[i] != OPEN) continue;
         if (sw.proveEqual(outA[i], outB[i])) { state[i] = SAME; ++r.bySat; }
         else {
            if (r.mismatches.size() < VerifyResult::MAX_LISTED)
               r.mismatches.push_back(makeMismatch(i, g, [&](unsigned node) { return sw.modelBit(node); }));
            ++r.numDiffering;
            state[i] = DIFF;
         }
         report();
      }
   }

   // Never report a counter-example that does not reproduce.
   for (const VerifyMismatch& m : r.mismatches)
      if (!confirms(g, m, outA[m.output], outB[m.output])) {
         r.ok = false;
         r.error = "internal error: counter-example for output " +
                   std::to_string(m.output) + " does not reproduce";
         return r;
      }

   // Mismatches were found in two phases; list them by output index.
   std::sort(r.mismatches.begin(), r.mismatches.end(),
             [](const VerifyMismatch& x, const VerifyMismatch& y) { return x.output < y.output; });
   return r;
}
