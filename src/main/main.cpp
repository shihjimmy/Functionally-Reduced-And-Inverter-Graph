/****************************************************************************
  FileName     [ main.cpp ]
  PackageName  [ main ]
  Synopsis     [ Define main() ]
  Author       [ Chung-Yang (Ric) Huang ]
  Copyright    [ Copyleft(c) 2007-present LaDs(III), GIEE, NTU, Taiwan ]
****************************************************************************/

#include "util.h"
#include "cmdParser.h"
#include "cmdCommon.h"
#include "cirCmd.h"
#include "cirSession.h"
#include "guiServer.h"

using namespace std;

static void
usage()
{
   cout << "Usage: fraig [ -File < doFile > ]\n"
        << "       fraig -Gui [ port ]    (browser front end, default port 8080)"
        << endl;
}

static void
myexit()
{
   usage();
   exit(-1);
}

// Stage 8: the program's state lives here, in main(), not in globals.
//   session  the current circuit (the cir commands keep a reference to it)
//   parser   owns every command object (some keep a reference to it)
// "session" is declared first so that it is destroyed after "parser":
// the commands that refer to it go away before it does.
int
main(int argc, char** argv)
{
   CirSession session;
   CmdParser  parser("fraig> ");

   // fraig -Gui [port]: serve the browser front end instead of the prompt
   if (argc >= 2 && myStrNCmp("-Gui", argv[1], 2) == 0) {
      int port = 8080;
      if (argc > 3 || (argc == 3 && (!myStr2Int(argv[2], port) ||
                                     port <= 0 || port > 65535))) {
         cerr << "Error: illegal port number!!\n";
         myexit();
      }
      if (!initCommonCmd(parser) || !initCirCmd(parser, session))
         return 1;
      return runGuiServer((unsigned short)port, parser);
   }

   if (argc == 3) {  // -file <doFile>
      if (myStrNCmp("-File", argv[1], 2) == 0) {
         if (!parser.openDofile(argv[2])) {
            cerr << "Error: cannot open file \"" << argv[2] << "\"!!\n";
            myexit();
         }
      }
      else {
         cerr << "Error: unknown argument \"" << argv[1] << "\"!!\n";
         myexit();
      }
   }
   else if (argc != 1) {
      cerr << "Error: illegal number of argument (" << argc << ")!!\n";
      myexit();
   }

   if (!initCommonCmd(parser) || !initCirCmd(parser, session))
      return 1;

   CmdExecStatus status = CMD_EXEC_DONE;
   while (status != CMD_EXEC_QUIT) {  // until "quit" or command error
      status = parser.execOneCmd();
      cout << endl;  // a blank line between each command
   }

   return 0;
}
