/****************************************************************************
  FileName     [ cirMgr.cpp ]
  PackageName  [ cir ]
  Synopsis     [ Define cir manager functions ]
  Author       [ Chung-Yang (Ric) Huang ]
  Copyright    [ Copyleft(c) 2008-present LaDs(III), GIEE, NTU, Taiwan ]
****************************************************************************/

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cstdio>
#include <ctype.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <sstream>
#include "cirMgr.h"
#include "cirGate.h"
#include "util.h"

using namespace std;

// TODO: Implement memeber functions for class CirMgr

// See cirMgr.h: these are here because here CirGate is a complete type.
CirMgr::CirMgr(): _flag(0), _fecReady(false), _simLog(nullptr), _rng(5489u) {}
CirMgr::~CirMgr() = default;

/*******************************/
/*   Flow rules                */
/*******************************/
std::optional<std::string_view>
CirMgr::whyNot(CirStep next) const
{
   constexpr std::string_view simulated = "Error: circuit has been simulated!! Do \"CIRFraig\" first!!";
   switch (next) {
      case CirStep::Optimize:
         if (_lastStep == CirStep::Simulate) return simulated;
         return std::nullopt;
      case CirStep::Strash:
         if (_lastStep == CirStep::Strash) return "Error: circuit has been strashed!!";
         if (_lastStep == CirStep::Simulate) return simulated;
         return std::nullopt;
      case CirStep::Fraig:
         if (_lastStep != CirStep::Simulate) return "Error: circuit is not yet simulated!!";
         return std::nullopt;
      case CirStep::Read:
      case CirStep::Simulate:
         return std::nullopt;
   }
   return std::nullopt;
}

const char*
CirMgr::stageName() const
{
   switch (_lastStep) {
      case CirStep::Read:     return "READ";
      case CirStep::Optimize: return "OPT";
      case CirStep::Strash:   return "STRASH";
      case CirStep::Simulate: return "SIMULATE";
      case CirStep::Fraig:    return "FRAIG";
   }
   return "?";
}

/*******************************/
/*   Parse errors              */
/*******************************/

enum CirParseError {
   EXTRA_SPACE,
   MISSING_SPACE,
   ILLEGAL_WSPACE,
   ILLEGAL_NUM,
   ILLEGAL_IDENTIFIER,
   ILLEGAL_SYMBOL_TYPE,
   ILLEGAL_SYMBOL_NAME,
   MISSING_NUM,
   MISSING_IDENTIFIER,
   MISSING_NEWLINE,
   MISSING_DEF,
   CANNOT_INVERTED,
   MAX_LIT_ID,
   REDEF_GATE,
   REDEF_SYMBOLIC_NAME,
   REDEF_CONST,
   NUM_TOO_SMALL,
   NUM_TOO_BIG,

   DUMMY_END
};

/*****************************************/
/*   AagLexer: the state of one file read  */
/*****************************************/
// Everything the parser needs while reading one AIGER file: the current
// line, the column, and the details for the next error message.  One
// AagLexer is created by readCircuit() and lives only while it reads
// (stage 8: these used to be file-level static variables, shared by every
// CirMgr).  buf keeps the reference program's 1024-character line limit, so
// that over-long lines give the same error messages as the reference.
struct CirMgr::AagLexer
{
   unsigned lineNo = 0;      // 0-based; messages print lineNo + 1
   unsigned colNo  = 0;      // 0-based; messages print colNo + 1
   char     buf[1024];       // the current line
   string   errMsg;          // details for parseError()
   int      errInt = 0;
   CirGate* errGate = nullptr;


