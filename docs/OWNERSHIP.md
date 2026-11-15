# 所有權設計（階段 6）

這份文件回答一個問題：**每一塊 heap 記憶體由誰負責釋放？**
階段 6 之前，答案散在各處的 `new` / `delete` 裡，要讀完整個程式才知道。
階段 6 之後，答案寫在**型別**上：

- `std::unique_ptr<T>`：我**擁有**這個物件。我被銷毀或重設時，它跟著被釋放。
- `T*` 或 `T&`：我只是**借用**，不負責釋放，而且我必須確定擁有者比我活得久。

規則只有一條：**每個 heap 物件剛好有一個擁有者，其他人都是借用。**
專案裡（第三方的 MiniSat 除外）不再有手寫的 `delete`。

## 1. 改之前：盤點

| 物件 | 誰 `new` | 誰 `delete` | 問題 |
|---|---|---|---|
| 閘（PI、PO、AIG、UNDEF） | `CirMgr` 的 parser | `deleteCircuit()` 迴圈；被移除的閘先放進 `_retired`，`freeRetired()` 再刪 | 正確，但要讀三個函式才看得出來 |
| 常數閘 `CONST 0` | 檔案層級的 `static` 初始化 | `deleteCircuit()` 刪掉後**再 new 一個** | `static` 成員：所有 `CirMgr` 共用同一個常數閘。只有一個電路時沒事，有兩個（例如 miter）就互相踩到 |
| `_totGateList`、`_piList`、`_poList`、`_fanoutInfo` | `new T[n]` | `delete []` | 手動管理陣列長度，`getNumTots()` 要和配置時一致 |
| PI／PO 的名字 | `new char[]` + `strcpy` | 閘的解構子 `delete []` | 用 `char*` 當字串；複製這個閘就會重複釋放 |
| 指令物件 `CmdExec` | `regCmd(..., new XxxCmd)` | **沒有人** | 洩漏（程式結束才回收，valgrind 歸類為 still reachable） |
| dofile 的 `ifstream` | `openDofile` | `closeDofile` | 巢狀 dofile 時放在 `stack<ifstream*>` 裡；出錯路徑要記得刪 |
| 全域 `cirMgr`、`cmdMgr` | `CIRRead`、`main` | `CIRRead` 重讀時；`cmdMgr` 沒人刪 | 全域變數的問題留到階段 8 |
| `myHashSet` / `myHashMap` 的桶 | `new vector[]` | `delete []` | 沒有禁止複製：複製後兩個物件指向同一塊 |

## 2. 改之後：誰擁有什麼

```
main()
 ├─ cmdMgr : unique_ptr<CmdParser>                       （全域，階段 8 移除）
 │    ├─ _cmdMap : map<string, unique_ptr<CmdExec>>      擁有每個指令物件
 │    └─ _dofile, _dofileStack : unique_ptr<ifstream>    擁有開著的 dofile
 └─ cirMgr : unique_ptr<CirMgr>                          （全域，階段 8 移除）
      ├─ _gates   : vector<unique_ptr<CirGate>>          擁有所有閘，索引 = 閘 ID；_gates[0] = CONST 0
      ├─ _retired : vector<unique_ptr<CirGate>>          擁有「已移除但還不能刪」的閘
      │
      └─ 以下都是借用（CirGate*），不負責釋放：
           _piList, _poList, _dfsList, _fanoutInfo[i],
           每個閘的 fanin（CirGateV 裡的指標）、_eqGate
```

### 決定 1：`CirMgr` 透過 `vector<unique_ptr<CirGate>>` 擁有所有閘

- 用閘 ID 當索引，和原本的 `_totGateList` 一樣是 O(1) 存取。
- `unique_ptr` 只有一個指標大小，存取時沒有額外成本（`get()` 只是讀出指標）。
- 電路被銷毀或重讀時，`vector` 銷毀 → 每個 `unique_ptr` 銷毀 → 每個閘被釋放。不需要寫迴圈。
- `CirMgr` 不需要手寫解構子，也不需要手寫「禁止複製」：`unique_ptr` 不能複製，所以
  `CirMgr` 自動變成不能複製、只能移動。這就是 **Rule of zero**：讓成員自己管好自己，類別就什麼都不用寫。

