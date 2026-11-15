# 第 8 章範例

對應 `review/README.md` 第 8 章。**一律在專案根目錄（`fraig/`）執行**，輸出放到 `/tmp`，不會弄髒專案。
這些檔案不是專案的一部分，只是拿來做實驗。

## 8.1 未定義行為

```bash
# 越界讀取：一般編譯看起來正常，ASan 抓得到
g++ -std=c++17 -O0 review/examples/ch8/ub1.cpp -o /tmp/ub1 && /tmp/ub1
g++ -std=c++17 -g -fsanitize=address review/examples/ch8/ub1.cpp -o /tmp/ub1a && /tmp/ub1a

# 有號整數溢位：UBSan 抓得到
g++ -std=c++17 -O0 review/examples/ch8/ub2.cpp -o /tmp/ub2 && /tmp/ub2 2147483647
g++ -std=c++17 -g -fsanitize=undefined review/examples/ch8/ub2.cpp -o /tmp/ub2u && /tmp/ub2u 2147483647

# 懸空指標（容器搬家）
g++ -std=c++17 -O0 review/examples/ch8/ub3.cpp -o /tmp/ub3 && /tmp/ub3
g++ -std=c++17 -g -fsanitize=address review/examples/ch8/ub3.cpp -o /tmp/ub3a && /tmp/ub3a
```

## 8.1 (a) 複製 CirMgr 會重複釋放

```bash
g++ -std=c++11 -g -fsanitize=address,undefined -DTA_KB_SETTING -w \
    -Isrc/cir -Isrc/cmd -Isrc/util -Isrc/sat \
    review/examples/ch8/copytest.cpp \
    src/cir/cirMgr.cpp src/cir/cirGate.cpp src/cir/cirFraig.cpp src/cir/cirOpt.cpp src/cir/cirSim.cpp \
    src/cmd/*.cpp src/util/*.cpp src/sat/*.cpp -o /tmp/copytest
/tmp/copytest        # 預期：AddressSanitizer: heap-use-after-free
```

## 8.2 所有權

```bash
g++ -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined review/examples/ch8/own1.cpp -o /tmp/own1 && /tmp/own1
# 把「複製擁有者」的那行打開，觀察編譯期錯誤：
g++ -std=c++17 -DTRY_COPY review/examples/ch8/own1.cpp -o /tmp/own1c
```

## 8.3 錯誤處理（需要支援 C++23 `<expected>` 的編譯器，如 g++ 12 以上）

```bash
g++ -std=c++23 -Wall -Wextra review/examples/ch8/err1.cpp -o /tmp/err1 && /tmp/err1
```

## 8.4 效能實驗

```bash
g++ -std=c++17 -O2 review/examples/ch8/bench.cpp -o /tmp/bench && /tmp/bench && /tmp/bench
```

數字會因機器而異（也會有雜訊，所以建議跑兩次以上）。重點是量級與趨勢，不是絕對值。
