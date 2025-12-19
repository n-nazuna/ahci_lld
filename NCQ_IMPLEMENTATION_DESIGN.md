# NCQ実装設計書

## 概要

AHCI Native Command Queuing（NCQ）の実装設計。
Non-NCQコマンドとNCQコマンドで異なる動作フローを実現する。

## コマンドの分類

### Non-NCQコマンド
- READ DMA (0x25), WRITE DMA (0x35), IDENTIFY (0xEC) など
- **同期実行**: コマンド完了まで待って結果を返す
- **PxCI**: Command Issue レジスタを使用
- **完了通知**: D2H Register FIS (status=0x50)

### NCQコマンド
- READ FPDMA QUEUED (0x60), WRITE FPDMA QUEUED (0x61)
- **非同期実行**: コマンド投入後すぐに戻る
- **PxSACT**: SATA Active レジスタを使用
- **受付完了通知**: D2H Register FIS (status=0x50)
- **実行完了通知**: Set Device Bits FIS (status=0x40)

## IOCTL設計

### AHCI_IOC_ISSUE_CMD
コマンド発行IOCTL。flagsフィールドのNCQフラグで動作を指定。

```c
struct ahci_cmd_request req;
req.command = 0x25;         // READ DMA
req.flags = 0;              // Non-NCQ: 同期実行
// または
req.command = 0x60;         // READ FPDMA QUEUED
req.flags = AHCI_CMD_FLAG_NCQ;  // NCQ: 非同期実行

ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
// Non-NCQ: ここで完了済み（status/error/dataが入っている）
// NCQ: ここで受付完了（tagが入っている、dataはまだ）
```

**判定ロジック:**
```c
bool is_ncq = (req.flags & AHCI_CMD_FLAG_NCQ);
```

### AHCI_IOC_PROBE_CMD
NCQコマンドの完了確認IOCTL。Non-NCQでは使用しない。

```c
struct ahci_sdb sdb;
ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);

// sdb.completed: 完了したスロットのビットマップ
// sdb.status[tag]: SDB FISから取得したstatus (0x40)
// sdb.error[tag]: SDB FISから取得したerror
// sdb.buffer[tag]: ユーザーバッファポインタ（データコピー済み）
```

## データ構造

### ahci_cmd_request
```c
struct ahci_cmd_request {
    __u8 command;           // ATAコマンドコード
    __u8 features;
    __u8 device;
    __u8 tag;               // NCQのみ: 割り当てられたタグ（0-31）
    
    __u64 lba;
    __u16 count;
    __u32 flags;            // WRITE/ATAPI/NCQフラグ
    
    __u64 buffer;           // ユーザーバッファ
    __u32 buffer_len;
    
    // 出力（Non-NCQ: ISSUE_CMD完了時、NCQ: PROBE_CMD完了時）
    __u8 status;
    __u8 error;
    __u64 lba_out;
    __u16 count_out;
};

// フラグ定義
#define AHCI_CMD_FLAG_WRITE     (1 << 0)  // Write方向
#define AHCI_CMD_FLAG_ATAPI     (1 << 1)  // ATAPIコマンド
#define AHCI_CMD_FLAG_NCQ       (1 << 2)  // NCQ（非同期実行）
```

### ahci_sdb (Set Device Bits情報)
```c
struct ahci_sdb {
    __u32 sactive;          // PxSACT（現在アクティブなスロット）
    __u32 completed;        // 今回完了したスロット
    
    __u8 status[32];        // SDB FISのstatus (0x40)
    __u8 error[32];         // SDB FISのerror
    __u64 buffer[32];       // ユーザーバッファポインタ
};
```

## コマンド実行フロー

### Non-NCQコマンド（同期実行）

```
[Userspace]              [Kernel]                [Hardware]

ISSUE_CMD
  ├─ command=0x25 ─────► コマンド判定
  │                      is_ncq = false (flags & NCQ == 0)
  │                      ahci_port_issue_cmd()
  │                      ├─ スロット0使用
  │                      ├─ PxCI[0] = 1 ────────► コマンド実行
  │                      │                        データ転送
  │                      │                        D2H FIS送信
  │                      ├─ ポーリング待機 ◄───── PxCI[0] = 0
  │                      ├─ D2H FIS読取
  │                      │  status = 0x50
  │                      │  error = 0x00
  │                      └─ データコピー
  ◄──────────────────── 完了（status/error/data）
```

### NCQコマンド（非同期実行）

#### Phase 1: コマンド発行（ISSUE_CMD）

```
[Userspace]              [Kernel]                [Hardware]

ISSUE_CMD
  ├─ command=0x60 ─────► コマンド判定
  │                      is_ncq = true (flags & NCQ != 0)
  │                      ahci_port_issue_cmd_async()
  │                      ├─ スロット割当（0-31）
  │                      ├─ PxSACT[tag] = 1
  │                      ├─ PxCI[tag] = 1 ───────► コマンド受付
  │                      │                         D2H FIS送信
  │                      └─ D2H FIS読取 ◄───────── status = 0x50
  │                         （受付完了）            （キュー投入完了）
  ◄──────────────────── 即座に戻る（tag返却）
  
                                                  [バックグラウンド]
                                                  データ転送実行...
```

