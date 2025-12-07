# AHCI Low Level Driver (ahci_lld)

## 概要

AHCI Low Level Driverは、AHCIコントローラーの各ポートを独立したキャラクタデバイスとして公開するLinux kernelドライバーです。従来のブロックデバイスとしてではなく、キャラクタデバイスとして実装することで、ポートレベルでの直接的な制御を可能にします。

また、HBA全体の制御用に `/dev/ahci_lld_ghc` デバイスを提供し、Global HBA Control (GHC) レジスタへの直接アクセスを可能にします。

## 特徴

- **ポート単位のデバイス**: 各AHCIポートが独立した `/dev/ahci_lld_pN` デバイスとして公開
- **GHC制御デバイス**: `/dev/ahci_lld_ghc` でHBA全体のレジスタ操作が可能
- **AHCI仕様準拠**: AHCI 1.3.1仕様に基づいた実装
  - ポート初期化・クリーンアップ (Section 10.3)
  - COMRESET実装 (Section 10.4.2)
  - ポート停止・開始 (Section 10.3.1, 10.3.2)
- **共通ユーティリティ**: レジスタポーリングなどの共通処理を関数化
- **IOCTL API**: ユーザー空間からのポート制御インターフェース

## アーキテクチャ

```mermaid
graph TB
    subgraph "User Space"
        APP[Application]
    end
    
    subgraph "Kernel Space"
        DEV0["Device: /dev/ahci_lld_p0"]
        DEV1["Device: /dev/ahci_lld_p1"]
        DEV2["Device: /dev/ahci_lld_p2"]
        DEVGHC["Device: /dev/ahci_lld_ghc<br/>GHC Control"]
        
        MAIN["ahci_lld_main.c<br/>PCI & Device Management"]
        HBA["ahci_lld_hba.c<br/>HBA Operations"]
        PORT["ahci_lld_port.c<br/>Port Operations"]
        UTIL["ahci_lld_util.c<br/>Common Utilities"]
        REG["ahci_lld_reg.h<br/>Register Definitions"]
        
        HEADER["ahci_lld.h<br/>Common Definitions"]
    end
    
    subgraph "Hardware"
        PCI["PCI Device<br/>AHCI Controller"]
        AHCI_PORT0["AHCI Port 0"]
        AHCI_PORT1["AHCI Port 1"]
        AHCI_PORT2["AHCI Port 2"]
    end
    
    APP -->|open/read/write/ioctl| DEV0
    APP -->|open/read/write/ioctl| DEV1
    APP -->|open/read/write/ioctl| DEV2
    APP -->|read/write GHC regs| DEVGHC
    
    DEV0 --> MAIN
    DEV1 --> MAIN
    DEV2 --> MAIN
    DEVGHC --> MAIN
    
    MAIN --> HBA
    MAIN --> PORT
    HBA --> UTIL
    PORT --> UTIL
    HBA --> HEADER
    PORT --> HEADER
    MAIN --> HEADER
    HEADER --> REG
    
    MAIN -->|PCI Access| PCI
    HBA -->|Configure / Reset| PCI
    PORT -->|Port manipulate| PCI
    
    PCI --> AHCI_PORT0
    PCI --> AHCI_PORT1
    PCI --> AHCI_PORT2
```

## モジュール構成

### ファイル構成

```
ahci_lld/
├── ahci_lld.h              # 共通ヘッダーファイル
├── ahci_lld_reg.h          # AHCIレジスタ定義
├── ahci_lld_ioctl.h        # IOCTL定義（ユーザー空間共有用）
├── ahci_lld_main.c         # メインモジュール（PCI、デバイス管理）
├── ahci_lld_hba.c          # HBA操作モジュール
├── ahci_lld_port.c         # ポート操作モジュール
├── ahci_lld_util.c         # 共通ユーティリティ関数
├── ahci_lld_buffer.c       # DMAバッファ管理（未実装）
├── test_port_start_stop.c  # ポート制御テストプログラム
├── Makefile                # ビルド設定
├── README.md               # このファイル
└── serial-ata-ahci-spec-rev1-3-1-3.pdf  # AHCI 1.3.1仕様書
```

