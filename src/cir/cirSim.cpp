/****************************************************************************
  FileName     [ cirSim.cpp ]
  PackageName  [ cir ]
  Synopsis     [ Define cir simulation functions ]
  Author       [ Chung-Yang (Ric) Huang ]
  Copyright    [ Copyleft(c) 2008-present LaDs(III), GIEE, NTU, Taiwan ]
****************************************************************************/

#include <fstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <unordered_map>
#include <cassert>
#include "cirMgr.h"
#include "cirGate.h"
#include "util.h"

using namespace std;

/**************************************************************************
   Parallel-pattern simulation
   ---------------------------
   Every gate value is a 64-bit word: bit k is the gate's value under
   pattern k. One pass over the DFS list therefore simulates 64 patterns,
   and the cost per pass is O(#gates).

   FEC groups
   ----------
   Gates that have produced identical (or exactly inverted) words so far are
   candidates for being equivalent. After each pass every group is split by
   its members' new values using a hash map from "normalized value" to
   sub-group, i.e. O(1) expected per member, O(#gates) per pass.
   Normalizing: a value v and its complement ~v belong to the same group,
   so the key is whichever of v / ~v has bit 0 cleared, and the member's
   phase records whether it was complemented.
**************************************************************************/


/************************************************/
/*   Public member functions about Simulation   */
/************************************************/
// Simulate random patterns until the FEC groups stop changing for a number
// of consecutive passes. Bigger circuits get more patience, because one
// pass is less likely to split anything in a big circuit.
void
CirMgr::randomSim()
{
   assert(!whyNot(CirStep::Simulate));
   _lastStep = CirStep::Simulate;
   const size_t nPi = getNumPIs();
   const size_t maxStablePasses = std::clamp<size_t>(getNumAigs() / 100, 4, 32);

   vector<size_t> piWords(nPi);
   size_t nPatterns = 0, stablePasses = 0;
   while (stablePasses < maxStablePasses) {
      const size_t oldGroups = _fecGrps.size();
      size_t oldMembers = 0;
      for (const IdList& grp : _fecGrps) oldMembers += grp.size();

      for (size_t& w : piWords) w = _rng();
      simulatePatterns(piWords, kPatternWidth);
      nPatterns += kPatternWidth;
      cout << "\rTotal #FEC Group = " << _fecGrps.size() << flush;

      size_t newMembers = 0;
      for (const IdList& grp : _fecGrps) newMembers += grp.size();
      const bool changed = (_fecGrps.size() != oldGroups) ||
                           (newMembers != oldMembers);
      stablePasses = changed ? 0 : stablePasses + 1;
   }
   cout << "\r" << nPatterns << " patterns simulated." << endl;
}

// Read patterns ("0101...", one per whitespace-separated token) and
// simulate them 64 at a time. A malformed pattern stops the simulation;
// the patterns of the unfinished 64-pattern chunk are then dropped.
void
CirMgr::fileSim(ifstream& patternFile)
{
   assert(!whyNot(CirStep::Simulate));
   _lastStep = CirStep::Simulate;   // even if the file has an error (as the reference)
   const size_t nPi = getNumPIs();
   vector<size_t> piWords(nPi, 0);
   size_t nInChunk = 0, nPatterns = 0;
   bool error = false;

   string pattern;
   while (patternFile >> pattern) {
      if (pattern.size() != nPi) {
         cerr << "\nError: Pattern(" << pattern << ") length(" << pattern.size()
              << ") does not match the number of inputs(" << nPi
              << ") in a circuit!!" << endl;
         error = true; break;
      }
      size_t bad = pattern.find_first_not_of("01");
      if (bad != string::npos) {
         cerr << "\nError: Pattern(" << pattern << ") contains a non-0/1 "
              << "character(\'" << pattern[bad] << "\')." << endl;
         error = true; break;
      }
      for (size_t i = 0; i < nPi; ++i)
         if (pattern[i] == '1') piWords[i] |= (size_t(1) << nInChunk);

      if (++nInChunk == kPatternWidth) {
         simulatePatterns(piWords, nInChunk);
         nPatterns += nInChunk;
         cout << "\rTotal #FEC Group = " << _fecGrps.size() << flush;
         nInChunk = 0;
         fill(piWords.begin(), piWords.end(), 0);
      }
   }
   if (!error && nInChunk > 0) {   // last, partially filled chunk
      simulatePatterns(piWords, nInChunk);   // unused bits are all-0 patterns
      nPatterns += nInChunk;
      cout << "\rTotal #FEC Group = " << _fecGrps.size() << flush;
   }
   cout << "\r" << nPatterns << " patterns simulated." << endl;
}

