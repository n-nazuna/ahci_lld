# AHCI LLD IOCTL仕様書

**AHCI Low Level Driver - ユーザ空間インターフェース仕様**

Version: 1.0  
Date: 2025-12-20

---

## 目次

1. [概要](#概要)
2. [IOCTLコマンド一覧](#ioctlコマンド一覧)
3. [データ構造](#データ構造)
4. [使用例](#使用例)
5. [エラーコード](#エラーコード)

---

## 概要

AHCI LLDドライバは、ユーザ空間からAHCIハードウェアを直接制御するためのIOCTLインターフェースを提供します。

### デバイスノード

- **ポートデバイス**: `/dev/ahci_lld_p<N>` (N=0-31)
  - 各SATAポートに対応
  - コマンド発行、ポート制御を担当

- **GHC制御デバイス**: `/dev/ahci_lld_ghc`
  - HBAグローバル制御
  - レジスタ直接アクセス

### IOCTLマジックナンバー

```c
#define AHCI_IOC_MAGIC 'A'
```

---

## IOCTLコマンド一覧

### 1. AHCI_IOC_PORT_RESET

**定義:**
```c
#define AHCI_IOC_PORT_RESET _IO('A', 1)
```

**目的:** ポートのCOMRESET（通信リセット）を実行

**パラメータ:** なし

**動作:**
1. ポートを停止（PxCMD.ST=0）
2. PxSCTL.DET=1を設定（COMRESET開始）
3. 10ms待機
4. PxSCTL.DET=0に戻す（COMRESET解除）
5. PxSSTS.DET=3（PHY確立）を待機（最大1秒）
6. PxSERRをクリア

**戻り値:**
- `0`: 成功
- `-ETIMEDOUT`: タイムアウト（PHY確立失敗）

**使用例:**
```c
int fd = open("/dev/ahci_lld_p0", O_RDWR);
if (ioctl(fd, AHCI_IOC_PORT_RESET) < 0) {
    perror("Port reset failed");
}
```

**注意事項:**
- デバイスとの通信が完全にリセットされる
- 実行中のコマンドは中断される

---

### 2. AHCI_IOC_PORT_START

**定義:**
```c
#define AHCI_IOC_PORT_START _IO('A', 2)
```

**目的:** ポートのコマンド処理を開始

**パラメータ:** なし

**動作:**
1. デバイス接続確認（PxSSTS.DET=3）
2. FIS受信有効化（PxCMD.FRE=1）
3. PxCMD.FR=1を待機
4. PxISをクリア
5. コマンド処理開始（PxCMD.ST=1）
6. デバイスレディ待機（PxTFD: BSY=0, DRQ=0）

**戻り値:**
- `0`: 成功
- `-ETIMEDOUT`: タイムアウト

**使用例:**
```c
if (ioctl(fd, AHCI_IOC_PORT_START) < 0) {
    perror("Port start failed");
}
```

**注意事項:**
- コマンド発行前に必ず実行が必要
- RESETとセットで使用

---

### 3. AHCI_IOC_PORT_STOP

**定義:**
```c
#define AHCI_IOC_PORT_STOP _IO('A', 3)
```

**目的:** ポートのコマンド処理を停止

**パラメータ:** なし

**動作:**
1. PxCMD.ST=0（コマンド処理停止）
2. PxCMD.CR=0を待機（最大500ms）
3. PxCMD.FRE=0（FIS受信停止）
4. PxCMD.FR=0を待機（最大500ms）

**戻り値:**
- `0`: 成功
- `-ETIMEDOUT`: タイムアウト

**使用例:**
```c
if (ioctl(fd, AHCI_IOC_PORT_STOP) < 0) {
    perror("Port stop failed");
}
```

**注意事項:**
- 実行中のコマンドは中断される可能性がある

---

### 4. AHCI_IOC_ISSUE_CMD

**定義:**
```c
#define AHCI_IOC_ISSUE_CMD _IOWR('A', 4, struct ahci_cmd_request)
```

**目的:** ATAコマンドを発行（同期/非同期）

**パラメータ:** `struct ahci_cmd_request`

#### 構造体定義

```c
struct ahci_cmd_request {
    /* 入力フィールド */
    __u8  command;        /* ATAコマンドコード */
    __u8  device;         /* デバイスレジスタ（通常0x40） */
    __u16 features;       /* フィーチャーレジスタ（16ビット） */
    __u64 lba;            /* 48ビットLBAアドレス */
    __u16 count;          /* セクタ数（16ビット） */
    __u32 flags;          /* コマンドフラグ */
    __u64 buffer;         /* ユーザ空間バッファアドレス */
    __u32 buffer_len;     /* バッファサイズ（バイト） */
    __u32 timeout_ms;     /* タイムアウト（ミリ秒） */
    __u32 tag;            /* NCQ: スロット番号（0-31） */
    
    /* 出力フィールド */
    __u8  status;         /* ATAステータスレジスタ */
    __u8  error;          /* ATAエラーレジスタ */
    __u8  device_out;     /* 結果デバイスレジスタ */
    __u64 lba_out;        /* 結果LBA */
    __u16 count_out;      /* 結果セクタ数 */
};
```

#### フラグ定義

```c
#define AHCI_CMD_FLAG_WRITE      (1 << 0)  /* 書き込み方向 */
#define AHCI_CMD_FLAG_ATAPI      (1 << 1)  /* ATAPIコマンド */
#define AHCI_CMD_FLAG_NCQ        (1 << 2)  /* NCQ（非同期実行） */
#define AHCI_CMD_FLAG_PREFETCH   (1 << 3)  /* プリフェッチ可能 */
```

#### 主要ATAコマンド

| コマンド | コード | 説明 | タイプ |
|---------|--------|------|--------|
| IDENTIFY DEVICE | 0xEC | デバイス情報取得 | Non-NCQ |
| READ DMA EXT | 0x25 | DMA読み取り | Non-NCQ |
| WRITE DMA EXT | 0x35 | DMA書き込み | Non-NCQ |
| READ FPDMA QUEUED | 0x60 | NCQ読み取り | NCQ |
| WRITE FPDMA QUEUED | 0x61 | NCQ書き込み | NCQ |

#### 動作モード

**同期モード（Non-NCQ）:**
- `flags`に`AHCI_CMD_FLAG_NCQ`を設定**しない**
- コマンド完了まで待機（ブロッキング）
- **常にスロット0を使用**（固定）
- 戻り値として完了ステータス取得

**非同期モード（NCQ）:**
- `flags`に`AHCI_CMD_FLAG_NCQ`を設定
- 即座にリターン（ノンブロッキング）
- `tag`フィールドでスロット番号を指定（0-31）
- `AHCI_IOC_PROBE_CMD`で完了確認が必要

#### NCQコマンドのパラメータエンコーディング

NCQコマンド（0x60/0x61）の場合、特殊なエンコーディングが必要：

```c
/* READ/WRITE FPDMA QUEUEDの場合 */
req.command = 0x60;  /* または 0x61 */
req.features = sector_count;  /* セクタ数をFeaturesレジスタに */
req.count = (tag << 3);       /* タグをCount[7:3]に（FIS用） */
req.tag = tag;                /* ドライバ用スロット番号 */
req.device = 0x40;            /* LBAモード */
req.lba = lba_address;        /* 48ビットLBA */
req.flags = AHCI_CMD_FLAG_NCQ;
```

#### 戻り値

- `0`: 成功
- `-EINVAL`: 無効なパラメータ
- `-ENOMEM`: メモリ不足
- `-ETIMEDOUT`: タイムアウト（同期モードのみ）
- `-EIO`: ハードウェアエラー
- `-EBUSY`: スロット使用中（NCQモード）
- `-EFAULT`: ユーザ空間バッファアクセスエラー

#### 使用例

**同期READ（Non-NCQ）:**
```c
struct ahci_cmd_request req;
uint8_t buffer[512];

memset(&req, 0, sizeof(req));
req.command = 0x25;           /* READ DMA EXT */
req.device = 0x40;            /* LBA mode */
req.lba = 0x1000;             /* LBA address */
req.count = 1;                /* 1 sector */
req.buffer = (uint64_t)buffer;
req.buffer_len = 512;
req.timeout_ms = 5000;
req.flags = 0;                /* Synchronous */

if (ioctl(fd, AHCI_IOC_ISSUE_CMD, &req) < 0) {
    perror("Command failed");
} else {
    printf("Status: 0x%02x\n", req.status);
    /* buffer contains read data */
}
```

**非同期READ（NCQ）:**
```c
struct ahci_cmd_request req[4];
uint8_t buffers[4][512];

for (int i = 0; i < 4; i++) {
    memset(&req[i], 0, sizeof(req[i]));
    req[i].command = 0x60;           /* READ FPDMA QUEUED */
    req[i].features = 1;             /* 1 sector in Features */
    req[i].device = 0x40;
    req[i].lba = 0x1000 + i * 8;
    req[i].count = (i << 3);         /* Tag in Count[7:3] */
    req[i].tag = i;                  /* Slot number */
    req[i].buffer = (uint64_t)buffers[i];
    req[i].buffer_len = 512;
    req[i].timeout_ms = 5000;
    req[i].flags = AHCI_CMD_FLAG_NCQ;
    
    /* Returns immediately */
    if (ioctl(fd, AHCI_IOC_ISSUE_CMD, &req[i]) < 0) {
        perror("NCQ issue failed");
    }
}

/* Poll for completion (see PROBE_CMD) */
```

---

### 5. AHCI_IOC_PROBE_CMD

**定義:**
```c
#define AHCI_IOC_PROBE_CMD _IOR('A', 5, struct ahci_sdb)
```

**目的:** NCQコマンドの完了確認（ノンブロッキング）

**パラメータ:** `struct ahci_sdb`

#### 構造体定義

```c
struct ahci_sdb {
    __u32 sactive;        /* PxSACTレジスタ値（現在アクティブなスロット） */
    __u32 completed;      /* 新規完了スロットのビットマップ */
    __u8  status[32];     /* 各スロットのATAステータス */
    __u8  error[32];      /* 各スロットのATAエラー */
    __u64 buffer[32];     /* 各スロットのバッファポインタ */
};
```

#### 動作

1. PxSACTレジスタを読み取り（アクティブスロット確認）
2. 使用中スロットをスキャン
3. PxSACTでクリアされたスロット = 完了
4. SDB FIS（オフセット0x58）からステータス/エラー読み取り
5. **READコマンドの場合**: カーネルバッファからユーザバッファへデータコピー（完了検出時に自動実行）
6. 完了スロットのビットマップと情報を返す

**注意**: カーネルバッファは解放されません。次回の`AHCI_IOC_ISSUE_CMD`で同じtagを使用すると、自動的に破棄されます。

#### 戻り値

- `0`: 成功（完了コマンドなしでも成功）
- `-EFAULT`: ユーザ空間へのコピー失敗

#### 使用例

```c
struct ahci_sdb sdb;

while (1) {
    if (ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb) < 0) {
        perror("Probe failed");
        break;
    }
    
    printf("Active slots: 0x%08x\n", sdb.sactive);
    printf("Completed: 0x%08x\n", sdb.completed);
    
    /* Check each slot */
    for (int i = 0; i < 32; i++) {
        if (sdb.completed & (1 << i)) {
            printf("Slot %d completed: status=0x%02x error=0x%02x\n",
                   i, sdb.status[i], sdb.error[i]);
            /* Data is now in user buffer */
        }
    }
    
    /* Exit if all commands completed */
    if (sdb.sactive == 0) {
        break;
    }
    
    usleep(100000);  /* 100ms polling interval */
}
```

#### 注意事項

- ノンブロッキング（即座にリターン）
- 完了コマンドがない場合、`completed=0`で正常リターン
- 同じスロットは一度しか`completed`に現れない
- **READデータは完了検出時に自動コピー済み**（`ahci_check_slot_completion`内で実行）
- **バッファ解放は行わない** - 次回の`AHCI_IOC_ISSUE_CMD`（tag上書き時）または`AHCI_IOC_FREE_SLOT`で解放

---

### 6. AHCI_IOC_READ_REGS

**定義:**
```c
#define AHCI_IOC_READ_REGS _IOR('A', 6, struct ahci_port_regs)
```

**目的:** ポートレジスタダンプの取得

**パラメータ:** `struct ahci_port_regs`

**ステータス:** **未実装**

**戻り値:**
- `-ENOSYS`: 未実装

**注意:**
- 将来の拡張用プレースホルダー
- デバッグ目的で全ポートレジスタを一括取得予定

---

### 6. AHCI_IOC_FREE_SLOT

**定義:**
```c
#define AHCI_IOC_FREE_SLOT _IOW('A', 12, int)
```

**目的:** NCQスロットの解放とバッファクリーンアップ

**パラメータ:** `int` (スロット番号 0-31)

#### 動作

1. スロット番号の妥当性チェック（0-31範囲）
2. スロット使用中フラグ確認
3. 関連するカーネルバッファ（`slot->buffer`）を解放
4. スロット管理ビットマップ（`slots_in_use`）をクリア
5. アクティブスロットカウンタをデクリメント
6. スロット情報をゼロクリア

**注意:** Non-NCQコマンドは常にスロット0を使用します。Non-NCQコマンド完了時に自動的に解放されます。

#### メモリ管理

- NCQコマンド発行時に確保されたカーネルバッファ（`kmalloc`）を解放
- SGバッファ（DMA用）は解放**しない**（再利用される）
- コマンドテーブル（`cmd_tables[slot]`）も解放しない（永続的）

#### 戻り値

- `0`: 成功
- `-EFAULT`: パラメータ取得失敗

#### 使用タイミング

**必須ではない**が、以下の場合に使用を推奨：

1. **長時間未使用スロット**: 完了後すぐに再利用しない場合
2. **メモリ節約**: メモリ使用量を最小化したい場合
3. **エラーリカバリ**: 異常終了したスロットのクリーンアップ

**自動解放タイミング:**

- **Non-NCQ完了時**: スロット0が自動的に解放される（`ahci_port_issue_cmd`内）
- **NCQ tag上書き時**: 同じスロット番号で新しいNCQコマンドを発行すると、古いバッファを自動破棄
- **Probe後の再利用**: 完了したNCQスロットは次回Issue時に自動的にクリーンアップ

#### 使用例

```c
struct ahci_sdb sdb;

/* Probe for completion */
if (ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb) < 0) {
    perror("Probe failed");
}

/* Process completed commands */
for (int slot = 0; slot < 32; slot++) {
    if (sdb.completed & (1 << slot)) {
        printf("Slot %d completed\n", slot);
        
        /* Option 1: Explicitly free slot */
        if (ioctl(fd, AHCI_IOC_FREE_SLOT, &slot) < 0) {
            perror("Free slot failed");
        }
        
        /* Option 2: Let it auto-free on next Issue (recommended) */
    }
}
```

#### 注意事項

- **通常は不要**: tag上書き時の自動破棄で十分
- **早期解放**: メモリを即座に解放したい場合に使用
- **スロット再利用**: FREE_SLOT後、そのスロットは即座に再利用可能
- **完了前の解放**: 実行中のコマンドには使用しない（未定義動作）

---

## データ構造

### ahci_cmd_request

詳細は[AHCI_IOC_ISSUE_CMD](#4-ahci_ioc_issue_cmd)を参照。

### ahci_sdb

詳細は[AHCI_IOC_PROBE_CMD](#5-ahci_ioc_probe_cmd)を参照。

### ステータスビット（ATA Status Register）

```c
#define ATA_STATUS_BSY   (1 << 7)  /* Busy */
#define ATA_STATUS_DRDY  (1 << 6)  /* Device Ready */
#define ATA_STATUS_DF    (1 << 5)  /* Device Fault */
#define ATA_STATUS_DSC   (1 << 4)  /* Device Seek Complete */
#define ATA_STATUS_DRQ   (1 << 3)  /* Data Request */
#define ATA_STATUS_CORR  (1 << 2)  /* Corrected Data */
#define ATA_STATUS_IDX   (1 << 1)  /* Index */
#define ATA_STATUS_ERR   (1 << 0)  /* Error */
```

**正常完了:**
- Non-NCQ: `0x50` (DRDY=1, DSC=1)
- NCQ: `0x40` (DRDY=1)

**エラー:**
- `status & ATA_STATUS_ERR` が1の場合、`error`レジスタを確認

---

## 使用例

### 完全なNCQ読み取りシーケンス

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "ahci_lld_ioctl.h"

#define NUM_CMDS 4
#define SECTOR_SIZE 512

int main(void)
{
    int fd;
    struct ahci_cmd_request req[NUM_CMDS];
    uint8_t buffers[NUM_CMDS][SECTOR_SIZE];
    struct ahci_sdb sdb;
    
    /* Open port device */
    fd = open("/dev/ahci_lld_p0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    /* Reset and start port */
    if (ioctl(fd, AHCI_IOC_PORT_RESET) < 0) {
        perror("reset");
        close(fd);
        return 1;
    }
    
    if (ioctl(fd, AHCI_IOC_PORT_START) < 0) {
        perror("start");
        close(fd);
        return 1;
    }
    
    /* Issue NCQ commands */
    for (int i = 0; i < NUM_CMDS; i++) {
        memset(&req[i], 0, sizeof(req[i]));
        
        req[i].command = 0x60;                /* READ FPDMA QUEUED */
        req[i].features = 1;                  /* Sector count */
        req[i].device = 0x40;                 /* LBA mode */
        req[i].lba = 0x1000 + i * 8;          /* Different LBAs */
        req[i].count = (i << 3);              /* Tag in bits 7:3 */
        req[i].tag = i;                       /* Slot number */
        req[i].buffer = (uint64_t)buffers[i];
        req[i].buffer_len = SECTOR_SIZE;
        req[i].timeout_ms = 5000;
        req[i].flags = AHCI_CMD_FLAG_NCQ;
        
        if (ioctl(fd, AHCI_IOC_ISSUE_CMD, &req[i]) < 0) {
            perror("issue");
            close(fd);
            return 1;
        }
        
        printf("Issued command %d: tag=%d LBA=0x%llx\n",
               i, req[i].tag, (unsigned long long)req[i].lba);
    }
    
    /* Poll for completion */
    int completed_count = 0;
    for (int poll = 0; poll < 10 && completed_count < NUM_CMDS; poll++) {
        usleep(100000);  /* 100ms */
        
        if (ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb) < 0) {
            perror("probe");
            close(fd);
            return 1;
        }
        
        printf("Poll %d: sactive=0x%08x completed=0x%08x\n",
               poll, sdb.sactive, sdb.completed);
        
        /* Process completed commands */
        for (int i = 0; i < NUM_CMDS; i++) {
            if (sdb.completed & (1 << i)) {
                printf("  Tag %d completed: status=0x%02x error=0x%02x\n",
                       i, sdb.status[i], sdb.error[i]);
                
                /* Check for errors */
                if (sdb.status[i] & 0x01) {  /* ERR bit */
                    fprintf(stderr, "    ERROR: 0x%02x\n", sdb.error[i]);
                } else {
                    /* Print first 16 bytes of data */
                    printf("    Data: ");
                    for (int j = 0; j < 16; j++) {
                        printf("%02x ", buffers[i][j]);
                    }
                    printf("\n");
                }
                
                completed_count++;
            }
        }
    }
    
    if (completed_count == NUM_CMDS) {
        printf("All commands completed successfully!\n");
    } else {
        fprintf(stderr, "Timeout: only %d/%d completed\n",
                completed_count, NUM_CMDS);
    }
    
    close(fd);
    return (completed_count == NUM_CMDS) ? 0 : 1;
}
```

---

## エラーコード

### システムエラー

| エラー | 説明 |
|--------|------|
| `-EINVAL` | 無効なパラメータ（範囲外のタグ、サイズ等） |
| `-ENOMEM` | カーネルメモリ不足 |
| `-EFAULT` | ユーザ空間バッファアクセス失敗 |
| `-EBUSY` | リソース使用中（スロット、ポート等） |
| `-ETIMEDOUT` | タイムアウト |
| `-EIO` | ハードウェアI/Oエラー |
| `-ENOSYS` | 未実装機能 |

### ハードウェアエラー（PxIS）

コマンド実行中にPxISレジスタで以下のビットがセットされた場合、`-EIO`を返す：

- **TFES (bit 30)**: Task File Error Status
- **HBFS (bit 29)**: Host Bus Fatal Error
- **HBDS (bit 28)**: Host Bus Data Error
- **IFS (bit 27)**: Interface Fatal Error

詳細は`PxTFD.STS.ERR`と`PxTFD.ERR`レジスタで確認可能。

---

## 制限事項

### バッファサイズ

- **最大**: 256MB（`AHCI_MAX_BUFFER_SIZE`）
- **推奨**: 128KB単位（SGバッファサイズに合わせる）

### NCQスロット数

- **範囲**: 0-31（32スロット）
- **同時実行**: ハードウェア依存（通常32まで）

### タイムアウト

- **デフォルト**: 30秒（`AHCI_CMD_TIMEOUT_MS`）
- **最小**: 100ms
- **最大**: `UINT_MAX` ms

### 同時オープン

- 各ポートデバイスは複数プロセスから同時にオープン可能
- ただし、同期が必要（カーネル内部でロックなし）
- 推奨: 1プロセス1ポート

---

## NCQバッファのメモリライフサイクル

NCQコマンドのバッファ管理は以下のライフサイクルに従います：

### バッファ確保（AHCI_IOC_ISSUE_CMD時）

```c
/* 新規NCQコマンド発行時 */
req.tag = 5;  /* スロット5を使用 */
req.flags = AHCI_CMD_FLAG_NCQ;
req.buffer_len = 512;

ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
```

**動作（NCQの場合）:**
1. `tag=5`が既に使用中かチェック
2. **古いバッファが存在する場合**: `kfree(slots[5].buffer)` で自動破棄
3. **古いスロット管理をクリア**: `ahci_free_slot(port, 5)` で解放
4. 新しいバッファを`kmalloc(512, GFP_KERNEL)`で確保
5. WRITEの場合: ユーザバッファからコピー
6. 新しいスロットを確保（`ahci_alloc_slot`）
7. DMA転送開始（即座にreturn）

**動作（Non-NCQの場合）:**
1. **常にスロット0を使用**（固定、ハードコード）
2. バッファを`kmalloc`で確保
3. WRITEの場合: ユーザバッファからコピー
4. DMA転送開始・完了待機（ブロッキング）
5. **完了後に自動的に`ahci_free_slot(port, 0)`を呼び出し**
6. READの場合: ユーザバッファへコピー
7. バッファを`kfree`して返却

### データ転送完了（AHCI_IOC_PROBE_CMD時）

```c
struct ahci_sdb sdb;
ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);

if (sdb.completed & (1 << 5)) {
    /* スロット5完了 */
    /* READデータは既にユーザバッファにコピー済み */
}
```

**動作:**
1. `ahci_check_slot_completion()`内で完了検出
2. **READの場合**: `copy_to_user()`で自動コピー
3. ステータス/エラー情報を設定
4. **バッファは保持したまま**（解放しない）

### バッファ解放（2つの方法）

#### 方法1: 自動解放（推奨）

**Non-NCQ:**
```c
/* Non-NCQコマンドは常に自動解放 */
req.command = 0x25;  /* READ DMA EXT */
req.flags = 0;       /* Non-NCQ */
ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
/* → 完了後、スロット0が自動的に解放される */
```

**NCQ tag上書き:**
```c
/* 同じtagで新しいコマンド発行 */
req.tag = 5;  /* 前回と同じtag */
req.flags = AHCI_CMD_FLAG_NCQ;
ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
/* → 古いslots[5]が自動的に解放される */
```

#### 方法2: 明示的解放（オプション）

```c
int slot = 5;
ioctl(fd, AHCI_IOC_FREE_SLOT, &slot);
/* → すぐにメモリ解放（NCQのみ） */
```

**注意:** スロット0への明示的FREE_SLOTは不要です（Non-NCQは自動管理）。ただし、NCQで`tag=0`を使用した場合は、FREE_SLOTまたはtag上書きで解放できます。

### メモリリーク防止

- **Non-NCQ**: 完了時に自動的にスロット0を解放（ユーザ操作不要）
- **NCQ tag再利用**: 同じtagで新規Issue → 自動破棄で100%安全
- **NCQ FREE_SLOT**: 長期間未使用のtagを明示的に解放（オプション）
- **ドライバアンロード**: 全スロットのバッファを自動解放

### 推奨使用パターン

```c
/* ループ内でNCQを継続的に使用 */
for (int i = 0; i < 1000; i++) {
    int tag = i % 32;  /* 32スロットを循環利用 */
    
    /* 1. コマンド発行（tag再利用で自動クリーンアップ） */
    req.tag = tag;
    ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
    
    /* 2. 完了確認（データは自動コピー済み） */
    ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);
    
    /* 3. FREE_SLOTは不要（次回Issueで自動破棄される） */
}
```

---

## まとめ

本IOCTL仕様により、ユーザ空間から以下が可能：

1. **ポート制御**: RESET, START, STOP
2. **同期コマンド実行**: ブロッキングI/O（Non-NCQ）
3. **非同期コマンド実行**: ノンブロッキングI/O（NCQ）
4. **完了ポーリング**: NCQコマンドの完了確認

適切に使用することで、高性能なSATAデバイスアクセスを実現できます。