### モジュール間の責務分離

| モジュール | 責務 |
|-----------|------|
| `ahci_lld_main.c` | PCIデバイスの検出・登録、キャラクタデバイスの作成・管理、ドライバーのライフサイクル管理、GHCデバイスの実装、IOCTLハンドラ |
| `ahci_lld_hba.c` | HBAレベルの操作（リセット、AHCI有効化、グローバル設定） |
| `ahci_lld_port.c` | ポートレベルの操作（初期化、クリーンアップ、COMRESET、停止、開始） |
| `ahci_lld_util.c` | 共通ユーティリティ関数（レジスタポーリング、タイムアウト処理） |
| `ahci_lld_reg.h` | AHCIレジスタ定義（オフセット、ビットマスク、定数） |
| `ahci_lld_ioctl.h` | IOCTL定義とデータ構造（ユーザー空間とカーネル空間で共有） |
| `ahci_lld_buffer.c` | Data IO用DMAバッファの割り当て・管理（将来実装予定） |

## データ構造

### `struct ahci_hba`

HBAの状態を管理する構造体。

```c
struct ahci_hba {
    struct pci_dev *pdev;                      // PCIデバイス
    void __iomem *mmio;                        // MMIO領域のベースアドレス
    size_t mmio_size;                          // MMIO領域のサイズ
    
    u32 ports_impl;                            // Ports Implemented (PI) レジスタ値
    int n_ports;                               // 実装されているポート数
    
    struct ahci_port_device *ports[AHCI_MAX_PORTS];  // ポートデバイスの配列
    struct ahci_ghc_device *ghc_dev;           // GHC制御デバイス
    
    dev_t dev_base;                            // デバイス番号のベース
    struct class *class;                       // デバイスクラス
};
```

### `struct ahci_port_device`

各ポートのキャラクタデバイス情報を管理する構造体。

```c
struct ahci_port_device {
    struct cdev cdev;                          // キャラクタデバイス
    struct device *device;                     // デバイスオブジェクト
    dev_t devno;                               // デバイス番号
    int port_no;                               // ポート番号
    void __iomem *port_mmio;                   // ポートMMIO領域
    struct ahci_hba *hba;                      // 親HBA構造体へのポインタ
};
```

### `struct ahci_ghc_device`

GHC (Global HBA Control) 制御用デバイスの情報を管理する構造体。

```c
struct ahci_ghc_device {
    struct cdev cdev;                          // キャラクタデバイス
    struct device *device;                     // デバイスオブジェクト
    dev_t devno;                               // デバイス番号
    void __iomem *mmio;                        // HBA全体のMMIO領域
    struct ahci_hba *hba;                      // 親HBA構造体へのポインタ
};
```

## 関数リファレンス

### ahci_lld_main.c

#### モジュール初期化・終了

##### `ahci_lld_init()`
```c
static int __init ahci_lld_init(void)
```
**役割**: ドライバーモジュールの初期化  
**処理内容**:
1. キャラクタデバイスのメジャー番号を動的に割り当て
2. デバイスクラス `/sys/class/ahci_lld` を作成
3. PCIドライバーを登録

**返り値**:
- `0`: 成功
- 負の値: エラーコード

---

##### `ahci_lld_exit()`
```c
static void __exit ahci_lld_exit(void)
```
**役割**: ドライバーモジュールの終了処理  
**処理内容**:
1. PCIドライバーの登録解除
2. デバイスクラスの破棄
3. キャラクタデバイスの登録解除

---

#### PCIドライバー関数

