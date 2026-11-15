# fraig 設計說明

這份文件說明 fraig 的資料結構、每個指令的演算法與複雜度，以及 GUI 的架構。
讀完它應該能回答：「每個類別負責什麼？每個指令怎麼做？為什麼是對的？要花多少時間？」

程式碼裡的註解是英文（沿用原專案），這份文件用中文解釋設計的理由。

## 1. 整體架構

```
            ┌──────────────┐        ┌──────────────────────┐
  鍵盤/dofile│  CLI 迴圈     │        │  瀏覽器 (guiPage.h)   │
            │  main.cpp     │        │  畫圖、按鈕、log      │
            └──────┬───────┘        └──────────┬───────────┘
                   │ 指令字串                    │ HTTP (localhost)
                   │                  ┌─────────┴──────────┐
                   │                  │ guiServer.cpp      │
                   │                  └─────────┬──────────┘
                   ▼                            ▼ 指令字串
            ┌─────────────────────────────────────────────┐
            │ cmd：CmdParser（指令註冊表、解析、執行）        │
            └──────────────────────┬──────────────────────┘
                                   ▼  CmdExec::exec()
            ┌─────────────────────────────────────────────┐
            │ cir：cirCmd.cpp（指令）→ CirMgr（電路與流程規則） │
            │      cirOpt / cirSim / cirFraig / cirJson    │
            └───────────┬─────────────────────┬───────────┘
                        ▼                     ▼
                  util（雜湊、工具）       sat（MiniSat）
```

**關鍵決定：GUI 不直接呼叫電路程式碼。** 每個按鈕都送出一個指令字串（例如 `CIRSTRash`），
走的是和鍵盤輸入完全相同的 `CmdParser`。電路資料則透過一個新指令 `CIRJson` 以 JSON 輸出。
好處是 CLI 和 GUI 不可能行為不一致，GUI 也不需要知道 `CirMgr` 的內部結構；
依賴方向維持「前端 → cmd → cir」，`cmd` 仍然不認識 `cir`。

### 1.1 類別與它們的關係（階段 9 之後）

```
main()  持有 ──┬── CirSession ─────── 擁有 0 或 1 個 ──► CirMgr
               │                                        │ 擁有（unique_ptr）
               │                                        ▼
               │                                   CirGate（抽象）◄── 借用 ── CirGateV（邊）
               │                                     ▲  子類別：CirPiGate、CirPoGate、
               │                                     │         CirAigGate、CirConstGate、CirUndefGate
               │
               └── CmdParser ──── 擁有所有指令 ──► CmdExec（抽象）
                                                     ├── CirCommand（持有 CirSession&）
                                                     │     └── CirReadCmd、CirPrintCmd、CirGateCmd、CirSweepCmd、
                                                     │         CirOptCmd、CirStrashCmd、CirSimCmd、CirFraigCmd、
                                                     │         CirWriteCmd、CirJsonCmd、CirVerifyCmd
                                                     ├── ParserCommand（持有 CmdParser&）
                                                     │     └── HelpCmd、HistoryCmd、DofileCmd
                                                     ├── QuitCmd
                                                     └── UsageCmd（擁有 MyUsage）
runGuiServer(port, parser) ── 建立 GuiServer（持有 CmdParser&，擁有 Job）
CirVerifyCmd ── 呼叫 verifyAag() ── 內部用自己的 AagFile、Graph、Simulator、SatSweeper（不碰 CirGate）
```

