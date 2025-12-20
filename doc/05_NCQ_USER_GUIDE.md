# NCQ (Native Command Queuing) ユーザーガイド

**AHCI Low Level Driver - NCQ機能の使い方とベストプラクティス**

Version: 1.0  
Date: 2025-12-20

---

## 目次

1. [NCQとは](#ncqとは)
2. [Non-NCQとNCQの違い](#non-ncqとncqの違い)
3. [基本的な使い方](#基本的な使い方)
4. [ユースケース](#ユースケース)
5. [パフォーマンス最適化](#パフォーマンス最適化)
6. [トラブルシューティング](#トラブルシューティング)

---

## NCQとは

### Native Command Queuing (NCQ) の概要

NCQは、SATA 2.0で導入された機能で、複数のコマンドを同時にキューイングし、ディスクが最適な順序で実行できるようにする仕組みです。

**メリット:**

| 項目 | Non-NCQ | NCQ |
|-----|---------|-----|
| 同時コマンド数 | 1個 | 最大32個 |
| 実行順序 | 発行順 | ディスク最適化 |
| レイテンシ | 高い | 低い |
| スループット | 低い | 高い |
| シークタイム最適化 | なし | あり |

**仕様:**
- AHCI Specification 1.3.1, Section 8 - Native Command Queuing
- SATA 3.x, Section 13 - Native Command Queuing
- ATA8-ACS, FPDMA (First-Party DMA) commands

### 動作原理

```
Non-NCQ (逐次実行):
  App → Cmd0 (100ms) → Cmd1 (100ms) → Cmd2 (100ms)
  Total: 300ms

NCQ (並列実行):
  App → Cmd0 ┐
      → Cmd1 ├─ Disk optimizes → All done (150ms)
      → Cmd2 ┘
  Total: 150ms (最大2倍高速)
```

---

## Non-NCQとNCQの違い

### 実行モデル

| 特徴 | Non-NCQ | NCQ |
|------|---------|-----|
| **実行方式** | 同期（ブロッキング） | 非同期（ノンブロッキング） |
| **コマンド** | READ DMA (0x25), WRITE DMA (0x35) | READ FPDMA QUEUED (0x60), WRITE FPDMA QUEUED (0x61) |
| **スロット** | 常にスロット0 | 0-31（ユーザ指定） |
| **レジスタ** | PxCIのみ | PxSACT + PxCI |
| **完了FIS** | D2H Register FIS | Set Device Bits FIS |
| **Status値** | 0x50 (DRDY\|DSC) | 0x40 (DRDY) |
| **IOCTL** | ISSUE_CMD（完了まで待機） | ISSUE_CMD + PROBE_CMD（分離） |

### フロー比較

**Non-NCQ:**
```c
// 1回のIOCTLで完了まで待機
ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
// ここで完了済み、req.status=0x50, データ取得済み
```

**NCQ:**
```c
// Phase 1: コマンド発行（即座に戻る）
req.flags = AHCI_CMD_FLAG_NCQ;
req.tag = 5;  // ユーザ指定
ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
// すぐ戻る、バックグラウンドで実行中

// Phase 2: 完了確認（別のタイミング）
ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);
// sdb.completed & (1<<5) → 完了
// sdb.status[5]=0x40, データ取得済み
```

---

## 基本的な使い方

### 1. 単一NCQコマンド実行

```c
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "ahci_lld_ioctl.h"

int main() {
    int fd = open("/dev/ahci_lld_p0", O_RDWR);
    uint8_t buffer[512];
    
    // コマンド準備
    struct ahci_cmd_request req = {0};
    req.command = 0x60;              // READ FPDMA QUEUED
    req.features = 1;                // sector count (count field)
    req.device = 0x40;               // LBA mode
    req.lba = 100;                   // LBA
    req.count = (5 << 3);            // Tag 5 in bits 7:3
    req.tag = 5;                     // スロット番号
    req.buffer = (uint64_t)buffer;
    req.buffer_len = 512;
    req.flags = AHCI_CMD_FLAG_NCQ;   // NCQ有効
    req.timeout_ms = 5000;
    
    // Phase 1: コマンド発行
    if (ioctl(fd, AHCI_IOC_ISSUE_CMD, &req) < 0) {
        perror("ISSUE_CMD failed");
        return 1;
    }
    printf("Command issued: tag=%u\n", req.tag);
    
    // Phase 2: 完了ポーリング
    struct ahci_sdb sdb = {0};
    int timeout = 50; // 50回まで (50ms)
    
    while (timeout-- > 0) {
        if (ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb) < 0) {
            perror("PROBE_CMD failed");
            return 1;
        }
        
        // タグ5が完了したか確認
        if (sdb.completed & (1 << 5)) {
            printf("Completed: status=0x%02x, error=0x%02x\n",
                   sdb.status[5], sdb.error[5]);
            
            // データは既にbufferにコピー済み
            printf("First bytes: %02x %02x %02x %02x\n",
                   buffer[0], buffer[1], buffer[2], buffer[3]);
            break;
        }
        
        usleep(1000); // 1ms待機
    }
    
    if (timeout <= 0) {
        printf("Timeout! Active slots: 0x%08x\n", sdb.sactive);
    }
    
    close(fd);
    return 0;
}
```

---

### 2. 複数NCQコマンド並列実行

```c
#define NUM_CMDS 8

int main() {
    int fd = open("/dev/ahci_lld_p0", O_RDWR);
    uint8_t buffers[NUM_CMDS][512];
    struct ahci_cmd_request reqs[NUM_CMDS];
    
    // Phase 1: 8個のコマンドを一気に発行
    for (int i = 0; i < NUM_CMDS; i++) {
        memset(&reqs[i], 0, sizeof(reqs[i]));
        reqs[i].command = 0x60;              // READ FPDMA QUEUED
        reqs[i].features = 1;                // 1 sector
        reqs[i].device = 0x40;
        reqs[i].lba = i * 8;                 // 異なるLBA
        reqs[i].count = (i << 3);            // Tag i
        reqs[i].tag = i;
        reqs[i].buffer = (uint64_t)buffers[i];
        reqs[i].buffer_len = 512;
        reqs[i].flags = AHCI_CMD_FLAG_NCQ;
        
        if (ioctl(fd, AHCI_IOC_ISSUE_CMD, &reqs[i]) < 0) {
            perror("ISSUE_CMD failed");
            continue;
        }
        printf("Issued tag %d, LBA %llu\n", i, reqs[i].lba);
    }
    
    // Phase 2: 全コマンド完了待機
    uint32_t completed_mask = 0;
    int timeout = 100;
    
    while (completed_mask != 0xFF && timeout-- > 0) {
        struct ahci_sdb sdb = {0};
        
        if (ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb) < 0) {
            perror("PROBE_CMD failed");
            break;
        }
        
        // 新規完了を処理
        uint32_t newly = sdb.completed & ~completed_mask;
        for (int i = 0; i < NUM_CMDS; i++) {
            if (newly & (1 << i)) {
                printf("Tag %d completed: status=0x%02x, error=0x%02x\n",
                       i, sdb.status[i], sdb.error[i]);
                completed_mask |= (1 << i);
            }
        }
        
        printf("Active: 0x%08x, Completed: %d/%d\n",
               sdb.sactive, __builtin_popcount(completed_mask), NUM_CMDS);
        
        usleep(1000);
    }
    
    if (completed_mask == 0xFF) {
        printf("All commands completed successfully!\n");
    } else {
        printf("Timeout: completed=%d/%d\n",
               __builtin_popcount(completed_mask), NUM_CMDS);
    }
    
    close(fd);
    return 0;
}
```

---

### 3. READ/WRITE混在

```c
int main() {
    int fd = open("/dev/ahci_lld_p0", O_RDWR);
    uint8_t read_buf[512];
    uint8_t write_buf[512];
    
    // 書き込みデータ準備
    memset(write_buf, 0xAA, 512);
    
    // WRITE FPDMA QUEUED (tag 0)
    struct ahci_cmd_request write_req = {0};
    write_req.command = 0x61;              // WRITE FPDMA QUEUED
    write_req.features = 1;
    write_req.device = 0x40;
    write_req.lba = 200;
    write_req.count = (0 << 3);            // Tag 0
    write_req.tag = 0;
    write_req.buffer = (uint64_t)write_buf;
    write_req.buffer_len = 512;
    write_req.flags = AHCI_CMD_FLAG_NCQ | AHCI_CMD_FLAG_WRITE;
    
    ioctl(fd, AHCI_IOC_ISSUE_CMD, &write_req);
    printf("WRITE issued: tag 0\n");
    
    // READ FPDMA QUEUED (tag 1)
    struct ahci_cmd_request read_req = {0};
    read_req.command = 0x60;               // READ FPDMA QUEUED
    read_req.features = 1;
    read_req.device = 0x40;
    read_req.lba = 100;
    read_req.count = (1 << 3);             // Tag 1
    read_req.tag = 1;
    read_req.buffer = (uint64_t)read_buf;
    read_req.buffer_len = 512;
    read_req.flags = AHCI_CMD_FLAG_NCQ;
    
    ioctl(fd, AHCI_IOC_ISSUE_CMD, &read_req);
    printf("READ issued: tag 1\n");
    
    // 両方完了待機
    uint32_t target = 0x3; // bit 0 and 1
    while (1) {
        struct ahci_sdb sdb = {0};
        ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);
        
        if ((sdb.completed & target) == target) {
            printf("Both completed!\n");
            printf("WRITE status=0x%02x\n", sdb.status[0]);
            printf("READ status=0x%02x\n", sdb.status[1]);
            break;
        }
        usleep(1000);
    }
    
    close(fd);
    return 0;
}
```

---

## ユースケース

### 1. ランダムアクセスの最適化

```c
// ランダムLBAからの読み取り（シーク時間最適化）
uint64_t lbas[] = {1000, 50, 2000, 100, 1500, 200};
int num_lbas = sizeof(lbas) / sizeof(lbas[0]);

for (int i = 0; i < num_lbas; i++) {
    struct ahci_cmd_request req = {0};
    req.command = 0x60;
    req.lba = lbas[i];
    req.tag = i;
    req.count = (i << 3);
    req.features = 1;
    // ... (省略)
    ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
}

// ディスクが最適順序で実行（例: 50→100→200→1000→1500→2000）
```

**効果:** シーク時間が最大50%削減される可能性

---

### 2. I/O集約型ワークロード

```c
// データベース等で複数テーブルを並列読み取り
for (int table = 0; table < 8; table++) {
    struct ahci_cmd_request req = {0};
    req.lba = table * 1000; // 各テーブルの先頭
    req.tag = table;
    // ... (NCQ設定)
    ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
}

// CPUは他の処理を実行可能（ノンブロッキング）
process_other_tasks();

// 後で結果を回収
struct ahci_sdb sdb;
ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);
```

**効果:** CPU使用率向上、レイテンシ削減

---

### 3. ストリーミング + ランダムアクセス混在

```c
// ログファイルへの逐次書き込み (tag 0-3)
for (int i = 0; i < 4; i++) {
    // WRITE FPDMA QUEUED to sequential LBAs
    req.lba = log_position + i;
    req.tag = i;
    // ...
}

// インデックス更新のためのランダム読み取り (tag 4-7)
for (int i = 0; i < 4; i++) {
    // READ FPDMA QUEUED from random LBAs
    req.lba = index_lbas[i];
    req.tag = 4 + i;
    // ...
}

// ディスクがバランスよく実行
```

---

## パフォーマンス最適化

### 1. キュー深度の選択

| Queue Depth | 使用シーン | パフォーマンス |
|-------------|-----------|---------------|
| 1-4 | 低負荷、レイテンシ重視 | 低スループット、低レイテンシ |
| 8-16 | バランス型ワークロード | バランス |
| 24-32 | 高スループット重視 | 高スループット、やや高レイテンシ |

**推奨:** 通常は8-16で十分

---

### 2. タグ管理戦略

```c
// 方式1: 順次割り当て（シンプル）
static int next_tag = 0;
req.tag = next_tag;
next_tag = (next_tag + 1) % 32;

// 方式2: ビットマップ管理（正確）
static uint32_t used_tags = 0;
int tag = find_first_zero_bit(&used_tags);
if (tag < 32) {
    used_tags |= (1 << tag);
    req.tag = tag;
}

// 完了時
used_tags &= ~(1 << tag);
```

**推奨:** ビットマップ方式（正確なスロット管理）

---

### 3. ポーリング間隔

```c
// 低レイテンシ優先（高CPU使用率）
usleep(100);  // 100μs

// バランス型（推奨）
usleep(1000); // 1ms

// スループット優先（低CPU使用率）
usleep(10000); // 10ms
```

**推奨:** 1ms（レイテンシとCPU効率のバランス）

---

### 4. エラー率が高い場合

```c
// NCQでエラー多発 → Non-NCQに切り替え
if (error_count > THRESHOLD) {
    req.flags &= ~AHCI_CMD_FLAG_NCQ;  // NCQ無効
    req.command = 0x25;  // READ DMA EXT
    // 同期実行にフォールバック
}
```

---

## トラブルシューティング

### Q1: タイムアウトが発生する

**原因:**
- デバイスがNCQに対応していない
- タグエンコーディングミス

**解決策:**
```c
// デバイス確認
struct ahci_cmd_request id = {0};
id.command = 0xEC;  // IDENTIFY DEVICE
ioctl(fd, AHCI_IOC_ISSUE_CMD, &id);
// Word 75を確認（NCQ Queue Depth）

// タグエンコーディング確認
req.count = (tag << 3);  // ビット7:3にタグ
req.tag = tag;           // スロット番号
```

---

### Q2: status=0x50が返る（NCQで0x40が期待値）

**原因:**
- Non-NCQコマンド（0x25など）を使っている
- NCQフラグ未設定

**解決策:**
```c
req.command = 0x60;              // FPDMA READ (NCQ)
req.flags = AHCI_CMD_FLAG_NCQ;   // NCQフラグ必須
```

---

### Q3: 一部タグが完了しない

**原因:**
- スロット衝突（同じタグを重複使用）
- PxSACTビット確認不足

**解決策:**
```c
// PROBE_CMD前に使用中タグ確認
struct ahci_sdb sdb;
ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);

if (sdb.sactive & (1 << tag)) {
    // まだ実行中、使用不可
}
```

---

### Q4: データが壊れている

**原因:**
- WRITEフラグ未設定
- バッファアライメント不正

**解決策:**
```c
// WRITE時
req.flags = AHCI_CMD_FLAG_NCQ | AHCI_CMD_FLAG_WRITE;

// バッファアライメント確認（ページ境界推奨）
void *buffer = aligned_alloc(4096, 512);
req.buffer = (uint64_t)buffer;
```

---

### Q5: パフォーマンスが出ない

**チェックリスト:**
1. デバイスがNCQ対応か確認（IDENTIFY word 75）
2. キュー深度が十分か（推奨: 8以上）
3. ランダムアクセスパターンか（シーケンシャルならNCQ不要）
4. ポーリング頻度が適切か（推奨: 1ms）
5. CPU使用率確認（100%なら間隔を広げる）

---

## まとめ

NCQを効果的に活用するポイント：

1. **ユースケース判断**: ランダムI/O、並列処理で効果大
2. **適切なキュー深度**: 8-16が最適
3. **タグ管理**: ビットマップで正確に管理
4. **エラーハンドリング**: Non-NCQへのフォールバック用意
5. **ポーリング最適化**: 1ms間隔が標準

詳細な仕様は以下を参照：
- `01_IOCTL_SPECIFICATION.md` - IOCTL詳細
- `02_AHCI_HARDWARE_SPECIFICATION.md` - レジスタとFIS
- `03_FUNCTION_API_SPECIFICATION.md` - 内部API
- `04_OPERATION_FLOW_SPECIFICATION.md` - 動作フロー