/*************************************************/
/*   Private member functions about Simulation   */
/*************************************************/
// One 64-pattern pass: set the input words, evaluate the DFS list in
// order, log, and refine the FEC groups. "nPatterns" (<= 64) only limits
// the log. "words" holds one word per PI; UNDEF gates are simulated as 0,
// unless "words" also has one word per UNDEF gate (fraig's
// counter-examples carry the UNDEF values of the SAT model).
void
CirMgr::simulatePatterns(const vector<size_t>& words, size_t nPatterns)
{
   const size_t nPi = getNumPIs();
   for (size_t i = 0; i < nPi; ++i)
      getPi(i)->setPValue(words[i]);
   for (size_t j = 0; j < _undefList.size(); ++j)
      getGate(_undefList[j])->setPValue(nPi + j < words.size() ? words[nPi + j] : 0);
   for (CirGate* g : _dfsList) g->pSim();   // PI / CONST: no-op

   if (_simLog) writeSimLog(words, nPatterns);
   refineFECGroups();
}

// One line per pattern: "<PI bits> <PO bits>", first PI / PO leftmost.
void
CirMgr::writeSimLog(const vector<size_t>& piWords, size_t nPatterns) const
{
   string line;
   for (size_t k = 0; k < nPatterns; ++k) {
      line.clear();
      for (size_t i = 0; i < getNumPIs(); ++i)
         line += ((piWords[i] >> k) & 1) ? '1' : '0';
      line += ' ';
      for (unsigned i = 0, n = getNumPOs(); i < n; ++i)
         line += ((getPo(i)->getPValue()() >> k) & 1) ? '1' : '0';
      *_simLog << line << '\n';
   }
}

// After the first simulated word: CONST 0 and every AIG gate of the DFS
// list, grouped by value, where v and ~v go to the same group.
void
CirMgr::initFECGroups()
{
   markDfsCone();
   IdList all(1, 0);   // CONST 0 (litId 0) first: IDs stay ascending
   for (unsigned id = 1, n = getNumTots(); id < n; ++id) {
      CirGate* g = getGate(id);
      if (g && g->isAig() && g->isGlobalRef()) all.push_back(id * 2);
   }
   vector<IdList> groups;
   splitByValue(all, true, groups);
   clearFECGroups();
   setFECGroups(groups);
   _fecReady = true;
}

// Later words: members of a group already agree, with their recorded
// phase, on every earlier pattern. They stay together only if they still
// agree WITH THAT SAME PHASE, so the key is the phase-corrected value.
// (Re-grouping by "v or ~v" again would wrongly keep a pair that was equal
// before and is inverted now.)
void
CirMgr::refineFECGroups()
{
   if (!_fecReady) { initFECGroups(); return; }
   vector<IdList> refined;
   for (const IdList& grp : _fecGrps) splitByValue(grp, false, refined);
   setFECGroups(refined);
}