##### `ahci_lld_probe()`
```c
static int ahci_lld_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
```
**役割**: PCIデバイスが検出された際の初期化処理  
**処理内容**:
1. HBA構造体の割り当て
2. PCIデバイスの有効化
3. バスマスタDMAの有効化
4. PCI領域（BAR5）の要求とMMIOマッピング
5. HBAのリセットとAHCIモード有効化（`ahci_hba_reset()`, `ahci_hba_enable()`呼び出し）
6. Ports Implemented (PI) レジスタの読み取り
7. 実装されているポートごとにキャラクタデバイスを作成

**引数**:
- `pdev`: 検出されたPCIデバイス
- `ent`: マッチしたPCI IDエントリ

**返り値**:
- `0`: 成功
- 負の値: エラーコード

**エラー処理**: リソース確保失敗時は、確保済みリソースを逆順で解放

---

##### `ahci_lld_remove()`
```c
static void ahci_lld_remove(struct pci_dev *pdev)
```
**役割**: PCIデバイスが取り外された際のクリーンアップ  
**処理内容**:
1. 全ポートデバイスの破棄
2. MMIOマッピングの解除
3. PCI領域の解放
4. PCIデバイスの無効化
5. HBA構造体の解放

**引数**:
- `pdev`: 取り外されるPCIデバイス

---

#### ポートデバイス管理

##### `ahci_create_port_device()`
```c
static int ahci_create_port_device(struct ahci_hba *hba, int port_no)
```
**役割**: 指定されたポート番号のキャラクタデバイスを作成  
**処理内容**:
1. `ahci_port_device`構造体の割り当て
2. ポート番号とMMIOアドレスの設定
3. キャラクタデバイス（cdev）の初期化と追加
4. `/dev/ahci_lld_pX` デバイスノードの作成
5. HBAのポート配列に登録

**引数**:
- `hba`: HBA構造体
- `port_no`: ポート番号（0-31）

**返り値**:
- `0`: 成功
- 負の値: エラーコード

**デバイス名**: `/dev/ahci_lld_p{port_no}`

---

##### `ahci_destroy_port_device()`
```c
static void ahci_destroy_port_device(struct ahci_hba *hba, int port_no)
```
**役割**: 指定されたポート番号のキャラクタデバイスを破棄  
**処理内容**:
1. デバイスノードの削除
2. キャラクタデバイスの削除
3. `ahci_port_device`構造体の解放

**引数**:
- `hba`: HBA構造体
- `port_no`: ポート番号（0-31）

---

#### ファイルオペレーション

##### `ahci_lld_open()`
```c
static int ahci_lld_open(struct inode *inode, struct file *file)
```
**役割**: デバイスファイルのオープン処理  
**処理内容**:
1. `inode`からポートデバイス構造体を取得
2. `file->private_data`にポートデバイスへのポインタを保存
3. オープンログを出力

**返り値**: 常に `0` （成功）

---

##### `ahci_lld_release()`
```c
static int ahci_lld_release(struct inode *inode, struct file *file)
```
**役割**: デバイスファイルのクローズ処理  
**処理内容**:
1. クローズログを出力

**返り値**: 常に `0` （成功）

---

##### `ahci_lld_read()`
```c
static ssize_t ahci_lld_read(struct file *file, char __user *buf,
                              size_t count, loff_t *ppos)
```
**役割**: デバイスからのデータ読み取り（将来実装予定）  
**現在の実装**: スタブ

**返り値**: `-EINVAL` （未実装）

---

##### `ahci_lld_write()`
```c
static ssize_t ahci_lld_write(struct file *file, const char __user *buf,
                               size_t count, loff_t *ppos)
```
**役割**: デバイスへのデータ書き込み（将来実装予定）  
**現在の実装**: スタブ

**返り値**: `-EINVAL` （未実装）

---

##### `ahci_lld_ioctl()`
```c
static long ahci_lld_ioctl(struct file *file, unsigned int cmd,
                            unsigned long arg)
```
**役割**: デバイス制御コマンドの実行（将来実装予定）  
**現在の実装**: スタブ

**返り値**: `-ENOTTY` （未実装）

---

