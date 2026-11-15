#include <iostream>
#include <memory>
#include <vector>

// 邊 (in0/in1) 只是「看一下」別人，不負責刪除 -> 裸指標沒問題
struct Gate {
   int   id;
   Gate* in0 = nullptr;
   Gate* in1 = nullptr;
};

// 唯一的擁有者：vector<unique_ptr<Gate>>
// 不需要手寫解構子、複製建構子、賦值運算子
class Circuit {
public:
   Gate* add(int id) {
      _gates.push_back(std::make_unique<Gate>());
      _gates.back()->id = id;
      return _gates.back().get();
   }
private:
   std::vector<std::unique_ptr<Gate>> _gates;
};

int main() {
   Circuit c;
   Gate* a = c.add(1);
   Gate* b = c.add(2);
   Gate* g = c.add(3);
   g->in0 = a;
   g->in1 = b;
   std::cout << g->in0->id + g->in1->id << "\n";
#ifdef TRY_COPY
   Circuit d = c;   // 複製擁有者：編譯期就被擋下
#endif
}
