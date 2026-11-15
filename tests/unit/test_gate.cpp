// CirGateV and the gate classes.
#include <type_traits>
#include <gtest/gtest.h>
#include "cirGate.h"

// The point of stage 7: an integer is not an edge.  These are compile-time
// facts; if someone adds an implicit conversion back, this file stops compiling.
static_assert(!std::is_convertible<size_t, CirGateV>::value,
              "an integer must not convert to an edge implicitly");
static_assert(!std::is_convertible<CirGate*, CirGateV>::value,
              "a gate pointer must be turned into an edge explicitly");
static_assert(!std::is_convertible<CirGateV, bool>::value,
              "an edge is not a bool (only explicit tests like if (e))");
static_assert(sizeof(CirGateV) == sizeof(void*), "an edge is one pointer");
// AIG gates are most of a circuit.  On 64-bit Linux (gcc and clang use the
// same layout rules) one is 64 bytes; adding a field in the wrong place adds 8.
static_assert(sizeof(void*) != 8 || sizeof(CirAigGate) == 64, "CirAigGate grew");

TEST(CirGateV, DefaultIsNull)
{
   CirGateV e;
   EXPECT_FALSE(e);
   EXPECT_EQ(e.gate(), nullptr);
}

TEST(CirGateV, KeepsPointerAndPhase)
{
   CirAigGate g(7, 1);
   CirGateV pos(&g), neg(&g, true);
   EXPECT_TRUE(pos);
   EXPECT_EQ(pos.gate(), &g);
   EXPECT_EQ(neg.gate(), &g);
   EXPECT_FALSE(pos.isInv());
   EXPECT_TRUE(neg.isInv());
   EXPECT_EQ(pos.litId(), 14u);
   EXPECT_EQ(neg.litId(), 15u);
   EXPECT_NE(pos, neg);
}

TEST(CirGateV, InversionIsAValueOperation)
{
   CirAigGate g(3, 1);
   const CirGateV e(&g);
   EXPECT_EQ(~e, CirGateV(&g, true));
   EXPECT_EQ(~~e, e);
   EXPECT_EQ(e ^ false, e);
   EXPECT_EQ(e ^ true, ~e);
   EXPECT_FALSE(e.isInv());   // e itself did not change
}

TEST(CirGateV, FloatMeansDrivenByUndef)
{
   CirUndefGate u(5);
   CirPiGate p(6, 2);
   EXPECT_TRUE(CirGateV(&u).isFloat());
   EXPECT_FALSE(CirGateV(&p).isFloat());
}

TEST(GateType, TagsAndNames)
{
   CirPiGate pi(1, 2);
   CirPoGate po(9, 3);
   CirAigGate aig(4, 5);
   CirConstGate c(0);
   CirUndefGate u(8);
   EXPECT_TRUE(pi.isPi());   EXPECT_EQ(pi.type(), GateType::Pi);
   EXPECT_TRUE(po.isPo());   EXPECT_STREQ(po.typeName(), "PO");
   EXPECT_TRUE(aig.isAig()); EXPECT_STREQ(aig.typeName(), "AIG");
   EXPECT_TRUE(c.isConst()); EXPECT_STREQ(c.typeName(), "CONST");
   EXPECT_TRUE(u.isUndef()); EXPECT_STREQ(u.typeName(), "UNDEF");
   EXPECT_FALSE(pi.isAig());
}

TEST(Gate, FaninsThroughTheCommonInterface)
{
   CirPiGate a(1, 2), b(2, 3);
   CirAigGate g(3, 4);
   g.setIn0(CirGateV(&a));
   g.setIn1(CirGateV(&b, true));
   const CirGate& base = g;
   ASSERT_EQ(base.numFanins(), 2u);
   EXPECT_EQ(base.fanin(0), CirGateV(&a));
   EXPECT_EQ(base.fanin(1), CirGateV(&b, true));
   EXPECT_EQ(a.numFanins(), 0u);

   CirPoGate po(10, 5);
   po.setIn0(CirGateV(&g, true));
   EXPECT_EQ(po.numFanins(), 1u);
   EXPECT_EQ(po.fanin(0).gate(), &g);
}

TEST(Gate, AsAigIsACheckedDowncast)
{
   CirPiGate pi(1, 2);
   CirAigGate aig(3, 4);
   CirGate* p = &pi;
   CirGate* q = &aig;
   EXPECT_EQ(p->asAig(), nullptr);
   EXPECT_EQ(q->asAig(), &aig);
}

TEST(Gate, ReplaceFaninKeepsThePhaseOfTheEdge)
{
   CirPiGate a(1, 2), b(2, 3);
   CirAigGate g(3, 4);
   g.setIn0(CirGateV(&a, true));
   g.setIn1(CirGateV(&b));
   g.replaceFanin(&a, &b, true);           // !a  ->  !(!b) = b
   EXPECT_EQ(g.in0(), CirGateV(&b));
   EXPECT_EQ(g.in1(), CirGateV(&b));
}
