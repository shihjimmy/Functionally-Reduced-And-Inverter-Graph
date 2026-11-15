#include <expected>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
using namespace std;

// 風格 A：回傳 bool + 全域變數（fraig 現在的寫法）
static string errMsg;
bool parseA(const string& s, unsigned& out) {
   if (s.empty()) { errMsg = "Missing number"; return false; }
   for (char c : s)
      if (c < '0' || c > '9') { errMsg = "Illegal number \"" + s + "\""; return false; }
   out = stoul(s);
   return true;
}

// 風格 B：例外
unsigned parseB(const string& s) {
   if (s.empty()) throw runtime_error("Missing number");
   for (char c : s)
      if (c < '0' || c > '9') throw runtime_error("Illegal number \"" + s + "\"");
   return stoul(s);
}

// 風格 C：optional（成功或失敗，沒有原因）
optional<unsigned> parseC(const string& s) {
   if (s.empty()) return nullopt;
   for (char c : s)
      if (c < '0' || c > '9') return nullopt;
   return stoul(s);
}

// 風格 D：expected（C++23，成功值或錯誤原因，二選一）
expected<unsigned, string> parseD(const string& s) {
   if (s.empty()) return unexpected("Missing number");
   for (char c : s)
      if (c < '0' || c > '9') return unexpected("Illegal number \"" + s + "\"");
   return stoul(s);
}

int main() {
   for (string in : {string("42"), string("4x2")}) {
      cout << "輸入 \"" << in << "\"\n";
      unsigned v = 0;
      if (parseA(in, v)) cout << "  A: " << v << "\n"; else cout << "  A: 失敗 (" << errMsg << ")\n";
      try { unsigned b = parseB(in); cout << "  B: " << b << "\n"; }
      catch (const exception& e) { cout << "  B: 例外 (" << e.what() << ")\n"; }
      if (auto r = parseC(in)) cout << "  C: " << *r << "\n"; else cout << "  C: 失敗 (沒有原因)\n";
      auto d = parseD(in);
      if (d) cout << "  D: " << *d << "\n"; else cout << "  D: 失敗 (" << d.error() << ")\n";
   }
}