   bool
   parseError(CirParseError err)
   {
      switch (err) {
         case EXTRA_SPACE:
            cerr << "[ERROR] Line " << lineNo+1 << ", Col " << colNo+1
                 << ": Extra space character is detected!!" << endl;
            break;
         case MISSING_SPACE:
            cerr << "[ERROR] Line " << lineNo+1 << ", Col " << colNo+1
                 << ": Missing space character!!" << endl;
            break;
         case ILLEGAL_WSPACE: // for non-space white space character
            cerr << "[ERROR] Line " << lineNo+1 << ", Col " << colNo+1
                 << ": Illegal white space char(" << errInt
                 << ") is detected!!" << endl;
            break;
         case ILLEGAL_NUM:
            cerr << "[ERROR] Line " << lineNo+1 << ": Illegal "
                 << errMsg << "!!" << endl;
            break;
         case ILLEGAL_IDENTIFIER:
            cerr << "[ERROR] Line " << lineNo+1 << ": Illegal identifier \""
                 << errMsg << "\"!!" << endl;
            break;
         case ILLEGAL_SYMBOL_TYPE:
            cerr << "[ERROR] Line " << lineNo+1 << ", Col " << colNo+1
                 << ": Illegal symbol type (" << errMsg << ")!!" << endl;
            break;
         case ILLEGAL_SYMBOL_NAME:
            cerr << "[ERROR] Line " << lineNo+1 << ", Col " << colNo+1
                 << ": Symbolic name contains un-printable char(" << errInt
                 << ")!!" << endl;
            break;
         case MISSING_NUM:
            cerr << "[ERROR] Line " << lineNo+1 << ", Col " << colNo+1
                 << ": Missing " << errMsg << "!!" << endl;
            break;
         case MISSING_IDENTIFIER:
            cerr << "[ERROR] Line " << lineNo+1 << ": Missing \""
                 << errMsg << "\"!!" << endl;
            break;
         case MISSING_NEWLINE:
            cerr << "[ERROR] Line " << lineNo+1 << ", Col " << colNo+1
                 << ": A new line is expected here!!" << endl;
            break;
         case MISSING_DEF:
            cerr << "[ERROR] Line " << lineNo+1 << ": Missing " << errMsg
                 << " definition!!" << endl;
            break;
         case CANNOT_INVERTED:
            cerr << "[ERROR] Line " << lineNo+1 << ", Col " << colNo+1
                 << ": " << errMsg << " " << errInt << "(" << errInt/2
                 << ") cannot be inverted!!" << endl;
            break;
         case MAX_LIT_ID:
            cerr << "[ERROR] Line " << lineNo+1 << ", Col " << colNo+1
                 << ": Literal \"" << errInt << "\" exceeds maximum valid ID!!"
                 << endl;
            break;
         case REDEF_GATE:
            cerr << "[ERROR] Line " << lineNo+1 << ": Literal \"" << errInt
                 << "\" is redefined, previously defined as "
                 << errGate->typeName() << " in line " << errGate->getLineNo()
                 << "!!" << endl;
            break;
         case REDEF_SYMBOLIC_NAME:
            cerr << "[ERROR] Line " << lineNo+1 << ": Symbolic name for \""
                 << errMsg << errInt << "\" is redefined!!" << endl;
            break;
         case REDEF_CONST:
            cerr << "[ERROR] Line " << lineNo+1 << ", Col " << colNo+1
                 << ": Cannot redefine constant (" << errInt << ")!!" << endl;
            break;
         case NUM_TOO_SMALL:
            cerr << "[ERROR] Line " << lineNo+1 << ": " << errMsg
                 << " is too small (" << errInt << ")!!" << endl;
            break;
         case NUM_TOO_BIG:
            cerr << "[ERROR] Line " << lineNo+1 << ": " << errMsg
                 << " is too big (" << errInt << ")!!" << endl;
            break;
         default: break;
      }
      return false;
   }

   void
   getErrorToken(char *tmp)
   {
      assert(!isspace(*tmp));
      char *end = tmp;
      while ((*(++end) != 0) && !isspace(*end));
      *end = 0;
      errMsg = tmp;
   }