### ahci_lld_hba.c

#### HBA操作関数

##### `ahci_hba_reset()`
```c
int ahci_hba_reset(struct ahci_hba *hba)
```
**役割**: HBAのハードウェアリセットを実行  
**処理内容**:
1. GHC (Global HBA Control) レジスタの HR (HBA Reset) ビットをセット
2. HRビットがクリアされるまで待機（最大1000ms）
3. タイムアウトチェック

**仕様準拠**: AHCI仕様 Section 10.4.2 HBA Reset

**引数**:
- `hba`: HBA構造体

**返り値**:
- `0`: 成功
- `-ETIMEDOUT`: リセットタイムアウト

**タイミング**: 
- リセット完了までの待機: 最大1000ms
- ポーリング間隔: 1ms

**レジスタ操作**:
- `GHC (0x04)`: HR bit (bit 0) をセット/クリア確認

---

##### `ahci_hba_enable()`
```c
int ahci_hba_enable(struct ahci_hba *hba)
```
**役割**: AHCIモードを有効化  
**処理内容**:
1. GHC (Global HBA Control) レジスタの AE (AHCI Enable) ビットをセット
2. AEビットがセットされたことを確認

**仕様準拠**: AHCI仕様 Section 3.1.1 GHC - Global HBA Control

**引数**:
- `hba`: HBA構造体

**返り値**:
- `0`: 成功
- `-EIO`: AHCI有効化失敗

**レジスタ操作**:
- `GHC (0x04)`: AE bit (bit 31) をセット/確認

**注意**: 
- リセット後はAEビットがクリアされるため、必ず`ahci_hba_reset()`の後に実行する必要がある

---

### ahci_lld_port.c

#### ポート操作関数

##### `ahci_port_init()`
```c
int ahci_port_init(struct ahci_port_device *port)
```
**役割**: AHCI仕様書 Section 10.3.1 に従ってポートを初期化  
**処理内容**:
1. PxCMD.ST, CR, FRE, FR が全て0（アイドル状態）であることを確認
2. 必要に応じてポートを停止し、CRとFRのクリアを待機
3. PxSSTSでデバイス接続状態を確認
4. PxSERRをクリア（DIAG.Xを含む）
5. PxCMD.FREを有効化してFIS受信を開始
6. 割り込みを有効化（DHRS, ERROR, PCS, PRCS）

**仕様準拠**: AHCI仕様 Section 10.3 Port Initialization

**引数**:
- `port`: ポートデバイス構造体

**返り値**:
- `0`: 成功
- `-ETIMEDOUT`: タイムアウト

**レジスタ操作**:
- `PxCMD`: ST, FRE, CR, FRビットの制御
- `PxSSTS`: デバイス検出状態の確認
- `PxSERR`: エラーレジスタのクリア
- `PxIE`: 割り込み有効化
- `PxIS`: 割り込みステータスのクリア

---

##### `ahci_port_cleanup()`
```c
void ahci_port_cleanup(struct ahci_port_device *port)
```
**役割**: AHCI仕様書 Section 10.3 に従ってポートを停止  
**処理内容**:
1. 割り込みを無効化
2. PxISをクリア
3. PxCMD.STをクリアし、CRのクリアを待機
4. PxCMD.FREをクリアし、FRのクリアを待機

**引数**:
- `port`: ポートデバイス構造体

**レジスタ操作**:
- `PxIE`: 割り込み無効化
- `PxIS`: 割り込みステータスのクリア
- `PxCMD`: ST, FREビットのクリア、CR, FRのクリア待機

---

##### `ahci_port_comreset()`
```c
int ahci_port_comreset(struct ahci_port_device *port)
```
**役割**: AHCI仕様書 Section 10.4.2 に従ってCOMRESETを実行  
**処理内容**:
1. ポートが動作中の場合は停止
2. PxSCTL.DET = 1 (COMRESET開始)
3. 10ms待機
4. PxSCTL.DET = 0 (COMRESET解除)
5. PxSSTS.DET = 3 (PHY確立) まで待機 (最大1000ms)
6. PxSERRをクリア

