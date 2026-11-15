/****************************************************************************
  FileName     [ cirCmd.cpp ]
  PackageName  [ cir ]
  Synopsis     [ Define basic cir package commands ]
  Author       [ Chung-Yang (Ric) Huang ]
  Copyright    [ Copyleft(c) 2008-present LaDs(III), GIEE, NTU, Taiwan ]
****************************************************************************/

#include <cassert>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <sstream>
#include "cirVerify.h"
#include "cirMgr.h"
#include "cirGate.h"
#include "cirCmd.h"
#include "util.h"

using namespace std;

extern int effLimit;

bool
initCirCmd(CmdParser& parser, CirSession& session)
{
   if (!(parser.regCmd("CIRRead", 4, std::make_unique<CirReadCmd>(session)) &&
         parser.regCmd("CIRPrint", 4, std::make_unique<CirPrintCmd>(session)) &&
         parser.regCmd("CIRGate", 4, std::make_unique<CirGateCmd>(session)) &&
         parser.regCmd("CIRSWeep", 5, std::make_unique<CirSweepCmd>(session)) &&
         parser.regCmd("CIROPTimize", 6, std::make_unique<CirOptCmd>(session)) &&
         parser.regCmd("CIRSTRash", 6, std::make_unique<CirStrashCmd>(session)) &&
         parser.regCmd("CIRSIMulate", 6, std::make_unique<CirSimCmd>(session)) &&
         parser.regCmd("CIRFraig", 4, std::make_unique<CirFraigCmd>(session)) &&
         parser.regCmd("CIRWrite", 4, std::make_unique<CirWriteCmd>(session)) &&
         parser.regCmd("CIRJson", 4, std::make_unique<CirJsonCmd>(session)) &&
         parser.regCmd("CIRVerify", 4, std::make_unique<CirVerifyCmd>(session))
      )) {
      cerr << "Registering \"cir\" commands fails... exiting" << endl;
      return false;
   }
   return true;
}


//----------------------------------------------------------------------
//    CIRRead <(string fileName)> [-Replace]
//----------------------------------------------------------------------
CmdExecStatus
CirReadCmd::exec(const string& option)
{
   // check option
   vector<string> options;
   if (!CmdExec::lexOptions(option, options))
      return CMD_EXEC_ERROR;
   if (options.empty())
      return CmdExec::errorOption(CMD_OPT_MISSING, "");

   bool doReplace = false;
   string fileName;
   for (size_t i = 0, n = options.size(); i < n; ++i) {
      if (myStrNCmp("-Replace", options[i], 2) == 0) {
         if (doReplace) return CmdExec::errorOption(CMD_OPT_EXTRA,options[i]);
         doReplace = true;
      }
      else {
         if (fileName.size())
            return CmdExec::errorOption(CMD_OPT_ILLEGAL, options[i]);
         fileName = options[i];
      }
   }

   if (circuit() != nullptr) {
      if (doReplace) {
         cerr << "Note: original circuit is replaced..." << endl;
         _session.clear();   // frees the old circuit and all its gates
      }
      else {
         cerr << "Error: circuit already exists!!" << endl;
         return CMD_EXEC_ERROR;
      }
   }
   unique_ptr<CirMgr> c = std::make_unique<CirMgr>();
   if (!c->readCircuit(fileName))
      return CMD_EXEC_ERROR;          // "c" is freed; the session has no circuit
   _session.setCircuit(std::move(c));

   return CMD_EXEC_DONE;
}

void
CirReadCmd::usage(ostream& os) const
{
   os << "Usage: CIRRead <(string fileName)> [-Replace]" << endl;
}

void
CirReadCmd::help() const
{
   cout << setw(15) << left << "CIRRead: "
        << "read in a circuit and construct the netlist" << endl;
}