   // Read the number until space character is encountered.
   // If any non-digit or non-space char is encountered, return false.
   // In the end, colNo will point to the non-digit character.
   bool
   getUint(unsigned& num, const string& numType)
   {
      if (buf[colNo] == 0) {
         errMsg = numType; return parseError(MISSING_NUM); }
      assert(!isspace(buf[colNo]));
      char *tmp = buf + colNo;
      num = 0;
      do {
         if (!isdigit(buf[colNo])) {
            getErrorToken(tmp);
            errMsg = numType + "(" + errMsg + ")";
            return parseError(ILLEGAL_NUM);
         }
         num *= 10;
         num += unsigned(buf[colNo] - '0');
         if (buf[++colNo] == 0 || isspace(buf[colNo])) break;
      } while (!isspace(buf[colNo]));
      return true;
   }

   // colNo will be updated!!
   // Check if tmp contains identifier.
   // However, does not check if tmp contains extra char.
   bool checkIdentifier(const char* identifier)
   {
      char *tmp = buf + colNo;
      if (*tmp == 0) {
         errMsg = identifier; return parseError(MISSING_IDENTIFIER);
      }
      for (int i = 0, len = strlen(identifier); i < len; ++i, ++colNo)
         if (buf[colNo] == 0 || buf[colNo] != identifier[i]) {
            getErrorToken(tmp); return parseError(ILLEGAL_IDENTIFIER); }
      return true;
   }

   // If leadSpace == true, the first char is a space. <== colNo will ++
   // The followed char cannot be any white space char.
   // Make sure buf[colNo] != 0 (after the leading WS)
   bool checkWS(bool leadSpace)
   {
      if (leadSpace) {
         if (buf[colNo] == 0 || buf[colNo] != ' ')
            return parseError(MISSING_SPACE);
         ++colNo;
      }
      if (buf[colNo] == ' ') return parseError(EXTRA_SPACE);
      if (isspace(buf[colNo])) {
         errInt = int(buf[colNo]); return parseError(ILLEGAL_WSPACE); }
      return true;
   }
};

/**************************************************************/
/*   class CirMgr member functions for circuit construction   */
/**************************************************************/
bool
CirMgr::readCircuit(const string& fileName)
{
   ifstream cirin(fileName.c_str());
   if (!cirin) {
      cerr << "Cannot open design \"" << fileName << "\"!!" << endl;
      return false;
   }
   {  // snapshot of the original file, for CIRVerify
      ostringstream text;
      text << cirin.rdbuf();
      _sourceName = fileName;
      _sourceText = text.str();
      cirin.clear();
      cirin.seekg(0);
   }
   AagLexer lx;   // parser state for this file only
   if (!parseHeader(cirin, lx)) return false;
   if (!parseInput(cirin, lx)) return false;
   if (!parseLatch(cirin, lx)) return false;
   if (!parseOutput(cirin, lx)) return false;
   if (!parseAig(cirin, lx)) return false;
   if (!parseSymbol(cirin, lx)) return false;
   if (!parseComment(cirin, lx)) return false;
   
   connectFanins();
   genDfsList();

   checkFloatList();
   checkUnusedList();

   return true;
}