**仕様準拠**: AHCI仕様 Section 10.4.2 Port Reset

**引数**:
- `port`: ポートデバイス構造体

**返り値**:
- `0`: 成功
- 負の値: エラーコード

**タイミング**: 
- COMRESET保持時間: 10ms
- PHY確立待機: 最大1000ms

**レジスタ操作**:
- `PxSCTL`: DETフィールド (bit 3-0) の操作
- `PxSSTS`: DETフィールド (bit 3-0) の確認
- `PxSERR`: エラーレジスタのクリア

---

##### `ahci_port_stop()`
```c
int ahci_port_stop(struct ahci_port_device *port)
```
**役割**: AHCI仕様書 Section 10.3.2 に従ってポートを停止  
**処理内容**:
1. PxCMD.STをクリア
2. PxCMD.CRのクリアを待機 (最大500ms)
3. オプション: PxCMD.FREをクリアしてFIS受信を停止
4. オプション: PxCMD.FRのクリアを待機 (最大500ms)

**仕様準拠**: AHCI仕様 Section 10.3.2 System Software Specific Initialization

**引数**:
- `port`: ポートデバイス構造体

**返り値**:
- `0`: 成功
- `-ETIMEDOUT`: タイムアウト

**レジスタ操作**:
- `PxCMD`: ST, FREビットのクリア、CR, FRビットの確認

---

##### `ahci_port_start()`
```c
int ahci_port_start(struct ahci_port_device *port)
```
**役割**: AHCI仕様書 Section 10.3.1 に従ってポートを開始  
**処理内容**:
1. PxSSTSでデバイス接続を確認
2. PxCMD.FREを有効化してFIS受信を開始
3. PxCMD.FRの立ち上がりを待機 (最大500ms)
4. PxISをクリア
5. PxCMD.STを有効化してコマンド処理を開始

**前提条件**: PxCLB/PxFBが設定済みであること

**仕様準拠**: AHCI仕様 Section 10.3.1 System Software Specific Initialization

**引数**:
- `port`: ポートデバイス構造体

**返り値**:
- `0`: 成功
- `-ETIMEDOUT`: タイムアウト

**レジスタ操作**:
- `PxSSTS`: デバイス検出状態の確認
- `PxCMD`: FRE, STビットの設定、FRビットの確認
- `PxIS`: 割り込みステータスのクリア

---

### ahci_lld_util.c

#### 共通ユーティリティ関数

##### `ahci_wait_bit_clear()`
```c
int ahci_wait_bit_clear(void __iomem *mmio, u32 reg, u32 mask,
                        int timeout_ms, struct device *dev, const char *bit_name)
```
**役割**: レジスタの指定ビットがクリアされるまでポーリング  
**処理内容**:
1. タイムアウトするまで1msごとにレジスタを読み取り
2. 指定ビットがクリアされたら成功
3. タイムアウト時はエラーログを出力

**パラメータ**:
- `mmio`: MMIOベースアドレス
- `reg`: レジスタオフセット
- `mask`: 監視するビットマスク
- `timeout_ms`: タイムアウト時間（ミリ秒）
- `dev`: デバイス構造体（ログ用、NULLも可）
- `bit_name`: ビット名（ログ用、NULLも可）

**返り値**:
- `0`: 成功
- `-ETIMEDOUT`: タイムアウト

---

##### `ahci_wait_bit_set()`
```c
int ahci_wait_bit_set(void __iomem *mmio, u32 reg, u32 mask,
                      int timeout_ms, struct device *dev, const char *bit_name)
```
**役割**: レジスタの指定ビットがセットされるまでポーリング  
**処理内容**: `ahci_wait_bit_clear()` と同様だが、ビットのセットを待機

**パラメータ**: `ahci_wait_bit_clear()` と同じ

