# AHCI ハードウェア操作仕様書

**AHCI Low Level Driver - レジスタ・FIS・ハードウェア制御仕様**

Version: 1.0  
Date: 2025-12-20  
Based on: AHCI Specification 1.3.1

---

## 目次

1. [概要](#概要)
2. [レジスタ定義](#レジスタ定義)
3. [FIS構造](#fis構造)
4. [ハードウェア制御シーケンス](#ハードウェア制御シーケンス)
5. [DMAメモリ構造](#dmaメモリ構造)
6. [コマンド実行メカニズム](#コマンド実行メカニズム)

---

## 概要

AHCI（Advanced Host Controller Interface）は、SATAデバイスを制御するための標準的なレジスタインターフェースです。本ドキュメントでは、AHCI LLDドライバが使用するレジスタ、FIS、制御シーケンスを詳述します。

### アドレス空間

- **BAR5 (ABAR)**: PCI Configuration Spaceのベースアドレスレジスタ5
  - 全AHCIレジスタがマップされる
  - サイズ: 通常4KB以上

### メモリマップ構造

```
0x0000-0x002C: Generic Host Control (GHC) Registers
0x0040-0x00A0: Reserved / Vendor Specific
0x0100-0x017F: Port 0 Registers
0x0180-0x01FF: Port 1 Registers
...
0x1080-0x10FF: Port 31 Registers
```

---

## レジスタ定義

### 1. Generic Host Control (GHC) Registers

#### CAP - Host Capabilities (Offset 0x00, 32-bit, RO)

HBAのハードウェア機能を示す。

```c
#define AHCI_CAP           0x00

/* Capability bits */
#define AHCI_CAP_S64A      (1 << 31)  /* 64-bit addressing */
#define AHCI_CAP_SNCQ      (1 << 30)  /* Native Command Queuing */
#define AHCI_CAP_SSNTF     (1 << 29)  /* SNotification Register */
#define AHCI_CAP_SMPS      (1 << 28)  /* Mechanical Presence Switch */
#define AHCI_CAP_SSS       (1 << 27)  /* Staggered Spin-up */
#define AHCI_CAP_SALP      (1 << 26)  /* Aggressive Link PM */
#define AHCI_CAP_SAL       (1 << 25)  /* Activity LED */
#define AHCI_CAP_SCLO      (1 << 24)  /* Command List Override */
#define AHCI_CAP_ISS_MASK  (0xF << 20) /* Interface Speed Support */
#define AHCI_CAP_SAM       (1 << 18)  /* AHCI mode only */
#define AHCI_CAP_SPM       (1 << 17)  /* Port Multiplier */
#define AHCI_CAP_FBSS      (1 << 16)  /* FIS-based Switching */
#define AHCI_CAP_PMD       (1 << 15)  /* PIO Multiple DRQ Block */
#define AHCI_CAP_SSC       (1 << 14)  /* Slumber State Capable */
#define AHCI_CAP_PSC       (1 << 13)  /* Partial State Capable */
#define AHCI_CAP_NCS_MASK  (0x1F << 8) /* Number of Command Slots */
#define AHCI_CAP_CCCS      (1 << 7)   /* Command Completion Coalescing */
#define AHCI_CAP_EMS       (1 << 6)   /* Enclosure Management */
#define AHCI_CAP_SXS       (1 << 5)   /* External SATA */
#define AHCI_CAP_NP_MASK   (0x1F << 0) /* Number of Ports */
```

**使用例:**
```c
u32 cap = ioread32(mmio + AHCI_CAP);
int num_ports = (cap & AHCI_CAP_NP_MASK) + 1;
int num_slots = ((cap & AHCI_CAP_NCS_MASK) >> 8) + 1;
bool ncq_support = !!(cap & AHCI_CAP_SNCQ);
```

#### GHC - Global HBA Control (Offset 0x04, 32-bit, RW)

HBAのグローバル制御。

```c
#define AHCI_GHC           0x04

#define AHCI_GHC_AE        (1 << 31)  /* AHCI Enable */
#define AHCI_GHC_MRSM      (1 << 2)   /* MSI Revert to Single Message */
#define AHCI_GHC_IE        (1 << 1)   /* Interrupt Enable */
#define AHCI_GHC_HR        (1 << 0)   /* HBA Reset */
```

**使用方法:**
- **AHCI有効化**: `GHC.AE = 1`
- **HBAリセット**: `GHC.HR = 1`（自動的に0に戻る）
- **割り込み有効化**: `GHC.IE = 1`

#### IS - Interrupt Status (Offset 0x08, 32-bit, RWC)

ポートごとの割り込み状態。

```c
#define AHCI_IS            0x08
```

ビット`i`が1の場合、ポート`i`に割り込み発生。クリアするには1を書き込む。

#### PI - Ports Implemented (Offset 0x0C, 32-bit, RO)

実装されているポートのビットマップ。

```c
#define AHCI_PI            0x0C
```

ビット`i`が1の場合、ポート`i`が実装されている。

#### VS - AHCI Version (Offset 0x10, 32-bit, RO)

AHCIバージョン番号。

```c
#define AHCI_VS            0x10

/* Version format: 0xMMmmrrrr (Major.Minor.Revision) */
/* Example: 0x00010301 = AHCI 1.3.1 */
```

---

### 2. Port Registers

各ポートは0x80バイトのレジスタ空間を持つ。  
ベースアドレス: `0x100 + (port_num * 0x80)`

#### PxCLB/PxCLBU - Command List Base Address (Offset 0x00/0x04, 64-bit, RW)

コマンドリストのDMAアドレス（1KB境界アライメント必須）。

```c
#define AHCI_PORT_CLB      0x00
#define AHCI_PORT_CLBU     0x04
```

**設定:**
```c
/* Command list must be 1KB-aligned */
dma_addr_t cmd_list_dma = ...;
iowrite32((u32)(cmd_list_dma & 0xFFFFFFFF), port_mmio + AHCI_PORT_CLB);
iowrite32((u32)(cmd_list_dma >> 32), port_mmio + AHCI_PORT_CLBU);
```

#### PxFB/PxFBU - FIS Base Address (Offset 0x08/0x0C, 64-bit, RW)

受信FIS領域のDMAアドレス（256バイト境界アライメント必須）。

```c
#define AHCI_PORT_FB       0x08
#define AHCI_PORT_FBU      0x0C
```

#### PxIS - Interrupt Status (Offset 0x10, 32-bit, RWC)

ポート固有の割り込みステータス。

```c
#define AHCI_PORT_IS       0x10

#define AHCI_PORT_IS_CPDS  (1 << 31)  /* Cold Port Detect Status */
#define AHCI_PORT_IS_TFES  (1 << 30)  /* Task File Error Status */
#define AHCI_PORT_IS_HBFS  (1 << 29)  /* Host Bus Fatal Error */
#define AHCI_PORT_IS_HBDS  (1 << 28)  /* Host Bus Data Error */
#define AHCI_PORT_IS_IFS   (1 << 27)  /* Interface Fatal Error */
#define AHCI_PORT_IS_INFS  (1 << 26)  /* Interface Non-fatal Error */
#define AHCI_PORT_IS_OFS   (1 << 24)  /* Overflow Status */
#define AHCI_PORT_IS_IPMS  (1 << 23)  /* Incorrect Port Multiplier */
#define AHCI_PORT_IS_PRCS  (1 << 22)  /* PhyRdy Change Status */
#define AHCI_PORT_IS_DMPS  (1 << 7)   /* Device Mechanical Presence */
#define AHCI_PORT_IS_PCS   (1 << 6)   /* Port Connect Change */
#define AHCI_PORT_IS_DPS   (1 << 5)   /* Descriptor Processed */
#define AHCI_PORT_IS_UFI   (1 << 4)   /* Unknown FIS Interrupt */
#define AHCI_PORT_IS_SDBS  (1 << 3)   /* Set Device Bits Interrupt */
#define AHCI_PORT_IS_DSS   (1 << 2)   /* DMA Setup FIS Interrupt */
#define AHCI_PORT_IS_PSS   (1 << 1)   /* PIO Setup FIS Interrupt */
#define AHCI_PORT_IS_DHRS  (1 << 0)   /* Device to Host Register FIS */
```

**エラービット:**
- `TFES`, `HBFS`, `HBDS`, `IFS`: 致命的エラー
- クリア: 1を書き込む

**NCQ完了:**
- `SDBS`: Set Device Bits FIS受信（NCQコマンド完了）

#### PxIE - Interrupt Enable (Offset 0x14, 32-bit, RW)

PxISと同じビット定義。有効化したいビットを1にする。

```c
#define AHCI_PORT_IE       0x14
```

#### PxCMD - Command and Status (Offset 0x18, 32-bit, RW)

ポートのコマンド制御とステータス。

```c
#define AHCI_PORT_CMD      0x18

#define AHCI_PORT_CMD_ICC_MASK   (0xF << 28)  /* Interface Communication Control */
#define AHCI_PORT_CMD_ICC_ACTIVE (1 << 28)    /* Active */
#define AHCI_PORT_CMD_ICC_PARTIAL (2 << 28)   /* Partial */
#define AHCI_PORT_CMD_ICC_SLUMBER (6 << 28)   /* Slumber */
#define AHCI_PORT_CMD_ASP        (1 << 27)    /* Aggressive Slumber/Partial */
#define AHCI_PORT_CMD_ALPE       (1 << 26)    /* Aggressive Link PM Enable */
#define AHCI_PORT_CMD_DLAE       (1 << 25)    /* Drive LED on ATAPI Enable */
#define AHCI_PORT_CMD_ATAPI      (1 << 24)    /* Device is ATAPI */
#define AHCI_PORT_CMD_APSTE      (1 << 23)    /* Automatic Partial to Slumber */
#define AHCI_PORT_CMD_FBSCP      (1 << 22)    /* FIS-based Switching Capable Port */
#define AHCI_PORT_CMD_ESP        (1 << 21)    /* External SATA Port */
#define AHCI_PORT_CMD_CPD        (1 << 20)    /* Cold Presence Detection */
#define AHCI_PORT_CMD_MPSP       (1 << 19)    /* Mechanical Presence Switch */
#define AHCI_PORT_CMD_HPCP       (1 << 18)    /* Hot Plug Capable Port */
#define AHCI_PORT_CMD_PMA        (1 << 17)    /* Port Multiplier Attached */
#define AHCI_PORT_CMD_CPS        (1 << 16)    /* Cold Presence State */
#define AHCI_PORT_CMD_CR         (1 << 15)    /* Command List Running (RO) */
#define AHCI_PORT_CMD_FR         (1 << 14)    /* FIS Receive Running (RO) */
#define AHCI_PORT_CMD_MPSS       (1 << 13)    /* Mechanical Presence Switch State */
#define AHCI_PORT_CMD_CCS_MASK   (0x1F << 8)  /* Current Command Slot (RO) */
#define AHCI_PORT_CMD_FRE        (1 << 4)     /* FIS Receive Enable */
#define AHCI_PORT_CMD_CLO        (1 << 3)     /* Command List Override */
#define AHCI_PORT_CMD_POD        (1 << 2)     /* Power On Device */
#define AHCI_PORT_CMD_SUD        (1 << 1)     /* Spin-Up Device */
#define AHCI_PORT_CMD_ST         (1 << 0)     /* Start */
```

**重要な操作:**
1. **FIS受信開始**: `PxCMD.FRE = 1` → `PxCMD.FR = 1`を待つ
2. **コマンド処理開始**: `PxCMD.ST = 1` → `PxCMD.CR = 1`を待つ
3. **停止**: `PxCMD.ST = 0` → `PxCMD.CR = 0`を待つ

#### PxTFD - Task File Data (Offset 0x20, 32-bit, RO)

最新のTask File（ステータス/エラー）。

```c
#define AHCI_PORT_TFD      0x20

#define AHCI_PORT_TFD_ERR_MASK (0xFF << 8)   /* Error */
#define AHCI_PORT_TFD_STS_MASK (0xFF << 0)   /* Status */

/* Status bits */
#define AHCI_PORT_TFD_STS_BSY  (1 << 7)      /* Busy */
#define AHCI_PORT_TFD_STS_DRQ  (1 << 3)      /* Data Request */
#define AHCI_PORT_TFD_STS_ERR  (1 << 0)      /* Error */
```

**デバイスレディ判定:**
```c
u32 tfd = ioread32(port_mmio + AHCI_PORT_TFD);
bool ready = !(tfd & (AHCI_PORT_TFD_STS_BSY | AHCI_PORT_TFD_STS_DRQ));
```

#### PxSIG - Signature (Offset 0x24, 32-bit, RO)

デバイスタイプ識別。

```c
#define AHCI_PORT_SIG      0x24

#define AHCI_SIG_ATA       0x00000101  /* SATA drive */
#define AHCI_SIG_ATAPI     0xEB140101  /* SATAPI drive */
#define AHCI_SIG_SEMB      0xC33C0101  /* Enclosure management bridge */
#define AHCI_SIG_PM        0x96690101  /* Port multiplier */
```

#### PxSSTS - Serial ATA Status (Offset 0x28, 32-bit, RO)

SATA PHYステータス（SCR0: SStatus）。

```c
#define AHCI_PORT_SSTS     0x28

#define AHCI_PORT_SSTS_IPM_MASK  (0xF << 8)   /* Interface Power Management */
#define AHCI_PORT_SSTS_IPM_ACTIVE (1 << 8)    /* Active */
#define AHCI_PORT_SSTS_IPM_PARTIAL (2 << 8)   /* Partial */
#define AHCI_PORT_SSTS_IPM_SLUMBER (6 << 8)   /* Slumber */

#define AHCI_PORT_SSTS_SPD_MASK  (0xF << 4)   /* Current Interface Speed */
#define AHCI_PORT_SSTS_SPD_GEN1  (1 << 4)     /* Generation 1 (1.5 Gbps) */
#define AHCI_PORT_SSTS_SPD_GEN2  (2 << 4)     /* Generation 2 (3 Gbps) */
#define AHCI_PORT_SSTS_SPD_GEN3  (3 << 4)     /* Generation 3 (6 Gbps) */

#define AHCI_PORT_SSTS_DET_MASK  (0xF << 0)   /* Device Detection */
#define AHCI_PORT_SSTS_DET_NONE  (0 << 0)     /* No device detected */
#define AHCI_PORT_SSTS_DET_PRESENT_NOPHY (1 << 0) /* Device present, no PHY */
#define AHCI_PORT_SSTS_DET_PRESENT_PHY (3 << 0)   /* Device present and PHY established */
```

**デバイス検出確認:**
```c
u32 ssts = ioread32(port_mmio + AHCI_PORT_SSTS);
if ((ssts & AHCI_PORT_SSTS_DET_MASK) == AHCI_PORT_SSTS_DET_PRESENT_PHY) {
    /* Device connected and ready */
}
```

#### PxSCTL - Serial ATA Control (Offset 0x2C, 32-bit, RW)

SATA PHY制御（SCR2: SControl）。

```c
#define AHCI_PORT_SCTL     0x2C

#define AHCI_PORT_SCTL_IPM_MASK  (0xF << 8)   /* Interface Power Management Transitions Allowed */
#define AHCI_PORT_SCTL_IPM_NONE  (0 << 8)     /* No restrictions */
#define AHCI_PORT_SCTL_IPM_NOPARTIAL (1 << 8) /* Partial disabled */
#define AHCI_PORT_SCTL_IPM_NOSLUMBER (2 << 8) /* Slumber disabled */
#define AHCI_PORT_SCTL_IPM_BOTH  (3 << 8)     /* Both disabled */

#define AHCI_PORT_SCTL_SPD_MASK  (0xF << 4)   /* Speed Allowed */
#define AHCI_PORT_SCTL_SPD_ANY   (0 << 4)     /* No speed restriction */
#define AHCI_PORT_SCTL_SPD_GEN1  (1 << 4)     /* Limit to Gen1 */
#define AHCI_PORT_SCTL_SPD_GEN2  (2 << 4)     /* Limit to Gen2 */
#define AHCI_PORT_SCTL_SPD_GEN3  (3 << 4)     /* Limit to Gen3 */

#define AHCI_PORT_SCTL_DET_MASK  (0xF << 0)   /* Device Detection Initialization */
#define AHCI_PORT_SCTL_DET_NONE  (0 << 0)     /* No action */
#define AHCI_PORT_SCTL_DET_INIT  (1 << 0)     /* Perform interface initialization (COMRESET) */
#define AHCI_PORT_SCTL_DET_DISABLE (4 << 0)   /* Disable SATA interface */
```

**COMRESET実行:**
```c
/* Initiate COMRESET */
iowrite32(AHCI_PORT_SCTL_DET_INIT, port_mmio + AHCI_PORT_SCTL);
msleep(10);
/* De-assert COMRESET */
iowrite32(AHCI_PORT_SCTL_DET_NONE, port_mmio + AHCI_PORT_SCTL);
```

#### PxSERR - Serial ATA Error (Offset 0x30, 32-bit, RWC)

SATA診断エラー（SCR1: SError）。

```c
#define AHCI_PORT_SERR     0x30

/* Diagnostics (bits 16-23) */
#define AHCI_PORT_SERR_DIAG_X  (1 << 26)  /* Exchanged */
#define AHCI_PORT_SERR_DIAG_F  (1 << 25)  /* Unknown FIS Type */
#define AHCI_PORT_SERR_DIAG_T  (1 << 24)  /* Transport state transition error */
#define AHCI_PORT_SERR_DIAG_S  (1 << 23)  /* Link sequence error */
#define AHCI_PORT_SERR_DIAG_H  (1 << 22)  /* Handshake error */
#define AHCI_PORT_SERR_DIAG_C  (1 << 21)  /* CRC error */
#define AHCI_PORT_SERR_DIAG_D  (1 << 20)  /* Disparity error */
#define AHCI_PORT_SERR_DIAG_B  (1 << 19)  /* 10B to 8B decode error */
#define AHCI_PORT_SERR_DIAG_W  (1 << 18)  /* Comm Wake */
#define AHCI_PORT_SERR_DIAG_I  (1 << 17)  /* Phy Internal Error */
#define AHCI_PORT_SERR_DIAG_N  (1 << 16)  /* PhyRdy Change */

/* Errors (bits 0-15) */
#define AHCI_PORT_SERR_ERR_E   (1 << 11)  /* Internal error */
#define AHCI_PORT_SERR_ERR_P   (1 << 10)  /* Protocol error */
#define AHCI_PORT_SERR_ERR_C   (1 << 9)   /* Persistent communication error */
#define AHCI_PORT_SERR_ERR_T   (1 << 8)   /* Transient data integrity error */
#define AHCI_PORT_SERR_ERR_M   (1 << 1)   /* Recovered communications error */
#define AHCI_PORT_SERR_ERR_I   (1 << 0)   /* Recovered data integrity error */
```

**クリア:**
```c
iowrite32(0xFFFFFFFF, port_mmio + AHCI_PORT_SERR);  /* Clear all errors */
```

#### PxSACT - Serial ATA Active (Offset 0x34, 32-bit, RW)

NCQアクティブスロットのビットマップ。

```c
#define AHCI_PORT_SACT     0x34
```

**NCQコマンド発行:**
```c
int slot = 5;
iowrite32(1 << slot, port_mmio + AHCI_PORT_SACT);  /* Activate NCQ slot */
iowrite32(1 << slot, port_mmio + AHCI_PORT_CI);    /* Issue command */
```

**完了確認:**
```c
u32 sact = ioread32(port_mmio + AHCI_PORT_SACT);
if (!(sact & (1 << slot))) {
    /* NCQ command completed */
}
```

#### PxCI - Command Issue (Offset 0x38, 32-bit, RW)

コマンド発行ビットマップ。

```c
#define AHCI_PORT_CI       0x38
```

**コマンド発行:**
```c
iowrite32(1 << slot, port_mmio + AHCI_PORT_CI);
```

**完了待機:**
```c
while (ioread32(port_mmio + AHCI_PORT_CI) & (1 << slot)) {
    /* Wait for completion */
    msleep(1);
}
```

---

## FIS構造

FIS (Frame Information Structure) はホストとデバイス間の通信プロトコル。

### FIS Types

```c
#define FIS_TYPE_REG_H2D    0x27  /* Register FIS - Host to Device */
#define FIS_TYPE_REG_D2H    0x34  /* Register FIS - Device to Host */
#define FIS_TYPE_DMA_ACT    0x39  /* DMA Activate FIS */
#define FIS_TYPE_DMA_SETUP  0x41  /* DMA Setup FIS */
#define FIS_TYPE_DATA       0x46  /* Data FIS */
#define FIS_TYPE_BIST       0x58  /* BIST Activate FIS */
#define FIS_TYPE_PIO_SETUP  0x5F  /* PIO Setup FIS */
#define FIS_TYPE_DEV_BITS   0xA1  /* Set Device Bits FIS */
```

### 1. Register FIS - Host to Device (20 bytes)

コマンド発行用。

```c
struct fis_reg_h2d {
    u8  fis_type;        /* 0x27 */
    u8  flags;           /* bit 7: C (Command), bit 3-0: PM Port */
    u8  command;         /* ATA command code */
    u8  features;        /* Features register (7:0) */
    
    u8  lba_low;         /* LBA (7:0) */
    u8  lba_mid;         /* LBA (15:8) */
    u8  lba_high;        /* LBA (23:16) */
    u8  device;          /* Device register */
    
    u8  lba_low_exp;     /* LBA (31:24) */
    u8  lba_mid_exp;     /* LBA (39:32) */
    u8  lba_high_exp;    /* LBA (47:40) */
    u8  features_exp;    /* Features register (15:8) */
    
    u8  count;           /* Sector count (7:0) */
    u8  count_exp;       /* Sector count (15:8) */
    u8  icc;             /* Isochronous Command Completion */
    u8  control;         /* Control register */
    
    u32 aux;             /* Auxiliary (for 48-bit commands) */
} __packed;

#define FIS_H2D_FLAG_CMD  (1 << 7)  /* Command bit (必須) */
```

**使用例:**
```c
struct fis_reg_h2d *fis = (struct fis_reg_h2d *)cmd_tbl->cfis;
memset(fis, 0, sizeof(*fis));

fis->fis_type = FIS_TYPE_REG_H2D;
fis->flags = FIS_H2D_FLAG_CMD;
fis->command = 0x25;  /* READ DMA EXT */
fis->device = 0x40;   /* LBA mode */
fis->lba_low = lba & 0xFF;
fis->lba_mid = (lba >> 8) & 0xFF;
fis->lba_high = (lba >> 16) & 0xFF;
fis->lba_low_exp = (lba >> 24) & 0xFF;
fis->lba_mid_exp = (lba >> 32) & 0xFF;
fis->lba_high_exp = (lba >> 40) & 0xFF;
fis->count = sector_count & 0xFF;
fis->count_exp = (sector_count >> 8) & 0xFF;
```

### 2. Register FIS - Device to Host (20 bytes)

コマンド完了結果。

```c
struct fis_reg_d2h {
    u8  fis_type;        /* 0x34 */
    u8  flags;           /* bit 6: I (Interrupt), bit 3-0: PM Port */
    u8  status;          /* Status register */
    u8  error;           /* Error register */
    
    u8  lba_low;         /* LBA (7:0) */
    u8  lba_mid;         /* LBA (15:8) */
    u8  lba_high;        /* LBA (23:16) */
    u8  device;          /* Device register */
    
    u8  lba_low_exp;     /* LBA (31:24) */
    u8  lba_mid_exp;     /* LBA (39:32) */
    u8  lba_high_exp;    /* LBA (47:40) */
    u8  reserved1;
    
    u8  count;           /* Sector count (7:0) */
    u8  count_exp;       /* Sector count (15:8) */
    u16 reserved2;
    
    u32 reserved3;
} __packed;

#define FIS_D2H_FLAG_INTERRUPT (1 << 6)
```

**読み取り:**
```c
struct fis_reg_d2h *d2h = (struct fis_reg_d2h *)(fis_area + 0x40);
u8 status = d2h->status;
u8 error = d2h->error;
u64 lba_result = ((u64)d2h->lba_high_exp << 40) |
                 ((u64)d2h->lba_mid_exp << 32) |
                 ((u64)d2h->lba_low_exp << 24) |
                 ((u64)d2h->lba_high << 16) |
                 ((u64)d2h->lba_mid << 8) |
                 ((u64)d2h->lba_low);
```

### 3. Set Device Bits FIS (8 bytes)

NCQ完了通知。

```c
struct fis_set_dev_bits {
    u8  fis_type;        /* 0xA1 */
    u8  flags;           /* bit 6: I, bit 5: N (Notification) */
    u8  status;          /* Status register */
    u8  error;           /* Error register */
    
    u32 protocol_specific;  /* Protocol specific (NCQ queue tag info) */
} __packed;

#define FIS_SDB_INTERRUPT     (1 << 6)
#define FIS_SDB_NOTIFICATION  (1 << 5)
```

**NCQ完了読み取り:**
```c
struct fis_set_dev_bits *sdb = (struct fis_set_dev_bits *)(fis_area + 0x58);
u8 status = sdb->status;  /* NCQ completion status (0x40 = success) */
u8 error = sdb->error;
```

### Received FIS Area (256 bytes)

デバイスから受信したFISを格納するメモリ領域（256バイト境界アライメント）。

```c
#define AHCI_RX_FIS_DMA     0x00  /* DMA Setup FIS */
#define AHCI_RX_FIS_PIO     0x20  /* PIO Setup FIS */
#define AHCI_RX_FIS_D2H     0x40  /* D2H Register FIS (main result) */
#define AHCI_RX_FIS_SDB     0x58  /* Set Device Bits FIS (NCQ result) */
#define AHCI_RX_FIS_UNK     0x60  /* Unknown FIS */
#define AHCI_RX_FIS_SIZE    256   /* Total size */
```

**アクセス:**
```c
void *fis_area = port->fis_area;  /* DMA coherent memory */
struct fis_reg_d2h *d2h = (struct fis_reg_d2h *)(fis_area + AHCI_RX_FIS_D2H);
struct fis_set_dev_bits *sdb = (struct fis_set_dev_bits *)(fis_area + AHCI_RX_FIS_SDB);
```

---

## ハードウェア制御シーケンス

### 1. HBA初期化

```
1. PCI Configuration Space設定
   - Enable Bus Mastering
   - Map BAR5 (ABAR)

2. AHCI Enable
   - Set GHC.AE = 1

3. HBA Reset (optional)
   - Set GHC.HR = 1
   - Wait for GHC.HR = 0 (max 1 second)
   - Set GHC.AE = 1 again

4. Read HBA Capabilities
   - CAP: capabilities
   - PI: implemented ports
   - VS: version

5. Enable Interrupts (optional)
   - Set GHC.IE = 1
```

### 2. Port初期化

AHCI 1.3.1 Section 10.3.1に準拠。

```
1. Port Idle状態確認
   - PxCMD.ST = 0
   - PxCMD.CR = 0
   - PxCMD.FRE = 0
   - PxCMD.FR = 0
   - If not idle → Stop port first

2. DMAメモリ割り当て
   - Command List: 1KB (1KB-aligned)
   - FIS Area: 256B (256B-aligned)
   - Command Tables: 4KB each (128B-aligned)
   - SG Buffers: 128KB each (page-aligned)

3. DMAアドレス設定
   - Write PxCLB/PxCLBU
   - Write PxFB/PxFBU

4. デバイス接続確認
   - Check PxSSTS.DET = 0x3 (PHY established)

5. エラークリア
   - Write 0xFFFFFFFF to PxSERR

6. FIS受信有効化
   - Set PxCMD.FRE = 1
   - Wait for PxCMD.FR = 1 (max 500ms)

7. 割り込み有効化 (optional)
   - Write PxIE (DHRS, SDBS, errors)

8. 割り込みステータスクリア
   - Write 0xFFFFFFFF to PxIS

9. コマンド処理開始
   - Set PxCMD.ST = 1
   - Wait for PxCMD.CR = 1

10. デバイスレディ待機
    - Wait for PxTFD.STS.BSY = 0 and PxTFD.STS.DRQ = 0
```

### 3. COMRESET（通信リセット）

AHCI 1.3.1 Section 10.4.2。

```
1. Port停止
   - Ensure PxCMD.ST = 0 and PxCMD.CR = 0

2. COMRESET開始
   - Write PxSCTL.DET = 1 (initiate)

3. Wait
   - msleep(10) minimum

4. COMRESET解除
   - Write PxSCTL.DET = 0

5. PHY確立待機
   - Wait for PxSSTS.DET = 3 (max 1 second)

6. エラークリア
   - Write 0xFFFFFFFF to PxSERR

7. Success
```

### 4. Port開始

```
1. デバイス接続確認
   - Check PxSSTS.DET = 3

2. FIS受信有効化
   - Set PxCMD.FRE = 1
   - Wait for PxCMD.FR = 1

3. 割り込みクリア
   - Write 0xFFFFFFFF to PxIS

4. コマンド処理開始
   - Set PxCMD.ST = 1
   - Wait for PxCMD.CR = 1

5. デバイスレディ待機
   - Poll PxTFD until BSY=0 and DRQ=0
```

### 5. Port停止

```
1. コマンド処理停止
   - Clear PxCMD.ST (write 0)
   - Wait for PxCMD.CR = 0 (max 500ms)

2. FIS受信停止
   - Clear PxCMD.FRE (write 0)
   - Wait for PxCMD.FR = 0 (max 500ms)

3. Success
```

---

## DMAメモリ構造

### 1. Command List (1KB, 1KB-aligned)

32個のCommand Headerの配列。

```c
struct ahci_cmd_header {
    u16 flags;           /* CFL, A, W, P, R, B, C, PMP */
    u16 prdtl;           /* Physical Region Descriptor Table Length */
    u32 prdbc;           /* PRD Byte Count */
    u32 ctba;            /* Command Table Base Address (low 32-bit) */
    u32 ctbau;           /* Command Table Base Address (upper 32-bit) */
    u32 reserved[4];
} __packed;  /* 32 bytes */

/* Flags */
#define AHCI_CMD_HDR_CFL_MASK  (0x1F << 0)   /* Command FIS Length (DWORDs) */
#define AHCI_CMD_HDR_A         (1 << 5)      /* ATAPI */
#define AHCI_CMD_HDR_W         (1 << 6)      /* Write (H2D) */
#define AHCI_CMD_HDR_P         (1 << 7)      /* Prefetchable */
#define AHCI_CMD_HDR_R         (1 << 8)      /* Reset */
#define AHCI_CMD_HDR_B         (1 << 9)      /* BIST */
#define AHCI_CMD_HDR_C         (1 << 10)     /* Clear Busy upon R_OK */
#define AHCI_CMD_HDR_PMP_MASK  (0xF << 12)   /* Port Multiplier Port */
```

**設定例:**
```c
struct ahci_cmd_header *hdr = &cmd_list[slot];
memset(hdr, 0, sizeof(*hdr));

hdr->flags = 5;  /* FIS length = 5 DWORDs (20 bytes) */
if (is_write)
    hdr->flags |= AHCI_CMD_HDR_W;
hdr->prdtl = num_prdt_entries;
hdr->ctba = (u32)(cmd_table_dma & 0xFFFFFFFF);
hdr->ctbau = (u32)(cmd_table_dma >> 32);
```

### 2. Command Table (Variable size, 128B-aligned)

各コマンド専用のテーブル。

```c
struct ahci_cmd_table {
    u8  cfis[64];        /* Command FIS */
    u8  acmd[16];        /* ATAPI Command */
    u8  reserved[48];
    struct ahci_prdt_entry prdt[];  /* Physical Region Descriptor Table */
} __packed;
```

**最小サイズ**: 128 bytes  
**最大サイズ**: 128 + (65535 * 16) bytes (理論上)

### 3. PRDT Entry (16 bytes)

Scatter-Gatherリスト。

```c
struct ahci_prdt_entry {
    u32 dba;             /* Data Base Address (low) */
    u32 dbau;            /* Data Base Address (upper) */
    u32 reserved;
    u32 dbc;             /* Data Byte Count (bit 0 must be 1, bit 21:1 = byte count - 1) */
} __packed;

#define AHCI_PRDT_DBC_MASK  (0x3FFFFF << 0)  /* Max 4MB per entry */
#define AHCI_PRDT_DBC_INTERRUPT (1 << 31)    /* Interrupt on completion */
```

**設定例:**
```c
struct ahci_prdt_entry *prdt = &cmd_tbl->prdt[0];
prdt->dba = (u32)(sg_buffer_dma & 0xFFFFFFFF);
prdt->dbau = (u32)(sg_buffer_dma >> 32);
prdt->dbc = (128 * 1024 - 1) | 1;  /* 128KB, byte count - 1, bit 0 = 1 */
```

---

## コマンド実行メカニズム

### Non-NCQコマンド（同期）

```
1. Command Headerビルド (slot 0)
   - FIS length = 5
   - W bit for writes
   - PRDTL = number of PRDT entries
   - CTBA/CTBAU = command table DMA address

2. Command FISビルド (H2D)
   - Type = 0x27
   - Command bit = 1
   - Fill command, device, LBA, count, features

3. PRDT設定
   - For each SG buffer:
     - DBA/DBAU = buffer DMA address
     - DBC = size - 1

4. Clear PxIS
   - Write 0xFFFFFFFF

5. Issue command
   - Write (1 << 0) to PxCI

6. Poll for completion
   - While PxCI & 0x1:
     - Check PxIS for errors (TFES, HBFS, IFS)
     - msleep(1)
     - Timeout check

7. Read result
   - Read D2H FIS at offset 0x40
   - Extract status, error, LBA, count

8. Clear PxIS
```

### NCQコマンド（非同期）

```
1. Command Headerビルド (slot N)
   - Same as non-NCQ

2. Command FISビルド (FPDMA QUEUED)
   - Command = 0x60 (READ) or 0x61 (WRITE)
   - Features = sector count
   - Count = tag << 3
   - Device = 0x40
   - LBA = address

3. PRDT設定
   - Same as non-NCQ

4. Issue NCQ command
   - Write (1 << N) to PxSACT
   - Write (1 << N) to PxCI

5. Return immediately (non-blocking)

Completion (separate):
1. Poll PxSACT
   - If bit N cleared → command N completed

2. Read SDB FIS at offset 0x58
   - status, error

3. Mark slot as completed
```

---

## まとめ

本ドキュメントでは、AHCI LLDドライバが使用するハードウェアレジスタ、FIS構造、制御シーケンスを詳述しました。

**重要ポイント:**
- レジスタはメモリマップドI/O
- FISはホスト-デバイス間通信プロトコル
- DMAメモリは適切なアライメントが必須
- Non-NCQは同期、NCQは非同期実行
- エラー処理はPxIS/PxSERRを監視

AHCI 1.3.1仕様に準拠した実装により、信頼性の高いSATAデバイス制御を実現しています。
