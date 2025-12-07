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

## 主な用途

- AHCIプロトコルの学習・実験
- カスタムATAコマンドのテスト
- デバイスの低レベル診断
- ストレージデバイスのファームウェア開発支援

## 特徴

### 1. ユーザー空間からの直接制御

IOCTLインターフェースを通じて、カーネル空間を経由せずにAHCIハードウェアを操作できます。

```c
// ポートリセット → 起動 → IDENTIFYコマンド発行
ioctl(fd, AHCI_IOC_PORT_RESET, NULL);
ioctl(fd, AHCI_IOC_PORT_START, NULL);
ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
```

### 2. Scatter-Gather DMA転送

大容量データ転送を効率的に処理します。

- **バッファサイズ**: 128 KB × 最大2048個
- **最大転送サイズ**: 256 MB
- **動的割り当て**: 必要に応じて自動拡張

### 3. 詳細なコマンド結果

Register D2H FISから完全な結果情報を取得できます。

```c
req.status;      // 0x50 = 正常完了
req.error;       // エラーコード
req.lba_out;     // 完了時のLBA
req.count_out;   // 完了時のセクタ数
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

## 制限事項

- **同時発行**: 1ポートにつき1コマンドのみ（NCQ未対応）
- **割り込み**: ポーリングモード（割り込み未使用）
- **ATAPI**: 未対応
- **Port Multiplier**: 未対応
- **Hot Plug**: 未対応

## 注意事項

⚠️ **このドライバーは実験的/教育的な目的で開発されています。**

- 本番環境での使用は推奨しません
- データ損失のリスクがあります
- 既存のlibataドライバーとの競合を避けるため、適切な設定が必要です

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
