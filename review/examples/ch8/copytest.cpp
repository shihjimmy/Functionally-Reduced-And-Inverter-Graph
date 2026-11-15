#include "cirMgr.h"
#include "cirGate.h"
#include "cmdParser.h"
CmdParser* cmdMgr = 0;
int main() {
   CirMgr a; cirMgr = &a;
   a.readCircuit("tests.fraig/sim01.aag");
   {
      CirMgr b = a;      // 隱含的複製建構子：只複製指標值
      std::cout << "copied, b has " << b.getNumPIs() << " PIs\n";
   }                      // b 解構：delete 掉 a 也指著的那些 gate
   std::cout << "back in main, a destructs next\n";
}