**返り値**:
- `0`: 成功
- `-ETIMEDOUT`: タイムアウト

---

## デバイスインターフェース

### ポートデバイス (`/dev/ahci_lld_pN`)

各AHCIポート用のキャラクタデバイス（N = 0〜31）

**現在の実装**:
- `open()`: ポートデバイスをオープン
- `release()`: ポートデバイスをクローズ
- `ioctl()`: ポート制御コマンド
  - `AHCI_IOC_PORT_RESET`: COMRESETによるポートリセット
  - `AHCI_IOC_PORT_START`: ポート開始（FIS受信/コマンド処理有効化）
  - `AHCI_IOC_PORT_STOP`: ポート停止
  - `AHCI_IOC_ISSUE_CMD`: コマンド発行（未実装）
  - `AHCI_IOC_READ_REGS`: レジスタダンプ取得（未実装）
- `read()`: 未実装
- `write()`: 未実装

### GHCデバイス (`/dev/ahci_lld_ghc`)

HBA全体の制御用デバイス

**実装機能**:
- `open()`: GHCデバイスをオープン
- `release()`: GHCデバイスをクローズ
- `read()`: Generic Host Control領域（0x00-0xFF）のレジスタ読み取り
  - 4バイト単位でレジスタ値を読み取り
  - `lseek()`でオフセット指定可能
- `write()`: レジスタへの書き込み
  - 4バイト単位でレジスタ値を書き込み
  - 読み取り専用レジスタ（CAP, PI, VS, CAP2）への書き込みは拒否
  - `lseek()`でオフセット指定可能
- `ioctl()`: 未実装（将来的にHBAリセットなどの特殊操作を実装予定）

**使用例**:
```bash
# CAPレジスタ（offset 0x00）を読み取り
sudo dd if=/dev/ahci_lld_ghc bs=4 count=1 skip=0 | xxd

# GHCレジスタ（offset 0x04）を読み取り
sudo dd if=/dev/ahci_lld_ghc bs=4 count=1 skip=1 | xxd
```

---

## レジスタマップ

### Generic Host Control レジスタ

| オフセット | 名前 | 説明 |
|-----------|------|------|
| 0x00 | CAP | HBA Capabilities |
| 0x04 | GHC | Global HBA Control |
| 0x08 | IS | Interrupt Status |
| 0x0C | PI | Ports Implemented |

### Port レジスタ (Base + 0x100 + port * 0x80)

| オフセット | 名前 | 説明 |
|-----------|------|------|
| 0x00 | PxCLB | Command List Base Address |
| 0x04 | PxCLBU | Command List Base Address Upper 32-bits |
| 0x08 | PxFB | FIS Base Address |
| 0x0C | PxFBU | FIS Base Address Upper 32-bits |
| 0x10 | PxIS | Interrupt Status |
| 0x14 | PxIE | Interrupt Enable |
| 0x18 | PxCMD | Command and Status |
| 0x20 | PxTFD | Task File Data |
| 0x28 | PxSSTS | SATA Status |
| 0x2C | PxSCTL | SATA Control |
| 0x30 | PxSERR | SATA Error |
| 0x34 | PxSACT | SATA Active |
| 0x38 | PxCI | Command Issue |

## ビルド方法

```bash
# モジュールのビルド
make

# モジュールのロード
sudo insmod ahci_lld.ko

# または make経由
make load

# モジュールのアンロード
sudo rmmod ahci_lld

# または make経由
make unload

# クリーンビルド
make clean
```

## 使用方法

### 1. 既存ドライバーのアンバインド

```bash
# SATAコントローラーのPCIアドレスを確認
lspci | grep SATA

# 既存ドライバーからアンバインド（例: 0000:00:17.0）
echo "0000:00:17.0" | sudo tee /sys/bus/pci/drivers/ahci/unbind
```

### 2. ドライバーのロード

```bash
sudo insmod ahci_lld.ko
```

### 3. デバイスの確認