//----------------------------------------------------------------------
//    CIRPrint [-Summary | -Netlist | -PI | -PO | -FLoating | -FECpairs]
//----------------------------------------------------------------------
CmdExecStatus
CirPrintCmd::exec(const string& option)
{
   // check option
   string token;
   if (!CmdExec::lexSingleOption(option, token))
      return CMD_EXEC_ERROR;

   if (!circuit()) {
      cerr << "Error: circuit is not yet constructed!!" << endl;
      return CMD_EXEC_ERROR;
   }
   if (token.empty() || myStrNCmp("-Summary", token, 2) == 0)
      circuit()->printSummary();
   else if (myStrNCmp("-Netlist", token, 2) == 0)
      circuit()->printNetlist();
   else if (myStrNCmp("-PI", token, 3) == 0)
      circuit()->printPIs();
   else if (myStrNCmp("-PO", token, 3) == 0)
      circuit()->printPOs();
   else if (myStrNCmp("-FLoating", token, 3) == 0)
      circuit()->printFloatGates();
   else if (myStrNCmp("-FECpairs", token, 4) == 0)
      circuit()->printFECPairs();
   else
      return CmdExec::errorOption(CMD_OPT_ILLEGAL, token);

   return CMD_EXEC_DONE;
}

void
CirPrintCmd::usage(ostream& os) const
{  
   os << "Usage: CIRPrint [-Summary | -Netlist | -PI | -PO | -FLoating "
      << "| -FECpairs]" << endl;
}

void
CirPrintCmd::help() const
{  
   cout << setw(15) << left << "CIRPrint: " << "print circuit\n";
}

//----------------------------------------------------------------------
//    CIRGate <<(int gateId)> [<-FANIn | -FANOut><(int level)>]>
//----------------------------------------------------------------------
CmdExecStatus
CirGateCmd::exec(const string& option)
{
   if (!circuit()) {
      cerr << "Error: circuit has not been read!!" << endl;
      return CMD_EXEC_ERROR;
   }

   // check option
   vector<string> options;
   if (!CmdExec::lexOptions(option, options))
      return CMD_EXEC_ERROR;

   if (options.empty())
      return CmdExec::errorOption(CMD_OPT_MISSING, "");

   int gateId = -1, level = 0;
   bool doFanin = false, doFanout = false;
   CirGate* thisGate = 0;
   for (size_t i = 0, n = options.size(); i < n; ++i) {
      bool checkLevel = false;
      if (myStrNCmp("-FANIn", options[i], 5) == 0) {
         if (doFanin || doFanout)
            return CmdExec::errorOption(CMD_OPT_ILLEGAL, options[i]);
         doFanin = true;
         checkLevel = true;
      }
      else if (myStrNCmp("-FANOut", options[i], 5) == 0) {
         if (doFanin || doFanout)
            return CmdExec::errorOption(CMD_OPT_ILLEGAL, options[i]);
         doFanout = true;
         checkLevel = true;
      }
      else if (!thisGate) {
         if (!myStr2Int(options[i], gateId) || gateId < 0)
            return CmdExec::errorOption(CMD_OPT_ILLEGAL, options[i]);
         thisGate = circuit()->getGate(gateId);
         if (!thisGate) {
            cerr << "Error: Gate(" << gateId << ") not found!!" << endl;
            return CmdExec::errorOption(CMD_OPT_ILLEGAL, options[0]);
         }
      }
      else if (thisGate)
         return CmdExec::errorOption(CMD_OPT_EXTRA, options[i]);
      else
         return CmdExec::errorOption(CMD_OPT_ILLEGAL, options[i]);
      if (checkLevel) {
         if (++i == n)
            return CmdExec::errorOption(CMD_OPT_MISSING, options[i-1]);
         if (!myStr2Int(options[i], level) || level < 0)
            return CmdExec::errorOption(CMD_OPT_ILLEGAL, options[i]);
         checkLevel = true;
      }
   }

   if (!thisGate) {
      cerr << "Error: Gate id is not specified!!" << endl;
      return CmdExec::errorOption(CMD_OPT_MISSING, options.back());
   }

   if (doFanin)
      thisGate->reportFanin(level);
   else if (doFanout)
      thisGate->reportFanout(*circuit(), level);
   else
      thisGate->reportGate(*circuit());

   return CMD_EXEC_DONE;
}

void
CirGateCmd::usage(ostream& os) const
{
   os << "Usage: CIRGate <<(int gateId)> [<-FANIn | -FANOut><(int level)>]>"
      << endl;
}

void
CirGateCmd::help() const
{
   cout << setw(15) << left << "CIRGate: " << "report a gate\n";
}

//----------------------------------------------------------------------
//    CIRSWeep
//----------------------------------------------------------------------
CmdExecStatus
CirSweepCmd::exec(const string& option)
{
   if (!circuit()) {
      cerr << "Error: circuit is not yet constructed!!" << endl;
      return CMD_EXEC_ERROR;
   }
   // check option
   vector<string> options;
   CmdExec::lexOptions(option, options);

   if (!options.empty())
      return CmdExec::errorOption(CMD_OPT_EXTRA, options[0]);

   circuit()->sweep();

   return CMD_EXEC_DONE;
}

