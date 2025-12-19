# コマンド発行仕様書

AHCI Low Level Driver (ahci_lld) のコマンド発行機構の詳細仕様です。

---

## 目次

1. [概要](#概要)
2. [アーキテクチャ](#アーキテクチャ)
3. [データ構造](#データ構造)
4. [コマンド発行フロー](#コマンド発行フロー)
5. [Scatter-Gather DMA](#scatter-gather-dma)
6. [エラーハンドリング](#エラーハンドリング)
7. [パフォーマンス特性](#パフォーマンス特性)
8. [制限事項と今後の拡張](#制限事項と今後の拡張)

---

## 概要

### 基本設計方針

このドライバは **シングルスロット・ポーリングモード** で動作します。

| 項目 | 仕様 |
|-----|------|
| コマンドスロット | **1個のみ（Slot 0固定）** |
| 実行モード | **逐次実行**（1コマンドずつ） |
| 完了検出 | **ポーリング**（PxCIレジスタを監視） |
| 割り込み | **未使用** |
| NCQ | **未対応** |
| 最大転送サイズ | **256 MB** (Scatter-Gather使用) |

### AHCI仕様との対応

| AHCI機能 | 仕様上限 | 本実装 |
|---------|---------|--------|
| コマンドスロット数 | 32 | **1** |
| PRDTエントリ数/コマンド | 65535 | **2048** |
| 転送サイズ/PRDT | 4 MB | **128 KB** |
| 割り込み | 対応 | **未使用** |

---

## アーキテクチャ

### コマンドスロット配置

```
Command List (1KB, 32 entries × 32 bytes)
┌────────────────────────────────────┐
│ Slot 0  ✅ [ACTIVE]                │ ← 本実装で使用
│   - Command Header                 │
│   - Points to: Command Table       │
├────────────────────────────────────┤
│ Slot 1  ⭕ [UNUSED]                │
├────────────────────────────────────┤
│ Slot 2  ⭕ [UNUSED]                │
├────────────────────────────────────┤
│   ...                              │
├────────────────────────────────────┤
│ Slot 31 ⭕ [UNUSED]                │
└────────────────────────────────────┘
```

**理由**:
- シンプルな実装（排他制御不要）
- デバッグが容易
- 教育目的に適している

### メモリレイアウト

```
┌─────────────────────────────────────────┐
│ System Memory (DMA Coherent)           │
├─────────────────────────────────────────┤
│                                         │
│ Command List (1KB)                      │
│   PxCLB → ┌──────────────────┐         │
│           │ Slot 0 Header    │         │
│           └──────────────────┘         │
│                                         │
│ FIS Area (256B)                         │
│   PxFB → ┌──────────────────┐         │
│          │ D2H Register FIS │ ← 結果   │
│          │ PIO Setup FIS    │         │
│          │ DMA Setup FIS    │         │
│          │ Set Device Bits  │         │
│          └──────────────────┘         │
│                                         │
│ Command Table (4KB)                     │
│   CTBA → ┌──────────────────┐         │
│          │ Command FIS      │ 64B     │
│          │ ATAPI Command    │ 16B     │
│          │ Reserved         │ 48B     │
│          │ PRDT Entries     │ 16B×N   │
│          └──────────────────┘         │
│                                         │
│ SG Buffers (128KB × 8~2048)            │
│   ┌─────────────────┐                 │
│   │ SG Buffer [0]   │ 128KB           │
│   ├─────────────────┤                 │
│   │ SG Buffer [1]   │ 128KB           │
│   ├─────────────────┤                 │
│   │      ...        │                 │
│   ├─────────────────┤                 │
│   │ SG Buffer [N]   │ 128KB           │
│   └─────────────────┘                 │
└─────────────────────────────────────────┘
```

---

## データ構造

### 1. Command Request (ユーザー空間 ↔ カーネル空間)

```c
struct ahci_cmd_request {
    /* ===== 入力パラメータ ===== */
    
    __u8  command;          // ATAコマンドコード
                            // 例: 0xEC (IDENTIFY)
                            //     0x25 (READ DMA EXT)
                            //     0x35 (WRITE DMA EXT)
    
    __u8  features;         // Featuresレジスタ (15:0)
                            // 通常は0、特殊コマンドで使用
    
    __u8  device;           // Deviceレジスタ
                            // 通常: 0x40 (LBAモード)
                            // bit 6: LBA mode
                            // bit 4: Device select (0=master)
    
    __u8  reserved1;
    
    __u64 lba;              // 48-bit LBA (Logical Block Address)
                            // 0 ~ 281,474,976,710,655
                            // セクタ単位（512バイト/セクタ）
    
    __u16 count;            // セクタ数 (16-bit)
                            // 0 = 65536セクタ
                            // 最大 32 MB (65536 × 512)
    
    __u16 reserved2;
    
    __u32 flags;            // コマンドフラグ
                            // AHCI_CMD_FLAG_WRITE: WRITE方向
                            // 0: READ方向
    
    __u64 buffer;           // ユーザー空間バッファポインタ
                            // READ: 出力先
                            // WRITE: 入力元
    
    __u32 buffer_len;       // バッファサイズ（バイト単位）
                            // 最大: 256 MB (268,435,456)
                            // = count × 512
    
    __u32 timeout_ms;       // タイムアウト時間（ミリ秒）
                            // ※現在未実装（5秒固定）
    
    /* ===== 出力パラメータ (D2H FISから取得) ===== */
    
    __u8  status;           // Statusレジスタ
                            // bit 7: BSY (Busy)
                            // bit 6: DRDY (Device Ready)
                            // bit 5: DF (Device Fault)
                            // bit 3: DRQ (Data Request)
                            // bit 0: ERR (Error)
                            // 正常: 0x50 (DRDY | DSC)
    
    __u8  error;            // Errorレジスタ
                            // status.ERR=1の場合のみ有効
    
    __u8  device_out;       // Deviceレジスタ（結果）
    
    __u8  reserved3;
    
    __u64 lba_out;          // 完了時のLBA
                            // 正常完了: lba + count
                            // エラー時: エラー発生位置
    
    __u16 count_out;        // 完了時のセクタ数
                            // 通常: 0
                            // エラー時: 残セクタ数
    
    __u16 reserved4;
};
```

### 2. Command Header (AHCI 1.3.1 Section 4.2.2)

```c
struct ahci_cmd_header {
    u16 flags;              // Command flags
                            // bit 15-11: Reserved
                            // bit 10: PMP (Port Multiplier Port)
                            // bit 9: C (Clear Busy upon R_OK)
                            // bit 8: B (BIST)
                            // bit 7: R (Reset)
                            // bit 6: P (Prefetchable)
                            // bit 5: W (Write) ★重要
                            // bit 4-0: CFL (Command FIS Length)
                            //          = FISサイズ/4
                            //          H2D FIS: 20バイト → 5
    
    u16 prdtl;              // PRDT Length
                            // PRDTエントリ数（0~65535）
                            // 本実装: 最大2048
    
    u32 prdbc;              // PRD Byte Count
                            // 転送完了バイト数（HBAが書き込む）
    
    u64 ctba;               // Command Table Base Address
                            // 128バイトアライメント必須
    
    u32 reserved[4];        // Reserved
};
```

### 3. Command Table (AHCI 1.3.1 Section 4.2.3)

```c
struct ahci_cmd_table {
    u8  cfis[64];           // Command FIS
                            // Register H2D FIS: 20バイト
                            // 残り44バイト: Reserved
    
    u8  acmd[16];           // ATAPI Command
                            // 本実装では未使用
    
    u8  reserved[48];       // Reserved
    
    struct ahci_prdt_entry prdt[0];  // PRDT entries
                                     // 可変長配列
};
```

### 4. Register H2D FIS (AHCI 1.3.1 Section 10.5.5)

```c
struct fis_reg_h2d {
    u8  fis_type;           // 0x27 = Register H2D
    
    u8  flags;              // bit 7: C (Command/Control)
                            //        1 = Command register update
                            //        0 = Control register update
                            // bit 3-0: PM Port
    
    u8  command;            // Command register (ATA command)
    u8  features;           // Features register (7:0)
    
    u8  lba_low;            // LBA (7:0)
    u8  lba_mid;            // LBA (15:8)
    u8  lba_high;           // LBA (23:16)
    u8  device;             // Device register
    
    u8  lba_low_exp;        // LBA (31:24)
    u8  lba_mid_exp;        // LBA (39:32)
    u8  lba_high_exp;       // LBA (47:40)
    u8  features_exp;       // Features (15:8)
    
    u8  count;              // Count (7:0)
    u8  count_exp;          // Count (15:8)
    u8  icc;                // Isochronous Command Completion
    u8  control;            // Control register
    
    u8  aux0;               // Auxiliary (7:0)
    u8  aux1;               // Auxiliary (15:8)
    u8  aux2;               // Auxiliary (23:16)
    u8  aux3;               // Auxiliary (31:24)
} __packed;
```

### 5. Register D2H FIS (AHCI 1.3.1 Section 10.5.6)

```c
struct fis_reg_d2h {
    u8  fis_type;           // 0x34 = Register D2H
    
    u8  flags;              // bit 6: I (Interrupt)
                            // bit 3-0: PM Port
    
    u8  status;             // Status register
    u8  error;              // Error register
    
    u8  lba_low;            // LBA (7:0)
    u8  lba_mid;            // LBA (15:8)
    u8  lba_high;           // LBA (23:16)
    u8  device;             // Device register
    
    u8  lba_low_exp;        // LBA (31:24)
    u8  lba_mid_exp;        // LBA (39:32)
    u8  lba_high_exp;       // LBA (47:40)
    u8  reserved1;
    
    u8  count;              // Count (7:0)
    u8  count_exp;          // Count (15:8)
    u16 reserved2;
    
    u32 reserved3;
} __packed;
```

### 6. PRDT Entry (AHCI 1.3.1 Section 4.2.3.3)

```c
struct ahci_prdt_entry {
    u64 dba;                // Data Base Address
                            // 2バイトアライメント必須
                            // 本実装: SGバッファのDMAアドレス
    
    u32 reserved;           // Reserved
    
    u32 dbc;                // Data Byte Count
                            // bit 31: I (Interrupt on Completion)
                            // bit 21-0: Byte count - 1
                            //           0 = 1バイト
                            //           0x1FFFFF = 4 MB
                            // 本実装: 最大 128KB - 1
};
```

---

## コマンド発行フロー

### フローチャート

```
┌─────────────────────────────────────────┐
│ ユーザー空間アプリケーション                │
└─────────────────────────────────────────┘
           │
           │ ioctl(fd, AHCI_IOC_ISSUE_CMD, &req)
           ↓
┌─────────────────────────────────────────┐
│ ahci_lld_main.c: ahci_lld_ioctl()      │
├─────────────────────────────────────────┤
│ 1. copy_from_user(&req)                 │
│ 2. kmalloc(kernel_buffer, req.len)     │
│ 3. if (WRITE) copy_from_user(data)     │
└─────────────────────────────────────────┘
           │
           │ ahci_port_issue_cmd(port, &req, kernel_buffer)
           ↓
┌─────────────────────────────────────────┐
│ ahci_lld_cmd.c: ahci_port_issue_cmd()  │
├─────────────────────────────────────────┤
│ ■ Phase 1: 準備                         │
│   - ポート状態確認 (PxCMD.ST == 1?)    │
│   - 転送方向判定 (READ/WRITE)          │
│                                         │
│ ■ Phase 2: Command Header構築          │
│   - Slot 0のヘッダをクリア              │
│   - flags: CFL=5, W=(write?)           │
│   - prdtl: PRDTエントリ数               │
│   - ctba: Command TableのDMAアドレス    │
│                                         │
│ ■ Phase 3: Register H2D FIS構築        │
│   - fis_type = 0x27                     │
│   - flags = 0x80 (Command bit)          │
│   - command, device, features          │
│   - LBA (48-bit分割)                    │
│   - count (16-bit分割)                  │
│                                         │
│ ■ Phase 4: Scatter-Gather準備          │
│   - sg_needed = (len + 128K-1) / 128K   │
│   - ahci_port_ensure_sg_buffers(n)     │
│   - if (WRITE):                         │
│       memcpy(kernel_buf → SG[0..n])    │
│                                         │
│ ■ Phase 5: PRDT構築                    │
│   - for each SG buffer:                │
│       prdt[i].dba = sg_buffers_dma[i]  │
│       prdt[i].dbc = chunk_size - 1     │
│   - cmd_hdr->prdtl = n                 │
│                                         │
│ ■ Phase 6: コマンド発行                │
│   - iowrite32(0xFFFFFFFF, PxIS)        │
│   - iowrite32(0x1, PxCI)  ← bit 0 set │
│                                         │
│ ■ Phase 7: 完了待機（ポーリング）       │
│   - timeout = req.timeout_ms (def:5s)  │
│   - while (timeout > 0):                │
│       ci = ioread32(PxCI)               │
│       if (!(ci & 0x1)) break            │
│       msleep(1)                         │
│       timeout--                         │
│                                         │
│ ■ Phase 8: 結果取得                    │
│   - is = ioread32(PxIS)                 │
│   - Check error bits (TFES, HBFS, ..)  │
│   - d2h_fis = fis_area->rfis           │
│   - Extract: status, error, lba, count │
│                                         │
│ ■ Phase 9: データ転送（READ時）        │
│   - if (READ):                          │
│       memcpy(SG[0..n] → kernel_buf)    │
│                                         │
│ ■ Phase 10: 後処理                     │
│   - iowrite32(is, PxIS)  ← Clear IS    │
│   - return 0 or error                   │
└─────────────────────────────────────────┘
           │
           │ return to ioctl handler
           ↓
┌─────────────────────────────────────────┐
│ ahci_lld_main.c (continued)             │
├─────────────────────────────────────────┤
│ 4. if (READ) copy_to_user(data)        │
│ 5. copy_to_user(&req)  ← 結果フィールド │
│ 6. kfree(kernel_buffer)                 │
└─────────────────────────────────────────┘
           │
           │ return to user space
           ↓
┌─────────────────────────────────────────┐
│ ユーザー空間アプリケーション                │
├─────────────────────────────────────────┤
│ - Check req.status                      │
│ - Check req.error                       │
│ - Use transferred data                  │
└─────────────────────────────────────────┘
```

### タイミングチャート

```
時刻    動作                                  レジスタ状態
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
t=0     PxISクリア                            PxIS = 0x00000000
        iowrite32(0xFFFFFFFF, PxIS)          PxCI = 0x00000000

t=1     コマンド発行                          PxIS = 0x00000000
        iowrite32(0x1, PxCI)                 PxCI = 0x00000001
                                             PxTFD = 0x00000080 (BSY)

t=2~N   HBAがコマンド処理中                   PxCI = 0x00000001
        (デバイスとのやり取り)                PxTFD = 0x00000080 (BSY)
        ...ポーリング中...
        
        ├─ HBA: Command Headerを読む
        ├─ HBA: Command FISをデバイスに送信
        ├─ Device: コマンド処理
        ├─ Device/HBA: DMA転送
        └─ Device: D2H FISを返送

t=N     コマンド完了                          PxIS = 0x00000001 (DHRS)
        HBAがPxCIをクリア                    PxCI = 0x00000000 ★
        HBAがPxISをセット                     PxTFD = 0x00000050 (DRDY)

t=N+1   ソフトウェアが検出                    
        PxCI==0を確認
        
t=N+2   D2H FIS読み取り
        status, error, lba, count取得
        
t=N+3   PxISクリア                            PxIS = 0x00000000
        iowrite32(is, PxIS)
```

---

## Scatter-Gather DMA

### 基本概念

大容量データを小さなバッファに分割して転送する仕組み。

**メリット**:
- 連続した物理メモリ不要
- 大容量転送が可能
- メモリ断片化に強い

**デメリット**:
- PRDTエントリ数が増える
- memcpy回数が増える（パフォーマンス低下）

### SGバッファ設定

```c
#define AHCI_SG_BUFFER_SIZE     (128 * 1024)    // 128 KB
#define AHCI_SG_BUFFER_COUNT    2048            // 最大2048個
#define AHCI_MAX_TRANSFER_SIZE  (AHCI_SG_BUFFER_SIZE * AHCI_SG_BUFFER_COUNT)
                                                // = 256 MB
```

### 動的割り当て戦略

```
初期状態: 8個 (1 MB)
  ├─ SG[0]: 128 KB
  ├─ SG[1]: 128 KB
  ├─  ...
  └─ SG[7]: 128 KB

転送要求: 10 MB
  ↓
必要数計算: ceil(10MB / 128KB) = 80個
  ↓
不足分確保: 80 - 8 = 72個を追加割り当て
  ↓
転送実行: 80個のSGバッファ使用
  ↓
次回転送でも80個利用可能（再割り当て不要）
```

### データ分割例（1 MB転送）

```
User Buffer (連続1MB)
┌────────────────────────────────────────┐
│ 0x00000 ~ 0xFFFFF (1,048,576 bytes)   │
└────────────────────────────────────────┘
           │ split
           ↓
┌──────────────┐  ┌──────────────┐
│ SG Buffer[0] │  │ PRDT Entry 0 │
│ 128 KB       │→ │ DBA: DMA[0]  │
│ 0x00000-1FFFF│  │ DBC: 131071  │
└──────────────┘  └──────────────┘

┌──────────────┐  ┌──────────────┐
│ SG Buffer[1] │  │ PRDT Entry 1 │
│ 128 KB       │→ │ DBA: DMA[1]  │
│ 0x20000-3FFFF│  │ DBC: 131071  │
└──────────────┘  └──────────────┘

       ...              ...

┌──────────────┐  ┌──────────────┐
│ SG Buffer[7] │  │ PRDT Entry 7 │
│ 128 KB       │→ │ DBA: DMA[7]  │
│ 0xE0000-FFFFF│  │ DBC: 131071  │
└──────────────┘  └──────────────┘

Total: 8 PRDT entries
```

### PRDT構築コード

```c
int i, prdt_count = 0;
u32 remaining = req->buffer_len;
struct ahci_prdt_entry *prdt = cmd_tbl->prdt;

for (i = 0; i < sg_needed && remaining > 0; i++) {
    u32 chunk = (remaining > AHCI_SG_BUFFER_SIZE) 
                ? AHCI_SG_BUFFER_SIZE 
                : remaining;
    
    prdt[i].dba = port->sg_buffers_dma[i];  // DMAアドレス
    prdt[i].dbc = chunk - 1;                // サイズ - 1 (0-based)
    prdt[i].reserved = 0;
    
    remaining -= chunk;
    prdt_count++;
}

cmd_hdr->prdtl = prdt_count;
```

---

## エラーハンドリング

### エラー検出

コマンド完了後、PxISレジスタのエラービットをチェック：

```c
u32 is = ioread32(port_mmio + AHCI_PORT_IS);

// エラービット判定
if (is & (AHCI_PORT_INT_TFES |   // Task File Error
          AHCI_PORT_INT_HBFS |   // Host Bus Fatal Error
          AHCI_PORT_INT_HBDS |   // Host Bus Data Error
          AHCI_PORT_INT_IFS)) {  // Interface Fatal Error
    
    // エラー情報取得
    u32 tfd = ioread32(port_mmio + AHCI_PORT_TFD);
    u32 serr = ioread32(port_mmio + AHCI_PORT_SERR);
    
    dev_err(port->device, 
            "Command error: PxIS=0x%08x PxTFD=0x%08x PxSERR=0x%08x\n",
            is, tfd, serr);
    
    // エラービットクリア
    iowrite32(is, port_mmio + AHCI_PORT_IS);
    iowrite32(serr, port_mmio + AHCI_PORT_SERR);
    
    return -EIO;
}
```

### エラー種類

| ビット | 名称 | 意味 | 原因 |
|-------|------|------|------|
| PxIS[30] | TFES | Task File Error | ATAコマンドエラー |
| PxIS[29] | HBFS | Host Bus Fatal Error | PCIバスエラー |
| PxIS[28] | HBDS | Host Bus Data Error | DMA転送エラー |
| PxIS[27] | IFS | Interface Fatal Error | SATA通信エラー |

### D2H FISエラー情報

```c
struct fis_reg_d2h *d2h_fis = &fis_area->rfis;

req->status = d2h_fis->status;  // 0x51 = ERR bit set
req->error = d2h_fis->error;    // 詳細エラーコード

// ATA Error Register (ATA8-ACS)
// bit 7: BBK (Bad Block)
// bit 6: UNC (Uncorrectable Error)
// bit 5: MC (Media Changed)
// bit 4: IDNF (ID Not Found)
// bit 3: MCR (Media Change Request)
// bit 2: ABRT (Aborted Command)
// bit 1: TK0NF (Track 0 Not Found)
// bit 0: AMNF (Address Mark Not Found)
```

### タイムアウトハンドリング

```c
int timeout = req->timeout_ms > 0 ? req->timeout_ms : 5000;

while (timeout > 0) {
    u32 ci = ioread32(port_mmio + AHCI_PORT_CI);
    
    if (!(ci & 0x1)) {
        // 完了
        break;
    }
    
    msleep(1);
    timeout--;
}

if (timeout <= 0) {
    dev_err(port->device, 
            "Command timeout (PxCI=0x%08x PxIS=0x%08x)\n",
            ioread32(port_mmio + AHCI_PORT_CI),
            ioread32(port_mmio + AHCI_PORT_IS));
    return -ETIMEDOUT;
}
```

---

## パフォーマンス特性

### 実測値

**テスト環境**:
- CPU: Intel Core (Cannon Lake PCH)
- HDD: WDC WD5000AZLX-08K2TA0 (500GB, SATA Gen3)
- Kernel: Linux 6.14.0

| 操作 | サイズ | 時間 | スループット | PRDT数 |
|-----|--------|------|-------------|--------|
| IDENTIFY | 512 B | ~5 ms | - | 1 |
| READ | 512 B | ~5 ms | 0.1 MB/s | 1 |
| READ | 1 MB | ~5 ms | ~200 MB/s | 8 |
| WRITE | 1 MB | ~47 ms | ~22 MB/s | 8 |

### ボトルネック分析

```
コマンド発行オーバーヘッド内訳:

1. copy_from_user (WRITE時)        ~0.1 ms
2. SGバッファへの分割コピー         ~0.5 ms
3. Command/FIS/PRDT構築            ~0.1 ms
4. コマンド発行                     ~0.01 ms
5. HBA/デバイス処理時間            ~4-45 ms  ← 主なボトルネック
6. SGバッファからの結合コピー (READ) ~0.3 ms
7. copy_to_user (READ時)           ~0.1 ms
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Total:                             ~5-47 ms
```

**改善の余地**:
- ✅ **割り込み使用**: ポーリングのCPU使用率を削減
- ✅ **ゼロコピー**: ユーザーバッファを直接DMA（mmap使用）
- ✅ **複数スロット**: 同時コマンド発行（NCQ）
- ⚠️ **SSD使用**: HDD → SSD で劇的に高速化

---

## 制限事項と今後の拡張

### 現在の制限事項

| 項目 | 制限 | 理由 |
|-----|------|------|
| **コマンドスロット** | 1個のみ | シンプル実装、排他制御不要 |
| **NCQ** | 未対応 | 複数スロット未実装のため |
| **割り込み** | 未使用 | ポーリングのみ |
| **ATAPI** | 未対応 | CD/DVDドライブ未サポート |
| **Port Multiplier** | 未対応 | 複数デバイス未サポート |
| **48-bit LBA範囲** | 全範囲OK | ✅ 対応済み |
| **最大転送サイズ** | 256 MB | SGバッファ上限 |

### 拡張案

#### 1. 割り込み対応

**メリット**:
- CPU使用率削減
- レイテンシ改善
- 電力効率向上

**実装概要**:
```c
// 割り込みハンドラ登録
ret = request_irq(pdev->irq, ahci_interrupt, IRQF_SHARED, 
                  DRIVER_NAME, hba);

// 割り込みハンドラ
static irqreturn_t ahci_interrupt(int irq, void *dev_id)
{
    struct ahci_hba *hba = dev_id;
    u32 is = ioread32(hba->mmio + AHCI_IS);
    
    // 各ポートの割り込みを処理
    for (i = 0; i < hba->n_ports; i++) {
        if (is & (1 << i)) {
            ahci_handle_port_interrupt(hba->ports[i]);
        }
    }
    
    return IRQ_HANDLED;
}

// wait_queueで待機
wait_event_timeout(port->cmd_wait, 
                   !(ioread32(port_mmio + AHCI_PORT_CI) & 0x1),
                   msecs_to_jiffies(timeout_ms));
```

#### 2. 複数スロット対応（NCQ）

**メリット**:
- 並列実行によるスループット向上
- ディスクの内部最適化
- レイテンシ削減

**実装概要**:
```c
// スロット管理
struct ahci_port_device {
    ...
    unsigned long slots_in_use;     // ビットマップ
    spinlock_t slot_lock;           // スロット排他制御
    struct ahci_slot {
        struct completion done;     // 完了待ち
        int status;                 // 結果
    } slots[32];
};

// スロット割り当て
int slot = find_first_zero_bit(&port->slots_in_use, 32);
set_bit(slot, &port->slots_in_use);

// コマンド発行
iowrite32(1 << slot, port_mmio + AHCI_PORT_CI);

// 完了待ち
wait_for_completion_timeout(&port->slots[slot].done, timeout);
```

#### 3. ゼロコピーDMA

**メリット**:
- memcpyオーバーヘッド削減
- CPU使用率削減
- 大容量転送の高速化

**実装概要**:
```c
// ユーザーバッファをピン留め
struct page **pages;
ret = get_user_pages_fast(buffer, nr_pages, FOLL_WRITE, pages);

// PRDTにユーザーページのDMAアドレスを直接設定
for (i = 0; i < nr_pages; i++) {
    dma_addr_t dma = dma_map_page(dev, pages[i], ...);
    prdt[i].dba = dma;
    prdt[i].dbc = PAGE_SIZE - 1;
}
```

#### 4. 非同期I/O対応

**メリット**:
- アプリケーションのブロッキング削減
- 複数I/Oの並行実行

**実装概要**:
```c
// io_uring / libaio対応
static long ahci_lld_ioctl(struct file *file, unsigned int cmd, ...)
{
    case AHCI_IOC_ISSUE_CMD_ASYNC:
        // 非同期コマンド発行
        ret = ahci_port_issue_cmd_async(port, &req, buffer);
        // すぐに戻る
        return -EIOCBQUEUED;
    
    case AHCI_IOC_POLL_COMPLETION:
        // 完了確認
        return ahci_port_poll_completion(port, &result);
}
```

---

## 付録

### ATAコマンド一覧

| コマンド | コード | 方向 | 説明 |
|---------|-------|------|------|
| IDENTIFY DEVICE | 0xEC | READ | デバイス情報取得 (512B) |
| READ SECTORS EXT | 0x24 | READ | PIO読み込み (48-bit) |
| WRITE SECTORS EXT | 0x34 | WRITE | PIO書き込み (48-bit) |
| READ DMA EXT | 0x25 | READ | DMA読み込み (48-bit) ✅ |
| WRITE DMA EXT | 0x35 | WRITE | DMA書き込み (48-bit) ✅ |
| READ FPDMA QUEUED | 0x60 | READ | NCQ読み込み ❌ |
| WRITE FPDMA QUEUED | 0x61 | WRITE | NCQ書き込み ❌ |
| FLUSH CACHE EXT | 0xEA | - | キャッシュフラッシュ |

### 参考資料

- **AHCI Specification 1.3.1** - Intel (2011)
  - Section 3: Register Definitions
  - Section 4: System Memory Structures
  - Section 5: Command List and Command Tables
  - Section 10: Software Guidelines

- **Serial ATA Specification Rev 3.5** - SATA-IO
  - Section 10: Link Layer
  - Section 11: Transport Layer

- **ATA8-ACS** - INCITS (T13)
  - ATA Command Set
  - Register Definitions

---

**最終更新**: 2025-12-20  
**バージョン**: 1.0