// Split one candidate list by the members' current values and append the
// resulting groups (2 members or more) to "out". A hash map from key to
// sub-group makes this O(1) expected per member.
//   allowInverse = true : key = v or ~v (whichever has bit 0 cleared)
//   allowInverse = false: key = v, complemented if the member's phase is 1
// Members are visited in ascending ID order, so new groups stay sorted;
// phases are re-based on each new group's first member.
void
CirMgr::splitByValue(const IdList& members, bool allowInverse,
                     vector<IdList>& out) const
{
   // The value of a member, normalized so that members that must stay
   // together have the same key, and the member's phase relative to it.
   auto keyOf = [&](unsigned lit, size_t& key, unsigned& phase) {
      size_t v = getGate(lit / 2)->getPValue()();
      if (!allowInverse && (lit & 1)) v = ~v;
      const bool inv = allowInverse && (v & 1);
      key = inv ? ~v : v;
      phase = (allowInverse ? 0 : (lit & 1)) ^ inv;
   };

   // Fast path (stage 8.5): in most rounds most groups do not split.  If
   // every member is still there and has the same key, the group stays as
   // it is: no hash table, no copy of the members' order.
   if (!allowInverse && members.size() > 1) {
      bool same = true;
      size_t key0 = 0, key;
      unsigned phase;
      for (size_t i = 0; i < members.size() && same; ++i) {
         if (!getGate(members[i] / 2)) { same = false; break; }   // merged away
         keyOf(members[i], key, phase);
         if (i == 0) key0 = key; else same = (key == key0);
      }
      if (same && (members[0] & 1) == 0) { out.push_back(members); return; }
   }

   // General case: split by key.  Small groups look their key up in a
   // vector (no allocation of a hash table); big ones use a hash table.
   const bool small = members.size() <= 16;
   unordered_map<size_t, size_t> subOf;   // key -> index in "subs" (big groups)
   vector<size_t> subKey;                 // key of each sub (small groups)
   vector<IdList> subs;
   vector<bool> headInv;                  // value inversion of each head
   for (unsigned lit : members) {
      if (!getGate(lit / 2)) continue;    // merged away during fraig
      size_t key;
      unsigned phase;
      keyOf(lit, key, phase);

      size_t idx = subs.size();
      if (small) {
         for (size_t k = 0; k < subKey.size(); ++k)
            if (subKey[k] == key) { idx = k; break; }
         if (idx == subs.size()) subKey.push_back(key);
      }
      else {
         auto [it, isNew] = subOf.try_emplace(key, subs.size());
         if (!isNew) idx = it->second;
      }
      if (idx == subs.size()) {
         subs.push_back(IdList(1, lit / 2 * 2));
         headInv.push_back(phase);
      }
      else
         subs[idx].push_back(lit / 2 * 2 + (phase != headInv[idx]));
   }
   for (IdList& sub : subs)
      if (sub.size() > 1) out.push_back(std::move(sub));
}

// Used by fraig after merging: keep only gates that still exist and are
// still reachable from the POs (CONST 0 is always kept). If a group's first
// member is dropped, phases are re-based so the new first one is positive.
void
CirMgr::purgeFECGroups()
{
   rebuildDerivedLists();
   markDfsCone();
   vector<IdList> kept;
   for (const IdList& grp : _fecGrps) {
      IdList alive;
      for (unsigned lit : grp) {
         CirGate* g = getGate(lit / 2);
         if (g && (g->isConst() || g->isGlobalRef())) alive.push_back(lit);
      }
      if (alive.size() < 2) continue;
      const unsigned flip = alive[0] % 2;
      for (unsigned& lit : alive) lit ^= flip;
      kept.push_back(std::move(alive));
   }
   setFECGroups(kept);
}

// Replace the groups by "groups" (which is consumed) and re-number _fecId:
// every gate that was in an old group loses its old _fecId first.
void
CirMgr::setFECGroups(vector<IdList>& groups)
{
   for (const IdList& grp : _fecGrps)
      for (unsigned lit : grp)
         if (CirGate* g = getGate(lit / 2)) g->resetFECId();
   _fecGrps.swap(groups);
   clearList(groups);
   for (size_t i = 0; i < _fecGrps.size(); ++i)
      for (unsigned lit : _fecGrps[i])
         getGate(lit / 2)->setFECId(unsigned(i * 2 + lit % 2));
}

void
CirMgr::clearFECGroups()
{
   for (const IdList& grp : _fecGrps)
      for (unsigned lit : grp)
         if (CirGate* g = getGate(lit / 2)) g->resetFECId();
   clearList(_fecGrps);
   _fecReady = false;
}
