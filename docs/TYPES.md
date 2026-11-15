# 型別安全設計（階段 7）

階段 6 讓「誰擁有記憶體」寫在型別上。階段 7 處理另一類問題：**型別說的和實際存的不一致**。
這類問題不會讓測試失敗，但會讓下一個讀程式（或叫 AI 改程式）的人誤解，改錯地方。

目標：**一個型別只代表一種東西；不合法的操作在編譯期就被擋下。**

## 1. 改之前：盤點

| 問題 | 在哪裡 | 為什麼危險 |
|---|---|---|
| `CirGateV` 可以從任何 `size_t` 隱含建構 | `CirGateV(size_t v = 0)` | `CirGateV e = 5;` 會編譯通過，得到一個指向位址 5 的「邊」 |
| `CirGateV` 在讀檔期間存的是 **literal 編號**，不是指標 | parser 把 `litId` 塞進 `_in0`，`genConnections()` 再換成指標 | 同一個型別在不同時間代表不同東西；在這段期間呼叫 `gate()` 會得到垃圾指標 |
| 換成指標要透過**全域** `cirMgr` | `CirPoGate::genConnections()` 寫 `cirMgr->checkConnectedGate(...)` | 只有「全域指標指向的那個 `CirMgr`」能正確讀檔，另外建立的 `CirMgr`（例如單元測試）會連到別的電路 |
| 「浮接」旗標存在邊上，但它其實是**閘的屬性** | `AIG_FLT_FANIN` 位元，`replaceFanin` 要記得重設 | 同一件事存兩份（邊的旗標、閘是不是 UNDEF），可能不一致 |
| 胖介面：每個閘都有 `getIn0()`、`getIn1()` | `CirGate` 的虛擬函式，PI 回傳 0 | 對 PI 呼叫 `getIn0()` 不會出錯，只會安靜地回傳空邊；呼叫端要自己記得「只有 AIG 才能問」 |
| 閘型別用 5 個虛擬函式回答 | `isPi()`、`isPo()`、`isAig()`... 各自覆寫 | 每次問型別都是一次虛擬呼叫；型別其實建構時就固定了 |
| `GateType`、`ParsePorts` 是沒有作用域的 `enum` | `PI_GATE` 等在全域；`PI`、`PO`、`AIG` 在 `CirMgr` 裡 | 同一個概念有兩套名字（`PI_GATE` 和 `PI`）；`enum` 可以隱含轉成整數，`_numDecl[PI_GATE]` 這種錯誤會編譯通過 |
| `getTypeStr()` 每次回傳新的 `std::string` | 5 個覆寫 | 只是為了印出 "AIG"，每次都配置一次記憶體 |
| 一大段註解掉的 `createMiter` | `cirMgr.cpp` | 死程式碼，而且引用了要被移除的介面；`CIRVerify` 已經取代它 |

## 2. 改之後

### 決定 1：`CirGateV` = 「指向閘的邊，可能反相」，只有這個意思

```cpp
class CirGateV {
public:
   CirGateV() = default;                                   // 空邊
   explicit CirGateV(CirGate* g, bool inv = false);        // 必須明確給指標
   CirGate* gate() const;
   bool     isInv() const;
   CirGateV operator~() const;                             // 反相的同一條邊
   CirGateV operator^(bool inv) const;                     // inv 為真時反相
   explicit operator bool() const;                         // 是不是空邊
   std::uintptr_t bits() const;                            // 只給雜湊、排序用
   ...
};
```

- **沒有從整數的隱含轉換**。`CirGateV e = 5;` 和 `CirGateV(size_t(g) + 1)` 都不再能編譯；
  要寫 `CirGateV(g, true)`。
- 位元存在 `std::uintptr_t`（標準保證能放下指標的整數型別），不是 `size_t`。
- **值語意**：「把邊反相」寫成 `~e` 或 `e ^ inv`，回傳新的邊，不再有 `setInv()`、`replace()` 這類會改自己的函式。
- 拿掉「浮接」位元：`isFloat()` 改成直接問 `gate()->isUndef()`。只有一份資料，就不會不一致。
- 低位元的指標標記（pointer tagging）保留：它讓一條邊只占 8 bytes，是 AIG 工具常見的做法。
  但現在標記的細節只出現在這個類別的實作裡，其他程式碼看不到。

### 決定 2：讀檔時的 literal 另外存，不塞進 `CirGateV`

parser 把每個 PO、AIG 的 fanin literal 存在 `CirMgr::readCircuit()` 的區域清單裡；
所有閘建立完之後，`CirMgr::connectFanins()` 一次把 literal 換成 `CirGateV` 並建 fanout。

- `CirGateV` 從建立的那一刻起就是合法的邊。
- 閘的 `genConnections()` 虛擬函式整個拿掉，連帶拿掉它對全域 `cirMgr` 的依賴：
  任何一個 `CirMgr` 物件都能正確讀檔（單元測試需要這點，階段 8 也是）。

### 決定 3：閘型別是建構時固定的標籤

```cpp
enum class GateType : unsigned char { Undef, Pi, Po, Aig, Const };

class CirGate {
public:
   GateType type() const { return _type; }
   bool isPi() const  { return _type == GateType::Pi; }     // 不是虛擬函式
   ...
   const char* typeName() const;                            // "PI"、"AIG"…，不配置記憶體
protected:
   CirGate(GateType t, unsigned id, unsigned line);
private:
   const GateType _type;
};
```

