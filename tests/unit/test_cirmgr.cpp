// CirMgr: reading, and the internal invariants after every editing command.
#include <algorithm>
#include <random>
#include <tuple>
#include <gtest/gtest.h>
#include "cirGate.h"
#include "test_util.h"

// Lets a test break a netlist on purpose, to check that checkInvariants()
// notices.  (CirMgr declares it a friend; nothing else uses it.)
struct CirMgrTestPeer
{
   static GateList& fanouts(CirMgr& m, unsigned id) { return m._fanoutInfo[id]; }
   static GateList& dfs(CirMgr& m) { return m._dfsList; }
};

// PIs 1, 2;  gate 3 = 1 & 2;  gate 4 = !1 & 5, where 5 is never defined
// (an UNDEF gate);  the PO (ID M + 1 = 6) is !4.
static const char* kSmall =
   "aag 5 2 0 1 2\n"
   "2\n"
   "4\n"
   "9\n"
   "6 2 4\n"
   "8 3 10\n";

TEST(CirMgrRead, BuildsEdgesAndFanouts)
{
   std::unique_ptr<CirMgr> m = readAagText(kSmall);
   ASSERT_TRUE(m);
   EXPECT_EQ(m->checkInvariants(), "");

   const CirAigGate* g4 = m->getGate(4)->asAig();
   ASSERT_TRUE(g4);
   EXPECT_EQ(g4->in0(), CirGateV(m->getGate(1), true));   // literal 3 = !1
   EXPECT_TRUE(m->getGate(5)->isUndef());                 // literal 10 was never defined
   EXPECT_TRUE(g4->in1().isFloat());
   EXPECT_EQ(m->getPo(0)->in0(), CirGateV(m->getGate(4), true));   // literal 9 = !4
   EXPECT_EQ(m->getFanouts(1).size(), 2u);                // gates 3 and 4
   EXPECT_EQ(m->getNumAigs(), 2u);
}

// Stage 7 removed the global cirMgr from reading: two managers can read
// two circuits at the same time without seeing each other.
TEST(CirMgrRead, TwoManagersAreIndependent)
{
   std::unique_ptr<CirMgr> a = readAagFile("tests.fraig/sim01.aag");
   std::unique_ptr<CirMgr> b = readAagFile("tests.fraig/ISCAS85/C17.aag");
   ASSERT_TRUE(a);
   ASSERT_TRUE(b);
   EXPECT_EQ(a->checkInvariants(), "");
   EXPECT_EQ(b->checkInvariants(), "");
   EXPECT_NE(a->const0(), b->const0());    // stage 6: not one static CONST gate
   b.reset();                              // freeing one ...
   EXPECT_EQ(a->checkInvariants(), "");    // ... does not touch the other
}

TEST(CirMgrRead, RejectsBadFiles)
{
   EXPECT_FALSE(readAagText("aag 1 1 0 0 0\n3\n"));     // odd PI literal
   EXPECT_FALSE(readAagText("aag 2 1 0 1 1\n2\n4\n4 2\n"));   // AIG with one input
}

TEST(CheckInvariants, NoticesABrokenFanoutList)
{
   std::unique_ptr<CirMgr> m = readAagText(kSmall);
   ASSERT_TRUE(m);
   CirMgrTestPeer::fanouts(*m, 1).pop_back();
   EXPECT_NE(m->checkInvariants(), "");
}

TEST(CheckInvariants, NoticesABadDfsOrder)
{
   std::unique_ptr<CirMgr> m = readAagText(kSmall);
   ASSERT_TRUE(m);
   GateList& dfs = CirMgrTestPeer::dfs(*m);
   std::reverse(dfs.begin(), dfs.end());   // fanouts before fanins
   EXPECT_NE(m->checkInvariants(), "");
}

// --- every editing command keeps the invariants ------------------------------

// Runs one command; fails the test with the invariant message if broken.
template <class Command>
void runAndCheck(CirMgr& m, const char* what, Command cmd)
{
   { Quiet q; cmd(m); }
   EXPECT_EQ(m.checkInvariants(), "") << "after " << what;
}

void allCommands(CirMgr& m)
{
   runAndCheck(m, "sweep",     [](CirMgr& c) { c.sweep(); });
   runAndCheck(m, "optimize",  [](CirMgr& c) { c.optimize(); });
   runAndCheck(m, "strash",    [](CirMgr& c) { c.strash(); });
   runAndCheck(m, "random sim",[](CirMgr& c) { c.randomSim(); });
   runAndCheck(m, "fraig",     [](CirMgr& c) { c.fraig(); });
   runAndCheck(m, "sweep",     [](CirMgr& c) { c.sweep(); });
   runAndCheck(m, "optimize",  [](CirMgr& c) { c.optimize(); });
}

class EveryCommand : public ::testing::TestWithParam<const char*> {};

TEST_P(EveryCommand, KeepsInvariants)
{
   std::unique_ptr<CirMgr> m = readAagFile(GetParam());
   ASSERT_TRUE(m) << GetParam();
   EXPECT_EQ(m->checkInvariants(), "");
   allCommands(*m);
}

