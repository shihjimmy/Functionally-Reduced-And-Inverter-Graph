# 去除全域狀態（階段 8）

全域變數的問題不是「不好看」，而是**任何程式碼都能讀寫它，所以讀一個函式時看不出它依賴什麼**。
`CirGate::reportGate()` 的簽名沒有任何參數，實際上卻讀了全域的 `cirMgr`；只有一個電路時沒事，
單元測試同時建兩個 `CirMgr` 就會讀到別人的資料。

目標：**每個函式需要的東西，都從參數或成員拿到；看簽名就知道它依賴什麼。**

## 1. 改之前：盤點

| 全域狀態 | 在哪裡 | 誰用 |
|---|---|---|
| `unique_ptr<CirMgr> cirMgr` | `cirMgr.cpp` | 所有 cir 指令；`CirGate::reportGate()`、`reportFanout()` |
| `unique_ptr<CmdParser> cmdMgr` | `main.cpp` | `main`；HELP、HISTORY、DOfile 指令；GUI 伺服器 |
| `static CirCmdState curCmd` | `cirCmd.cpp` | 流程規則（模擬後不能 optimize、fraig 前要先模擬…）與 GUI 的 JSON |
| parser 的 `lineNo`、`colNo`、`buf[1024]`、`errMsg`、`errInt`、`errGate` | `cirMgr.cpp` 檔案層級 `static` | 讀檔的每個函式 |
| `MyUsage myUsage` | `util.cpp` | `main` 歸零、USAGE 指令回報 |
| `RandomNumGen rnGen` | `util.cpp` | **沒有人用**（模擬用 `CirMgr` 自己的 `mt19937_64`） |
| `Job job` | `guiServer.cpp` | GUI 的背景工作 |

## 2. 改之後：誰持有什麼

```
main()
 ├─ CirSession session      目前的電路（或沒有）：unique_ptr<CirMgr>
 ├─ CmdParser  parser       擁有所有指令物件
 │    ├─ cir 指令            建構時拿到 session 的參考
 │    ├─ HELP / HISTORY / DOfile   建構時拿到 parser 的參考
 │    └─ USAGE              自己持有 MyUsage（建立時開始計時）
 └─ runGui(port, parser)    GuiServer 物件持有背景工作的狀態
```

### 決定 1：流程規則屬於電路本身

「模擬之後不能 optimize」「fraig 之前要先模擬」是**電路的狀態**，不是指令層的事：
同一個電路做過哪些步驟，決定了接下來能做什麼。所以 `curCmd` 變成 `CirMgr` 的成員：

```cpp
enum class CirStep { Read, Optimize, Strash, Simulate, Fraig };
const char* CirMgr::whyNot(CirStep next) const;   // 可以做：nullptr；不行：錯誤訊息
const char* CirMgr::stageName() const;            // "READ"、"SIMULATE"…（GUI 用）
```

- `optimize()`、`strash()`、`randomSim()`、`fileSim()`、`fraig()` 自己更新狀態，
  開頭用 `assert(!whyNot(...))` 檢查前提：**規則由領域層保證**，不再靠每個指令記得檢查。
- 指令只負責「印出訊息、不呼叫」：`if (const char* e = c->whyNot(CirStep::Fraig)) { cerr << e; ... }`。
- 換電路 = 建新的 `CirMgr`，狀態自然回到 READ，不需要另外重設。
- 錯誤訊息逐字保留（規格要求和參考程式一致）。

### 決定 2：`CirSession` 持有目前的電路，指令持有它的參考

```cpp
class CirSession {
public:
   CirMgr* circuit() const;               // 借用；沒有電路時是 nullptr
   void setCircuit(unique_ptr<CirMgr>);   // 換電路（舊的自動釋放）
   void clear();
};
```

`initCirCmd(CmdParser&, CirSession&)` 註冊指令時把 session 傳進去，每個 cir 指令把它存成成員參考。
參考（`&`）而不是指標，表示「一定有、而且比我活得久」：session 由 `main` 建立，活到程式結束。

### 決定 3：需要 parser 的指令，建構時拿到 parser

HELP、HISTORY、DOfile 要呼叫 `CmdParser` 的函式。`initCommonCmd(CmdParser&)` 註冊時把自己傳進去。
這裡有一個循環：parser 擁有指令，指令引用 parser。這是安全的，因為**擁有者（parser）一定比被擁有者活得久**，
指令只是借用它。

