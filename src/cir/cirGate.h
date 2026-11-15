/****************************************************************************
  FileName     [ cirGate.h ]
  PackageName  [ cir ]
  Synopsis     [ Define basic gate data structures ]
  Author       [ Chung-Yang (Ric) Huang ]
  Copyright    [ Copyleft(c) 2008-present LaDs(III), GIEE, NTU, Taiwan ]
****************************************************************************/

#ifndef CIR_GATE_H
#define CIR_GATE_H

#include <string>
#include <vector>
#include <iostream>
#include <climits>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include "cirDef.h"
#include "sat.h"


// TODO: Feel free to define your own classes, variables, or functions.

class CirGate;

//------------------------------------------------------------------------
//   Define classes
//------------------------------------------------------------------------
class CirPValue
{
public:
   CirPValue(size_t v = 0): _pValue(v) {}

//   void rand();

   inline size_t operator()() const { return _pValue; }
   inline CirPValue operator ~() const { return ~_pValue; }
   inline CirPValue operator & (const CirPValue& p) const {
      size_t v = _pValue & p._pValue; return v; }
   inline CirPValue& operator &= (const CirPValue& v) {
      _pValue &= v._pValue; return *this; }
   inline bool operator == (const CirPValue& v) const {
      return (_pValue == v._pValue); }

   friend std::ostream& operator << (std::ostream& os, const CirPValue& v);

private:
   size_t     _pValue;
};

// An edge of the AIG: "this gate's output, possibly inverted".
//
// That is all it can mean.  It is built only from a gate pointer (there is
// no conversion from an integer), and it is a value: inverting an edge
// gives a new edge (~e, e ^ inv) instead of changing it in place.
//
// Implementation: the inversion flag is kept in the lowest bit of the
// pointer (gates are allocated at least 2-byte aligned, so that bit is
// always 0).  One edge is 8 bytes, as in most AIG tools.  This trick is
// private to this class; nobody else sees the bits except through bits(),
// which exists only for hashing and ordering.
class CirGateV
{
public:
   CirGateV() = default;                                    // the null edge
   explicit CirGateV(CirGate* g, bool inv = false)
      : _bits(reinterpret_cast<std::uintptr_t>(g) | (inv ? INV_BIT : 0)) {}

   CirGate* gate() const { return reinterpret_cast<CirGate*>(_bits & ~INV_BIT); }
   bool isInv() const { return (_bits & INV_BIT) != 0; }
   // The edge drives from an undefined gate (printed with a '*').
   bool isFloat() const;
   unsigned gateId() const;
   unsigned litId() const;                                   // 2 * gateId + inv

   CirGateV operator~() const { CirGateV e; e._bits = _bits ^ INV_BIT; return e; }
   CirGateV operator^(bool inv) const { return inv ? ~*this : *this; }
   explicit operator bool() const { return _bits != 0; }    // not the null edge

   bool operator==(const CirGateV& e) const { return _bits == e._bits; }
   bool operator!=(const CirGateV& e) const { return _bits != e._bits; }
   bool operator< (const CirGateV& e) const { return _bits <  e._bits; }
   std::uintptr_t bits() const { return _bits; }            // for hashing only

   friend std::ostream& operator << (std::ostream&, const CirGateV&);

private:
   static const std::uintptr_t INV_BIT = 1;
   std::uintptr_t _bits = 0;
};

class CirAigGate;

class CirGate
{
public:
   virtual ~CirGate() {}

   // Basic access methods
   unsigned getGid() const { return _gid; }
   unsigned getLineNo() const { return _lineNo; }

   // The type is fixed when the gate is built: these are plain comparisons,
   // not virtual calls.
   GateType type() const { return _type; }
   const char* typeName() const { return gateTypeName(_type); }   // "PI", "AIG", ...
   bool isPi() const    { return _type == GateType::Pi; }
   bool isPo() const    { return _type == GateType::Po; }
   bool isAig() const   { return _type == GateType::Aig; }
   bool isConst() const { return _type == GateType::Const; }
   bool isUndef() const { return _type == GateType::Undef; }
   // The gate as an AIG gate, or nullptr if it is not one (a checked
   // downcast: the type tag already knows the answer, no dynamic_cast).
   CirAigGate* asAig();
   const CirAigGate* asAig() const;

