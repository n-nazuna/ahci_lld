# IOCTL インターフェース仕様書

AHCI Low Level Driver の IOCTL インターフェース仕様（現在の実装 + NCQ拡張）

---

## 目次

1. [現在の実装](#現在の実装)
2. [NCQ拡張設計](#ncq拡張設計)
3. [使用例](#使用例)

---

## 現在の実装

```c
/* ahci_lld_ioctl.h */
#define AHCI_LLD_IOC_MAGIC 'A'

struct ahci_cmd_request {
    __u8 command;           // ATAコマンドコード
    __u8 features;          // Features register
    __u8 device;            // Device register
    __u8 reserved1;
    __u64 lba;              // LBA (48-bit)
    __u16 count;            // Sector count
    __u16 reserved2;
    __u32 flags;            // コマンドフラグ
    __u64 buffer;           // ユーザー空間バッファポインタ
    __u32 buffer_len;       // バッファサイズ
    __u32 timeout_ms;       // タイムアウト (ミリ秒)
    
    /* 出力 */
    __u8 status;            // ATAステータス
    __u8 error;             // ATAエラー
    __u8 device_out;        // Device register (出力)
    __u8 reserved3;
    __u64 lba_out;          // LBA (出力)
    __u16 count_out;        // Sector count (出力)
    __u16 reserved4;
};

/* 既存IOCTL */
#define AHCI_IOC_ISSUE_CMD         _IOWR(AHCI_LLD_IOC_MAGIC, 10, struct ahci_cmd_request)
```

**動作**: 同期実行（コマンド完了まで待機）

**特徴**:
- ✅ シンプル (1コマンド = 1 ioctl)
- ✅ スロット0のみ使用
- ❌ 並列実行不可

---

## NCQ拡張設計

### IOCTL一覧

| IOCTL | 番号 | 用途 | 説明 |
|-------|------|------|------|
| `AHCI_IOC_ISSUE_CMD` | 10 | コマンド発行 | 同期/非同期対応（拡張） |
| `AHCI_IOC_PROBE_CMD` | 11 | 完了確認 | 完了コマンド情報取得（新規） |

### 1. コマンド発行IOCTL（拡張）

```c
/* ahci_lld_ioctl.h */

struct ahci_cmd_request {
    /* 既存フィールド */
    __u8 command;
    __u8 features;
    __u8 device;
    __u8 reserved1;
    __u64 lba;
    __u16 count;
    __u16 reserved2;
    __u32 flags;            // AHCI_CMD_FLAG_ASYNC を追加
    __u64 buffer;
    __u32 buffer_len;
    __u32 timeout_ms;
    
    /* 既存の出力 */
    __u8 status;
    __u8 error;
    __u8 device_out;
    __u8 reserved3;
    __u64 lba_out;
    __u16 count_out;
    __u16 reserved4;
    
    /* ===== NCQ拡張: 新規追加 ===== */
    __u8 tag;               // 出力: 割り当てられたタグ (0-31)
    __u8 reserved5[3];
};

/* flags フィールド（既存 + 新規） */
#define AHCI_CMD_FLAG_WRITE  (1 << 0)  // 既存
#define AHCI_CMD_FLAG_ATAPI  (1 << 1)  // 既存
#define AHCI_CMD_FLAG_ASYNC  (1 << 2)  // 新規: 非同期実行

/**
 * AHCI_IOC_ISSUE_CMD - コマンド発行
 * 
 * 同期実行（既存の動作）:
 *   flags = 0 または AHCI_CMD_FLAG_WRITE
 *   → 完了まで待機、bufferにデータが入る
 * 
 * 非同期実行（NCQ拡張）:
 *   flags に AHCI_CMD_FLAG_ASYNC を含める
 *   → 即座に戻る、req.tag に割り当てタグが入る
 *   → データは AHCI_IOC_PROBE_CMD で取得
 */
#define AHCI_IOC_ISSUE_CMD   _IOWR(AHCI_LLD_IOC_MAGIC, 10, struct ahci_cmd_request)
```

### 2. 完了確認IOCTL（新規）

```c
/**
 * struct ahci_sdb - Set Device Bits 情報（完了コマンド情報）
 * 
 * AHCI Set Device Bits FIS (FIS Type 0xA1) に相当する情報を格納。
 * NCQコマンドの完了通知に使用される。
 */
struct ahci_sdb {
    /* === Output === */
    __u32 sactive;              // PxSACT値（現在アクティブなスロット）
    __u32 completed;            // 完了スロット（このProbe呼び出しで返すスロット）
    
    /* 32個のスロット情報（インデックス = タグ番号） */
    __u8 status[32];            // ATAステータス（完了スロットのみ有効）
    __u8 error[32];             // ATAエラー（完了スロットのみ有効）
    __u64 buffer[32];           // バッファポインタ（完了スロットのみ有効）
};

/**
 * AHCI_IOC_PROBE_CMD - 完了コマンドを取得（非ブロッキング）
 * 
 * 完了したコマンドの情報を取得する。即座に戻る。
 * 
 * 動作:
 *   1. PxSACT を sdb.sactive に格納（現在アクティブなスロット）
 *   2. 完了したスロットを sdb.completed ビットマップに格納
 *   3. 完了スロットの sdb.status[tag], sdb.error[tag], sdb.buffer[tag] を設定
 *   4. データは既にユーザーバッファにコピー済み
 *   5. 返却したスロットは内部でクリア（次回Probeでは返さない）
 * 
 * 使い方:
 *   struct ahci_sdb sdb;
 *   ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);
 *   
 *   printf("Active slots: 0x%08x\n", sdb.sactive);
 *   printf("Completed slots: 0x%08x\n", sdb.completed);
 *   
 *   for (int tag = 0; tag < 32; tag++) {
 *       if (sdb.completed & (1 << tag)) {
 *           printf("Tag %d: status=0x%02x error=0x%02x buffer=%p\n",
 *                  tag, sdb.status[tag], sdb.error[tag],
 *                  (void*)sdb.buffer[tag]);
 *           // sdb.buffer[tag] には既にデータがコピー済み
 *       }
 *   }
 * 
 * 注意:
 *   - completed ビットが立っているスロットのみ有効
 *   - 一度返されたスロットは次回 Probe では返されない
 *   - 新しく完了したものだけが返される
 * 
 * 戻り値:
 *   0: 成功（sdb.completed を確認）
 */
#define AHCI_IOC_PROBE_CMD   _IOR(AHCI_LLD_IOC_MAGIC, 11, struct ahci_sdb)
```

---

## 使用例

### 例1: 同期実行（既存の動作）

```c
int main() {
    int fd = open("/dev/ahci_lld_p0", O_RDWR);
    char buffer[512];
    
    // READ DMA EXT（同期）- 従来通り
    struct ahci_cmd_request req = {
        .command = 0x25,
        .device = 0x40,
        .lba = 0,
        .count = 1,
        .flags = 0,  // 非同期フラグなし → 同期実行
        .buffer = (uint64_t)buffer,
        .buffer_len = 512,
        .timeout_ms = 5000
    };
    
    ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);  // 完了まで待機
    printf("Status: 0x%02x\n", req.status);
    // buffer にデータが入っている
    
    close(fd);
    return 0;
}
```

### 例2: 非同期実行（NCQ）- 基本

```c
int main() {
    int fd = open("/dev/ahci_lld_p0", O_RDWR);
    char buffer[4096];
    
    // READ FPDMA QUEUED（非同期）
    struct ahci_cmd_request req = {
        .command = 0x60,        // READ FPDMA QUEUED
        .features = 8,          // Sector count
        .device = 0x40,
        .lba = 1000,
        .count = 8,
        .flags = AHCI_CMD_FLAG_ASYNC,  // 非同期実行
        .buffer = (uint64_t)buffer,
        .buffer_len = 4096,
        .timeout_ms = 5000
    };
    
    // 発行（すぐ戻る）
    ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
    printf("Issued with tag: %d\n", req.tag);
    uint8_t my_tag = req.tag;
    
    // 他の処理...
    do_other_work();
    
    // 完了確認（Probe使用）
    while (1) {
        struct ahci_sdb sdb;
        ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);
        
        if (sdb.completed & (1 << my_tag)) {
            printf("Tag %d completed: status=0x%02x error=0x%02x\n",
                   my_tag, sdb.status[my_tag], sdb.error[my_tag]);
            // buffer にデータが入っている
            break;
        }
        
        usleep(100);  // 100us待機
    }
    
    close(fd);
    return 0;
}
```

### 例3: 複数コマンド並行実行

```c
#define NUM_CMDS 4

int main() {
    int fd = open("/dev/ahci_lld_p0", O_RDWR);
    char *buffers[NUM_CMDS];
    uint8_t tags[NUM_CMDS];
    uint32_t pending = 0;  // ビットマップ
    
    // 4個のコマンドを発行
    for (int i = 0; i < NUM_CMDS; i++) {
        buffers[i] = malloc(4096);
        
        struct ahci_cmd_request req = {
            .command = 0x60,
            .features = 8,
            .device = 0x40,
            .lba = i * 1000,
            .count = 8,
            .flags = AHCI_CMD_FLAG_ASYNC,
            .buffer = (uint64_t)buffers[i],
            .buffer_len = 4096,
            .timeout_ms = 5000
        };
        
        ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
        tags[i] = req.tag;
        pending |= (1 << req.tag);
        printf("Issued tag %d\n", req.tag);
    }
    
    // 全完了を待つ
    while (pending != 0) {
        struct ahci_sdb sdb;
        ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);
        
        printf("Active: 0x%08x, Completed: 0x%08x\n",
               sdb.sactive, sdb.completed);
        
        // 完了したコマンドを処理
        for (int tag = 0; tag < 32; tag++) {
            if (sdb.completed & (1 << tag)) {
                printf("Tag %d completed: status=0x%02x error=0x%02x\n",
                       tag, sdb.status[tag], sdb.error[tag]);
                pending &= ~(1 << tag);
            }
        }
        
        if (sdb.completed == 0) {
            usleep(100);  // 100us待機
        }
    }
    
    for (int i = 0; i < NUM_CMDS; i++) {
        free(buffers[i]);
    }
    
    close(fd);
    return 0;
}
```

### 例4: リアルタイム監視

```c
int main() {
    int fd = open("/dev/ahci_lld_p0", O_RDWR);
    
    // 複数コマンドを連続発行
    for (int i = 0; i < 16; i++) {
        char *buffer = malloc(4096);
        struct ahci_cmd_request req = {
            .command = 0x60,
            .features = 8,
            .device = 0x40,
            .lba = i * 1000,
            .count = 8,
            .flags = AHCI_CMD_FLAG_ASYNC,
            .buffer = (uint64_t)buffer,
            .buffer_len = 4096
        };
        ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
    }
    
    // リアルタイム監視
    int completed_count = 0;
    while (completed_count < 16) {
        struct ahci_sdb sdb;
        ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);
        
        // アクティブスロット表示
        printf("Active slots: ");
        for (int i = 0; i < 32; i++) {
            if (sdb.sactive & (1 << i)) printf("%d ", i);
        }
        printf("\n");
        
        // 完了処理
        for (int tag = 0; tag < 32; tag++) {
            if (sdb.completed & (1 << tag)) {
                printf("✓ Tag %d done: status=0x%02x\n",
                       tag, sdb.status[tag]);
                completed_count++;
            }
        }
        
        usleep(1000);  // 1ms
    }
    
    close(fd);
    return 0;
}

---

## 実装ステップ

### Phase 1: 構造体拡張（現在の実装に追加）

1. `ahci_cmd_request` に `tag` フィールド追加
2. `AHCI_CMD_FLAG_ASYNC` フラグ定義
3. 既存の同期動作は完全に維持

### Phase 2: Probe IOCTL実装

1. `ahci_sdb` 構造体定義（sactive + 32個の配列）
2. `AHCI_IOC_PROBE_CMD` 実装
3. 完了スロットのスキャン機構
4. 完了フラグ管理（一度返したスロットは次回返さない）

### Phase 3: 非同期実行対応

1. スロット管理機構（ビットマップ）
2. `AHCI_CMD_FLAG_ASYNC` 時の動作分岐
3. コマンド発行後に即座にreturn
4. 完了コマンドのキューイング

---

**最終更新**: 2025-12-20  
**バージョン**: 2.0 (Simplified)
