/****************************************************************************
  FileName     [ guiServer.h ]
  PackageName  [ main ]
  Synopsis     [ Browser front end of fraig ]
****************************************************************************/

#ifndef GUI_SERVER_H
#define GUI_SERVER_H

class CmdParser;

// Serve the GUI on http://localhost:<port> until the process is stopped,
// running commands through "parser" (whose commands must already be
// registered).  Returns non-zero on setup failure.
int runGuiServer(unsigned short port, CmdParser& parser);

#endif // GUI_SERVER_H
