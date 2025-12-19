# AHCI Low Level Driver (ahci_lld)

Linux用の低レベルAHCI（Advanced Host Controller Interface）ドライバー実装です。  
AHCIハードウェアを直接制御し、ユーザー空間から任意のATAコマンドを発行できます。

## 概要

このドライバーは、AHCIコントローラーをユーザー空間から直接操作するための実験的/教育的実装です。  
既存のlibataドライバーとは別に動作し、以下の機能を提供します：

- **ポート制御**: COMRESET、ポート起動/停止
- **ATAコマンド発行**: IDENTIFY DEVICE、READ/WRITE DMA EXTなど
- **大容量転送**: Scatter-Gather DMAで最大256MB転送対応
- **詳細な結果取得**: D2H FISからステータス、エラー、LBA、カウント情報を取得

### AHCI/SATA仕様準拠

このドライバーは以下の業界標準仕様に基づいて実装されています：

- **AHCI 1.3.1** (Serial ATA Advanced Host Controller Interface, Rev 1.3.1)
- **SATA 3.x** (Serial ATA Specification)
- **ATA8-ACS** (ATA/ATAPI Command Set)

全ての関数とデータ構造は仕様書のセクション番号と共に文書化されています。

## 主な用途

- AHCIプロトコルの学習・実験
- カスタムATAコマンドのテスト
- デバイスの低レベル診断
- ストレージデバイスのファームウェア開発支援
- SATA/AHCIの実装研究

## アーキテクチャ

```
User Space Application
        ↓ (IOCTL)
Character Device (/dev/ahci_lld_p*)
        ↓
ahci_lld Driver
        ↓
AHCI HBA (Hardware)
        ↓ (SATA Link)
SATA Device (HDD/SSD)
```

### モジュール構成

| モジュール | 役割 | AHCI仕様参照 |
|-----------|------|-------------|
| `ahci_lld_main.c` | ドライバ初期化、IOCTL処理 | - |
| `ahci_lld_hba.c` | HBAリセット、AHCI有効化 | Section 10.4.3 |
| `ahci_lld_port.c` | ポート初期化、開始/停止、COMRESET | Section 10.3, 10.4.2 |
| `ahci_lld_cmd.c` | ATAコマンド発行、FIS構築 | Section 5 |
| `ahci_lld_buffer.c` | DMAバッファ管理 | Section 4.2 |
| `ahci_lld_util.c` | レジスタポーリングなど | - |

## 特徴

### 1. ユーザー空間からの直接制御

IOCTLインターフェースを通じて、カーネル空間を経由せずにAHCIハードウェアを操作できます。

```c
// ポートリセット → 起動 → IDENTIFYコマンド発行
ioctl(fd, AHCI_IOC_PORT_RESET, NULL);    // COMRESET (AHCI 10.4.2)
ioctl(fd, AHCI_IOC_PORT_START, NULL);    // Start port (AHCI 10.3.1)
ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);     // Issue ATA command
```

### 2. Scatter-Gather DMA転送

AHCI 1.3.1 Section 4.2のPRDT（Physical Region Descriptor Table）を使用した効率的なデータ転送。

- **バッファサイズ**: 128 KB × 最大2048個
- **最大転送サイズ**: 256 MB (512 byte/sector × 524,288 sectors)
- **動的割り当て**: 必要に応じて自動拡張
- **アライメント**: DMAコヒーレント、ページアライメント

### 3. 詳細なコマンド結果

Register D2H FIS（AHCI 1.3.1 Section 10.5.6）から完全な結果情報を取得できます。

```c
req.status;      // 0x50 = 正常完了 (DRDY, no errors)
req.error;       // エラーコード (ATA Error register)
req.lba_out;     // 完了時のLBA (48-bit)
req.count_out;   // 完了時のセクタ数 (16-bit)
req.device_out;  // Device register
```

## クイックスタート

### 1. ビルド

```bash
make
```

### 2. モジュールロード

```bash
sudo insmod ahci_lld.ko
```

デバイスファイルが作成されます: `/dev/ahci_lld_p0` ～ `/dev/ahci_lld_p5`

### 3. テスト実行

```bash
# IDENTIFY DEVICEコマンド
gcc -o test_identify test_identify.c
sudo ./test_identify 0

# 1MB読み書きテスト
gcc -o test_rw_1m test_rw_1m.c
sudo ./test_rw_1m 0
```

### 4. モジュールアンロード

```bash
sudo rmmod ahci_lld
```

## 使用例

### IDENTIFY DEVICEコマンド

```c
#include "ahci_lld_ioctl.h"

int fd = open("/dev/ahci_lld_p0", O_RDWR);
struct ahci_cmd_request req = {0};
uint8_t buffer[512];

// COMRESET & Port Start
ioctl(fd, AHCI_IOC_PORT_RESET, NULL);
ioctl(fd, AHCI_IOC_PORT_START, NULL);

// IDENTIFY DEVICE
req.command = 0xEC;
req.device = 0x40;
req.buffer = (__u64)(unsigned long)buffer;
req.buffer_len = 512;
req.flags = 0;

ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);

printf("Status: 0x%02x\n", req.status);
// Parse IDENTIFY data from buffer...
```