bool
CirMgr::parseHeader(ifstream& cirin, AagLexer& lx)
{
   assert (lx.lineNo == 0 && lx.colNo == 0);
   cirin.getline(lx.buf, 1024);

   // aag M I L O A
   const int aagLength = 3;
   if (!lx.checkWS(false)) return false;
   if (!lx.checkIdentifier("aag")) return false;

   const string portStr[5] = { "number of variables", "number of PIs",
                  "number of latches", "number of POs", "number of AIGs" };
   // Need to check 0; otherwise lx.getErrorToken(lx.buf) will seg fault
   if (lx.buf[aagLength] == 0) {
      lx.errMsg = portStr[0]; return lx.parseError(MISSING_NUM); }
   // Take care "aagkk", but not "aag3", nor "aag<tab>"
   if (!isspace(lx.buf[aagLength]) && !isdigit(lx.buf[aagLength])) {
      lx.getErrorToken(lx.buf); return lx.parseError(ILLEGAL_IDENTIFIER); }

   lx.colNo = aagLength;
   unsigned* const fields[] = { &_header.maxVar, &_header.numPi, &_header.numLatch,
                                &_header.numPo, &_header.numAig };
   for (size_t i = 0; i < std::size(fields); ++i) {
      if (lx.buf[lx.colNo] == 0) {
         lx.errMsg = portStr[i]; return lx.parseError(MISSING_NUM); }
      if (!lx.checkWS(true)) return false;
      if (!lx.getUint(*fields[i], portStr[i])) return false;
   }
   if (lx.buf[lx.colNo] != 0) return lx.parseError(MISSING_NEWLINE);

   // Check numbers
   // M must >= I + L + A
   if (_header.maxVar < _header.numPi + _header.numLatch + _header.numAig) {
      lx.errMsg = "Number of variables"; lx.errInt = _header.maxVar;
      return lx.parseError(NUM_TOO_SMALL);
   }
   // L must = 0
   if (_header.numLatch != 0) {
      lx.errMsg = "latches"; return lx.parseError(ILLEGAL_NUM);
   }

   // Create lists: IDs 0 (CONST) .. M (variables), M+1 .. M+O (POs)
   const unsigned numTots = _header.maxVar + _header.numPo + 1;
   _gates.clear();
   _gates.resize(numTots);            // all empty: no gate yet
   _piList.assign(_header.numPi, nullptr);
   _poList.assign(_header.numPo, nullptr);
   _fanoutInfo.assign(numTots, GateList());
   _gates[0] = std::make_unique<CirConstGate>(0);

   return true;
}

bool CirMgr::parseInput(ifstream& cirin, AagLexer& lx)
{
   for (size_t i = 0, n = _header.numPi; i < n; ++i) {
      ++lx.lineNo; lx.colNo = 0;
      cirin.getline(lx.buf, 1024);
      if (cirin.eof()) { lx.errMsg = "PI"; return lx.parseError(MISSING_DEF); }
      if (!lx.checkWS(false)) return false;
      unsigned litId;
      CirGate *gate = checkGate(litId, GateType::Pi, "PI", lx);
      if (!gate) return false;
      _piList[i] = static_cast<CirPiGate*>(gate);
      if (lx.buf[lx.colNo] != 0) return lx.parseError(MISSING_NEWLINE);
   }

   return true;
}

bool CirMgr::parseLatch(ifstream&, AagLexer&) { return true; }   // latches are not supported

bool CirMgr::parseOutput(ifstream& cirin, AagLexer& lx)
{
   size_t poId = _header.maxVar + 1;
   for (size_t i = 0,  nPo = _header.numPo; i < nPo; ++i, ++poId) {
      ++lx.lineNo; lx.colNo = 0;
      cirin.getline(lx.buf, 1024);
      if (cirin.eof()) { lx.errMsg = "PO"; return lx.parseError(MISSING_DEF); }
      if (!lx.checkWS(false)) return false;
      unsigned litId;
      if (!checkId(litId, "PO", lx)) return false;
      _gates[poId] = std::make_unique<CirPoGate>(poId, lx.lineNo+1);
      _poList[i] = static_cast<CirPoGate*>(_gates[poId].get());
      PendingFanins p = { _poList[i], { litId, 0 }, 1 };
      _pending.push_back(p);
      if (lx.buf[lx.colNo] != 0) return lx.parseError(MISSING_NEWLINE);
   }

   return true;
}

