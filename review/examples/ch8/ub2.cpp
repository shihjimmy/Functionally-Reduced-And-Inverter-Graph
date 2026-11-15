#include <cstdlib>
#include <iostream>

// 編譯器可以假設「有號整數不會溢位」，所以 x + 1 > x 被當成永遠成立
bool alwaysTrue(int x) { return x + 1 > x; }

int main(int argc, char** argv) {
   int x = (argc > 1) ? std::atoi(argv[1]) : 0;   // 執行時才知道數值
   int y = x + 1;                                  // x 是 INT_MAX 時溢位
   std::cout << "x + 1 = " << y << "\n";
   std::cout << "(x + 1 > x) = " << alwaysTrue(x) << "\n";
}
