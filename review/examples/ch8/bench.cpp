// 三個效能實驗：每個都量中位數（5 次）
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <random>
#include <vector>
using namespace std;
using Clock = chrono::steady_clock;

template <class F>
double medianMs(F f, int reps = 5) {
   vector<double> t;
   for (int i = 0; i < reps; ++i) {
      auto a = Clock::now();
      f();
      auto b = Clock::now();
      t.push_back(chrono::duration<double, milli>(b - a).count());
   }
   sort(t.begin(), t.end());
   return t[t.size() / 2];
}

static volatile uint64_t sink;

// ---------- 實驗 1：指標追逐，記憶體順序 vs 打亂 ----------
struct Node { uint64_t val; Node* next; char pad[48]; };   // 64 bytes，和 CirGate 差不多大

void exp1() {
   const size_t N = 2000000;
   vector<Node*> nodes(N);
   for (size_t i = 0; i < N; ++i) { nodes[i] = new Node(); nodes[i]->val = i; }

   // 依配置順序串起來
   for (size_t i = 0; i + 1 < N; ++i) nodes[i]->next = nodes[i + 1];
   nodes[N - 1]->next = nullptr;
   Node* headSeq = nodes[0];
   double tSeq = medianMs([&] {
      uint64_t s = 0;
      for (Node* p = headSeq; p; p = p->next) s += p->val;
      sink = s;
   });

   // 用隨機順序串起來（節點本身沒動，只是走訪順序不同）
   vector<size_t> perm(N);
   iota(perm.begin(), perm.end(), 0);
   mt19937_64 rng(1);
   shuffle(perm.begin(), perm.end(), rng);
   for (size_t i = 0; i + 1 < N; ++i) nodes[perm[i]]->next = nodes[perm[i + 1]];
   nodes[perm[N - 1]]->next = nullptr;
   Node* headRnd = nodes[perm[0]];
   double tRnd = medianMs([&] {
      uint64_t s = 0;
      for (Node* p = headRnd; p; p = p->next) s += p->val;
      sink = s;
   });

   printf("實驗1 走訪 %zu 個 64B 節點：配置順序 %.1f ms，隨機順序 %.1f ms，慢 %.1fx\n",
          N, tSeq, tRnd, tRnd / tSeq);
   for (auto p : nodes) delete p;
}

// ---------- 實驗 2：虛擬函式 vs 型別標籤 ----------
struct Base {
   int tag;
   uint64_t a, b;
   explicit Base(int t) : tag(t), a(1), b(2) {}
   virtual uint64_t eval() const = 0;
   virtual ~Base() {}
};
struct And : Base { And() : Base(0) {} uint64_t eval() const override { return a & b; } };
struct Or  : Base { Or()  : Base(1) {} uint64_t eval() const override { return a | b; } };

void exp2() {
   const size_t N = 2000000;
   mt19937 rng(7);
   vector<Base*> v(N);
   for (size_t i = 0; i < N; ++i) v[i] = (rng() & 1) ? static_cast<Base*>(new And()) : new Or();

   double tVirt = medianMs([&] {
      uint64_t s = 0;
      for (int pass = 0; pass < 10; ++pass)
         for (Base* p : v) s += p->eval();
      sink = s;
   });
   double tTag = medianMs([&] {
      uint64_t s = 0;
      for (int pass = 0; pass < 10; ++pass)
         for (Base* p : v) s += (p->tag == 0) ? (p->a & p->b) : (p->a | p->b);
      sink = s;
   });
   printf("實驗2 %zu 個物件 x10 趟：虛擬函式 %.1f ms，型別標籤 %.1f ms，比值 %.2fx\n",
          N, tVirt, tTag, tVirt / tTag);
   for (auto p : v) delete p;
}

// ---------- 實驗 3：逐個 new vs 連續的 vector ----------
struct Obj { uint64_t val; char pad[56]; };   // 64 bytes

void exp3() {
   const size_t N = 2000000;
   double tNewCreate = medianMs([&] {
      vector<Obj*> v(N);
      for (size_t i = 0; i < N; ++i) { v[i] = new Obj(); v[i]->val = i; }
      for (auto p : v) delete p;
   });
   double tVecCreate = medianMs([&] {
      vector<Obj> v;
      v.reserve(N);
      for (size_t i = 0; i < N; ++i) { v.emplace_back(); v.back().val = i; }
      sink = v.size();
   });
   printf("實驗3 建立並銷毀 %zu 個 64B 物件：逐個 new/delete %.1f ms，連續 vector %.1f ms，比值 %.1fx\n",
          N, tNewCreate, tVecCreate, tNewCreate / tVecCreate);
}

int main() {
   exp1();
   exp2();
   exp3();
}