void
CirSweepCmd::usage(ostream& os) const
{
   os << "Usage: CIRSWeep" << endl;
}

void
CirSweepCmd::help() const
{
   cout << setw(15) << left << "CIRSWeep: "
        << "remove unused gates\n";
}

//----------------------------------------------------------------------
//    CIROPTimize
//----------------------------------------------------------------------
CmdExecStatus
CirOptCmd::exec(const string& option)
{
   if (!circuit()) {
      cerr << "Error: circuit is not yet constructed!!" << endl;
      return CMD_EXEC_ERROR;
   }
   // check option
   vector<string> options;
   CmdExec::lexOptions(option, options);

   if (!options.empty())
      return CmdExec::errorOption(CMD_OPT_EXTRA, options[0]);

   if (const auto err = circuit()->whyNot(CirStep::Optimize)) {
      cerr << *err << endl;
      return CMD_EXEC_ERROR;
   }
   circuit()->optimize();

   return CMD_EXEC_DONE;
}

void
CirOptCmd::usage(ostream& os) const
{
   os << "Usage: CIROPTimize" << endl;
}

void
CirOptCmd::help() const
{
   cout << setw(15) << left << "CIROPTimize: "
        << "perform trivial optimizations\n";
}

//----------------------------------------------------------------------
//    CIRSTRash
//----------------------------------------------------------------------
CmdExecStatus
CirStrashCmd::exec(const string& option)
{
   if (!circuit()) {
      cerr << "Error: circuit is not yet constructed!!" << endl;
      return CMD_EXEC_ERROR;
   }
   // check option
   vector<string> options;
   CmdExec::lexOptions(option, options);

   if (!options.empty())
      return CmdExec::errorOption(CMD_OPT_EXTRA, options[0]);

   if (const auto err = circuit()->whyNot(CirStep::Strash)) {
      cerr << *err << endl;
      return CMD_EXEC_ERROR;
   }
   circuit()->strash();

   return CMD_EXEC_DONE;
}

void
CirStrashCmd::usage(ostream& os) const
{
   os << "Usage: CIRSTRash" << endl;
}

void
CirStrashCmd::help() const
{
   cout << setw(15) << left << "CIRSTRash: "
        << "perform structural hash on the circuit netlist\n";
}

//----------------------------------------------------------------------
//    CIRSIMulate <-Random | -File <string patternFile>>
//                [-Output (string logFile)]
//----------------------------------------------------------------------
CmdExecStatus
CirSimCmd::exec(const string& option)
{
   if (!circuit()) {
      cerr << "Error: circuit is not yet constructed!!" << endl;
      return CMD_EXEC_ERROR;
   }
   // check option
   vector<string> options;
   CmdExec::lexOptions(option, options);

   ifstream patternFile;
   ofstream logFile;
   bool doRandom = false, doFile = false, doLog = false;
   for (size_t i = 0, n = options.size(); i < n; ++i) {
      if (myStrNCmp("-Random", options[i], 2) == 0) {
         if (doRandom || doFile)
            return CmdExec::errorOption(CMD_OPT_ILLEGAL, options[i]);
         doRandom = true;
      }
      else if (myStrNCmp("-File", options[i], 2) == 0) {
         if (doRandom || doFile)
            return CmdExec::errorOption(CMD_OPT_ILLEGAL, options[i]);
         if (++i == n)
            return CmdExec::errorOption(CMD_OPT_MISSING, options[i-1]);
         patternFile.open(options[i].c_str(), ios::in);
         if (!patternFile)
            return CmdExec::errorOption(CMD_OPT_FOPEN_FAIL, options[i]);
         doFile = true;
      }
      else if (myStrNCmp("-Output", options[i], 2) == 0) {
         if (doLog)
            return CmdExec::errorOption(CMD_OPT_ILLEGAL, options[i]);
         if (++i == n)
            return CmdExec::errorOption(CMD_OPT_MISSING, options[i-1]);
         logFile.open(options[i].c_str(), ios::out);
         if (!logFile)
            return CmdExec::errorOption(CMD_OPT_FOPEN_FAIL, options[i]);
         doLog = true;
      }
      else
         return CmdExec::errorOption(CMD_OPT_ILLEGAL, options[i]);
   }

   if (!doRandom && !doFile)
      return CmdExec::errorOption(CMD_OPT_MISSING, "");

   if (doLog)
      circuit()->setSimLog(&logFile);
   else circuit()->setSimLog(0);

   if (doRandom)
      circuit()->randomSim();
   else
      circuit()->fileSim(patternFile);
   circuit()->setSimLog(0);
   
   return CMD_EXEC_DONE;
}