| 類別 | 檔案 | 責任 | 擁有 / 借用 |
|---|---|---|---|
| `CirSession` | `cirSession.h` | 「目前的電路」，可能沒有 | 擁有 `unique_ptr<CirMgr>`；`main` 擁有它 |
| `CirMgr` | `cirMgr.h`、`cirMgr.cpp`、`cirOpt.cpp`、`cirSim.cpp`、`cirFraig.cpp`、`cirJson.cpp`、`cirCheck.cpp` | 一個電路：讀檔、所有改電路的操作、模擬、FEC、fraig、報告、流程規則（`whyNot`）、一致性檢查 | 擁有所有閘（`_gates`、`_retired`）；`_piList`、`_dfsList`、`_fanoutInfo` 是借用 |
| `CirMgr::AagLexer` | `cirMgr.cpp` | 讀一個 AIGER 檔時的 parser 狀態 | `readCircuit()` 的區域變數 |
| `CirGate` 與 5 個子類別 | `cirGate.h`、`cirGate.cpp` | 一個閘：型別標籤、fanin、模擬值、FEC 編號、SAT 變數、印出 | 被 `CirMgr` 擁有 |
| `CirGateV` | `cirGate.h` | 一條邊：閘指標 + 反相位元（8 bytes） | 借用閘 |
| `CirPValue` | `cirGate.h` | 一個閘 64 個樣本的模擬值 | 值 |
| `CmdParser` | `cmdParser.h` | 讀指令（鍵盤或 dofile）、歷史、補全、找指令並執行 | 擁有所有 `CmdExec`、開著的 dofile；`main` 擁有它 |
| `CmdExec` | `cmdParser.h` | 所有指令的抽象介面：`exec`、`usage`、`help` | — |
| `CirCommand`、`ParserCommand` | `cirCmd.h`、`cmdCommon.h` | 兩種指令基底：持有 session 或 parser 的參考 | 借用 |
| 16 個具體指令 | `cirCmd.cpp`、`cmdCommon.cpp` | 解析選項、印錯誤訊息、呼叫 `CirMgr` | 被 `CmdParser` 擁有 |
| `GuiServer`、`Job`、`JobOutput` | `guiServer.cpp` | HTTP 伺服器、背景工作的狀態、把輸出接到工作紀錄的 `streambuf` | `GuiServer` 擁有 `Job`、借用 `CmdParser` |
| `AagFile`、`Graph`、`Simulator`、`SatSweeper`、`Random64` | `cirVerify.cpp`（只在這個檔案裡看得到） | `CIRVerify` 自己的 AIGER 解析、結構雜湊圖、模擬、SAT sweeping | 都是 `verifyAag()` 的區域物件 |
| `VerifyResult`、`VerifyMismatch` | `cirVerify.h` | 驗證結果與反例 | 值 |
| `MyUsage` | `myUsage.h` | 量時間與記憶體 | 被 `UsageCmd` 擁有 |
| `HashMap`、`HashSet`、`Cache`、`RandomNumGen` | `src/util/` | 課程留下的工具類別；**目前沒有被使用**（strash 用 `std::unordered_map`、模擬用 `std::mt19937_64`） | — |
| `SatSolver`（MiniSat） | `src/sat/` | SAT 求解器，第三方程式碼，不改 | fraig、verify 各自建立區域物件 |

## 2. 資料結構

### 2.1 閘（`cirGate.h`）

| 類別 | 用途 | 特有資料 |
|---|---|---|
| `CirGate`（抽象） | 所有閘的共同部分 | ID、行號、型別標籤 `_type`、模擬值 `_pValue`、FEC 編號 `_fecId`、SAT 變數 `_satVar`、走訪標記 `_ref` |
| `CirPiGate` | 主要輸入 | 名字 |
| `CirPoGate` | 主要輸出 | 一個 fanin `_in0`、名字 |
| `CirAigGate` | AND 閘 | 兩個 fanin `_in0`、`_in1` |
| `CirConstGate` | 常數 0（ID 0） | — |
| `CirUndefGate` | 被引用但沒定義的閘 | — |

**型別**（階段 7，`docs/TYPES.md`）：`GateType` 是 `enum class`，建構時存進 `_type`，`isPi()` 等是一般比較。
處理任何閘的程式碼用 `numFanins()` / `fanin(i)`；只處理 AIG 的程式碼用 `asAig()` 拿到 `CirAigGate*`，再用 `in0()` / `in1()`。

**fanin 用 `CirGateV` 存**：「指向閘的邊，可能反相」。內部是一個 `uintptr_t`，最低 1 bit 是反相旗標，其餘是閘的指標
（`static_assert(alignof(CirGate) >= 2)` 保證那個 bit 可用）。只能從閘指標明確建構，沒有從整數的轉換；
反相是值運算（`~e`、`e ^ inv`）。「浮接」不另外存，`isFloat()` 就是「來源是 UNDEF 閘」。

