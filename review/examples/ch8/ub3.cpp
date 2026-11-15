#include <iostream>
#include <vector>

int main() {
   std::vector<int> v;
   v.push_back(1);
   int* p = &v[0];          // 指向元素
   for (int i = 0; i < 100; ++i) v.push_back(i);   // 容量不夠 -> 搬家，舊記憶體被釋放
   std::cout << *p << "\n";  // p 已經是懸空指標
}