void
CirSimCmd::usage(ostream& os) const
{
   os << "Usage: CIRSIMulate <-Random | -File <string patternFile>>\n"
      << "                   [-Output (string logFile)]" << endl;
}

void
CirSimCmd::help() const
{
   cout << setw(15) << left << "CIRSIMulate: "
        << "perform Boolean logic simulation on the circuit\n";
}

//----------------------------------------------------------------------
//    CIRFraig
//----------------------------------------------------------------------
CmdExecStatus
CirFraigCmd::exec(const string& option)
{
   if (!circuit()) {
      cerr << "Error: circuit is not yet constructed!!" << endl;
      return CMD_EXEC_ERROR;
   }
   // check option
   vector<string> options;
   CmdExec::lexOptions(option, options);

   if (!options.empty())
      return CmdExec::errorOption(CMD_OPT_EXTRA, options[0]);

   if (const auto err = circuit()->whyNot(CirStep::Fraig)) {
      cerr << *err << endl;
      return CMD_EXEC_ERROR;
   }
   circuit()->fraig();

   return CMD_EXEC_DONE;
}

void
CirFraigCmd::usage(ostream& os) const
{
   os << "Usage: CIRFraig" << endl;
}

void
CirFraigCmd::help() const
{
   cout << setw(15) << left << "CIRFraig: "
        << "perform Boolean logic simulation on the circuit\n";
}

//----------------------------------------------------------------------
//    CIRWrite [(int gateId)][-Output (string aagFile)]
//----------------------------------------------------------------------
CmdExecStatus
CirWriteCmd::exec(const string& option)
{
   if (!circuit()) {
      cerr << "Error: circuit is not yet constructed!!" << endl;
      return CMD_EXEC_ERROR;
   }
   // check option
   vector<string> options;
   CmdExec::lexOptions(option, options);

   if (options.empty()) {
      circuit()->writeAag(cout);
      return CMD_EXEC_DONE;
   }
   bool hasFile = false;
   int gateId;
   CirGate *thisGate = NULL;
   ofstream outfile;
   for (size_t i = 0, n = options.size(); i < n; ++i) {
      if (myStrNCmp("-Output", options[i], 2) == 0) {
         if (hasFile) 
            return CmdExec::errorOption(CMD_OPT_EXTRA, options[i]);
         if (++i == n)
            return CmdExec::errorOption(CMD_OPT_MISSING, options[i-1]);
         outfile.open(options[i].c_str(), ios::out);
         if (!outfile)
            return CmdExec::errorOption(CMD_OPT_FOPEN_FAIL, options[1]);
         hasFile = true;
      }
      else if (myStr2Int(options[i], gateId) && gateId >= 0) {
         if (thisGate != NULL)
            return CmdExec::errorOption(CMD_OPT_EXTRA, options[i]);
         thisGate = circuit()->getGate(gateId);
         if (!thisGate) {
            cerr << "Error: Gate(" << gateId << ") not found!!" << endl;
            return CmdExec::errorOption(CMD_OPT_ILLEGAL, options[i]);
         }
         if (!thisGate->isAig()) {
             cerr << "Error: Gate(" << gateId << ") is NOT an AIG!!" << endl;
            return CmdExec::errorOption(CMD_OPT_ILLEGAL, options[i]);
         }
      }
      else return CmdExec::errorOption(CMD_OPT_ILLEGAL, options[i]);
   }

   if (!thisGate) {
      assert (hasFile);
      circuit()->writeAag(outfile);
   }
   else if (hasFile) circuit()->writeGate(outfile, thisGate);
   else circuit()->writeGate(cout, thisGate);

   return CMD_EXEC_DONE;
}

void
CirWriteCmd::usage(ostream& os) const
{
   os << "Usage: CIRWrite [(int gateId)][-Output (string aagFile)]" << endl;
}