**讀檔**：parser 把 fanin 的 literal 暫存在 `_pending`，所有閘建好後 `connectFanins()` 依閘 ID 順序一次接上邊與 fanout，
不經過全域 `cirMgr`（階段 8 已經沒有全域的 `cirMgr`，見 `docs/GLOBALS.md`）。

**流程規則**：`CirMgr::whyNot(CirStep)` 依「這個電路上一步做了什麼」決定能不能 optimize／strash／fraig，
指令只負責印出它回傳的錯誤訊息。

**一致性檢查**：`CirMgr::checkInvariants()`（`cirCheck.cpp`）檢查 fanin／fanout 一致、DFS 順序、計數等，單元測試在每個改電路的指令後呼叫。

**fanout 不存在閘裡**，而是 `CirMgr::_fanoutInfo[gateId]`（`vector<CirGate*>`）。
同一個閘若兩個 fanin 都接到 X，X 的 fanout 清單裡會出現它兩次；這個「一條邊一筆」的規則讓搬移 fanout 時不需要特例。

### 2.2 電路管理者（`CirMgr`）

| 成員 | 意義 | 何時更新 |
|---|---|---|
| `_gates[id]` | **擁有**每個閘（`unique_ptr`）；ID → 閘；`[0]` 是 CONST 0；被移除的閘變成空的 | 讀檔、移除閘 |
| `_piList` / `_poList` | PI、PO 依檔案順序（借用的指標） | 讀檔 |
| `_fanoutInfo[id]` | 每個閘的 fanout | 讀檔、合併閘 |
| `_dfsList` | 從 PO 出發的 DFS 後序（fanin 先於 fanout），不含 UNDEF | `rebuildDerivedLists()` |
| `_undefList` / `_floatList` / `_unusedList` | 由 netlist 推導出來的清單 | `rebuildDerivedLists()` |
| `_fecGrps` | FEC 群組（見 2.3） | 模擬、fraig |
| `_retired` | **擁有**已從 netlist 拿掉、但記憶體還沒釋放的閘 | 見第 3 節 |

所有權（階段 6，詳見 `docs/OWNERSHIP.md`）：只有 `_gates` 和 `_retired` 擁有閘，其他所有 `CirGate*`
（`_piList`、`_dfsList`、`_fanoutInfo`、fanin）都是借用。`CirMgr` 沒有手寫解構子，也不能被複製：
成員 `unique_ptr` 不能複製，所以編譯器不會產生複製建構子（Rule of 0）。

### 2.3 FEC 群組

每組是一個 `IdList`，存 litId（`閘ID*2 + 相位`），依閘 ID 由小到大排；相位是相對於組內第一個成員，
所以第一個成員一定是正相。每個閘的 `_fecId = 組索引*2 + 相位`，讓 `cirg` 能在 O(1) 找到自己的組。
`vector<IdList>` 直接存值（不是 `vector<IdList*>`），不需要手動 `delete`。

## 3. 改電路的三個基本操作（`cirOpt.cpp`）

sweep、optimize、strash、fraig 都會改電路，它們共用三個操作，簿記只寫一次：

| 操作 | 做什麼 | 成本 |
|---|---|---|
| `mergeGate(old, driver)` | 把 old 的每個 fanout 改接到 driver（帶相位），然後 retire old | O(fanout 數 + fanin 的 fanout 數) |
| `retireGate(g)` | 把 g 從 fanin 的 fanout 清單拿掉，AIG 計數減一，所有權從 `_gates[id]` 移到 `_retired`（`std::move`）；**記憶體暫不釋放** | 同上 |
| `rebuildDerivedLists()` | 刪掉沒人引用的 UNDEF，重建 DFS、浮接、未使用清單，最後 `_retired.clear()` 釋放 retired 的閘 | O(閘數) |