INSTANTIATE_TEST_SUITE_P(TestCircuits, EveryCommand, ::testing::Values(
   "tests.fraig/opt01.aag", "tests.fraig/opt03.aag", "tests.fraig/opt05.aag",
   "tests.fraig/opt07.aag", "tests.fraig/strash01.aag", "tests.fraig/strash05.aag",
   "tests.fraig/strash08.aag", "tests.fraig/sim01.aag", "tests.fraig/sim05.aag",
   "tests.fraig/sim06.aag", "tests.fraig/test1.aag", "tests.fraig/test2.aag",
   "tests.fraig/ISCAS85/C17.aag", "tests.fraig/ISCAS85/C432.aag",
   "tests.fraig/ISCAS85/C880.aag"));

// Random circuits with constants, duplicate and complementary fanins,
// copies of gates and undefined fanins (a C++ cousin of tests/gen_aig.py).
std::string randomAag(unsigned seed)
{
   std::mt19937 rnd(seed);
   auto pick = [&](unsigned n) { return unsigned(rnd() % n); };
   const unsigned nIn = 1 + pick(6), nAnd = pick(40), nOut = 1 + pick(4), nUndef = pick(3);
   const unsigned M = nIn + nAnd + nUndef;
   std::vector<unsigned> lits;                       // usable fanin literals (positive)
   for (unsigned i = 1; i <= nIn; ++i) lits.push_back(2 * i);
   auto fanin = [&]() -> unsigned {
      const unsigned r = pick(100);
      if (r < 5) return pick(2);                                         // constant
      if (r < 10 && nUndef) return 2 * (nIn + nAnd + 1 + pick(nUndef)) + pick(2);   // undefined
      return lits[lits.size() - 1 - pick(std::min<unsigned>(lits.size(), 6))] + pick(2);
   };
   std::ostringstream os;
   os << "aag " << M << " " << nIn << " 0 " << nOut << " " << nAnd << "\n";
   for (unsigned i = 1; i <= nIn; ++i) os << 2 * i << "\n";
   std::ostringstream ands;
   std::vector<std::pair<unsigned, unsigned>> made;
   for (unsigned k = 0; k < nAnd; ++k) {
      const unsigned v = nIn + 1 + k;
      unsigned a = fanin(), b = fanin();
      if (pick(10) == 0 && !made.empty()) { std::tie(a, b) = made[pick(made.size())]; }  // a copy
      if (pick(10) == 0) b = a ^ pick(2);                                                // a&a, a&!a
      made.push_back(std::make_pair(a, b));
      ands << 2 * v << " " << a << " " << b << "\n";
      lits.push_back(2 * v);
   }
   for (unsigned i = 0; i < nOut; ++i) os << fanin() << "\n";
   os << ands.str();
   return os.str();
}

TEST(EveryCommandRandom, KeepsInvariants)
{
   for (unsigned seed = 1; seed <= 300; ++seed) {
      std::unique_ptr<CirMgr> m = readAagText(randomAag(seed));
      ASSERT_TRUE(m) << "seed " << seed << "\n" << randomAag(seed);
      ASSERT_EQ(m->checkInvariants(), "") << "seed " << seed;
      allCommands(*m);
      if (::testing::Test::HasFailure()) { ADD_FAILURE() << "seed " << seed << "\n" << randomAag(seed); return; }
   }
}

// --- flow rules (stage 8: they belong to the circuit, see CirMgr::whyNot) ---

TEST(FlowRules, FollowTheSpec)
{
   std::unique_ptr<CirMgr> m = readAagFile("tests.fraig/sim05.aag");
   ASSERT_TRUE(m);
   EXPECT_STREQ(m->stageName(), "READ");
   EXPECT_EQ(m->whyNot(CirStep::Fraig), std::optional<std::string_view>("Error: circuit is not yet simulated!!"));
   EXPECT_FALSE(m->whyNot(CirStep::Optimize).has_value());

   { Quiet q; m->strash(); }
   EXPECT_EQ(m->whyNot(CirStep::Strash), std::optional<std::string_view>("Error: circuit has been strashed!!"));

   { Quiet q; m->randomSim(); }
   EXPECT_STREQ(m->stageName(), "SIMULATE");
   const char* simulated = "Error: circuit has been simulated!! Do \"CIRFraig\" first!!";
   EXPECT_EQ(m->whyNot(CirStep::Optimize), std::optional<std::string_view>(simulated));
   EXPECT_EQ(m->whyNot(CirStep::Strash), std::optional<std::string_view>(simulated));
   EXPECT_FALSE(m->whyNot(CirStep::Fraig).has_value());
}

// fraig() strashes internally; that must not count as a CIRSTRash step
// (a bug found while moving the rules into CirMgr in stage 8).
TEST(FlowRules, FraigDoesNotCountAsStrash)
{
   std::unique_ptr<CirMgr> m = readAagFile("tests.fraig/sim05.aag");
   ASSERT_TRUE(m);
   { Quiet q; m->randomSim(); m->fraig(); }
   EXPECT_STREQ(m->stageName(), "FRAIG");
   EXPECT_FALSE(m->whyNot(CirStep::Strash).has_value());
   EXPECT_FALSE(m->whyNot(CirStep::Optimize).has_value());
}

// Each circuit has its own flow state.
TEST(FlowRules, PerCircuit)
{
   std::unique_ptr<CirMgr> a = readAagFile("tests.fraig/sim05.aag");
   std::unique_ptr<CirMgr> b = readAagFile("tests.fraig/sim05.aag");
   ASSERT_TRUE(a && b);
   { Quiet q; a->randomSim(); }
   EXPECT_STREQ(a->stageName(), "SIMULATE");
   EXPECT_STREQ(b->stageName(), "READ");
}