void
CirWriteCmd::help() const
{
   cout << setw(15) << left << "CIRWrite: "
        << "write the netlist to an ASCII AIG file (.aag)\n";
}


//----------------------------------------------------------------------
//    CIRJson
//    Print the circuit and the command state as one JSON object. It is
//    meant for programs (the GUI front end), not for people.
//----------------------------------------------------------------------
CmdExecStatus
CirJsonCmd::exec(const string& option)
{
   if (!CmdExec::lexNoOption(option))
      return CMD_EXEC_ERROR;

   // The flow stage tells the GUI which commands are allowed next.
   cout << "{\"stage\":\"" << (circuit() ? circuit()->stageName() : "INIT")
        << "\",\"circuit\":";
   if (circuit()) circuit()->writeJson(cout);
   else cout << "null";
   cout << "}" << endl;
   return CMD_EXEC_DONE;
}

void
CirJsonCmd::usage(ostream& os) const
{
   os << "Usage: CIRJson" << endl;
}

void
CirJsonCmd::help() const
{
   cout << setw(15) << left << "CIRJson: "
        << "print the circuit as JSON (used by the GUI)\n";
}

//----------------------------------------------------------------------
//    CIRVerify [-File (string aagFile)]
//----------------------------------------------------------------------
// Proves that the current circuit computes the same outputs as the
// original file read by CIRRead (or as another .aag file given by -File).
// Uses its own checker (cirVerify.cpp), not the code that optimized it.
CmdExecStatus
CirVerifyCmd::exec(const string& option)
{
   if (!circuit()) {
      cerr << "Error: circuit is not yet constructed!!" << endl;
      return CMD_EXEC_ERROR;
   }
   vector<string> options;
   CmdExec::lexOptions(option, options);

   string refName = circuit()->sourceName(), refText = circuit()->sourceText();
   for (size_t i = 0, n = options.size(); i < n; ++i) {
      if (myStrNCmp("-File", options[i], 2) == 0) {
         if (++i == n)
            return CmdExec::errorOption(CMD_OPT_MISSING, options[i-1]);
         ifstream in(options[i].c_str());
         if (!in) return CmdExec::errorOption(CMD_OPT_FOPEN_FAIL, options[i]);
         ostringstream text; text << in.rdbuf();
         refName = options[i]; refText = text.str();
      }
      else return CmdExec::errorOption(CMD_OPT_ILLEGAL, options[i]);
   }

   ostringstream current;
   circuit()->writeAag(current);

   cout << "Verifying against \"" << refName << "\"..." << endl;
   size_t shown = 0;   // length of the progress text on screen
   VerifyResult r = verifyAag(refText, current.str(), [&](const char* stage, size_t done, size_t total) {
      if (done == total || done % 256 == 0) {          // keep the terminal quiet
         ostringstream line;
         line << stage << " " << done << "/" << total;
         cout << "\r" << line.str() << flush;
         shown = std::max(shown, line.str().size());
      }
   });
   if (shown) cout << "\r" << string(shown, ' ') << "\r";   // erase it
   if (!r.ok) {
      cerr << "Error: " << r.error << "!!" << endl;
      return CMD_EXEC_ERROR;
   }
   cout << r.numOutputs << " outputs: " << r.byStructure << " identical by structure, "
        << r.bySat << " proven by SAT, " << r.numDiffering << " different." << endl;
   for (size_t i = 0; i < r.mismatches.size(); ++i) {
      const VerifyMismatch& m = r.mismatches[i];
      cout << "  Output " << m.output << " differs for PI values (PI 0 first): "
           << (m.piValues.empty() ? "(none)" : m.piValues);
      if (!m.undefs.empty()) cout << "; undefined signals: " << m.undefs;
      cout << endl;
   }
   if (r.numDiffering > r.mismatches.size())
      cout << "  ... and " << (r.numDiffering - r.mismatches.size()) << " more." << endl;
   cout << (r.equivalent() ? "Result: EQUIVALENT" : "Result: NOT EQUIVALENT") << endl;
   return CMD_EXEC_DONE;
}

void
CirVerifyCmd::usage(ostream& os) const
{
   os << "Usage: CIRVerify [-File (string aagFile)]" << endl;
}

void
CirVerifyCmd::help() const
{
   cout << setw(15) << left << "CIRVerify: "
        << "prove the circuit equivalent to the original (or another) file\n";
}