**為什麼要「先 retire、最後才釋放」？** 指令通常是一邊走 `_dfsList` 的副本一邊合併閘；
如果合併時立刻 `delete`，副本裡還留著那個指標，下一步就可能讀到已釋放的記憶體。
延後釋放讓「走訪中的指標一定有效」成為不需要逐處檢查的保證。
（fraig 還需要在走訪中途做模擬，會讀到已 retire 的閘，這個保證更重要。）

**為什麼推導清單用「整個重算」而不是逐步更新？** 每次重算是 O(n)，而指令本身就是 O(n)，
不會改變整體複雜度；逐步維護則容易漏掉某個情況（例如某閘失去最後一個 fanout 而變成「未使用」）。
選擇簡單、不會錯的做法。

**移除 fanout 時保留順序**：`cirg -fo` 會依 fanout 清單的順序印出，為了和參考程式一模一樣，
用「找到後 `erase`」而不是「和最後一個交換再刪」。成本是 O(該閘的 fanout 數)。

## 4. 指令與演算法

`n` = 閘數，`e` = 邊數（約 2n）。

### 4.1 CIRSWeep：移除走不到 PO 的閘 — O(n)

1. 標記 DFS 清單裡的閘，以及它們的 fanin（這樣才能標到 UNDEF，UNDEF 不在 DFS 清單裡）。
2. 依 ID 由小到大，沒被標記的 AIG／UNDEF 一律 retire。PI、PO、CONST 永遠保留。
3. `rebuildDerivedLists()`。DFS 清單不會變。

### 4.2 CIROPTimize：常數傳播與簡單化簡 — O(n + e)

依 DFS 順序走每個 AIG，`g = AND(a, b)`：

| 條件 | 結果 |
|---|---|
| a 是常數 1 | g = b |
| a 是常數 0 | g = 0 |
| a 和 b 是同一個訊號 | g = a |
| a 和 b 互為反相 | g = 0 |

（a、b 對調亦同。）**為什麼走一遍就夠？** DFS 順序保證走到 g 時，它的 fanin 都已經處理完、不會再變，
所以 g 看到的就是最終的 fanin。若規則成立就 `mergeGate(g, 結果)`。

### 4.3 CIRSTRash：結構雜湊 — 平均 O(n)

依 DFS 順序走每個 AIG，以「兩個 fanin 邊（指標 + 相位）排序後的組合」為鍵查雜湊表：
- 查不到：放進去。
- 查得到：已經有一個結構完全相同的閘，`mergeGate(g, 那個閘)`。

排序讓 `AND(a,b)` 和 `AND(b,a)` 得到同一個鍵。同樣因為 DFS 順序，g 的 fanin 已經是合併後的樣子，
所以「合併後才變得相同」的閘也會在同一遍被找到。雜湊表用 `std::unordered_map`，查詢平均 O(1)。

### 4.4 CIRSIMulate：64 位元平行模擬 — 每 64 個樣本 O(n)

**模擬**：每個閘的值是一個 64 位元字，第 k 位是第 k 個樣本下的值。依 DFS 順序算一遍
（AND 就是位元 `&`，反相就是 `~`），一次就模擬了 64 個樣本。UNDEF 依規格當作 0。

**FEC 群組的切分**（`splitByValue`）：用雜湊表把「值」對應到「子群組」，每個成員 O(1)。
（階段 8.5：先檢查群組裡所有成員的值是否都一樣，一樣就原封不動保留、不建雜湊表；16 個成員以下用線性搜尋。
sim13 的隨機模擬因此快了約 44%，輸出不變。）
- **第一個字**：所有 DFS 裡的 AIG 加上 CONST 0 一起分組；值 v 和 ~v 視為同一組（反相等價），
  鍵是「v 和 ~v 之中 bit 0 為 0 的那個」。
- **之後的字**：組內成員在之前所有樣本上已經「依各自記錄的相位」相等，
  所以新字也必須依**同一個相位**相等，鍵是「依相位修正後的值」。

  > 這一點我一開始寫錯過：每次都用「v 或 ~v」重新分組，會讓「之前相等、這次剛好全部相反」的一對
  > 留在同一組（相位被悄悄翻轉）。一般的隨機樣本幾乎不會觸發，但 fraig 的反例（可能 64 個都相同）
  > 會觸發，造成 fraig 無窮迴圈。修正後才正確。