bool CirMgr::parseAig(ifstream& cirin, AagLexer& lx)
{
   for (size_t i = 0, n = _header.numAig; i < n; ++i) {
      ++lx.lineNo; lx.colNo = 0;
      cirin.getline(lx.buf, 1024);
      if (cirin.eof()) { lx.errMsg = "AIG"; return lx.parseError(MISSING_DEF); }
      if (!lx.checkWS(false)) return false;
      unsigned litId;
      CirGate *gate = checkGate(litId, GateType::Aig, "AIG gate", lx);
      if (!gate) return false;
      unsigned in[2];
      for (int j = 0; j < 2; ++j) {
         if (!lx.checkWS(true)) return false;
         if (!checkId(in[j], "AIG input", lx)) return false;
      }
      if (lx.buf[lx.colNo] != 0) return lx.parseError(MISSING_NEWLINE);
      PendingFanins p = { gate, { in[0], in[1] }, 2 };   // connected later
      _pending.push_back(p);
   }
   return true;
}

bool CirMgr::parseSymbol(ifstream& cirin, AagLexer& lx)
{
   while (1) {
      // Read in next line!!
      ++lx.lineNo; lx.colNo = 0;
      cirin.getline(lx.buf, 1024);
      if (cirin.eof()) return true;
      char type = lx.buf[lx.colNo];
      if (type == 'i' || type == 'l' || type == 'o') {
         ++lx.colNo;
         if (!lx.checkWS(false)) return false;
         unsigned portId;
         if (!lx.getUint(portId, "symbol index")) return false;
         if (lx.buf[lx.colNo] == 0) {
            lx.errMsg = "symbolic name"; return lx.parseError(MISSING_IDENTIFIER); }
         if (lx.buf[lx.colNo] == ' ') {
            if (lx.buf[++lx.colNo] == 0) {
               lx.errMsg = "symbolic name";
               return lx.parseError(MISSING_IDENTIFIER);
            }
         }
         else 
            return lx.parseError(MISSING_SPACE);
         char *str = lx.buf + lx.colNo;
         for (; lx.buf[lx.colNo] != 0; ++lx.colNo) {
            if (!isprint(lx.buf[lx.colNo])) {
               lx.errInt = int(lx.buf[lx.colNo]);
               return lx.parseError(ILLEGAL_SYMBOL_NAME);
            }
         }
         if (type == 'i') {
            if (portId >= _header.numPi) {
               lx.errMsg = "PI index"; lx.errInt = portId;
               return lx.parseError(NUM_TOO_BIG);
            }
            CirPiGate *pi = _piList[portId];
            if (!pi->getName().empty()) {
               lx.errMsg = "i"; lx.errInt = portId;
               return lx.parseError(REDEF_SYMBOLIC_NAME);
            }
            pi->setName(str);
         }
         // else if (type == 'l')... NOT supported yet
         else { // if (type == 'o')
            assert(type == 'o');
            if (portId >= _header.numPo) {
               lx.errMsg = "PO index"; lx.errInt = portId;
               return lx.parseError(NUM_TOO_BIG);
            }
            CirPoGate *po = _poList[portId];
            if (!po->getName().empty()) {
               lx.errMsg = "o"; lx.errInt = portId;
               return lx.parseError(REDEF_SYMBOLIC_NAME);
            }
            po->setName(str);
         }
      }
      else if (type == 'c') return true;
      else if (type == ' ') return lx.parseError(EXTRA_SPACE);
      else if (isspace(type)) {
         lx.errInt = int(type); return lx.parseError(ILLEGAL_WSPACE); }
      else {
         lx.errMsg.resize(1); lx.errMsg[0] = type;
         return lx.parseError(ILLEGAL_SYMBOL_TYPE);
      }
   }
   return true;
}

bool CirMgr::parseComment(ifstream& cirin, AagLexer& lx) 
{
   // The first comment line should have been read
   if (cirin.eof()) return true;
   char type = lx.buf[lx.colNo];
   assert(type == 'c');
   if (lx.buf[++lx.colNo] != 0) return lx.parseError(MISSING_NEWLINE);

   return true; 
}