### READ DMA EXTコマンド

```c
// 1MBデータを読み込み (2048セクタ)
uint8_t *buffer = malloc(1024 * 1024);

req.command = 0x25;           // READ DMA EXT
req.device = 0x40;
req.lba = 100;                // LBA 100から開始
req.count = 2048;             // 2048セクタ = 1MB
req.buffer = (__u64)(unsigned long)buffer;
req.buffer_len = 1024 * 1024;
req.flags = 0;                // READ direction

ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
```

### WRITE DMA EXTコマンド

```c
req.command = 0x35;                    // WRITE DMA EXT
req.flags = AHCI_CMD_FLAG_WRITE;       // WRITE direction
// 他は同様...
```

## ドキュメント

### 📚 仕様書・リファレンス

- **[FUNCTION_API.md](FUNCTION_API.md)**: 全関数のAPI仕様書 ★NEW★
  - 全公開関数の詳細仕様（引数、戻り値、動作、エラー処理）
  - AHCI仕様書の対応セクション参照
  - 使用例とベストプラクティス
  - タイムアウト値とアライメント要件

- **[COMMAND_ISSUE_SPEC.md](COMMAND_ISSUE_SPEC.md)**: ATAコマンド発行機能の詳細仕様
  - IOCTL API仕様
  - データ構造
  - Scatter-Gather DMA転送の説明
  - サンプルコード
  - エラーハンドリング
  - Mermaid図（シーケンス、メモリレイアウト、状態遷移）

- **[README_DETAIL.md](README_DETAIL.md)**: 実装詳細とAPIリファレンス
  - アーキテクチャ詳細
  - モジュール構成
  - 関数リファレンス
  - データ構造定義

### 📖 AHCI/SATA仕様書

本ドライバを深く理解するには、以下の公式仕様書を参照してください：

- **AHCI 1.3.1 Specification** - Serial ATA Advanced Host Controller Interface
  - [Intel公式サイトからダウンロード可能]
  - 特に重要なセクション:
    - Section 3: Register descriptions
    - Section 4: System Memory Structures
    - Section 5: Command List and Command Tables
    - Section 10: Software Guidelines ★必読★

- **SATA 3.x Specification** - Serial ATA International Organization
- **ATA8-ACS** - ATA/ATAPI Command Set

## 対応環境

- **Linux Kernel**: 6.x系（6.14.0-36で動作確認）
- **アーキテクチャ**: x86_64
- **AHCI仕様**: AHCI 1.3.1準拠
- **SATA仕様**: SATA Revision 3.5 Gold準拠

### 動作確認済みハードウェア

- Intel Cannon Lake PCH SATA AHCI Controller (8086:a352)
- WDC WD5000AZLX-08K2TA0 (500GB HDD, SATA Gen3, NCQ 32)

## ファイル構成

```
ahci_lld/
├── ahci_lld.h              # ヘッダーファイル（構造体、定数）
├── ahci_lld_ioctl.h        # IOCTL定義
├── ahci_lld_fis.h          # FIS構造体定義
├── ahci_lld_main.c         # モジュール初期化、IOCTLハンドラ
├── ahci_lld_hba.c          # HBA操作
├── ahci_lld_port.c         # ポート制御
├── ahci_lld_buffer.c       # DMAバッファ管理（Scatter-Gather）
├── ahci_lld_cmd.c          # コマンド実行
├── ahci_lld_util.c         # ユーティリティ関数
├── test_identify.c         # IDENTIFYコマンドテスト
├── test_read_dma.c         # READ DMA EXTテスト
├── test_rw_1m.c            # 1MB読み書きテスト
├── COMMAND_ISSUE_SPEC.md   # コマンド発行機能仕様書
└── README_DETAIL.md        # 詳細ドキュメント
```

## IOCTL API

| IOCTL | 説明 |
|-------|------|
| `AHCI_IOC_PORT_RESET` | COMRESET（ポートリセット） |
| `AHCI_IOC_PORT_START` | ポート起動（FIS受信開始） |
| `AHCI_IOC_PORT_STOP` | ポート停止 |
| `AHCI_IOC_ISSUE_CMD` | ATAコマンド発行 |
| `AHCI_IOC_READ_REGS` | ポートレジスタダンプ |

詳細は[COMMAND_ISSUE_SPEC.md](COMMAND_ISSUE_SPEC.md)を参照してください。

## パフォーマンス

実測値（WDC WD5000AZLX-08K2TA0、SATA Gen3）:

| 操作 | 転送サイズ | 時間 | スループット |
|------|-----------|------|-------------|
| READ DMA EXT | 512 B | ~5 ms | - |
| READ DMA EXT | 1 MB | ~5.5 ms | ~180 MB/s |
| WRITE DMA EXT | 1 MB | ~5.5 ms | ~180 MB/s |