```bash
# デバイスファイルの確認
ls -l /dev/ahci_lld*

# カーネルログの確認
dmesg | tail

# バインド状態の確認
lspci -k -s 00:17.0
```

### 4. ポート操作のテスト

```bash
# テストプログラムのコンパイル
gcc -o test_port_start_stop test_port_start_stop.c

# テストの実行（PORT_START → PORT_STOP → PORT_RESET → PORT_START）
sudo ./test_port_start_stop

# カーネルログで詳細を確認
sudo dmesg | tail -40
```

## IOCTL API

### ポート制御コマンド

ユーザー空間から `ioctl()` を使用してポートを制御できます。

#### `AHCI_IOC_PORT_RESET` (0x4101)
**機能**: COMRESETによるポートリセット  
**引数**: なし  
**戻り値**: 成功時 0、失敗時 -1

```c
int fd = open("/dev/ahci_lld_p0", O_RDWR);
if (ioctl(fd, AHCI_IOC_PORT_RESET) < 0) {
    perror("PORT_RESET failed");
}
close(fd);
```

#### `AHCI_IOC_PORT_START` (0x4102)
**機能**: ポートの開始（FIS受信とコマンド処理を有効化）  
**引数**: なし  
**戻り値**: 成功時 0、失敗時 -1

```c
int fd = open("/dev/ahci_lld_p0", O_RDWR);
if (ioctl(fd, AHCI_IOC_PORT_START) < 0) {
    perror("PORT_START failed");
}
close(fd);
```

#### `AHCI_IOC_PORT_STOP` (0x4103)
**機能**: ポートの停止  
**引数**: なし  
**戻り値**: 成功時 0、失敗時 -1

```c
int fd = open("/dev/ahci_lld_p0", O_RDWR);
if (ioctl(fd, AHCI_IOC_PORT_STOP) < 0) {
    perror("PORT_STOP failed");
}
close(fd);
```

#### `AHCI_IOC_ISSUE_CMD` (0xC0404110A)
**機能**: AHCIコマンドの発行  
**引数**: `struct ahci_cmd_request *`  
**戻り値**: 成功時 0、失敗時 -1  
**実装状況**: 未実装

#### `AHCI_IOC_READ_REGS` (0x80484114)
**機能**: ポートレジスタのダンプ取得  
**引数**: `struct ahci_port_regs *`  
**戻り値**: 成功時 0、失敗時 -1  
**実装状況**: 未実装

---

## 動作確認済み環境

- カーネルバージョン: 6.14.0-36-generic
- ディストリビューション: Ubuntu 24.04
- コントローラー: Intel Cannon Lake PCH SATA AHCI Controller (8086:a352)

## 今後の実装予定

### 短期（フェーズ1）
- [x] ポート初期化処理の実装 (AHCI 10.3.1準拠)
- [x] ポートクリーンアップの実装 (AHCI 10.3準拠)
- [x] COMRESET実装 (AHCI 10.4.2準拠)
- [x] PORT_START/STOP実装 (AHCI 10.3.1/10.3.2準拠)
- [x] IOCTLインターフェースの基本実装
- [ ] READ_REGS ioctl実装
- [ ] DMAバッファ管理の実装 (`ahci_lld_buffer.c`)
- [ ] Command List / FIS 構造体の定義

### 中期（フェーズ2）
- [ ] IDENTIFY DEVICE コマンドの実装
- [ ] READ/WRITE コマンドの実装
- [ ] ISSUE_CMD ioctl実装
- [ ] エラーハンドリングの強化

### 長期（フェーズ3）
- [ ] NCQ (Native Command Queuing) サポート
- [ ] ホットプラグ対応
- [ ] パワーマネジメント
- [ ] 割り込みハンドラの実装

## ライセンス

GPL v2

## 参考資料

- [Serial ATA AHCI Specification](https://www.intel.com/content/www/us/en/io/serial-ata/ahci.html)
- Linux Kernel Documentation: drivers/ata/