- `enum class`：名字在 `GateType::` 之下，不會和別的 `PI` 撞名；不能隱含轉成整數。
- `: unsigned char` 讓它只占 1 byte，放在 `_fecId` 後面原本就有的 4 bytes padding 裡，物件大小不變。
  （第一次放在 `_lineNo` 後面，結果 `CirAigGate` 從 64 變成 72 bytes，見第 5 節。）
- 型別問題從虛擬呼叫變成一次比較。

### 決定 4：fanin 只有一套通用介面

| 介面 | 誰有 | 說明 |
|---|---|---|
| `numFanins()`、`fanin(i)` | 所有閘 | PI、CONST、UNDEF 回傳 0 個；`fanin(i)` 在 `i >= numFanins()` 時觸發 `assert` |
| `in0()`、`in1()` | 只有 `CirAigGate`（`CirPoGate` 只有 `in0()`） | 知道自己拿的是 AIG 時用 |
| `asAig()` | 所有閘 | 是 AIG 就回傳 `CirAigGate*`，否則 `nullptr` |

處理「任何閘」的程式碼（走訪 fanin、印 fanin、輸出 JSON）用 `numFanins()` / `fanin(i)` 迴圈；
只處理 AIG 的程式碼（optimize 的化簡、strash、fraig、寫檔）先 `asAig()` 拿到 `CirAigGate*`，再用 `in0()`、`in1()`。
「對 PI 問 `getIn0()`」從「安靜地得到空邊」變成「編譯錯誤」（`CirGate` 沒有 `in0()`）。

**為什麼不用 `dynamic_cast`**：型別標籤已經知道答案，`asAig()` 用 `static_cast` 就安全，而且沒有 RTTI 的成本。

### 決定 5：AIGER 標頭用有名字的欄位

`unsigned _numDecl[TOT_PARSE_PORTS]` 加上 `CirMgr` 裡的 `enum { VARS, PI, LATCH, PO, AIG }`，
改成 `struct AagHeader { unsigned maxVar, numPi, numLatch, numPo, numAig; }`。
`_numDecl[AIG]` 變成 `_header.numAig`，不再需要那組列舉名字。

### 決定 6：單元測試（原本的階段 5）

- 用 GoogleTest，CMake 的 `FetchContent` 下載固定版本 1.12.1（最後一個支援 C++11 的版本；階段 9 升級後再換）。
  `-DFRAIG_UNIT_TESTS=OFF` 可以關掉（離線時用）。
- 新增 `CirMgr::checkInvariants()`：檢查 netlist 的內部一致性，回傳第一個違反的說明。
  - 每個 `_gates[id]` 的 ID 等於 `id`
  - 每條 fanin 邊都指向存在的閘，而且這個閘的 fanout 清單裡有我；反過來每個 fanout 都真的以我為 fanin
  - `_dfsList` 裡 fanin 一定排在 fanout 前面，而且沒有被 retire 的閘
  - AIG 計數等於實際的 AIG 數
- 單元測試在每個改電路的指令（sweep、optimize、strash、fraig）之後呼叫它。
  黃金輸出測試只看得到「印出來的東西」，這些內部關係它看不到；這就是單元測試補上的部分。

## 3. 刻意不做的事

| 選項 | 為什麼不做 |
|---|---|
| 用 `std::variant` 取代閘的繼承 | 要 C++17；而且閘的種類固定、虛擬函式只剩印出與模擬，繼承仍然清楚 |
| 把邊改成「ID + 反相位元」（不用指標） | 每次取閘都要多一次查表，而且改動面太大；目前的指標標記已經被包在類別裡 |
| `getName()` 也從 `CirGate` 拿掉 | 「名字」對所有閘都有意義（沒有名字 = 空字串），不是「只有某種閘才能問」的操作，不算胖介面 |
| 改指令層的 `enum`（`CmdExecStatus` 等） | 屬於指令框架，階段 8、9 整理指令層時一起處理 |

## 4. 怎麼確認改對了

型別重構不應該改變任何輸出：ctest（黃金 + 隨機）、ASan/UBSan、`tests/fuzz_diff.sh`、
`scripts/ab_bench.sh` 交替比速度，外加新的單元測試。

## 5. 結果

| 檢查 | 結果 |
|---|---|
| ctest（gcc、clang、ASan/UBSan）：56 個黃金 + 1 個隨機電路 + 29 個單元測試 | 86/86 通過 |
| `tests/check_against_ref.sh`、`fuzz_diff.sh 300`、`fuzz_parser.sh`（150 個損壞檔） | 和參考程式完全一致 |
| 45 個測資的完整流程（ASan/UBSan，含 `cirg -fi/-fo`、兩輪 fraig、`cirv` 兩次） | 沒有報告 |
| 物件大小 | `CirAigGate` 64、`CirConstGate`/`CirUndefGate` 48 bytes，和階段 7 之前相同 |
| 速度（`scripts/ab_bench.sh`，和階段 6 的執行檔交替） | sim07 fraig 流程 1.00 倍、sim13 sweep+opt+strash+檔案模擬 0.99 倍、sim13 fraig 流程 0.98 倍 |

實作中值得記下的三件事：

1. **改介面後，編譯器列出 36 個錯誤**，正好是所有用到舊介面的地方。改型別比用搜尋找呼叫端可靠。
2. **物件大小**：1 byte 的 `_type` 放錯位置就讓每個 AIG 閘多 8 bytes（對齊），現在有 `static_assert` 守著。
3. **單元測試抓到黃金輸出抓不到的 bug**：在 `retireGate` 故意少移除一筆 fanout，56 個黃金輸出測試全過，
   單元測試立刻報 `CONST(0) has a fanout that is not in the netlist`。