bool
CirMgr::checkId(unsigned& litId, const string& err, AagLexer& lx)
{
   unsigned oldColNo = lx.colNo;
   if (!lx.getUint(litId, err + " literal ID")) return false;
   if (litId/2 > _header.maxVar) {
      lx.errInt = litId; lx.colNo = oldColNo; return lx.parseError(MAX_LIT_ID);
   }
   return true;
}

// For PI and AIG gates only!!
CirGate*
CirMgr::checkGate(unsigned& litId, GateType type, const string& err, AagLexer& lx)
{
   unsigned oldColNo = lx.colNo;
   if (!checkId(litId, err, lx)) return 0;
   if (litId < 2) { lx.errInt = litId; lx.colNo = oldColNo;
      lx.parseError(REDEF_CONST); return 0; }
   if (litId % 2 != 0) {
      lx.errMsg = err; lx.errInt = litId; lx.colNo = oldColNo; 
      lx.parseError(CANNOT_INVERTED); return 0;
   }
   CirGate *gate = getGate(litId/2);
   if (gate != 0) {
      lx.errInt = litId; lx.errGate = gate; lx.colNo = oldColNo; 
      lx.parseError(REDEF_GATE); return 0;
   }
   // The new gate is owned by _gates; the caller gets a borrowed pointer.
   unique_ptr<CirGate>& slot = _gates[litId/2];
   switch (type) {
      case GateType::Pi: slot = std::make_unique<CirPiGate>(litId/2, lx.lineNo+1); break;
      case GateType::Aig: slot = std::make_unique<CirAigGate>(litId/2, lx.lineNo+1); break;
      default: cerr << "Error: Unknown gate type (" << gateTypeName(type) << ")!!\n";
               exit(-1);
   }
   return slot.get();
}

// The edge for literal "litId"; creates an UNDEF gate if the variable was
// never defined.
CirGateV
CirMgr::edgeForLiteral(unsigned litId)
{
   const unsigned id = litId / 2;
   if (!getGate(id)) {
      _gates[id] = std::make_unique<CirUndefGate>(id);
      _undefList.push_back(id);
   }
   return CirGateV(getGate(id), litId % 2 != 0);
}

// After every gate of the file exists: turn the fanin literals into edges
// and record the fanouts.  Gates are connected in ID order, so the fanout
// lists (and the UNDEF list) come out in the same order as before.
void
CirMgr::connectFanins()
{
   std::sort(_pending.begin(), _pending.end(),
             [](const PendingFanins& a, const PendingFanins& b) {
                return a.gate->getGid() < b.gate->getGid(); });
   for (const PendingFanins& p : _pending) {
      for (unsigned k = 0; k < p.n; ++k) {
         const CirGateV e = edgeForLiteral(p.lit[k]);
         if (CirAigGate* aig = p.gate->asAig()) (k == 0 ? aig->setIn0(e) : aig->setIn1(e));
         else static_cast<CirPoGate*>(p.gate)->setIn0(e);
         _fanoutInfo[e.gateId()].push_back(p.gate);
      }
   }
   _pending.clear();
   _pending.shrink_to_fit();
}

void
CirMgr::genDfsList()
{
   clearList(_dfsList);
   CirGate::setGlobalRef();
   for (unsigned i = 0, n = getNumPOs(); i < n; ++i)
      getPo(i)->genDfsList(_dfsList);
}

void
CirPiGate::genDfsList(GateList& gateList)
{
   setToGlobalRef();
   gateList.push_back(this);
}

void
CirPoGate::genDfsList(GateList& gateList)
{
   setToGlobalRef();
   CirGate* g = _in0.gate();
   if (!g->isGlobalRef())
      g->genDfsList(gateList);
   gateList.push_back(this);
}

void
CirAigGate::genDfsList(GateList& gateList)
{
   setToGlobalRef();
   CirGate* g = _in0.gate();
   if (!g->isGlobalRef())
      g->genDfsList(gateList);
   g = _in1.gate();
   if (!g->isGlobalRef())
      g->genDfsList(gateList);
   gateList.push_back(this);
}