#### Phase 2: 完了確認（PROBE_CMD）

```
[Userspace]              [Kernel]                [Hardware]

PROBE_CMD ──────────────► ahci_check_slot_completion()
                          ├─ PxSACT読取 ◄──────── データ転送完了
                          │  tag bits cleared      SDB FIS送信
                          ├─ SDB FIS読取           PxSACT[tag] = 0
                          │  status = 0x40
                          │  error = 0x00
                          └─ データコピー
  ◄──────────────────── completed bits
                         status[tag]=0x40
                         error[tag]=0x00
                         data in buffer
```

## カーネル実装詳細

### ahci_port_issue_cmd() - Non-NCQ同期実行

```c
int ahci_port_issue_cmd(struct ahci_port_device *port,
                        struct ahci_cmd_request *req, void *buf)
{
    // スロット0固定使用
    // Command Header/Table設定
    // FIS構築
    // PRDT設定（SG buffers使用）
    
    // コマンド発行
    iowrite32(1, port_mmio + AHCI_PORT_CI);  // PxCI[0] = 1
    
    // 完了待機（ポーリング）
    while (timeout--) {
        if (!(ioread32(port_mmio + AHCI_PORT_CI) & 1))  // PxCI[0] == 0?
            break;
    }
    
    // D2H FIS読取
    d2h = (struct fis_reg_d2h *)(port->rx_fis + AHCI_RX_FIS_D2H);
    req->status = d2h->status;  // 0x50
    req->error = d2h->error;
    
    // データコピー（Read方向の場合）
    if (!is_write && buf)
        memcpy(buf, sg_buffers, len);
    
    return 0;  // 完了
}
```

### ahci_port_issue_cmd_async() - NCQ非同期実行

```c
int ahci_port_issue_cmd_async(struct ahci_port_device *port,
                               struct ahci_cmd_request *req, void *buf)
{
    // スロット割当（0-31から空きを探す）
    slot = ahci_alloc_slot(port);
    
    // スロット情報保存
    port->slots[slot].req = *req;  // 構造体コピー
    port->slots[slot].buffer = buf;
    
    // Command Header/Table設定（スロット番号使用）
    // FIS構築（FPDMA用）
    // PRDT設定
    
    // NCQコマンド発行
    iowrite32(1 << slot, port_mmio + AHCI_PORT_SACT);  // PxSACT[slot] = 1
    iowrite32(1 << slot, port_mmio + AHCI_PORT_CI);    // PxCI[slot] = 1
    
    // D2H FIS受信（受付完了、非ブロッキング）
    // ここでは待たずに即座に戻る
    
    req->tag = slot;
    return 0;  // 受付完了（実行は非同期）
}
```

### ahci_check_slot_completion() - NCQ完了チェック

```c
u32 ahci_check_slot_completion(struct ahci_port_device *port)
{
    // ハードウェアレジスタ読取
    sact = ioread32(port_mmio + AHCI_PORT_SACT);
    
    for (slot = 0; slot < 32; slot++) {
        if (!test_bit(slot, &port->slots_in_use))
            continue;
        if (port->slots[slot].completed)
            continue;
        
        // PxSACT[slot] == 0 なら完了
        if (!(sact & (1 << slot))) {
            // SDB FIS読取
            sdb = (struct fis_set_dev_bits *)(port->rx_fis + AHCI_RX_FIS_SDB);
            
            // SDB FISのstatusフィールドはエンコードされている
            // bit 6,5,4 = status[7,6,5]
            // bit 2,1,0 = status[2,1,0]
            u8 status_hi = (sdb->status & 0x70) << 1;
            u8 status_lo = (sdb->status & 0x07);
            port->slots[slot].req.status = status_hi | status_lo;  // 0x40
            port->slots[slot].req.error = sdb->error;
            
            // 完了マーク
            port->slots[slot].completed = true;
            set_bit(slot, &port->slots_completed);
        }
    }
}
```

### IOCTL Handler

