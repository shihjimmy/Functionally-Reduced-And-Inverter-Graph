# fraig 現代化路線圖

這個檔案是專案的「記憶」：換一個對話或換一台機器，只要先讀它，就知道做到哪、下一步是什麼。

## 目標

1. 把課程作業做完整（sweep / optimize / strash / sim / fraig）。**已完成**，另加瀏覽器 GUI。
2. 在不改變行為的前提下，一步一步把它改成現代 C++ 工程專案，放上 GitHub。
3. 過程中讓專案作者（使用者）真正看懂系統設計，而不只是拿到程式碼。

## 工作原則

- **每階段一個 commit**，訊息寫清楚「做了什麼、為什麼」。
- **重構之前先有安全網**：`scripts/compare.sh` 跟參考執行檔比對輸出，之後再加單元測試。重構的定義是「輸出完全不變」。
- **每一步先講設計與取捨，使用者同意再動手**；做完要附驗證結果，並問一兩個理解題。
- 使用者不手寫全部程式，但要能解釋「為什麼這樣設計」，並能判斷對錯。
- **效能宣稱要有數字**：說「變快了」就要附同一組測資、多次取中位數的前後數據，而且輸出仍與參考一致。先量測、再優化。
- **知識放在 repo，不放在對話裡**：`CLAUDE.md`（給 AI 協作者的指引：建置、驗證、禁止事項）、本檔（進度）、`review/`（學習筆記）。換對話時先讀這三個。

## 階段

| 階段 | 內容 | 狀態 |
|---|---|---|
| 0 | 工程基礎：`.gitignore`、`scripts/compare.sh`、把 hw6 的 cir / cmd 合併進 `src/` | 完成 |
| 1 | 讀懂現有系統：class / virtual、cmd 註冊機制、用 gdb 追 `cirr`、`genConnections`、`_fanoutInfo` | 進行中 |
| 2 | 建置系統：用 CMake 取代自製 Makefile 與 `include/` 符號連結；新增 GitHub Actions 自動編譯與測試（`tests/` 48 個黃金輸出測試，不需要 ref/） | 完成（舊 Makefile 暫時保留，待使用者同意後移除） |
| 3 | 編譯器護欄：`-Wall -Wextra`、AddressSanitizer / UBSan、valgrind；修掉警告 | 完成（自己的程式碼零警告；`FRAIG_WERROR`、`FRAIG_SANITIZE` 選項；CI 有 sanitizer job） |
| 4 | 完成功能（舊架構上，每項都用 `compare.sh` 驗證）：`sweep` → `optimize` → `strash` → `sim` → `fraig`；加上瀏覽器 GUI（`fraig -Gui`） | 完成（見 DESIGN.md） |
| 4.5 | 安全網：`CIRVerify` 獨立等價檢查（不需要 ref/，含反例）與它的突變自我測試；GUI 背景執行、進度條、Verify 按鈕；隨機 AIG 產生器 + 差分測試（`tests/fuzz_diff.sh`，1200 個隨機電路與參考程式一致）；parser 模糊測試（`tests/fuzz_parser.sh`，600 個損壞檔案的錯誤訊息逐字一致）；sanitizer 跑全部測資；效能基準線 `docs/benchmarks.md` | 完成 |
| 5 | 單元測試：用 GoogleTest 為 `CirMgr` 寫小電路測試，補足黃金輸出看不到的內部狀態 | 完成（併入階段 7）：GoogleTest 1.12.1、`CirMgr::checkInvariants()`、29 個測試 |
| 6 | 記憶體與所有權：`new / delete` 改成 `std::unique_ptr` 或明確的所有權；釐清誰負責刪除閘 | 完成：設計在 `docs/OWNERSHIP.md`，教學在 review 8.2.1；除了 `makeUnique.h` 沒有 `new`/`delete`；valgrind 結束時 0 bytes 未釋放；速度 A/B 不變 |
| 7 | 型別安全：拿掉 `CirGate` 的胖介面（PI 也有 `getIn0()`）；`enum class`；把 `CirGateV` 的指標標記包成小型型別 | 完成：設計在 `docs/TYPES.md`，教學在 review 3.10；另外拿掉讀檔時對全域 `cirMgr` 的依賴；輸出、速度、物件大小都不變 |
| 8 | 去全域：`cirMgr`、`cmdMgr`、`curCmd` 改由物件持有；流程規則（sim 之後才能 fraig）移進領域層 | 完成：設計在 `docs/GLOBALS.md`，教學在 review 4.8；另外移除 parser 的 6 個檔案層級 `static`、`myUsage`、`rnGen`、GUI 的全域 `job`；搬移時抓到並修正「fraig 內部的 strash 被算成一次 CIRSTRash」 |
| 8.5 | 量測驅動的效能優化：用 profiler 找熱點，一次只改一處，每項都用 A/B 證明 | 完成（在階段 9 之後做）：fraig 延遲建 CNF、fanin 相同不問 SAT、FEC 切分快速路徑；sim07 fraig 流程 3.5 → 1.5 秒、sim13 52.6 → 21.8 秒、sim13 隨機模擬 3.6 → 1.9 秒；兩項沒有效果的嘗試已退回。見 `docs/benchmarks.md` |
| 9 | 現代 C++：升級到 C++17，移除標頭檔中的 `using namespace std`，`CmdClass` 巨集改成 template 或 lambda | 完成（在 8.5 之前做）：C++17、GoogleTest 1.15.2、`std::make_unique`、標頭不再 `using namespace std`、三個指令巨集改成一般類別、`override`、`using` 別名、`optional`/`string_view`/結構化綁定/`[[nodiscard]]`/`std::filesystem` 等；教學在 review 7.7 |
| 10 | 文件與發佈：README、設計說明、效能數據、授權與課程素材問題 | 待做 |

階段 2、3 成本低又安全，所以排在功能之前；階段 6 以後的結構性重構要等功能完成、並有測試保護後才做。
階段 4.5 必須在任何結構性重構（6–8）之前完成：它是之後所有重構的安全網，也是效能的基準線，沒有它就無法知道重構有沒有讓程式變慢。
階段 8.5 刻意排在去全域（8）之後：資料結構層級的優化（例如把閘放進連續陣列）會改動大量程式碼，要在所有權與型別都整理乾淨之後再做。

## 發佈前要確認

- 程式檔頭標示 `Copyleft(c) ... LaDs(III), GIEE, NTU`，公開前要確認授權是否允許。
- `ref/` 的參考執行檔與預編譯的 `lib/libcmd-*.a`、課程 PDF、`tests.fraig/pattern.13`（約 76 MB，接近 GitHub 單檔 100 MB 上限）是否適合放進公開 repo。
- 公開 repo 若不放參考執行檔，測試要改成「預先產生的標準輸出檔」（golden files）。

## 學習筆記的寫法（使用者指定）

階段 2 以後的現代化（CMake、`unique_ptr` 所有權、型別安全、去全域、C++17）**全部做完之後**，
再把 `review/` 重寫成「以專案最終版程式碼當範例」的語法與現代 C++ 講解：

- 範例一律取自專案的真實程式碼，不另編玩具範例。
- 每個現代化步驟單獨一個 commit，講解時用 `git diff` 對照 C++11 舊寫法與新寫法。
- 重點主題：`unique_ptr` / 所有權（階段 6）、`enum class` 與小型別（階段 7）、
  物件持有狀態而非全域（階段 8）、C++17 特性（階段 9）、CMake 的 target 觀念（階段 2）。
- 階段 6 最重要：要講清楚「誰擁有閘、誰只是借用」，並在動手前先與使用者確認設計。