   // Fanins, for code that handles any kind of gate.  PI, CONST and UNDEF
   // have none; PO has one; AIG has two.  Code that knows it has an AIG
   // uses asAig()->in0() / in1() instead.
   virtual unsigned numFanins() const { return 0; }
   virtual CirGateV fanin([[maybe_unused]] unsigned i) const { assert(false); return CirGateV(); }

   // Symbolic name from the AIGER symbol table; "" if none (only PIs and POs
   // can have one, and AIGER does not allow an empty name).
   virtual const std::string& getName() const { static const std::string none; return none; }

   // Methods about circuit construction
   virtual void genDfsList(std::vector<CirGate*>&) { setToGlobalRef(); }

   // Methods about circuit optimization
   // Re-point the fanin that currently drives from "oldFanin" to "newFanin";
   // "inv" flips the phase of that edge. Gates without fanins ignore it.
   virtual void replaceFanin(CirGate* /*oldFanin*/, CirGate* /*newFanin*/, bool /*inv*/) {}

   // Methods about circuit simulation
   virtual void pSim() {}
   void setPValue(const CirPValue& v) { _pValue = v; }
   const CirPValue& getPValue() const { return _pValue; }
   
   // Methods about FRAIG
   void setFECId(unsigned i) { _fecId = i; }
   void resetFECId() { _fecId = UINT_MAX; }
   bool isFEC() const { return (_fecId != UINT_MAX); }
   unsigned getFECId() const { return _fecId; }
   void setEqGate(CirGate *g, bool inv) {
      CirGateV nv;
      while ((nv = g->getEqGate())) { g = nv.gate(); inv ^= nv.isInv(); }
      _eqGate = CirGateV(g, inv);
   }
   void resetEqGate() { _eqGate = CirGateV(); }
   const CirGateV& getEqGate() const { return _eqGate; }
   void setSatVar(Var v) { _satVar = v; }
   Var getSatVar() const { return _satVar; }

   // Printing functions
   virtual void printGate() const = 0;
   // The circuit is a parameter (stage 8): the FEC groups and the fanout
   // lists live in CirMgr, not in the gate.
   void reportGate(const CirMgr& circuit) const;
   void reportFanin(int level) const;
   void reportFanout(const CirMgr& circuit, int level) const;
   

   // Methods about _globalRef_s
   bool isGlobalRef() const { return (_ref == _globalRef_s); }
   bool isGlobalRef(unsigned diff) const {
                     return ((_ref + diff) == _globalRef_s); }
   void setToGlobalRef() const { _ref = _globalRef_s; }
   void setToGlobalRef(unsigned diff) const {
                                 _ref = unsigned(_globalRef_s - diff); }
   static void setGlobalRef() { _globalRef_s++; }
   static void setGlobalRef(unsigned offset) { _globalRef_s += offset; }

//   friend ostream& operator << (ostream& os, const CirAigGate* g);

private:
   // Printing functions
   void reportFaninRecur(int, int, bool) const;
   void reportFanoutRecur(const CirMgr&, int, int, bool) const;

protected:
   CirGate(GateType t, unsigned g, unsigned l)
      : _gid(g), _lineNo(l), _pValue(0), _fecId(UINT_MAX), _type(t), _satVar(0), _ref(0) {}

   unsigned              _gid;  // var ID
   unsigned              _lineNo;
   CirPValue             _pValue;
   unsigned              _fecId; // _fecId/2 => FEC ID; _fecId&1 => phase
   // 1 byte, placed in the 4 bytes of padding after _fecId (before the
   // 8-byte _eqGate), so sizeof(CirGate) does not grow.  Put it anywhere
   // else and every gate gets 8 bytes bigger (tests/unit/test_gate.cpp checks).
   const GateType        _type;
   CirGateV              _eqGate;
   Var                   _satVar;  // SAT variable, assigned in DFS order
   mutable unsigned      _ref;

   static unsigned       _globalRef_s;

};

class CirPiGate : public CirGate
{
public:
   CirPiGate(unsigned g, unsigned l): CirGate(GateType::Pi, g, l) {}