void
CirConstGate::genDfsList(GateList& gateList)
{
   setToGlobalRef();
   gateList.push_back(this);
}

/*
// Remove the entries that getGate() = NULL
void
CirMgr::updateUndefList()
{
   size_t i = 0;
   for (size_t j = 0, n = _undefList.size(); j< n; ++j) {
      if (!getGate(_undefList[j])) continue; // has been deleted
      if (_fanoutInfo[_undefList[j]].empty()) {
         deleteUndefGate(getGate(_undefList[j]));
         continue;
      }
      if (i != j) _undefList[i] = _undefList[j];
      ++i;
   }
   if (i == 0) clearList<IdList>(_undefList);
   else _undefList.resize(i);
}
*/

void
CirMgr::checkFloatList()
{
   clearList(_floatList);
   CirGate::setGlobalRef();
   for (size_t i = 0, n = _undefList.size(); i< n; ++i) {
      const GateList& fanouts = _fanoutInfo[_undefList[i]];
      for (size_t j = 0, m = fanouts.size(); j < m; ++j)
         if (!fanouts[j]->isGlobalRef()) {
            fanouts[j]->setToGlobalRef();
            _floatList.push_back(fanouts[j]->getGid());
         }
   }
   sort(_floatList.begin(), _floatList.end());
}

void
CirMgr::checkUnusedList()
{
   clearList(_unusedList);
   for (size_t i = 1, n = getNumTots(); i < n; ++i)
      if (_fanoutInfo[i].empty() && getGate(i))
         if (!getGate(i)->isPo())
            _unusedList.push_back(i);
}

CirGateV
CirMgr::litId2GateV(unsigned litId) const
{
   return CirGateV(litId2Gate(litId), litId % 2 != 0);
}

/**********************************************************/
/*   class CirMgr member functions for circuit printing   */
/**********************************************************/
/*********************
Circuit Statistics
==================
  PI          20
  PO          12
  AIG        130
------------------
  Total      162
*********************/
void
CirMgr::printSummary() const
{
   cout << endl;
   cout << "Circuit Statistics" << endl
        << "==================" << endl;
   unsigned tot = 0;
   tot += _header.numPi;
   cout << "  " << setw(7) << left << "PI"
        << setw(7) << right << _header.numPi << endl;
   tot += _header.numPo;
   cout << "  " << setw(7) << left << "PO"
        << setw(7) << right << _header.numPo << endl;
   tot += _header.numAig;
   cout << "  " << setw(7) << left << "AIG"
        << setw(7) << right << _header.numAig << endl;
   cout << "------------------" << endl;
   cout << "  Total  " << setw(7) << right << tot << endl;
}

void
CirMgr::printNetlist() const
{
   cout << endl;
   for (unsigned i = 0, n = _dfsList.size(); i < n; ++i) {
      cout << "[" << i << "] ";
      _dfsList[i]->printGate();
   }
}

void
CirMgr::printPIs() const
{
   cout << "PIs of the circuit:";
   for (unsigned i = 0, n = getNumPIs(); i < n; ++i)
      cout << " " << getPi(i)->getGid();
   cout << endl;
}

void
CirMgr::printPOs() const
{
   cout << "POs of the circuit:";
   for (unsigned i = 0, n = getNumPOs(); i < n; ++i)
      cout << " " << getPo(i)->getGid();
   cout << endl;
}

void
CirMgr::printFloatGates() const
{
   size_t fn = _floatList.size();
   if (fn) {
      cout << "Gates with floating fanin(s):";
      for (size_t i = 0; i < fn; ++i)
         cout << " " << _floatList[i];
      cout << endl;
   }
   size_t un = _unusedList.size();
   if (un) {
      cout << "Gates defined but not used  :";
      for (size_t i = 0; i < un; ++i)
         cout << " " << _unusedList[i];
      cout << endl;
   }
}