## トラブルシューティング

### コマンドがタイムアウトする

**症状**: `ahci_port_issue_cmd()` が `-ETIMEDOUT` を返す

**原因と対策**:
1. **ポートが開始していない**
   - `ahci_port_start()` を先に呼ぶ
   - `PxCMD.ST=1` であることを確認

2. **デバイスが接続されていない**
   - `PxSSTS.DET=0x3` を確認
   - COMRESETを実行: `ioctl(fd, AHCI_IOC_PORT_RESET, NULL)`

3. **デバイスがBUSY状態**
   - `PxTFD` レジスタを確認
   - ソフトリセットやハードリセットが必要な場合がある

### ポート起動に失敗する

**症状**: `ahci_port_start()` が `-ETIMEDOUT` を返す

**原因**: PxCMD.FRがセットされない

**対策**:
- PxCLB/PxFBが正しく設定されているか確認
- DMAバッファが適切にアライメントされているか確認
- `dmesg` でカーネルログを確認

### データ破損・読み書きエラー

**症状**: 読み書きしたデータが正しくない

**原因と対策**:
1. **バッファアライメント不良**
   - SGバッファはDMAコヒーレントである必要がある
   - `dma_alloc_coherent()` を使用

2. **転送サイズの誤り**
   - セクタ数 × 512バイト = バッファサイズ
   - LBAとカウントが範囲内か確認

3. **WRITE方向フラグの誤り**
   - WRITE時は `req.flags = AHCI_CMD_FLAG_WRITE` を設定

### HBAリセットが完了しない

**症状**: `ahci_hba_reset()` がタイムアウト

**対策**:
- BIOSでAHCIモードが有効か確認
- PCIコンフィグスペースが正しく設定されているか確認
- 他のドライバ（libata）がデバイスを使用していないか確認

### デバッグ方法

```bash
# カーネルログを監視
sudo dmesg -w

# レジスタダンプ
sudo cat /sys/kernel/debug/ahci_lld/port0/registers

# より詳細なログ
sudo insmod ahci_lld.ko dyndbg=+p
```

## 制限事項

### 現在の実装

| 機能 | 対応状況 | 備考 |
|-----|---------|------|
| コマンド同時発行 | ❌ | 1ポートにつき1コマンドのみ |
| NCQ | ❌ | Native Command Queuing未対応 |
| 割り込み | ❌ | ポーリングモードのみ |
| ATAPI | ❌ | CD/DVDドライブ未対応 |
| Port Multiplier | ❌ | 複数デバイス接続未対応 |
| Hot Plug | ❌ | 動的なデバイス着脱未対応 |
| Power Management | ❌ | スリープ/省電力モード未対応 |
| 64-bit DMA | ✅ | 対応済み |
| 48-bit LBA | ✅ | 大容量ドライブ対応 |

### AHCI仕様との差異

- **Command Slot**: 1個のみ使用（仕様では最大32個）
- **割り込み処理**: 未実装（ポーリングで代替）
- **エラーリカバリ**: 基本的な処理のみ
- **FIS自動受信**: 未使用

## 注意事項

### ⚠️ 重要な警告

**このドライバーは実験的/教育的な目的で開発されています。**

- ✋ 本番環境での使用は推奨しません
- ⚠️ データ損失のリスクがあります
- 🔧 適切なバックアップを取ってから使用してください
- 🚫 重要なデータが入ったドライブでは使用しないでください

### libataとの競合回避

既存のlibataドライバーと同じデバイスを使用する場合、競合が発生します。

**回避方法1**: libataをアンバインド
```bash
# libataからデバイスをアンバインド
echo "0000:00:17.0" | sudo tee /sys/bus/pci/drivers/ahci/unbind

# ahci_lldをロード
sudo insmod ahci_lld.ko

# 使用後、libataに戻す
sudo rmmod ahci_lld
echo "0000:00:17.0" | sudo tee /sys/bus/pci/drivers/ahci/bind
```

**回避方法2**: カーネルパラメータで特定のデバイスを除外
```bash
# /etc/default/grubに追加
GRUB_CMDLINE_LINUX="ahci.ignore_sss=1"
```

### セキュリティ

- このドライバーは `CAP_SYS_ADMIN` 権限（通常はroot）が必要です
- デバイスファイルのパーミッションに注意してください
- 悪意あるコマンドを発行すると、デバイスが損傷する可能性があります

## ライセンス

GPL v2

## 参考仕様

- **AHCI Specification**: Revision 1.3.1
- **SATA Specification**: Revision 3.5 Gold
- **ATA/ATAPI Command Set**: ACS-4

## 開発者

n-nazuna

## 変更履歴

- **2025-12-07**: 初版リリース
  - ATAコマンド発行機能実装
  - Scatter-Gather DMA転送対応（最大256MB）
  - IDENTIFY DEVICE、READ/WRITE DMA EXT対応
  - D2H FIS結果取得機能
