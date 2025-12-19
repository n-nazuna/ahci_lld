# NCQ (Native Command Queuing) 実装仕様書

AHCI Low Level Driver にNCQ機能を追加するための設計仕様書です。

---

## 目次

1. [NCQとは](#ncqとは)
2. [設計方針](#設計方針)
3. [必要な変更](#必要な変更)
4. [データ構造](#データ構造)
5. [コマンド発行フロー](#コマンド発行フロー)
6. [スロット管理](#スロット管理)
7. [割り込み処理](#割り込み処理)
8. [エラーハンドリング](#エラーハンドリング)
9. [実装ステップ](#実装ステップ)
10. [テスト計画](#テスト計画)

---

## NCQとは

### Native Command Queuing (NCQ) の概要

**NCQ** は、SATA 2.0で導入された機能で、複数のコマンドを同時にキューイングし、ディスクが最適な順序で実行できるようにする仕組みです。

### メリット

| 項目 | 非NCQ | NCQ |
|-----|-------|-----|
| **同時コマンド数** | 1個 | 最大32個 |
| **実行順序** | 発行順 | ディスク最適化 |
| **レイテンシ** | 高い | 低い |
| **スループット** | 低い | 高い |
| **シークタイム最適化** | なし | あり |

### AHCI/SATA仕様

- **AHCI 1.3.1**: Section 8 - Native Command Queuing
- **SATA 3.x**: Section 13 - Native Command Queuing
- **ATA8-ACS**: FPDMA (First-Party DMA) commands

### NCQの動作原理

```
通常のDMAコマンド:
  App → Cmd0 (100ms) → Cmd1 (100ms) → Cmd2 (100ms)
  Total: 300ms (逐次実行)

NCQコマンド:
  App → Cmd0 ┐
  App → Cmd1 ├→ Disk が最適順序で実行
  App → Cmd2 ┘    (シーク距離最小化)
  Total: 150ms (並列化 + 最適化)
```

---

## 設計方針

### 実装アプローチ

**段階的実装** を推奨します：

#### Phase 1: 基礎実装（最小限のNCQ）
- ✅ 複数スロット対応（2~4個程度）
- ✅ スロット管理機構
- ✅ READ/WRITE FPDMA QUEUEDコマンド対応
- ⚠️ ポーリングモード維持（割り込み未使用）

#### Phase 2: 割り込み対応
- ✅ 割り込みハンドラ実装
- ✅ wait_queue による待機
- ✅ 複数コマンド並行完了処理

#### Phase 3: 最適化
- ✅ 32スロット対応
- ✅ エラーリカバリ強化
- ✅ パフォーマンスチューニング

### 設計原則

1. **後方互換性**: 既存の非NCQコマンドも引き続きサポート
2. **段階的移行**: 段階的にNCQ機能を追加
3. **エラーハンドリング**: NCQ特有のエラー処理を実装
4. **性能測定**: ベンチマークで効果を検証

---

## 必要な変更

### 1. データ構造の拡張

#### ahci_port_device 構造体

```c
struct ahci_port_device {
    /* 既存フィールド */
    struct cdev cdev;
    struct device *device;
    dev_t devno;
    int port_no;
    void __iomem *port_mmio;
    struct ahci_hba *hba;
    
    /* DMA buffers (既存) */
    void *cmd_list;
    dma_addr_t cmd_list_dma;
    void *fis_area;
    dma_addr_t fis_area_dma;
    void *cmd_table;              // ❌ スロットごとに必要
    dma_addr_t cmd_table_dma;
    
    void *sg_buffers[AHCI_SG_BUFFER_COUNT];
    dma_addr_t sg_buffers_dma[AHCI_SG_BUFFER_COUNT];
    int sg_buffer_count;
    struct mutex sg_lock;
    
    /* ===== NCQ対応: 新規追加 ===== */
    
    /* Command Tables (32 slots) */
    void *cmd_tables[32];                  // スロットごとのCommand Table
    dma_addr_t cmd_tables_dma[32];
    
    /* Slot management */
    unsigned long slots_in_use;            // ビットマップ (32 bits)
    spinlock_t slot_lock;                  // スロット排他制御
    
    /* Per-slot information */
    struct ahci_cmd_slot {
        struct ahci_cmd_request *req;     // コマンド情報
        void *buffer;                      // カーネルバッファ
        u32 buffer_len;
        bool is_write;
        
        /* 完了待ち */
        struct completion done;            // 完了通知
        int result;                        // 結果コード
        
        /* タイムアウト */
        struct timer_list timeout_timer;   // タイムアウトタイマー
        unsigned long timeout_jiffies;
        
        /* SG buffer allocation */
        int sg_start_idx;                  // 使用開始SGバッファインデックス
        int sg_count;                      // 使用SGバッファ数
    } slots[32];
    
    /* NCQ capability */
    bool ncq_enabled;                      // NCQ有効/無効
    int ncq_depth;                         // NCQキュー深度 (1~32)
    
    /* Statistics */
    atomic_t active_slots;                 // アクティブスロット数
    u64 ncq_issued;                        // NCQコマンド発行数
    u64 ncq_completed;                     // NCQコマンド完了数
};
```

#### Command Request 拡張

```c
struct ahci_cmd_request {
    /* 既存フィールド */
    __u8 command;
    __u8 features;          // NCQでは tag を含む
    __u8 device;
    __u8 reserved1;
    __u64 lba;
    __u16 count;
    __u16 reserved2;
    __u32 flags;
    __u64 buffer;
    __u32 buffer_len;
    __u32 timeout_ms;
    
    /* 出力 */
    __u8 status;
    __u8 error;
    __u8 device_out;
    __u8 reserved3;
    __u64 lba_out;
    __u16 count_out;
    __u16 reserved4;
    
    /* ===== NCQ対応: 新規追加 ===== */
    __u8 tag;               // NCQタグ (0~31)、非NCQは0xFF
    __u8 ncq_flags;         // NCQ固有フラグ
    __u16 reserved5;
};

/* NCQフラグ */
#define AHCI_NCQ_FLAG_ASYNC     (1 << 0)  // 非同期実行（すぐ戻る）
#define AHCI_NCQ_FLAG_PRIORITY  (1 << 1)  // 高優先度
```

### 2. 新規IOCTL

```c
/* NCQ対応IOCTL */
#define AHCI_IOC_ISSUE_NCQ_CMD   _IOWR(AHCI_LLD_IOC_MAGIC, 11, struct ahci_cmd_request)
#define AHCI_IOC_WAIT_NCQ        _IOWR(AHCI_LLD_IOC_MAGIC, 12, struct ahci_ncq_wait)
#define AHCI_IOC_CANCEL_NCQ      _IOW(AHCI_LLD_IOC_MAGIC, 13, __u8)  // tag
#define AHCI_IOC_GET_NCQ_STATUS  _IOR(AHCI_LLD_IOC_MAGIC, 14, struct ahci_ncq_status)

struct ahci_ncq_wait {
    __u8 tag;               // 待つタグ (0xFF = any)
    __u32 timeout_ms;       // タイムアウト
    __u8 status;            // 結果 status
    __u8 error;             // 結果 error
};

struct ahci_ncq_status {
    __u32 active_slots;     // アクティブスロットのビットマップ
    __u32 completed_slots;  // 完了スロットのビットマップ
    __u32 error_slots;      // エラースロットのビットマップ
};
```

### 3. 割り込み対応

```c
/* HBA構造体に追加 */
struct ahci_hba {
    /* 既存フィールド */
    struct pci_dev *pdev;
    void __iomem *mmio;
    size_t mmio_size;
    u32 ports_impl;
    int n_ports;
    struct ahci_port_device *ports[AHCI_MAX_PORTS];
    struct ahci_ghc_device *ghc_dev;
    dev_t dev_base;
    struct class *class;
    
    /* ===== NCQ対応: 新規追加 ===== */
    int irq;                        // IRQ番号
    bool irq_enabled;               // 割り込み有効/無効
    spinlock_t irq_lock;            // 割り込みハンドラ用ロック
    
    /* Interrupt statistics */
    u64 irq_count;                  // 割り込み回数
    u64 spurious_irq_count;         // 不正割り込み回数
};
```

---

## データ構造

### NCQ Command FIS (READ/WRITE FPDMA QUEUED)

```c
struct fis_ncq_command {
    u8  fis_type;           // 0x27 (Register H2D)
    u8  flags;              // bit 7: C=1
    u8  command;            // 0x60 (READ) or 0x61 (WRITE)
    u8  features_low;       // Sector count (7:0)
    
    u8  lba_0;              // LBA (7:0)
    u8  lba_1;              // LBA (15:8)
    u8  lba_2;              // LBA (23:16)
    u8  device;             // 0x40 (LBA mode)
    
    u8  lba_3;              // LBA (31:24)
    u8  lba_4;              // LBA (39:32)
    u8  lba_5;              // LBA (47:40)
    u8  features_high;      // Sector count (15:8)
    
    u8  count_low;          // Tag (4:0) << 3
    u8  count_high;         // Priority (7:6)
    u8  icc;                // 0
    u8  control;            // 0
    
    u32 aux;                // 0
} __packed;

/* NCQコマンドコード */
#define ATA_CMD_READ_FPDMA_QUEUED   0x60
#define ATA_CMD_WRITE_FPDMA_QUEUED  0x61

/* Featuresフィールド = Sector count */
/* Countフィールド = Tag << 3 | Priority << 6 */
```

### Set Device Bits FIS (NCQ完了通知)

```c
struct fis_set_device_bits {
    u8  fis_type;           // 0xA1 (Set Device Bits)
    u8  flags;              // bit 6: I (Interrupt)
                            // bit 4: N (Notification)
    u8  status;             // Status register
    u8  error;              // Error register
    
    u32 sactive;            // SActive (completed tags)
} __packed;
```

---

## コマンド発行フロー

### NCQコマンド発行（非同期）

```
┌─────────────────────────────────────────┐
│ User Application                        │
└─────────────────────────────────────────┘
           │
           │ ioctl(AHCI_IOC_ISSUE_NCQ_CMD, &req)
           ↓
┌─────────────────────────────────────────┐
│ ahci_lld_ioctl()                        │
├─────────────────────────────────────────┤
│ 1. copy_from_user(&req)                 │
│ 2. スロット割り当て                       │
│    slot = ahci_alloc_slot(port)         │
│    if (slot < 0) return -EBUSY          │
│                                         │
│ 3. バッファ準備                          │
│    buffer = kmalloc(req.buffer_len)     │
│    if (WRITE) copy_from_user()          │
│                                         │
│ 4. スロット情報設定                       │
│    port->slots[slot].req = &req         │
│    port->slots[slot].buffer = buffer    │
│    init_completion(&slots[slot].done)   │
│                                         │
│ 5. NCQコマンド発行                       │
│    ahci_issue_ncq_cmd(port, slot, &req) │
│                                         │
│ 6. 非同期フラグ確認                       │
│    if (req.ncq_flags & ASYNC) {         │
│        req.tag = slot                   │
│        copy_to_user(&req)               │
│        return 0  // すぐ戻る              │
│    }                                    │
│                                         │
│ 7. 同期待機                              │
│    ret = wait_for_completion_timeout(   │
│        &slots[slot].done, timeout)      │
│                                         │
│ 8. 結果コピー                            │
│    if (READ) copy_to_user()             │
│    copy_to_user(&req)                   │
│    kfree(buffer)                        │
│    ahci_free_slot(port, slot)           │
└─────────────────────────────────────────┘
```

### ahci_issue_ncq_cmd() 詳細

```c
int ahci_issue_ncq_cmd(struct ahci_port_device *port, 
                       int slot, 
                       struct ahci_cmd_request *req)
{
    struct ahci_cmd_header *cmd_hdr;
    struct ahci_cmd_table *cmd_tbl;
    struct fis_ncq_command *fis;
    void __iomem *port_mmio = port->port_mmio;
    
    /* Step 1: Command Header設定 */
    cmd_hdr = &((struct ahci_cmd_header *)port->cmd_list)[slot];
    memset(cmd_hdr, 0, sizeof(*cmd_hdr));
    
    cmd_hdr->flags = ahci_calc_cfl(sizeof(struct fis_ncq_command));
    if (req->flags & AHCI_CMD_FLAG_WRITE)
        cmd_hdr->flags |= AHCI_CMD_WRITE;
    
    cmd_hdr->prdtl = calculate_prdt_count(req->buffer_len);
    cmd_hdr->ctba = port->cmd_tables_dma[slot];
    
    /* Step 2: Command Table設定 */
    cmd_tbl = port->cmd_tables[slot];
    memset(cmd_tbl, 0, sizeof(*cmd_tbl));
    
    /* Step 3: NCQ FIS構築 */
    fis = (struct fis_ncq_command *)cmd_tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->flags = FIS_H2D_FLAG_CMD;
    
    // NCQコマンドコード
    if (req->flags & AHCI_CMD_FLAG_WRITE)
        fis->command = ATA_CMD_WRITE_FPDMA_QUEUED;
    else
        fis->command = ATA_CMD_READ_FPDMA_QUEUED;
    
    // FeaturesフィールドにSector Countを設定
    fis->features_low = req->count & 0xFF;
    fis->features_high = (req->count >> 8) & 0xFF;
    
    // LBA設定
    fis->lba_0 = req->lba & 0xFF;
    fis->lba_1 = (req->lba >> 8) & 0xFF;
    fis->lba_2 = (req->lba >> 16) & 0xFF;
    fis->lba_3 = (req->lba >> 24) & 0xFF;
    fis->lba_4 = (req->lba >> 32) & 0xFF;
    fis->lba_5 = (req->lba >> 40) & 0xFF;
    
    fis->device = 0x40;  // LBA mode
    
    // CountフィールドにTag (bit 7-3)を設定
    fis->count_low = (slot & 0x1F) << 3;
    fis->count_high = 0;  // Priority: normal
    
    fis->icc = 0;
    fis->control = 0;
    fis->aux = 0;
    
    /* Step 4: PRDT構築（Scatter-Gather） */
    ret = ahci_build_prdt(port, slot, req);
    if (ret)
        return ret;
    
    /* Step 5: PxSACT設定（NCQ特有） */
    iowrite32(1 << slot, port_mmio + AHCI_PORT_SACT);
    
    /* Step 6: PxCI設定（コマンド発行） */
    iowrite32(1 << slot, port_mmio + AHCI_PORT_CI);
    
    dev_info(port->device, 
             "NCQ command issued: slot=%d cmd=0x%02x lba=0x%llx count=%u\n",
             slot, fis->command, req->lba, req->count);
    
    return 0;
}
```

---

## スロット管理

### スロット割り当て

```c
/**
 * ahci_alloc_slot - 空きスロットを割り当て
 * @port: ポートデバイス
 * 
 * Return: スロット番号(0~31), または -EBUSY
 */
int ahci_alloc_slot(struct ahci_port_device *port)
{
    unsigned long flags;
    int slot;
    
    spin_lock_irqsave(&port->slot_lock, flags);
    
    /* NCQ無効なら slot 0 のみ */
    if (!port->ncq_enabled) {
        if (port->slots_in_use & 0x1) {
            spin_unlock_irqrestore(&port->slot_lock, flags);
            return -EBUSY;
        }
        slot = 0;
    } else {
        /* 空きスロットを検索 */
        slot = find_first_zero_bit(&port->slots_in_use, port->ncq_depth);
        if (slot >= port->ncq_depth) {
            spin_unlock_irqrestore(&port->slot_lock, flags);
            return -EBUSY;
        }
    }
    
    /* スロットをマーク */
    set_bit(slot, &port->slots_in_use);
    atomic_inc(&port->active_slots);
    
    spin_unlock_irqrestore(&port->slot_lock, flags);
    
    dev_dbg(port->device, "Allocated slot %d\n", slot);
    return slot;
}

/**
 * ahci_free_slot - スロットを解放
 * @port: ポートデバイス
 * @slot: スロット番号
 */
void ahci_free_slot(struct ahci_port_device *port, int slot)
{
    unsigned long flags;
    
    if (slot < 0 || slot >= 32)
        return;
    
    spin_lock_irqsave(&port->slot_lock, flags);
    
    clear_bit(slot, &port->slots_in_use);
    atomic_dec(&port->active_slots);
    
    /* スロット情報クリア */
    port->slots[slot].req = NULL;
    port->slots[slot].buffer = NULL;
    port->slots[slot].result = 0;
    
    spin_unlock_irqrestore(&port->slot_lock, flags);
    
    dev_dbg(port->device, "Freed slot %d\n", slot);
}
```

### SGバッファ割り当て（スロット対応）

```c
/**
 * ahci_alloc_sg_for_slot - スロット用のSGバッファを割り当て
 * @port: ポートデバイス
 * @slot: スロット番号
 * @size: 必要なサイズ
 * 
 * 各スロットに専用のSGバッファ範囲を割り当てる
 */
int ahci_alloc_sg_for_slot(struct ahci_port_device *port, 
                            int slot, u32 size)
{
    int sg_needed = (size + AHCI_SG_BUFFER_SIZE - 1) / AHCI_SG_BUFFER_SIZE;
    int sg_start;
    
    mutex_lock(&port->sg_lock);
    
    /* 連続した空きSGバッファを検索 */
    sg_start = find_contiguous_sg_buffers(port, sg_needed);
    if (sg_start < 0) {
        /* 空きがない場合は拡張 */
        ret = expand_sg_buffers(port, sg_needed);
        if (ret) {
            mutex_unlock(&port->sg_lock);
            return ret;
        }
        sg_start = find_contiguous_sg_buffers(port, sg_needed);
    }
    
    /* スロットにSG範囲を記録 */
    port->slots[slot].sg_start_idx = sg_start;
    port->slots[slot].sg_count = sg_needed;
    
    /* SGバッファを使用中にマーク */
    mark_sg_buffers_in_use(port, sg_start, sg_needed, slot);
    
    mutex_unlock(&port->sg_lock);
    
    return 0;
}
```

---

## 割り込み処理

### 割り込みハンドラ

```c
/**
 * ahci_interrupt - AHCI割り込みハンドラ
 * @irq: IRQ番号
 * @dev_id: HBA構造体
 */
static irqreturn_t ahci_interrupt(int irq, void *dev_id)
{
    struct ahci_hba *hba = dev_id;
    void __iomem *mmio = hba->mmio;
    u32 irq_stat, port_irq_stat;
    int i, handled = 0;
    unsigned long flags;
    
    spin_lock_irqsave(&hba->irq_lock, flags);
    
    /* Global Interrupt Status (GHC.IS) */
    irq_stat = ioread32(mmio + AHCI_IS);
    if (!irq_stat) {
        spin_unlock_irqrestore(&hba->irq_lock, flags);
        hba->spurious_irq_count++;
        return IRQ_NONE;
    }
    
    hba->irq_count++;
    
    /* 各ポートの割り込みを処理 */
    for (i = 0; i < hba->n_ports; i++) {
        if (!(irq_stat & (1 << i)))
            continue;
        
        if (!hba->ports[i])
            continue;
        
        port_irq_stat = ioread32(hba->ports[i]->port_mmio + AHCI_PORT_IS);
        if (port_irq_stat) {
            ahci_handle_port_interrupt(hba->ports[i], port_irq_stat);
            handled = 1;
        }
    }
    
    /* Global ISクリア */
    iowrite32(irq_stat, mmio + AHCI_IS);
    
    spin_unlock_irqrestore(&hba->irq_lock, flags);
    
    return handled ? IRQ_HANDLED : IRQ_NONE;
}

/**
 * ahci_handle_port_interrupt - ポート割り込み処理
 * @port: ポートデバイス
 * @port_irq_stat: PxIS値
 */
static void ahci_handle_port_interrupt(struct ahci_port_device *port,
                                       u32 port_irq_stat)
{
    void __iomem *port_mmio = port->port_mmio;
    u32 ci, sact;
    int slot;
    
    /* PxCI (Command Issue) と PxSACT (SATA Active) を読む */
    ci = ioread32(port_mmio + AHCI_PORT_CI);
    sact = ioread32(port_mmio + AHCI_PORT_SACT);
    
    /* 完了したスロットを検出 */
    for (slot = 0; slot < 32; slot++) {
        if (!(port->slots_in_use & (1 << slot)))
            continue;
        
        /* NCQ: PxSACT がクリアされた */
        /* 非NCQ: PxCI がクリアされた */
        bool ncq_done = port->ncq_enabled && !(sact & (1 << slot));
        bool normal_done = !port->ncq_enabled && !(ci & (1 << slot));
        
        if (ncq_done || normal_done) {
            ahci_complete_slot(port, slot, port_irq_stat);
        }
    }
    
    /* PxISクリア */
    iowrite32(port_irq_stat, port_mmio + AHCI_PORT_IS);
}

/**
 * ahci_complete_slot - スロット完了処理
 * @port: ポートデバイス
 * @slot: スロット番号
 * @port_irq_stat: PxIS値
 */
static void ahci_complete_slot(struct ahci_port_device *port, 
                               int slot, u32 port_irq_stat)
{
    struct ahci_cmd_slot *cmd_slot = &port->slots[slot];
    struct ahci_fis_area *fis_area = port->fis_area;
    struct fis_reg_d2h *d2h_fis = &fis_area->rfis;
    int result = 0;
    
    /* エラーチェック */
    if (port_irq_stat & (AHCI_PORT_INT_TFES | AHCI_PORT_INT_HBFS | 
                         AHCI_PORT_INT_HBDS | AHCI_PORT_INT_IFS)) {
        dev_err(port->device, "Slot %d error: PxIS=0x%08x\n", 
                slot, port_irq_stat);
        result = -EIO;
    }
    
    /* D2H FISから結果を取得 */
    if (cmd_slot->req) {
        cmd_slot->req->status = d2h_fis->status;
        cmd_slot->req->error = d2h_fis->error;
        cmd_slot->req->device_out = d2h_fis->device;
        
        /* LBA, Count再構築 */
        cmd_slot->req->lba_out = 
            ((u64)d2h_fis->lba_high_exp << 40) |
            ((u64)d2h_fis->lba_mid_exp << 32) |
            ((u64)d2h_fis->lba_low_exp << 24) |
            ((u64)d2h_fis->lba_high << 16) |
            ((u64)d2h_fis->lba_mid << 8) |
            ((u64)d2h_fis->lba_low);
        
        cmd_slot->req->count_out = 
            ((u16)d2h_fis->count_exp << 8) | d2h_fis->count;
    }
    
    /* READ時: SGバッファからユーザーバッファへコピー */
    if (!cmd_slot->is_write && cmd_slot->buffer) {
        ahci_merge_sg_buffers(port, slot, cmd_slot->buffer);
    }
    
    /* タイマーキャンセル */
    del_timer_sync(&cmd_slot->timeout_timer);
    
    /* 結果設定 */
    cmd_slot->result = result;
    
    /* 完了通知 */
    complete(&cmd_slot->done);
    
    /* 統計更新 */
    port->ncq_completed++;
    
    dev_dbg(port->device, "Slot %d completed: status=0x%02x\n",
            slot, d2h_fis->status);
}
```

### 割り込み有効化

```c
/**
 * ahci_enable_interrupts - 割り込みを有効化
 * @hba: HBA構造体
 */
int ahci_enable_interrupts(struct ahci_hba *hba)
{
    void __iomem *mmio = hba->mmio;
    u32 ghc;
    int ret;
    
    /* IRQ登録 */
    ret = request_irq(hba->pdev->irq, ahci_interrupt, 
                      IRQF_SHARED, DRIVER_NAME, hba);
    if (ret) {
        dev_err(&hba->pdev->dev, "Failed to request IRQ %d: %d\n",
                hba->pdev->irq, ret);
        return ret;
    }
    
    hba->irq = hba->pdev->irq;
    hba->irq_enabled = true;
    
    /* GHC.IE (Global Interrupt Enable) を有効化 */
    ghc = ioread32(mmio + AHCI_GHC);
    ghc |= AHCI_GHC_IE;
    iowrite32(ghc, mmio + AHCI_GHC);
    
    /* 各ポートの割り込みを有効化 */
    for (i = 0; i < hba->n_ports; i++) {
        if (!hba->ports[i])
            continue;
        
        ahci_enable_port_interrupts(hba->ports[i]);
    }
    
    dev_info(&hba->pdev->dev, "Interrupts enabled (IRQ %d)\n", hba->irq);
    
    return 0;
}

/**
 * ahci_enable_port_interrupts - ポート割り込みを有効化
 * @port: ポートデバイス
 */
void ahci_enable_port_interrupts(struct ahci_port_device *port)
{
    void __iomem *port_mmio = port->port_mmio;
    u32 ie;
    
    /* PxIE設定 */
    ie = AHCI_PORT_INT_DHRS |    // D2H Register FIS
         AHCI_PORT_INT_SDBS |    // Set Device Bits FIS (NCQ用)
         AHCI_PORT_INT_TFES |    // Task File Error
         AHCI_PORT_INT_HBFS |    // Host Bus Fatal Error
         AHCI_PORT_INT_HBDS |    // Host Bus Data Error
         AHCI_PORT_INT_IFS;      // Interface Fatal Error
    
    iowrite32(ie, port_mmio + AHCI_PORT_IE);
    
    /* PxISクリア */
    iowrite32(0xFFFFFFFF, port_mmio + AHCI_PORT_IS);
}
```

---

## エラーハンドリング

### NCQ固有のエラー処理

NCQでは、エラー発生時に **どのコマンドがエラーか** を特定する必要があります。

```c
/**
 * ahci_handle_ncq_error - NCQエラー処理
 * @port: ポートデバイス
 */
void ahci_handle_ncq_error(struct ahci_port_device *port)
{
    void __iomem *port_mmio = port->port_mmio;
    u32 serr, ci, sact;
    int slot;
    
    /* Step 1: エラー情報取得 */
    serr = ioread32(port_mmio + AHCI_PORT_SERR);
    ci = ioread32(port_mmio + AHCI_PORT_CI);
    sact = ioread32(port_mmio + AHCI_PORT_SACT);
    
    dev_err(port->device, 
            "NCQ Error: PxSERR=0x%08x PxCI=0x%08x PxSACT=0x%08x\n",
            serr, ci, sact);
    
    /* Step 2: Read Log Ext コマンド発行してエラー詳細取得 */
    u8 error_log[512];
    int ret = ahci_read_log_ext(port, 0x10, error_log, 512);
    if (ret == 0) {
        /* NCQ Command Error Log (Log Address 10h) を解析 */
        u8 error_tag = error_log[0] & 0x1F;  // エラーが発生したタグ
        u8 error_status = error_log[2];
        u8 error_code = error_log[3];
        
        dev_err(port->device, 
                "NCQ Error Tag: %d Status: 0x%02x Error: 0x%02x\n",
                error_tag, error_status, error_code);
        
        /* エラーが発生したスロットのみ失敗扱い */
        if (error_tag < 32) {
            port->slots[error_tag].result = -EIO;
            complete(&port->slots[error_tag].done);
        }
    }
    
    /* Step 3: 残りのコマンドを再発行 or キャンセル */
    for (slot = 0; slot < 32; slot++) {
        if (!(port->slots_in_use & (1 << slot)))
            continue;
        
        if (sact & (1 << slot)) {
            /* まだ実行中 → キャンセル */
            port->slots[slot].result = -ECANCELED;
            complete(&port->slots[slot].done);
        }
    }
    
    /* Step 4: ポートをリセット */
    ahci_port_comreset(port);
    ahci_port_start(port);
    
    /* Step 5: PxSERRクリア */
    iowrite32(serr, port_mmio + AHCI_PORT_SERR);
}

/**
 * ahci_read_log_ext - Read Log Extコマンド発行
 * @port: ポートデバイス
 * @log_addr: ログアドレス (0x10 = NCQ Error Log)
 * @buffer: 出力バッファ
 * @len: バッファサイズ
 */
int ahci_read_log_ext(struct ahci_port_device *port, 
                      u8 log_addr, void *buffer, u32 len)
{
    struct ahci_cmd_request req = {0};
    
    req.command = 0x2F;  // READ LOG EXT
    req.device = 0x40;
    req.lba = log_addr;
    req.count = len / 512;
    req.flags = 0;  // READ
    req.buffer_len = len;
    req.timeout_ms = 5000;
    
    return ahci_port_issue_cmd_sync(port, &req, buffer);
}
```

---

## 実装ステップ

### Phase 1: 基礎構造（1週間）

1. ✅ データ構造拡張
   - `ahci_port_device` にスロット管理追加
   - Command Table を32個確保
   
2. ✅ スロット管理機構
   - `ahci_alloc_slot()`
   - `ahci_free_slot()`
   
3. ✅ NCQ FIS構築
   - `ahci_build_ncq_fis()`
   
4. ✅ 基本的なNCQコマンド発行（ポーリング）
   - `ahci_issue_ncq_cmd()`
   - まず2~4スロット対応

### Phase 2: 割り込み対応（1週間）

1. ✅ 割り込みハンドラ実装
   - `ahci_interrupt()`
   - `ahci_handle_port_interrupt()`
   
2. ✅ wait_queue/completion使用
   - ポーリングから割り込み待機へ移行
   
3. ✅ 複数スロット並行完了処理

### Phase 3: 完全実装（1週間）

1. ✅ 32スロット対応
2. ✅ NCQエラーハンドリング
3. ✅ Read Log Ext実装
4. ✅ 非同期IOCTL
5. ✅ 統計情報収集

### Phase 4: 最適化とテスト（1週間）

1. ✅ パフォーマンステスト
2. ✅ ストレステスト
3. ✅ エラー注入テスト
4. ✅ ドキュメント整備

---

## テスト計画

### 1. 機能テスト

```c
// test_ncq_basic.c
int main() {
    int fd = open("/dev/ahci_lld_p0", O_RDWR);
    struct ahci_cmd_request reqs[4];
    
    // 4個のREADコマンドを同時発行
    for (int i = 0; i < 4; i++) {
        reqs[i].command = 0x60;  // READ FPDMA QUEUED
        reqs[i].lba = i * 2048;
        reqs[i].count = 8;
        reqs[i].buffer_len = 4096;
        reqs[i].ncq_flags = AHCI_NCQ_FLAG_ASYNC;
        
        ioctl(fd, AHCI_IOC_ISSUE_NCQ_CMD, &reqs[i]);
        printf("Issued tag %d\n", reqs[i].tag);
    }
    
    // 全て完了を待つ
    for (int i = 0; i < 4; i++) {
        struct ahci_ncq_wait wait = {
            .tag = reqs[i].tag,
            .timeout_ms = 5000
        };
        ioctl(fd, AHCI_IOC_WAIT_NCQ, &wait);
        printf("Tag %d completed: status=0x%02x\n", 
               reqs[i].tag, wait.status);
    }
}
```

### 2. パフォーマンステスト

```bash
# 非NCQ vs NCQ のスループット比較
./bench_sequential_read    # 1スロット (非NCQ)
./bench_ncq_read           # 8スロット (NCQ)

# ランダムI/O性能
./bench_random_io --depth=1   # QD=1 (非NCQ)
./bench_random_io --depth=16  # QD=16 (NCQ)
```

### 3. ストレステスト

```bash
# 長時間実行
./stress_ncq --duration=3600 --depth=32

# 同時に複数プロセス
for i in {1..4}; do
    ./stress_ncq --depth=8 &
done
wait
```

### 4. エラーテスト

```bash
# ディスクを故意に切断してエラーハンドリング確認
./test_ncq_error_recovery
```

---

## 期待される性能向上

### シーケンシャルI/O

| モード | QD | スループット | 改善率 |
|-------|----|-----------:|------:|
| 非NCQ | 1  | 200 MB/s   | - |
| NCQ   | 8  | 220 MB/s   | +10% |
| NCQ   | 32 | 230 MB/s   | +15% |

### ランダムI/O (最も効果的)

| モード | QD | IOPS | 改善率 |
|-------|----|-----------:|------:|
| 非NCQ | 1  | 80 IOPS    | - |
| NCQ   | 8  | 400 IOPS   | +400% |
| NCQ   | 32 | 600 IOPS   | +650% |

**理由**: ディスクがシークを最適化できる

---

## 参考資料

- **AHCI 1.3.1 Specification** - Section 8: Native Command Queuing
- **SATA 3.x Specification** - Section 13: Native Command Queuing
- **ATA8-ACS** - READ/WRITE FPDMA QUEUED commands
- **Linux Kernel** - `drivers/ata/libahci.c` (参考実装)

---

**最終更新**: 2025-12-20  
**バージョン**: 1.0