**隨機模擬何時停**：每做 64 個樣本，看「組數或組內成員總數」有沒有變；連續 k 次沒變就停，
`k = clamp(AIG 數 / 100, 4, 32)`。電路越大越有耐心。參考程式用的條件不同，所以樣本數不會一樣。

**檔案模擬**：每讀滿 64 個樣本模擬一次；最後不滿 64 個的部分補全 0 樣本。遇到格式錯誤就停，
目前未滿 64 個的那批丟掉（和參考程式相同），之前模擬過的結果保留。

### 4.5 CIRFraig：用 SAT 證明並合併 — 分輪進行

**SAT 模型**（延遲建立，階段 8.5）：一開始只有 CONST 0 一個變數並固定為假。證明需要某個閘時，
`satVarOf()` 才把它的 fanin 錐加進求解器：PI 一個變數、UNDEF 一個**自由**變數、AIG 一個變數加上 AND 的三個子句。
原本一開始就把整個電路建好，MiniSat 每次回答 SAT 都要替所有變數指定值；改成延遲建立後 fraig 快了約 2 倍。
反例裡不在已建立錐內的輸入用隨機值，讓這個反例也能幫忙切分別的群組。

> UNDEF 用自由變數而不是 0：合併必須「不論那個沒定義的訊號是什麼值都成立」才安全。
> 模擬仍依規格把它當 0，所以反例會額外記下 UNDEF 在模型裡的值，模擬反例時套用，確保反例真的能把兩個閘分開。

**一輪**：
1. 依 DFS 順序走。每組第一個被走到的成員是「代表」（組裡有 CONST 0 就是 CONST 0）。
   之後的成員只和代表比。如果兩者 fanin 邊完全相同（順序可以相反），必然等價，直接合併、不問 SAT
   （`sameFanins`，階段 8.5；前面合併過後很常見）。否則建 `g XOR (代表 ^ 相位)`，問 SAT 能不能為 1。
   - 不能（UNSAT）：兩者等價，`mergeGate(g, 代表)`。代表在 DFS 裡比較早，不可能是 g 的 fanout，所以不會產生迴圈。
   - 能（SAT）：模型就是一組能分辨兩者的輸入，存成反例。每存滿 64 個就模擬一次、切分群組，
     再依「走過的順序」重新挑代表。這樣後面的閘就不必為已經被反例分開的對象呼叫 SAT。
2. 模擬剩下不滿 64 個的反例（"Updating by SAT"）。
3. 重建 DFS，把已合併的、和走不到 PO 的閘移出群組（"Updating by UNSAT"）。

**證明成功後把結論告訴 SAT solver**：證明 g ≡ r 之後，把這個等價關係永久加進 solver
（對 CONST 0 就是直接把 g 固定成常數）。它本來就被電路蘊含，不會改變任何答案，
但後面的證明不必再從頭推一次。實測 sim07 從 8.0 秒降到 3.4 秒（profile 顯示 97% 時間在 MiniSat 的 propagate）。
起初我猜是每次證明新增的 XOR 子句拖慢了 solver，改掉之後時間完全沒變，猜錯了；
真正的原因要靠 profile 才找得到。這是「先量測再優化」的實例。

**為什麼一定會結束**：每輪中，每組至少第二個成員會被證明，結果不是合併就是被反例分開，群組只會變少變小。

**最後**：群組清空後跑一次 strash，因為合併可能讓原本不同的閘變成結構相同。

**成本**：每輪每個 FEC 成員一次 SAT 呼叫（SAT 本身是 NP-complete，最壞是指數時間），
其他工作都是 O(n)。真正決定速度的是 SAT 呼叫次數，所以盡早用反例把群組切細很重要。

### 4.6 CIRVerify：獨立的等價檢查（`cirVerify.cpp`）

**問題**：fraig 的結果不唯一，不能逐字和參考程式比對；fraig 若有 bug，電路會「看起來正常但算錯」。
所以要直接回答：「目前的電路和讀進來的原檔，是否對**所有**輸入都有相同輸出？」

