#include <iostream>
#include <vector>

int main() {
   std::vector<int> v(4, 7);
   int sum = 0;
   for (int i = 0; i <= 4; ++i)   // 注意：<= 4，會多讀一格
      sum += v[i];
   std::cout << "sum = " << sum << "\n";
}
