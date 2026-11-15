// Helpers shared by the unit tests.
#ifndef FRAIG_TEST_UTIL_H
#define FRAIG_TEST_UTIL_H

#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include "cirMgr.h"

// Silences cout/cerr while alive (the commands print debug messages).
class Quiet
{
public:
   Quiet() : _out(std::cout.rdbuf(_sink.rdbuf())), _err(std::cerr.rdbuf(_sink.rdbuf())) {}
   ~Quiet() { std::cout.rdbuf(_out); std::cerr.rdbuf(_err); }
   std::string text() const { return _sink.str(); }
private:
   std::ostringstream _sink;
   std::streambuf*    _out;
   std::streambuf*    _err;
};

// Writes "text" to a temporary .aag file and reads it into a new CirMgr.
// Returns nullptr if the file does not parse.
inline std::unique_ptr<CirMgr> readAagText(const std::string& text)
{
   char name[] = "/tmp/fraig_unit_XXXXXX";
   const int fd = mkstemp(name);
   if (fd < 0) return nullptr;
   { std::ofstream f(name); f << text; }
   std::unique_ptr<CirMgr> m(new CirMgr);
   bool ok;
   { Quiet q; ok = m->readCircuit(name); }
   std::remove(name);
   if (!ok) return nullptr;
   return m;
}

inline std::unique_ptr<CirMgr> readAagFile(const std::string& path)
{
   std::unique_ptr<CirMgr> m(new CirMgr);
   Quiet q;
   if (!m->readCircuit(path)) return nullptr;
   return m;
}

#endif