**刻意獨立**：檢查器自己解析 AIGER 文字（`cirr` 時存下原檔快照、目前電路用 `writeAag` 寫成文字），
建在自己的小型圖結構上，**完全不用 `CirGate`／`CirMgr`**。網表程式碼的 bug 無法同時弄壞檢查器。
輸入依位置配對；未定義的訊號是自由變數，依變數編號在兩個電路間共用（和 fraig 的 SAT 語意一致）。

**做法**（兩個電路建進同一張圖）：
1. **結構雜湊**：相同的 AND 只建一次。輸出落在同一個節點 → 等價，不用 SAT。
2. **隨機模擬**（16 × 64 個樣本）：兩個輸出值不同 → 立刻得到反例。
3. **SAT sweeping**：依拓撲順序，對每個 AND 節點：
   - 先用「已證明的代表節點」取代它的 fanin，再查一次結構雜湊；命中就直接合併（大多數節點走這裡）。
   - 否則找模擬簽章相同（可差一個反相）的較早節點，用 SAT 證明；證明成功就把等式加進求解器，
     之後的 SAT 問題都只需處理「已證明點」之間的小區域。
   - 證明失敗會得到反例；錐內的輸入用 SAT 給的值、錐外用隨機值，每 64 個反例模擬一次，把候選群組切細。
   - CNF 是**延遲建立**的：只有被比較的節點的 fanin 錐才加進求解器。
4. 剩下的輸出對：代表節點相同 → 等價；否則再做一次 SAT。不等價時回報**反例**（每個 PI 的值），
   而且每個反例都會**重新模擬**確認兩邊輸出真的不同才回報。

**為什麼要 sweeping**：sim12 只有一個輸出，整個輸出錐一次丟給 SAT 要 35 秒；sweeping 之後 0.6 秒。
sim13：直接逐輸出 SAT 超過 10 分鐘；sweeping 340 秒；再加「代表節點重新雜湊」、延遲 CNF、反例錐外補隨機值，
降到約 90 秒（fraig 流程本身約 70 秒）。

**它也要被檢查**：`tests/verifier_selftest.sh` 把電路隨機改壞一點（`tests/mutate.py`：翻轉一個反相、
把一個 fanin 換成 PI 或常數），問 CIRVerify 和參考程式（CIRMiter + fraig）同一個問題，兩邊要一致；
有反例時再用參考程式的模擬器重播，確認輸出真的不同。目前 12 個電路 × 15 個突變 = 180 次，全部一致
（141 個不等價、39 個仍等價，例如改在沒用到的閘上）。

**限制**：這版 MiniSat 沒有衝突上限，極難的 SAT 問題無法中途放棄；GUI 裡也還不能取消。

## 5. 和參考程式的差異（刻意的）

| 項目 | 差異 | 原因 |
|---|---|---|
| 隨機模擬的樣本數 | 不同 | 停止條件是各自設計的，規格允許 |
| fraig 合併的閘數 | 有些電路我們合併得**更多** | 參考程式似乎有限制 SAT 的努力程度（例如 sim06 它要做兩次 fraig 才到 531 個 AIG，我們一次就到）。最終電路經 sweep/optimize 後一樣大或更小 |
| fraig 後未使用閘的數量 | 可能不同 | 同上；未使用的閘不影響功能，`cirsw` 後即消失 |
| 除錯訊息（`Sweeping:` 等） | 文字和順序可能不同 | 規格說這些是選擇性的，評分時會過濾 |

其餘輸出（`cirp` 全部選項、`cirg` 含 fanin/fanout、`cirw`、檔案模擬的 FEC 群組與 log、錯誤訊息）
都和參考程式逐字一致（見第 7 節）。

## 6. GUI

### 6.1 啟動

```bash
cmake --build build
build/fraig -Gui        # 或 build/fraig -Gui 9000 指定 port
```

然後在 Windows 的瀏覽器開 `http://localhost:8080`（WSL2 會自動把 localhost 轉到 WSL 裡）。
伺服器只綁 `127.0.0.1`，同網路的其他電腦連不到。`Ctrl-C` 結束。