### 決定 2：常數閘屬於各自的 `CirMgr`

原本的 `static CirGate* _const0` 改成 `_gates[0]` 擁有的一般閘，用 `const0()` 取得。
這修掉一個潛在 bug：兩個 `CirMgr` 不再共用同一個常數閘（之前 `deleteCircuit()` 刪除再重建它時，
另一個 `CirMgr` 手上的指標就懸空了）。

### 決定 3：移除閘時「轉移所有權」，而不是馬上刪

sweep、optimize、strash、fraig 移除一個閘時，別的地方可能還拿著它的指標
（例如 `_dfsList`，或正在走訪的迴圈）。所以不能當場刪：

```cpp
_retired.push_back(std::move(_gates[id]));   // 所有權從 _gates 移到 _retired；_gates[id] 變成空的
```

`std::move` 之後，`_gates[id]` 是空指標（「這個 ID 沒有閘」），閘本身還活著、指標仍然有效。
等到 `rebuildDerivedLists()` 重建完所有借用清單、沒有人再指向它時，`_retired.clear()` 才真的釋放。

**這是整個專案最重要的借用規則**：被 retire 的閘，在下一次 `freeRetired()` 之前都還能安全地讀，
之後就不行。原本用註解保證，現在型別本身就說明了：它還被 `_retired` 擁有。

### 決定 4：名字用 `std::string`

`char* _name` + `new char[]` + 解構子 `delete []` 換成 `std::string _name`。
沒有名字 = 空字串（AIGER 的符號名稱不可能是空的，parser 會報錯，所以不會混淆）。
PI／PO 閘因此不需要解構子。

**代價**（事後量的）：`std::string` 本身占 32 bytes，`char*` 只有 8 bytes，所以每個 PI／PO 閘大了 24 bytes
（`CirPiGate` 56 → 80、`CirPoGate` 64 → 88）。sim13 有約 6700 個 PI／PO，合計多約 160 KB；
程序的記憶體以 MB 計，基準量不出差別。AIG 閘沒有名字，大小不變。

### 決定 5：借用清單用 `vector<T*>`

`_piList`、`_poList`、`_fanoutInfo` 從 `new T[n]` 改成 `vector`。它們存的是指標，但**不擁有**閘：
擁有者永遠是 `_gates`。用 `vector` 是為了讓「陣列本身」的記憶體自動管理，和閘的所有權無關。

### 決定 6：`CmdParser` 擁有指令物件，所有權寫在函式簽名上

```cpp
bool regCmd(const string& cmd, unsigned nCmp, std::unique_ptr<CmdExec> e);   // 我收下它
CmdExec* getCmd(string cmd);                                                   // 借你看
```

呼叫端寫 `regCmd("CIRRead", 4, makeUnique<CirReadCmd>())`。只看簽名就知道 `regCmd` 會接手所有權，
`getCmd` 回傳的只是借用。

C++11 沒有 `std::make_unique`（C++14 才有），所以專案先在 `src/util/makeUnique.h` 放一個四行的版本，
階段 9 升級 C++17 後改用標準版並刪掉這個檔案。

**為什麼用 `makeUnique<T>()` 而不是 `std::unique_ptr<T>(new T)`**：不會出現裸的 `new`，
而且在 C++17 以前，`f(unique_ptr<A>(new A), g())` 這種寫法如果 `g()` 丟例外，`new A` 的結果可能洩漏；
`makeUnique` 沒有這個問題。

### 決定 7：dofile 的 `ifstream` 用 `unique_ptr`

巢狀 dofile 的堆疊變成 `stack<unique_ptr<ifstream>>`。關閉 = 讓 `unique_ptr` 被銷毀，
開檔失敗的路徑不再需要手動清理。

### 決定 8：全域的 `cirMgr`、`cmdMgr` 暫時改成 `unique_ptr`