```c
static long ahci_lld_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case AHCI_IOC_ISSUE_CMD:
        // ユーザーから要求コピー
        copy_from_user(&req, (void __user *)arg, sizeof(req));
        
        // バッファ割当
        if (req.buffer_len > 0) {
            data_buf = kmalloc(req.buffer_len, GFP_KERNEL);
            if (req.flags & AHCI_CMD_FLAG_WRITE)
                copy_from_user(data_buf, (void __user *)req.buffer, req.buffer_len);
        }
        
        // コマンドフラグで判定
        bool is_ncq = (req.flags & AHCI_CMD_FLAG_NCQ);
        
        if (is_ncq) {
            // NCQ: 非同期実行
            ret = ahci_port_issue_cmd_async(port_dev, &req, data_buf);
            if (ret == 0) {
                // tagのみ返す（data_bufは保持）
                copy_to_user((void __user *)arg, &req, sizeof(req));
            }
        } else {
            // Non-NCQ: 同期実行
            ret = ahci_port_issue_cmd(port_dev, &req, data_buf);
            if (ret == 0) {
                // status/error/dataを返す
                if (!(req.flags & AHCI_CMD_FLAG_WRITE) && req.buffer_len > 0)
                    copy_to_user((void __user *)req.buffer, data_buf, req.buffer_len);
                copy_to_user((void __user *)arg, &req, sizeof(req));
            }
            // 同期なのでバッファ即座解放
            kfree(data_buf);
        }
        break;
        
    case AHCI_IOC_PROBE_CMD:
        // NCQコマンドの完了確認
        ahci_check_slot_completion(port_dev);
        
        // 完了情報収集
        for (tag = 0; tag < 32; tag++) {
            if (port_dev->slots[tag].completed) {
                sdb.completed |= (1 << tag);
                sdb.status[tag] = port_dev->slots[tag].req.status;  // 0x40
                sdb.error[tag] = port_dev->slots[tag].req.error;
                sdb.buffer[tag] = port_dev->slots[tag].req.buffer;
                
                // データコピー（Read方向の場合）
                if (!port_dev->slots[tag].is_write && port_dev->slots[tag].buffer) {
                    copy_to_user((void __user *)port_dev->slots[tag].req.buffer,
                                 port_dev->slots[tag].buffer,
                                 port_dev->slots[tag].buffer_len);
                }
                
                // バッファ解放
                kfree(port_dev->slots[tag].buffer);
                port_dev->slots[tag].buffer = NULL;
                port_dev->slots[tag].completed = false;
            }
        }
        
        // 結果返却
        copy_to_user((void __user *)arg, &sdb, sizeof(sdb));
        break;
    }
}
```

## テストシナリオ

### Non-NCQコマンドテスト

```c
// READ DMA (同期)
req.command = 0x25;  // READ DMA EXT
req.lba = 0x1000;
req.count = 1;
req.buffer = (u64)buffer;
req.buffer_len = 512;

ret = ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
// ここで完了
printf("status=0x%02x error=0x%02x\n", req.status, req.error);  // 0x50, 0x00
// bufferにデータが入っている
```

### NCQコマンドテスト

```c
// READ FPDMA QUEUED (非同期)
req.command = 0x60;  // READ FPDMA QUEUED
req.flags = AHCI_CMD_FLAG_NCQ;  // NCQフラグ
req.lba = 0x1000;
req.count = 1;
req.buffer = (u64)buffer;
req.buffer_len = 512;

// コマンド発行
ret = ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
printf("tag=%d\n", req.tag);  // 0-31
// この時点ではデータはまだない

// 完了確認（ポーリング）
while (1) {
    struct ahci_sdb sdb = {0};
    ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);
    
    if (sdb.completed & (1 << req.tag)) {
        printf("status=0x%02x error=0x%02x\n",
               sdb.status[req.tag], sdb.error[req.tag]);  // 0x40, 0x00
        // bufferにデータが入っている
        break;
    }
    usleep(10000);
}
```

## レジスタ使用まとめ

| レジスタ | Non-NCQ | NCQ |
|---------|---------|-----|
| PxCI | 使用（スロット0固定） | 使用（スロット0-31動的） |
| PxSACT | 使用しない | 使用（スロット0-31動的） |
| 完了判定 | PxCI[0]==0 | PxSACT[tag]==0 |
| 完了FIS | D2H Register FIS | Set Device Bits FIS |
| 完了status | 0x50 (DRDY\|DSC) | 0x40 (DRDY) |

## FIS詳細

### D2H Register FIS (0x34)
Non-NCQ完了、NCQ受付完了で使用
```c
struct fis_reg_d2h {
    u8 fis_type;    // 0x34
    u8 flags;
    u8 status;      // 0x50 (DRDY | DSC)
    u8 error;       // 0x00
    // LBA, count, etc...
};
```
オフセット: `AHCI_RX_FIS_D2H = 0x40`

### Set Device Bits FIS (0xA1)
NCQ実行完了で使用
```c
struct fis_set_dev_bits {
    u8 fis_type;    // 0xA1
    u8 flags;
    u8 status;      // エンコード済み (実際は0x40相当)
    u8 error;       // 0x00
    u32 protocol_specific;
};
```
オフセット: `AHCI_RX_FIS_SDB = 0x58`

**statusフィールドのデコード:**
```c
u8 status_hi = (sdb->status & 0x70) << 1;  // bits[6:4] -> [7:5]
u8 status_lo = (sdb->status & 0x07);       // bits[2:0] -> [2:0]
u8 actual_status = status_hi | status_lo;  // 0x40
```

## まとめ

- **Non-NCQ = 同期、D2H FIS、PxCI、PROBE不要**
- **NCQ = 非同期、受付D2H + 完了SDB、PxSACT、PROBE必須**
- `AHCI_CMD_FLAG_NCQ`フラグで明示的に指定
- NCQコマンドは0x60/0x61だが、フラグで判定（筋が良い）