### 6.2 伺服器（`src/main/guiServer.cpp`）

| 端點 | 作用 |
|---|---|
| `GET /` | 網頁（`guiPage.h`，內嵌在執行檔裡，所以在任何目錄都能啟動） |
| `GET /api/state` | 執行 `CIRJson`，回傳電路與流程階段（有指令在跑時回 409） |
| `POST /api/cmd` | body 是一行指令；交給背景工作執行緒，立刻回 202（已有指令在跑時回 409） |
| `GET /api/job` | 目前指令的進度文字；跑完後（只回一次）再附上 status、完整輸出與新狀態 |
| `GET /api/files` | 目前目錄下（兩層內）的 `.aag` 檔 |

**背景工作**：fraig、verify 在大電路上要跑一兩分鐘，所以 HTTP 迴圈從不自己執行指令，
而是交給一個工作執行緒，網頁每 20～200 ms 輪詢 `/api/job`。一次只跑一個指令（都在改同一個電路）。
指令執行期間，HTTP 執行緒只讀 `Job` 紀錄（用 mutex 保護），不碰電路、也不寫 `cout`。

**進度不需要特別的 API**：指令本來就用 `\r` 覆寫的方式印進度（例如 `Sweeping nodes 2048/133151`）。
工作執行緒把 `cout`/`cerr` 導到一個加鎖的 `streambuf`，伺服器取最後一行 `\r` 之後的文字當進度；
網頁看到「數字/數字」就畫成進度條，否則顯示來回移動的忙碌條和經過秒數。

`q` 指令會在終端機問 Yes/No，GUI 無法回答，所以 GUI 擋掉它。目前還不能取消執行中的指令。

### 6.3 網頁（`src/main/guiPage.h`）

- **版面**：分層畫法。欄 = 邏輯深度（PI／CONST／UNDEF 在第 0 欄，AND 閘 = 最深的 fanin + 1，PO 在最後一欄）；
  欄內依 fanin 平均位置排序（barycenter），讓多數邊比較短。O(V + E) 加上排序。
- **看到變化**：每次指令前後比對 netlist。被移除的閘在原位以紅色淡出，fanin 被改接的閘以橘色圈標示，
  其餘的閘從舊位置平滑移到新位置，畫面也同時縮放到新電路。右側統計顯示增減。
- **模擬後**：同一 FEC 群組的閘同色，可以直接看到「哪些閘可能等價」，按 Fraig 後看它們被合併。
- **流程規則**：按鈕依規格的流程規則啟用或停用（例如模擬後不能 Optimize），滑鼠停在停用的按鈕上會說明原因。
- **Verify**：任何時候都能按，執行 `CIRVerify`（和讀進來的原檔比）。結果在記錄區以綠色（EQUIVALENT）
  或紅色（NOT EQUIVALENT，附反例）顯示；執行中所有按鈕停用，標題列顯示進度條。
- **大電路**：用 canvas 而不是 SVG；閘多時改畫直線、縮小時不畫標籤。sim13（約 8.5 萬個閘）可以開，但拖曳會比較慢。

## 7. 驗證

| 工具 | 檢查什麼 |
|---|---|
| `ctest`（`tests/`） | 56 個黃金輸出測試，不需要參考程式；`fraig-*` 案例以 `cirv` 結尾，測試本身就證明了等價 |
| `CIRVerify` | 我們自己的獨立等價檢查（4.6 節），不需要參考程式 |
| `tests/verifier_selftest.sh` | 檢查檢查器：突變測試，和參考程式比對結論與反例 |
| `scripts/compare.sh` | 和參考執行檔比對輸出（濾掉規格允許不同的除錯訊息與 `\r` 進度文字） |
| `scripts/check_equiv.sh` | 用參考程式的隱藏指令 `CIRMiter` 證明「我們最佳化後的電路」和原電路功能等價 |
| ASan／UBSan 版 | 記憶體錯誤、未定義行為、洩漏 |

