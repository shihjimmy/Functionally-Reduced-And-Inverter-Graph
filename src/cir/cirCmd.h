/****************************************************************************
  FileName     [ cirCmd.h ]
  PackageName  [ cir ]
  Synopsis     [ Define basic cir package commands ]
  Author       [ Chung-Yang (Ric) Huang ]
  Copyright    [ Copyleft(c) 2008-present LaDs(III), GIEE, NTU, Taiwan ]
****************************************************************************/

#ifndef CIR_CMD_H
#define CIR_CMD_H

#include "cmdParser.h"
#include "cirSession.h"

// Registers the cir commands; they all work on "session".
bool initCirCmd(CmdParser& parser, CirSession& session);

// Base of every cir command: it keeps a reference to the session (stage 8:
// there is no global cirMgr); circuit() is the current circuit or nullptr.
class CirCommand : public CmdExec
{
public:
   // Public on purpose: an inherited constructor keeps the access it has
   // here, and std::make_unique<CirReadCmd>(session) must be able to call
   // it.  CirCommand itself cannot be created anyway (exec() etc. are pure).
   explicit CirCommand(CirSession& s) : _session(s) {}

protected:
   CirMgr* circuit() const { return _session.circuit(); }
   CirSession& _session;
};

// "using CirCommand::CirCommand" inherits the constructor (C++11), so each
// command is constructed as CirReadCmd(session) without writing one.
// "final": nothing derives from a concrete command.
class CirReadCmd final : public CirCommand
{
public:
   using CirCommand::CirCommand;   // inherit the constructor
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
};

class CirPrintCmd final : public CirCommand
{
public:
   using CirCommand::CirCommand;   // inherit the constructor
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
};

class CirGateCmd final : public CirCommand
{
public:
   using CirCommand::CirCommand;   // inherit the constructor
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
};

class CirSweepCmd final : public CirCommand
{
public:
   using CirCommand::CirCommand;   // inherit the constructor
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
};

class CirOptCmd final : public CirCommand
{
public:
   using CirCommand::CirCommand;   // inherit the constructor
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
};

class CirStrashCmd final : public CirCommand
{
public:
   using CirCommand::CirCommand;   // inherit the constructor
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
};

class CirSimCmd final : public CirCommand
{
public:
   using CirCommand::CirCommand;   // inherit the constructor
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
};

class CirFraigCmd final : public CirCommand
{
public:
   using CirCommand::CirCommand;   // inherit the constructor
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
};

class CirWriteCmd final : public CirCommand
{
public:
   using CirCommand::CirCommand;   // inherit the constructor
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
};

class CirJsonCmd final : public CirCommand
{
public:
   using CirCommand::CirCommand;   // inherit the constructor
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
};

class CirVerifyCmd final : public CirCommand
{
public:
   using CirCommand::CirCommand;   // inherit the constructor
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
};

#endif // CIR_CMD_H