// Groups are printed in ascending order of their first (smallest) gate ID.
void
CirMgr::printFECPairs() const
{
   vector<size_t> order(_fecGrps.size());
   for (size_t i = 0; i < order.size(); ++i) order[i] = i;
   sort(order.begin(), order.end(), [this](size_t a, size_t b) {
      return _fecGrps[a][0] < _fecGrps[b][0]; });

   for (size_t i = 0; i < order.size(); ++i) {
      const IdList& grp = _fecGrps[order[i]];
      cout << "[" << i << "]";
      for (unsigned lit : grp)
         cout << " " << (lit % 2 ? "!" : "") << lit / 2;
      cout << endl;
   }
}

void
CirMgr::writeAag(ostream& outfile) const
{
   size_t nAig = 0;
   for (size_t i = 0, n = _dfsList.size(); i < n; ++i)
      if (_dfsList[i]->isAig()) ++nAig;
   outfile << "aag " << _header.maxVar << " " << _header.numPi << " "
           << _header.numLatch << " " << _header.numPo << " "
           << nAig << endl;
   for (size_t i = 0, n = _header.numPi; i < n; ++i)
      outfile << (getPi(i)->getGid()*2) << endl;
   for (size_t i = 0, n = _header.numPo; i < n; ++i)
      outfile << getPo(i)->in0().litId() << endl;
   for (size_t i = 0, n = _dfsList.size(); i < n; ++i) {
      const CirAigGate *g = _dfsList[i]->asAig();
      if (!g) continue;
      outfile << g->getGid()*2 << " " << g->in0().litId() << " "
           << g->in1().litId() << endl;
   }
   for (size_t i = 0, n = _header.numPi; i < n; ++i)
      if (!getPi(i)->getName().empty())
         outfile << "i" << i << " " << getPi(i)->getName() << endl;
   for (size_t i = 0, n = _header.numPo; i < n; ++i)
      if (!getPo(i)->getName().empty())
         outfile << "o" << i << " " << getPo(i)->getName() << endl;
   outfile << "c" << endl;
   outfile << "AAG output by Chung-Yang (Ric) Huang" << endl;
}

void
CirMgr::writeGate(ostream& outfile, CirGate *g) const
{
   GateList faninCone;
   CirGate::setGlobalRef();
   g->genDfsList(faninCone);

   size_t nAig = 0, npi = 0;
   unsigned maxId = 0;
   for (size_t i = 0, n = faninCone.size(); i < n; ++i) {
      if (faninCone[i]->isAig()) ++nAig;
      else if (faninCone[i]->isPi()) ++npi;
      if (faninCone[i]->getGid() > maxId) maxId = faninCone[i]->getGid();
   }
   outfile << "aag " << maxId << " " << npi << " 0 1 " << nAig << endl;
   for (size_t i = 0, n = _header.numPi; i < n; ++i)
      if (getPi(i)->isGlobalRef())
         outfile << (getPi(i)->getGid()*2) << endl;
   outfile << g->getGid()*2 << endl;
   for (size_t i = 0, n = faninCone.size(); i < n; ++i) {
      const CirAigGate *f = faninCone[i]->asAig();
      if (!f) continue;
      outfile << f->getGid()*2 << " " << f->in0().litId() << " "
           << f->in1().litId() << endl;
   }
   for (size_t i = 0, n = _header.numPi, j = 0; i < n; ++i)
      if (getPi(i)->isGlobalRef() && !getPi(i)->getName().empty())
         outfile << "i" << j++ << " " << getPi(i)->getName() << endl;
   outfile << "o0 " << g->getGid() << endl;
   outfile << "c" << endl;
   outfile << "Write gate (" << g->getGid()
           << ") by Chung-Yang (Ric) Huang" << endl;
}

/**********************************************************/
/*   class CirMgr member functions for hidden commands    */
/**********************************************************/
// After this command, cm1 and cm2 will be deleted!!