`CIRRead` 換電路時寫 `cirMgr = makeUnique<CirMgr>()`：舊電路在賦值時自動釋放。
其他程式碼照樣寫 `cirMgr->...` 和 `if (!cirMgr)`，不用改。
全域變數本身是另一個問題（任何地方都能改它），留到階段 8 處理。
（階段 8 已完成：兩者都變成 `main()` 的區域物件，見 `docs/GLOBALS.md`。）

### 決定 9：模擬 log 檔維持借用

`CirMgr::_simLog` 指向 `CIRSimulate` 指令裡的區域 `ofstream`。它只在那一個指令執行期間有效，
指令結束前會設回 `nullptr`。這是合理的借用：擁有者（指令的區域變數）明確比借用者活得久。

### 決定 10：`myHashSet` / `myHashMap` 的桶改用 `vector<vector<…>>`

它們目前沒有被使用（strash 用 `std::unordered_map`），但留著手寫 `delete []` 又沒有禁止複製，
是個陷阱。改成 `vector` 之後，複製、移動、銷毀都自動正確。

## 3. 刻意不做的事

| 選項 | 為什麼不用 |
|---|---|
| `std::shared_ptr` 管理閘 | 沒有「共同擁有」：閘永遠只屬於一個電路。`shared_ptr` 每次複製都要做原子計數，fanin/fanout 互相指向還會形成循環、永遠不被釋放 |
| fanin 改成 `unique_ptr` | fanin 是**借用**：一個閘可以是很多閘的 fanin。只有 `_gates` 擁有 |
| 把閘直接放在 `vector<CirAigGate>` 裡（不用指標） | 閘有好幾種子類別（多型），而且 `vector` 擴張時會搬家、讓所有指標失效。要做的話是階段 8.5 的效能題目（物件池 / arena），需要先量測 |
| 改動 `CirGateV` 的指標標記（把反相位元藏在指標低位） | 那是型別安全的問題，屬於階段 7 |
| 改動 MiniSat | 第三方程式碼，不改 |

## 4. 怎麼確認改對了

所有權重構**不應該改變任何輸出**，所以驗證就是全部回歸測試：

- ctest（56 個黃金案例 + 200 個隨機電路）
- ASan/UBSan 版 ctest，以及 45 個測資的完整流程：抓懸空指標（use-after-free）和重複釋放
- valgrind：洩漏從「still reachable」降到 0（指令物件現在會被釋放）
- `tests/fuzz_diff.sh`：和參考程式比對
- `scripts/bench.py`：速度和記憶體不能比 `docs/benchmarks.md` 的基準線差
- `grep -rn "\bdelete\b\|\bnew\b" src`（MiniSat 除外）只剩 `= delete` 和註解

## 5. 結果

| 檢查 | 結果 |
|---|---|
| ctest（gcc、clang、ASan/UBSan） | 57/57 通過 |
| 45 個測資完整流程（ASan/UBSan，含兩輪 fraig、重讀、`cirv` 兩次） | 沒有任何報告 |
| GUI（ASan/UBSan 版，背景執行緒換三次電路再 fraig + verify） | 沒有報告，三次都等價 |
| `tests/fuzz_diff.sh 300`、`tests/fuzz_parser.sh`（80 個損壞檔） | 和參考程式完全一致 |
| valgrind（`q -f` 結束時） | 改之前 67 塊、81,960 bytes 未釋放；改之後 **0 bytes** |
| `grep new/delete`（MiniSat 除外） | 改之前 49 行；改之後只剩 `makeUnique.h` 的一個 `new` |
| 速度（`scripts/ab_bench.sh` 交替執行） | 不變，見 `docs/benchmarks.md` |

實作時編譯器幫忙抓到的兩件事（教學筆記 review 8.2.1 有細節）：

- `char*` 改成 `string` 後，`if (_name)` 變成編譯錯誤，正好列出每個要改寫成 `!_name.empty()` 的地方。
- 新增 `const0()` 後，一個同名的區域變數變成「用還沒初始化的自己初始化自己」，被 `-Wall -Werror` 擋下。