   // Basic access methods
   void setName(const std::string& s) { _name = s; }
   const std::string& getName() const override { return _name; }

   // Methods about circuit construction
   void genDfsList(std::vector<CirGate*>&) override;

   // Methods about circuit simulation
//   void setRandPPattern() { _pValue.rand(); }

   // Printing functions
   void printGate() const override;

private:
   std::string   _name;   // a value, not a pointer: no destructor needed (Rule of zero)
};

class CirPoGate : public CirGate
{
public:
   CirPoGate(unsigned g, unsigned l): CirGate(GateType::Po, g, l) {}

   // Basic access methods
   CirGateV in0() const { return _in0; }
   void setIn0(CirGateV e) { _in0 = e; }
   unsigned numFanins() const override { return 1; }
   CirGateV fanin([[maybe_unused]] unsigned i) const override { assert(i == 0); return _in0; }
   void setName(const std::string& s) { _name = s; }
   const std::string& getName() const override { return _name; }

   // Methods about circuit construction
   void genDfsList(std::vector<CirGate*>&) override;

   // Methods about circuit simulation
   void pSim() override {
      _pValue  = _in0.isInv()? ~(_in0.gate()->getPValue())
               : _in0.gate()->getPValue();
   }

   // Methods about circuit optimization
   void replaceFanin(CirGate*, CirGate *n, bool inv) override { _in0 = CirGateV(n, _in0.isInv() ^ inv); }

   // Printing functions
   void printGate() const override;

private:
   CirGateV    _in0;
   std::string      _name;
};

class CirAigGate : public CirGate
{
public:
   CirAigGate(unsigned g, unsigned l): CirGate(GateType::Aig, g, l) {}
   CirGateV in0() const { return _in0; }
   CirGateV in1() const { return _in1; }
   void setIn0(CirGateV e) { _in0 = e; }
   void setIn1(CirGateV e) { _in1 = e; }
   unsigned numFanins() const override { return 2; }
   CirGateV fanin(unsigned i) const override { assert(i < 2); return i == 0 ? _in0 : _in1; }

   // Methods about circuit construction
   void genDfsList(std::vector<CirGate*>&) override;

   // Methods about circuit optimization
   // If both fanins are "o", only _in0 is replaced; the caller calls this
   // once per fanout entry, so the second call then replaces _in1.
   void replaceFanin(CirGate *o, CirGate *n, bool inv) override {
      if (o == _in0.gate()) _in0 = CirGateV(n, _in0.isInv() ^ inv);
      else                  _in1 = CirGateV(n, _in1.isInv() ^ inv);
   }

   // Methods about circuit simulation
   void pSim() override {
      _pValue  = _in0.isInv()? ~(_in0.gate()->getPValue())
               : _in0.gate()->getPValue();
      _pValue &= _in1.isInv()? ~(_in1.gate()->getPValue())
               : _in1.gate()->getPValue();
   }
   
   // Printing functions
   void printGate() const override;

private:
   CirGateV   _in0;
   CirGateV   _in1;
};

class CirConstGate : public CirGate
{
public:
   CirConstGate(unsigned g): CirGate(GateType::Const, g, 0) {} // _satVar = 0

   // Methods about circuit construction
   void genDfsList(std::vector<CirGate*>&) override;

   // Printing functions
   void printGate() const override;
};

class CirUndefGate : public CirGate
{
public:
   CirUndefGate(unsigned g): CirGate(GateType::Undef, g, 0) {}

   // Printing functions
   void printGate() const override {
      std::cerr << "CirUndefGate::printGate() is called!!" << std::endl;
      exit(-1);
   }

private:
};

// CirGateV keeps the inversion flag in the lowest bit of a gate pointer;
// that is only safe if gate addresses always have that bit 0.
static_assert(alignof(CirGate) >= 2, "CirGateV needs gate addresses with bit 0 free");

inline CirAigGate* CirGate::asAig() {
   return isAig() ? static_cast<CirAigGate*>(this) : nullptr; }
inline const CirAigGate* CirGate::asAig() const {
   return isAig() ? static_cast<const CirAigGate*>(this) : nullptr; }

inline bool CirGateV::isFloat() const { return gate()->isUndef(); }

#endif // CIR_GATE_H