### 決定 4：parser 的狀態是一個「每次讀檔」的物件

六個檔案層級的 `static` 變數和五個輔助函式變成 `struct AagLexer`（只在 `cirMgr.cpp` 裡看得到）。
`readCircuit()` 建立一個區域的 `AagLexer`，讀檔函式透過參數拿到它；讀完它就消失。

`buf[1024]` 的固定長度**刻意保留**：參考程式用同樣的長度，規格要求錯誤訊息一致，
超過 1023 個字元的行在兩邊會有一樣的行為。

### 決定 5：`CirGate` 的報告函式拿到 `const CirMgr&`

`reportGate()`、`reportFanout()` 要查 FEC 群組和 fanout 清單，這兩樣都在 `CirMgr` 裡。
改成 `reportGate(const CirMgr&)`：簽名就說出它需要電路。

### 決定 6：其他

- `MyUsage` 變成 USAGE 指令的成員；指令在啟動時註冊，所以計時從那時開始，和原本 `main` 裡 `myUsage.reset()` 相同。
- 沒有人用的 `rnGen` 直接刪掉。
- GUI 的 `Job job` 變成 `GuiServer` 物件的成員，伺服器拿到 `CmdParser&` 來執行指令。

## 3. 刻意保留的「全域」

| 東西 | 為什麼保留 |
|---|---|
| `CirGate::_globalRef_s`（走訪用的計數器，`static`） | 每次走訪前加一，比較「這個閘的標記是不是現在的值」。多個 `CirMgr` 共用一個計數器也正確，因為每次都拿到新的值。只有「兩個執行緒同時走訪」會出錯，而 GUI 一次只執行一個指令。要改成每個 `CirMgr` 一個計數器，閘就得知道自己屬於哪個電路，代價比問題大 |
| `cout` / `cerr` | 指令的輸出就是規格的一部分；GUI 用 `rdbuf` 暫時改向 |
| MiniSat 內部的 `static` | 第三方程式碼，不改 |
| 終端機設定 `stored_settings`（`myGetChar.cpp`） | 對應的是「這個程序的終端機」，本來就只有一個 |

## 4. 怎麼確認改對了

和階段 6、7 一樣，輸出不應該有任何改變：ctest（黃金 + 隨機 + 單元測試）、ASan/UBSan、
`fuzz_diff.sh`、`fuzz_parser.sh`（parser 的改動要特別靠它）、`ab_bench.sh`。
另外加單元測試：流程規則（`whyNot`）、兩個 `CirMgr` 同時讀檔再印報告。

## 5. 結果

| 檢查 | 結果 |
|---|---|
| ctest（gcc、clang、ASan/UBSan）：59 個黃金 + 1 個隨機電路 + 32 個單元測試 | 92/92 通過 |
| `tests/check_against_ref.sh`（含 3 個新的流程規則案例）、`fuzz_diff.sh 300`、`fuzz_parser.sh`（200 個損壞檔） | 和參考程式一致 |
| 45 個測資完整流程（ASan/UBSan，含 `help`、`history`、`usage`、`cirg`） | 沒有報告 |
| GUI（ASan/UBSan 版，換三次電路） | 沒有報告；fraig 之後 Strash 按鈕正確地可以按 |
| valgrind | 結束時 0 bytes 未釋放 |
| 速度（`ab_bench.sh`，和階段 7 交替） | sim07 0.98 倍、sim13 檔案模擬流程 0.99 倍 |
| `grep` 全域狀態（MiniSat 與第 3 節的刻意保留除外） | 沒有 |

實作中的三件事（教學筆記 review 4.8 有細節）：

1. **搬規則時抓到 bug**：`fraig()` 內部呼叫 `strash()`，規則搬進 `strash()` 後 fraig 會被記成一次 strash。
   改成公開的 `strash()`（步驟）＋私有的 `strashNetlist()`（工作），並加了單元測試。
2. **ODR 違規**：標頭檔裡寫 `namespace { struct AagLexer; }` 編譯得過，但每個 .cpp 看到的是不同的型別。
   改成 `CirMgr` 的私有巢狀型別。
3. **不完整型別**：`main.cpp` 銷毀 `CirMgr` 時看不到完整的 `CirGate`，所以 `CirMgr` 的建構子與解構子要定義在 .cpp。