目前結果（46 個測資：`tests.fraig/*.aag` 與 `ISCAS85/`）：
- `cirp`／`cirp -n`／`cirp -fl`／`-pi`／`-po` 在 sweep、optimize、strash 前後：全部一致。
- 每個閘的 `cirg`、`cirg -fo 2`、`cirg -fi 2`，在四種指令組合之後：全部一致（大電路抽樣約 400 個閘）。
- 14 個樣本檔的檔案模擬：FEC 群組、每個閘的值、log 檔全部一致。
- 錯誤處理（重複 strash、模擬後 optimize、未模擬就 fraig 等）：一致。
- 功能等價（`check_equiv.sh`）：除了 sim13 以外的 45 個測資，在「sweep／optimize／strash」、「隨機模擬 + fraig」、
  「optimize → strash → 模擬 → fraig → sweep → optimize → 模擬 → fraig」三種流程後，全部證明等價。
  sim13（約 8.5 萬閘）的 miter 很大，參考程式在 5 分鐘內證不完（UNKNOWN，代表逾時、不是失敗）；
  把時限放寬到 40 分鐘後，「隨機模擬 + fraig」的結果證明等價（參考程式跑了十多分鐘）。
- Sanitizer：45 個測資跑完整流程（含 fraig、重讀電路、`CIRJson`），沒有任何報告。
- 隨機電路：`tests/fuzz_diff.sh` 1000 個小電路 + 200 個較大電路（最多 400 個 AND），7 種指令流程的輸出
  和參考程式逐字一致，3 種 fraig 流程經 `cirv` 全部等價。`tests/fuzz_parser.sh` 把 10 個檔案各弄壞 60 次，
  600 個檔案在 ASan/UBSan 版下錯誤訊息都和參考程式一致，沒有崩潰。
- `CIRVerify`：全部 46 個測資在「strash → 隨機模擬 → fraig → sweep → optimize」後都判定 EQUIVALENT
  （和 `check_equiv.sh` 的結論一致）；sim13 約 90 秒。

## 8. 效能（實測）

**階段 8.5 之後的最新數字與每項優化的 A/B 證明在 `docs/benchmarks.md`。**

最新、可重現的基準線在 `docs/benchmarks.md`（`scripts/bench.py` 產生，含記憶體）；下表是較早的量測。

`cirsim -r; cirfraig; cirsw; ciropt` 的總時間，雲端環境 2 CPU、`-O3`，各跑一次（只看量級）：

| 電路 | 參考程式 | 我們 | 之後的 AIG 數（參考／我們） |
|---|---|---|---|
| sim06 | 0.27 s | 0.18 s | 531 / 531 |
| sim07 | 6.66 s | 3.30 s | 1175 / 1175 |
| sim09 | 0.17 s | 0.11 s | 1001 / 1001 |
| sim12 | 2.34 s | 1.25 s | 0 / 0 |
| sim13（約 8.5 萬閘） | 55.8 s | 48.4 s | 77313 / 77261 |
| C5315 | 0.18 s | 0.10 s | 1001 / 1001 |
| C7552 | 0.35 s | 0.23 s | 497 / 497 |

只算 `cirsim -r`（sim13）：參考 8.9 s／31 萬個樣本，我們 3.2 s／13.8 萬個樣本，最後 FEC 組數相近（3199／3215）。
sim13 剩下的時間主要是少數困難的 SAT 問題（MiniSat 的 `search`），模擬與 FEC 切分合計不到 3%。

## 9. 複雜度總表

| 指令 | 時間 | 主要結構 |
|---|---|---|
| CIRRead | O(n + e) | 陣列（ID 直接索引） |
| CIRSWeep | O(n + e) | 標記 + 依 ID 掃描 |
| CIROPTimize | O(n + e) | 一遍 DFS |
| CIRSTRash | 平均 O(n + e) | `unordered_map` |
| CIRSIMulate | 每 64 個樣本 O(n) | 64 位元字 + 雜湊切分 |
| CIRFraig | O(n) × 輪數 + SAT 呼叫 | MiniSat，反例批次模擬 |

「每個閘的每一步都是 O(1)（平均）」是可以做到的；「整個指令 O(1)」不可能，因為每個閘至少要看一次。
fraig 的 SAT 部分沒有多項式時間的保證，只能靠減少呼叫次數。
