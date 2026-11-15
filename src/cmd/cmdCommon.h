/****************************************************************************
  FileName     [ cmdCommon.h ]
  PackageName  [ cmd ]
  Synopsis     [ Define classes for common commands ]
  Author       [ Chung-Yang (Ric) Huang ]
  Copyright    [ Copyleft(c) 2007-present LaDs(III), GIEE, NTU, Taiwan ]
****************************************************************************/
#ifndef CMD_COMMON_H
#define CMD_COMMON_H

#include "cmdParser.h"
#include "myUsage.h"

// Registers HELp, Quit, HIStory, DOfile and USAGE in "parser".
bool initCommonCmd(CmdParser& parser);

// A command that needs the parser itself (HELp lists the commands, HIStory
// prints the history, DOfile opens a script).  The parser owns the command
// and outlives it, so a reference is safe (stage 8: no global cmdMgr).
class ParserCommand : public CmdExec
{
public:
   explicit ParserCommand(CmdParser& p) : _parser(p) {}   // public: see CirCommand

protected:
   CmdParser& _parser;
};

class HelpCmd final : public ParserCommand
{
public:
   using ParserCommand::ParserCommand;   // inherit the constructor
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
};

class HistoryCmd final : public ParserCommand
{
public:
   using ParserCommand::ParserCommand;   // inherit the constructor
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
};

class DofileCmd final : public ParserCommand
{
public:
   using ParserCommand::ParserCommand;   // inherit the constructor
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
};

class QuitCmd final : public CmdExec
{
public:
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
};

// USAGE owns the time/memory counters (stage 8: they were the global
// "myUsage").  The command is created at start-up, so the counters start
// when the program starts, as before.
class UsageCmd final : public CmdExec
{
public:
   CmdExecStatus exec(const std::string& option) override;
   void usage(std::ostream& os) const override;
   void help() const override;
private:
   MyUsage _usage;
};

#endif // CMD_COMMON_H
