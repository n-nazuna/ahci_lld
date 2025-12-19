# AHCI Low Level Driver - 関数API仕様書

このドキュメントは、AHCI Low Level Driver (ahci_lld) が提供する全ての公開関数のAPI仕様を記述します。

## 目次

1. [HBA操作関数](#hba操作関数) - `ahci_lld_hba.c`
2. [ポート操作関数](#ポート操作関数) - `ahci_lld_port.c`
3. [コマンド実行関数](#コマンド実行関数) - `ahci_lld_cmd.c`
4. [DMAバッファ管理関数](#dmaバッファ管理関数) - `ahci_lld_buffer.c`
5. [ユーティリティ関数](#ユーティリティ関数) - `ahci_lld_util.c`

---

## HBA操作関数

### `ahci_hba_reset()`

HBAのハードウェアリセットを実行します。

#### プロトタイプ
```c
int ahci_hba_reset(struct ahci_hba *hba);
```

#### パラメータ
- `hba`: HBA構造体へのポインタ

#### 戻り値
- `0`: 成功
- `-ETIMEDOUT`: リセットがタイムアウト（1秒以内に完了しなかった）

#### 説明
AHCI 1.3.1 Section 10.4.3 に従って、HBAの完全なハードウェアリセットを実行します。

**実行手順:**
1. GHC.HR (HBA Reset) ビットを1にセット
2. GHC.HRがハードウェアによってクリアされるまで待機（最大1秒）
3. リセット完了を確認

**使用タイミング:**
- HBA初期化時
- 重大なエラーからの回復時
- ハードウェアの状態をリセットする必要がある時

**注意事項:**
- リセット後、HBAは初期状態に戻るため、再初期化が必要
- 全てのポートが停止状態になる
- 進行中のコマンドは中断される

#### AHCI仕様書参照
- AHCI 1.3.1 Section 10.4.3 - HBA Reset
- AHCI 1.3.1 Section 3.1.2 - GHC (Global HBA Control)

#### 使用例
```c
struct ahci_hba *hba;
int ret;

ret = ahci_hba_reset(hba);
if (ret) {
    pr_err("HBA reset failed: %d\n", ret);
    return ret;
}

/* HBA reset完了後、再初期化が必要 */
ret = ahci_hba_enable(hba);
```

---

### `ahci_hba_enable()`

AHCIモードを有効化します。

#### プロトタイプ
```c
int ahci_hba_enable(struct ahci_hba *hba);
```

#### パラメータ
- `hba`: HBA構造体へのポインタ

#### 戻り値
- `0`: 成功
- `-EIO`: AHCI有効化失敗

#### 説明
GHC.AE (AHCI Enable) ビットをセットして、AHCIモードを有効化します（AHCI 1.3.1 Section 10.1.2）。

一部のHBAは起動時にレガシーIDEモードで動作しており、このビットをセットすることでAHCIモードに切り替える必要があります。

**実行手順:**
1. GHC.AEビットを1にセット
2. GHC.AEビットがセットされたことを確認

**使用タイミング:**
- HBAリセット後
- ドライバ初期化時

**注意事項:**
- このビットがセットできない場合、HBAはAHCIモードをサポートしていない
- 全てのポート操作の前に実行する必要がある

#### AHCI仕様書参照
- AHCI 1.3.1 Section 10.1.2 - Enable AHCI Mode
- AHCI 1.3.1 Section 3.1.2 - GHC.AE bit

#### 使用例
```c
ret = ahci_hba_enable(hba);
if (ret) {
    dev_err(&pdev->dev, "Failed to enable AHCI mode\n");
    return ret;
}
```

---

## ポート操作関数

### `ahci_port_init()`

ポートを初期化します。

#### プロトタイプ
```c
int ahci_port_init(struct ahci_port_device *port);
```

#### パラメータ
- `port`: ポートデバイス構造体へのポインタ

#### 戻り値
- `0`: 成功
- `-ETIMEDOUT`: 初期化タイムアウト

#### 説明
AHCI 1.3.1 Section 10.3.1 に従って、ポートを初期化します。

**前提条件:**
- PxCLB (Command List Base Address) がDMAバッファアドレスで設定済み
- PxFB (FIS Base Address) がDMAバッファアドレスで設定済み
- DMAバッファが適切にアライメントされている

**実行手順:**
1. ポートがアイドル状態であることを確認（PxCMD.ST=0, CR=0, FRE=0, FR=0）
   - アイドル状態でなければ、まずポートを停止
2. デバイス接続状態を確認（PxSSTS.DET）
3. PxSERRをクリア（全ビットに1を書き込んでRW1C）
4. PxSERR.DIAG.Xをクリア（初期D2H Register FIS受信のため）
5. PxCMD.FRE=1にしてFIS受信を有効化
6. PxCMD.FR=1になるまで待機（最大500ms）
7. ポート割り込みを有効化（PxIE）
8. PxISをクリア（ペンディング割り込みを削除）

**有効化される割り込み:**
- DHRS: D2H Register FIS受信
- ERROR: 各種エラー
- PCS: ポート接続変化
- PRCS: PhyRdy変化

#### AHCI仕様書参照
- AHCI 1.3.1 Section 10.3.1 - Port Initialization
- AHCI 1.3.1 Section 3.3 - Port Registers

#### 使用例
```c
struct ahci_port_device *port;
int ret;

/* DMAバッファを割り当て */
ret = ahci_port_alloc_dma_buffers(port);
if (ret)
    return ret;

/* DMAアドレスをポートレジスタに設定 */
ret = ahci_port_setup_dma(port);
if (ret)
    goto err_free_dma;

/* ポート初期化 */
ret = ahci_port_init(port);
if (ret) {
    dev_err(port->device, "Port init failed\n");
    goto err_free_dma;
}

/* 初期化成功、ポートはFIS受信可能状態 */
```

---

### `ahci_port_cleanup()`

ポートをクリーンアップします。

#### プロトタイプ
```c
void ahci_port_cleanup(struct ahci_port_device *port);
```

#### パラメータ
- `port`: ポートデバイス構造体へのポインタ

#### 戻り値
なし（void）

#### 説明
AHCI 1.3.1 Section 10.3 に従って、ポートを停止し、クリーンアップします。

**実行手順:**
1. ポート割り込みを無効化（PxIE=0）
2. PxISをクリア
3. PxCMD.STをクリアしてコマンド処理を停止
4. PxCMD.CR=0になるまで待機（最大500ms）
5. PxCMD.FREをクリアしてFIS受信を停止
6. PxCMD.FR=0になるまで待機（最大500ms）

**使用タイミング:**
- ドライバのアンロード時
- ポートのシャットダウン時
- エラーからの回復前

**注意事項:**
- タイムアウトが発生してもエラーを返さず、警告ログのみ出力
- DMAバッファの解放は行わない（別途 `ahci_port_free_dma_buffers()` を呼ぶ必要がある）

#### AHCI仕様書参照
- AHCI 1.3.1 Section 10.3.2 - Stop

---

### `ahci_port_comreset()`

ポートでCOMRESETを実行します。

#### プロトタイプ
```c
int ahci_port_comreset(struct ahci_port_device *port);
```

#### パラメータ
- `port`: ポートデバイス構造体へのポインタ

#### 戻り値
- `0`: 成功
- `-ETIMEDOUT`: ポート停止タイムアウト

#### 説明
AHCI 1.3.1 Section 10.4.2 および SATA 3.x Section 10.4 に従って、COMRESET（Communications Reset）シーケンスを実行します。

**COMRESETとは:**
SATAリンクのハードリセットで、以下を実行します:
- PHY層のリセット
- SATAリンク通信の再確立
- 接続デバイスのリセット
- OOB (Out-of-Band) シグナリングと速度ネゴシエーション

**実行手順:**
1. ポートが停止していることを確認（PxCMD.ST=0, CR=0）
   - 実行中の場合はまず停止
2. PxSCTL.DET=1をセット（インターフェース通信初期化を実行）
   - これによりCOMRESETがSATAリンクで開始される
3. 最低1ms待機（仕様上の最小値、実装では10ms）
4. PxSCTL.DET=0をセット（リセット解除）
5. PHYが準備完了になるまで待機（PxSSTS.DET=0x3、最大1秒）
   - DET=0x3: "Device detected and Phy communication established"
6. PxSERRをクリア（リセット中に蓄積されたエラーを削除）

**使用タイミング:**
- デバイス検出時
- リンクエラーからの回復時
- ホットプラグ後
- ポートの完全なリセットが必要な時

**注意事項:**
- COMRESETは破壊的な操作で、進行中のコマンドを中断する
- デバイスが接続されていない場合、PHY準備完了にならないが、これはエラーにしない
- COMRESET後、デバイスは初期状態に戻る

#### AHCI仕様書参照
- AHCI 1.3.1 Section 10.4.2 - Port Reset
- SATA 3.x Section 10.4 - COMRESET
- AHCI 1.3.1 Section 3.3.10 - PxSCTL (SATA Control)
- AHCI 1.3.1 Section 3.3.11 - PxSSTS (SATA Status)

#### 使用例
```c
/* リンクエラーからの回復 */
ret = ahci_port_comreset(port);
if (ret) {
    dev_err(port->device, "COMRESET failed\n");
    return ret;
}

/* COMRESET後、ポートを再起動 */
ret = ahci_port_start(port);
if (ret)
    return ret;
```

---

### `ahci_port_stop()`

ポートを停止します。

#### プロトタイプ
```c
int ahci_port_stop(struct ahci_port_device *port);
```

#### パラメータ
- `port`: ポートデバイス構造体へのポインタ

#### 戻り値
- `0`: 成功
- `-ETIMEDOUT`: ポート停止タイムアウト

#### 説明
AHCI 1.3.1 Section 10.3.2 に従って、ポートのコマンド処理を停止します。

**実行前提:**
以下の操作の前には必ずポートを停止する必要があります:
- COMRESET実行
- PxCLB/PxFBアドレスの変更
- HBAのシャットダウン

**実行手順:**
1. 既に停止している場合は即座に成功を返す
2. PxCMD.ST（Start）ビットをクリア
   - ソフトウェアがコマンド処理の停止を要求
3. PxCMD.CR（Command List Running）がクリアされるまで待機
   - ハードウェアが全コマンドの完了を確認（最大500ms）
4. オプション: PxCMD.FRE（FIS Receive Enable）をクリア
   - デバイスからのFIS受信を停止
5. PxCMD.FR（FIS Receive Running）がクリアされるまで待機

**タイムアウトについて:**
- AHCI仕様では、PxCMD.CRのクリアに最大500msを許容
- この時間内にクリアされない場合は、HBA/デバイスに異常がある可能性

#### AHCI仕様書参照
- AHCI 1.3.1 Section 10.3.2 - Stop
- AHCI 1.3.1 Section 3.3.7 - PxCMD (Port Command and Status)

#### 使用例
```c
/* COMRESET前にポートを停止 */
ret = ahci_port_stop(port);
if (ret) {
    dev_err(port->device, "Failed to stop port\n");
    return ret;
}

/* COMRESETを安全に実行可能 */
ret = ahci_port_comreset(port);
```

---

### `ahci_port_start()`

ポートを開始します。

#### プロトタイプ
```c
int ahci_port_start(struct ahci_port_device *port);
```

#### パラメータ
- `port`: ポートデバイス構造体へのポインタ

#### 戻り値
- `0`: 成功
- `-ETIMEDOUT`: FIS受信有効化タイムアウト

#### 説明
AHCI 1.3.1 Section 10.3.1 に従って、ポートを開始し、コマンド処理を有効化します。

**前提条件:**
- PxCLB (Command List Base) が設定済み
- PxFB (FIS Base) が設定済み
- ポートがアイドル状態（停止状態）

**実行手順:**
1. デバイス接続状態を確認（PxSSTS.DET=0x3）
   - 接続されていない場合は警告を出すが、処理は続行
2. 既に開始している場合は即座に成功を返す
3. PxCMD.FRE=1にしてFIS受信を有効化（まだの場合）
4. PxCMD.FR=1になるまで待機（最大500ms）
5. PxISをクリア（ペンディング割り込みを削除）
6. PxCMD.ST=1にしてコマンド処理を開始
   - コマンドリストからのコマンド処理が有効化される
7. デバイスが準備完了になるまで待機（最大1秒）
   - PxTFDレジスタを監視
   - BSY (Busy) とDRQ (Data Request) ビットがクリアされるのを待つ

**ポート開始後の状態:**
- ポートはコマンドリストからATAコマンドを受け付ける
- PxCIに書き込むことでコマンドを発行可能
- デバイスからのFISを受信可能

#### AHCI仕様書参照
- AHCI 1.3.1 Section 10.3.1 - Start
- AHCI 1.3.1 Section 3.3.7 - PxCMD

#### 使用例
```c
/* ポート初期化後、ポートを開始 */
ret = ahci_port_init(port);
if (ret)
    return ret;

ret = ahci_port_start(port);
if (ret) {
    dev_err(port->device, "Failed to start port\n");
    return ret;
}

/* ポート開始成功、コマンド発行可能 */
```

---

## コマンド実行関数

### `ahci_port_issue_cmd()`

汎用ATAコマンドを発行します。

#### プロトタイプ
```c
int ahci_port_issue_cmd(struct ahci_port_device *port,
                        struct ahci_cmd_request *req,
                        void *buf);
```

#### パラメータ
- `port`: ポートデバイス構造体へのポインタ
- `req`: コマンドリクエスト構造体へのポインタ（入出力パラメータ）
- `buf`: データバッファへのポインタ（READ時は出力、WRITE時は入力）

#### 戻り値
- `0`: 成功
- `-EINVAL`: 無効なパラメータ（ポートが停止、転送サイズ超過など）
- `-ENOMEM`: SGバッファ割り当て失敗
- `-EIO`: コマンドエラー
- `-ETIMEDOUT`: コマンドタイムアウト

#### 説明
AHCI 1.3.1 Section 5 に従って、汎用的なATAコマンドを発行し、結果を取得します。

**サポートするコマンドタイプ:**
- IDENTIFY DEVICE (0xEC)
- READ DMA EXT (0x25)
- WRITE DMA EXT (0x35)
- READ FPDMA QUEUED (0x60) - NCQ
- WRITE FPDMA QUEUED (0x61) - NCQ
- その他の標準ATAコマンド

**実行手順:**
1. ポートが開始状態（PxCMD.ST=1）であることを確認
2. 必要なScatter-Gatherバッファ数を計算
3. SGバッファを確保（不足している場合は動的に追加割り当て）
4. WRITE方向の場合、ユーザーバッファからSGバッファにデータをコピー
5. Command Header（スロット0）を構築
   - CFL (Command FIS Length) = 5 (20バイト ÷ 4)
   - W (Write) ビット: WRITE方向の場合にセット
   - PRDTL (PRDT Length): PRDTエントリ数
6. Command Table内にRegister H2D FISを構築
   - FIS Type = 0x27 (Register H2D)
   - Command, Features, LBA, Count, Device各フィールドを設定
7. PRDT (Physical Region Descriptor Table) を構築
   - 各SGバッファに対応するPRDTエントリを作成
   - DBA (Data Base Address): SGバッファのDMAアドレス
   - DBC (Data Byte Count): 転送バイト数 - 1（0ベース）
8. PxISをクリア
9. PxCIのビット0をセットしてコマンドを発行
10. コマンド完了を待機（ポーリング）
    - PxCIのビット0がクリアされるまで待機
    - タイムアウト: デフォルト5秒（`req->timeout_ms`で変更可能）
11. エラーチェック（PxISの各エラービット）
12. Register D2H FISから結果を抽出
    - status, error, device, LBA, count各フィールド
13. READ方向の場合、SGバッファからユーザーバッファにデータをコピー
14. PxISをクリア

**Command Request構造体:**
```c
struct ahci_cmd_request {
    u8  command;        // ATAコマンドコード
    u16 features;       // Features (15:0)
    u8  device;         // Device register
    u64 lba;            // LBA (47:0)
    u16 count;          // Sector count (15:0)
    u32 flags;          // フラグ (AHCI_CMD_FLAG_WRITE等)
    u32 buffer_len;     // バッファサイズ（バイト）
    u64 buffer;         // ユーザー空間バッファポインタ
    u32 timeout_ms;     // タイムアウト（ミリ秒）
    
    /* 結果フィールド（出力） */
    u8  status;         // D2H FIS status
    u8  error;          // D2H FIS error
    u8  device_out;     // D2H FIS device
    u64 lba_out;        // D2H FIS LBA
    u16 count_out;      // D2H FIS count
};
```

**Scatter-Gather DMA:**
- 1つのSGバッファ: 128KB
- 最大SGバッファ数: 2048個
- 最大転送サイズ: 256MB (128KB × 2048)
- SGバッファは必要に応じて動的に割り当てられる

#### AHCI仕様書参照
- AHCI 1.3.1 Section 5 - Command List and Command Tables
- AHCI 1.3.1 Section 4.2 - Command List Structure
- AHCI 1.3.1 Section 4.2.3 - Command Table
- ATA8-ACS - ATA Command Set

#### 使用例
```c
struct ahci_cmd_request req = {0};
u8 identify_data[512];

/* IDENTIFY DEVICE コマンド */
req.command = 0xEC;
req.features = 0;
req.device = 0;
req.lba = 0;
req.count = 0;
req.flags = 0;  // READ direction
req.buffer_len = 512;
req.timeout_ms = 5000;

ret = ahci_port_issue_cmd(port, &req, identify_data);
if (ret) {
    dev_err(port->device, "IDENTIFY failed: %d\n", ret);
    return ret;
}

/* 結果確認 */
if (req.status & 0x01) {
    dev_err(port->device, "IDENTIFY error: 0x%02x\n", req.error);
    return -EIO;
}

/* identify_dataにデバイス情報が格納されている */
```

---

### `ahci_port_issue_identify()`

IDENTIFY DEVICEコマンドを発行します（互換性関数）。

#### プロトタイプ
```c
int ahci_port_issue_identify(struct ahci_port_device *port, void *buf);
```

#### パラメータ
- `port`: ポートデバイス構造体へのポインタ
- `buf`: 512バイトのバッファ（IDENTIFY DATAを格納）

#### 戻り値
- `ahci_port_issue_cmd()` と同じ

#### 説明
IDENTIFY DEVICE (0xEC) コマンドを発行する簡易ラッパー関数です。内部的に `ahci_port_issue_cmd()` を呼び出します。

#### 使用例
```c
u8 identify_data[512];

ret = ahci_port_issue_identify(port, identify_data);
if (ret == 0) {
    /* identify_dataからデバイス情報を解析 */
}
```

---

## DMAバッファ管理関数

### `ahci_port_alloc_dma_buffers()`

ポート用のDMAバッファを割り当てます。

#### プロトタイプ
```c
int ahci_port_alloc_dma_buffers(struct ahci_port_device *port);
```

#### パラメータ
- `port`: ポートデバイス構造体へのポインタ

#### 戻り値
- `0`: 成功
- `-ENOMEM`: メモリ割り当て失敗

#### 説明
AHCI仕様に従って、ポート操作に必要な全てのDMAバッファを割り当てます。

**割り当てられるバッファ:**

1. **Command List (1KB, 1KB-aligned)**
   - 32個のCommand Headerエントリ（各32バイト）
   - AHCI 1.3.1 Section 4.2.2
   
2. **Received FIS Area (256バイト, 256バイト-aligned)**
   - デバイスから受信したFISを格納
   - D2H Register FIS, PIO Setup FIS, DMA Setup FIS等
   - AHCI 1.3.1 Section 4.2.1
   
3. **Command Table (4KB, 128バイト-aligned)**
   - Command FIS (64バイト)
   - ATAPI Command (16バイト)
   - Reserved (48バイト)
   - PRDT (Physical Region Descriptor Table)
   - AHCI 1.3.1 Section 4.2.3
   
4. **Scatter-Gather Buffers（初期8個、各128KB）**
   - データ転送用のDMAバッファ
   - 必要に応じて最大2048個まで動的拡張可能
   - 初期割り当て: 8個 = 1MB

**アライメント要件:**
- Command List: 1KBアライメント
- FIS Area: 256バイトアライメント
- Command Table: 128バイトアライメント
- SG Buffers: ページアライメント（通常4KB）

**メモリ特性:**
- `dma_alloc_coherent()` を使用
- キャッシュコヒーレント
- HBAから直接アクセス可能

#### AHCI仕様書参照
- AHCI 1.3.1 Section 4.2 - System Memory Structure
- AHCI 1.3.1 Section 4.2.2 - Command List Structure
- AHCI 1.3.1 Section 4.2.1 - Received FIS Structure
- AHCI 1.3.1 Section 4.2.3 - Command Table

#### 使用例
```c
ret = ahci_port_alloc_dma_buffers(port);
if (ret) {
    dev_err(port->device, "DMA buffer allocation failed\n");
    return ret;
}

/* バッファ割り当て成功後、アドレスをポートレジスタに設定 */
ret = ahci_port_setup_dma(port);
```

---

### `ahci_port_free_dma_buffers()`

ポート用のDMAバッファを解放します。

#### プロトタイプ
```c
void ahci_port_free_dma_buffers(struct ahci_port_device *port);
```

#### パラメータ
- `port`: ポートデバイス構造体へのポインタ

#### 戻り値
なし（void）

#### 説明
`ahci_port_alloc_dma_buffers()` で割り当てた全てのDMAバッファを解放します。

**解放されるバッファ:**
1. 全てのScatter-Gatherバッファ
2. Command Table
3. Received FIS Area
4. Command List

**使用タイミング:**
- ポートのシャットダウン時
- ドライバのアンロード時
- エラー処理時

**注意事項:**
- ポートを停止してから呼び出す必要がある
- バッファ使用中に呼び出すと、HBAがアクセス違反を起こす可能性がある

#### 使用例
```c
/* ポート停止 */
ahci_port_stop(port);

/* DMAバッファ解放 */
ahci_port_free_dma_buffers(port);
```

---

### `ahci_port_ensure_sg_buffers()`

必要な数のSGバッファを確保します。

#### プロトタイプ
```c
int ahci_port_ensure_sg_buffers(struct ahci_port_device *port, int needed);
```

#### パラメータ
- `port`: ポートデバイス構造体へのポインタ
- `needed`: 必要なSGバッファ数

#### 戻り値
- `0`: 成功（既に十分なバッファがあるか、追加割り当て成功）
- `-EINVAL`: 要求されたバッファ数が上限を超える
- `-ENOMEM`: バッファ割り当て失敗

#### 説明
指定された数のSGバッファが利用可能であることを保証します。不足している場合は、動的に追加割り当てを行います。

**動作:**
1. 現在のSGバッファ数が`needed`以上なら、即座に成功を返す
2. 不足している場合、追加で割り当て
3. 最大2048個まで割り当て可能

**スレッドセーフ:**
- mutexでロックされており、複数のスレッドから安全に呼び出し可能

**使用タイミング:**
- コマンド発行前（`ahci_port_issue_cmd()` 内で自動的に呼ばれる）
- 大容量転送の準備時

#### 使用例
```c
/* 10MBの転送に必要なSGバッファ数を計算 */
int needed = (10 * 1024 * 1024 + AHCI_SG_BUFFER_SIZE - 1) / AHCI_SG_BUFFER_SIZE;

ret = ahci_port_ensure_sg_buffers(port, needed);
if (ret) {
    dev_err(port->device, "Failed to allocate SG buffers\n");
    return ret;
}

/* SGバッファが十分確保されている */
```

---

### `ahci_port_setup_dma()`

DMAアドレスをポートレジスタに設定します。

#### プロトタイプ
```c
int ahci_port_setup_dma(struct ahci_port_device *port);
```

#### パラメータ
- `port`: ポートデバイス構造体へのポインタ

#### 戻り値
- `0`: 成功
- 負の値: エラー

#### 説明
割り当て済みのDMAバッファのアドレスを、ポートレジスタ（PxCLB, PxFB）に設定します。

**設定されるレジスタ:**
- PxCLB/PxCLBU: Command List Base Address (64-bit)
- PxFB/PxFBU: FIS Base Address (64-bit)

**前提条件:**
- `ahci_port_alloc_dma_buffers()` でバッファが割り当て済み
- ポートが停止状態

#### AHCI仕様書参照
- AHCI 1.3.1 Section 3.3.1 - PxCLB
- AHCI 1.3.1 Section 3.3.3 - PxFB

---

## ユーティリティ関数

### `ahci_wait_bit_clear()`

レジスタビットがクリアされるまで待機します。

#### プロトタイプ
```c
int ahci_wait_bit_clear(void __iomem *mmio, u32 reg, u32 mask,
                        int timeout_ms, struct device *dev,
                        const char *bit_name);
```

#### パラメータ
- `mmio`: MMIOベースアドレス
- `reg`: レジスタオフセット
- `mask`: 監視するビットマスク
- `timeout_ms`: タイムアウト時間（ミリ秒）
- `dev`: デバイス構造体（ログ用、NULLも可）
- `bit_name`: ビット名（ログ用、NULLも可）

#### 戻り値
- `0`: ビットがクリアされた
- `-ETIMEDOUT`: タイムアウト

#### 説明
指定されたレジスタの指定ビットが0になるまでポーリングで待機します。

**ポーリング間隔:**
- 1msごとにレジスタを読み取り

**使用例:**
- PxCMD.CR（Command List Running）のクリア待ち
- PxCMD.FR（FIS Receive Running）のクリア待ち
- GHC.HR（HBA Reset）のクリア待ち

#### 使用例
```c
/* PxCMD.CRがクリアされるまで最大500ms待機 */
ret = ahci_wait_bit_clear(port_mmio, AHCI_PORT_CMD,
                           AHCI_PORT_CMD_CR, 500,
                           port->device, "PxCMD.CR");
if (ret) {
    dev_err(port->device, "Timeout waiting for CR to clear\n");
    return ret;
}
```

---

### `ahci_wait_bit_set()`

レジスタビットがセットされるまで待機します。

#### プロトタイプ
```c
int ahci_wait_bit_set(void __iomem *mmio, u32 reg, u32 mask,
                      int timeout_ms, struct device *dev,
                      const char *bit_name);
```

#### パラメータ
- `mmio`: MMIOベースアドレス
- `reg`: レジスタオフセット
- `mask`: 監視するビットマスク
- `timeout_ms`: タイムアウト時間（ミリ秒）
- `dev`: デバイス構造体（ログ用、NULLも可）
- `bit_name`: ビット名（ログ用、NULLも可）

#### 戻り値
- `0`: ビットがセットされた
- `-ETIMEDOUT`: タイムアウト

#### 説明
指定されたレジスタの指定ビットが1になるまでポーリングで待機します。

**ポーリング間隔:**
- 1msごとにレジスタを読み取り

**使用例:**
- PxCMD.FR（FIS Receive Running）のセット待ち
- PxSSTS.DET（Device Detection）の確認

#### 使用例
```c
/* PxCMD.FRがセットされるまで最大500ms待機 */
ret = ahci_wait_bit_set(port_mmio, AHCI_PORT_CMD,
                         AHCI_PORT_CMD_FR, 500,
                         port->device, "PxCMD.FR");
if (ret) {
    dev_err(port->device, "Failed to enable FIS reception\n");
    return ret;
}
```

---

## 付録

### エラーコード一覧

| エラーコード | 意味 | 主な発生原因 |
|------------|------|-------------|
| `-ETIMEDOUT` | タイムアウト | HBAレジスタの変化が期限内に完了しない |
| `-ENOMEM` | メモリ不足 | DMAバッファ割り当て失敗 |
| `-EINVAL` | 無効な引数 | パラメータが範囲外、ポートが不正な状態 |
| `-EIO` | I/Oエラー | ATAコマンド実行エラー、HBAエラー |
| `-EFAULT` | メモリアクセスエラー | ユーザー空間とのデータコピー失敗 |

### タイムアウト値の選定基準

AHCI仕様書で規定されている値、または実装上の経験値に基づいています:

| 操作 | タイムアウト | 根拠 |
|-----|----------|------|
| HBAリセット | 1秒 | AHCI 1.3.1: "1 second is enough" |
| PxCMD.CRクリア | 500ms | AHCI 1.3.1: "software should wait at least 500ms" |
| COMRESET | 10ms | SATA仕様: "at least 1ms"、余裕を持って10ms |
| PHY準備完了 | 1秒 | 実装上の経験値 |
| ATAコマンド | 5秒 | デフォルト値、長時間コマンドは調整可能 |

### DMAバッファアライメント要件

AHCI 1.3.1 Section 4.2 で規定:

| バッファ | サイズ | アライメント |
|---------|-------|-------------|
| Command List | 1KB (32 entries × 32 bytes) | 1KB |
| Received FIS | 256 bytes | 256 bytes |
| Command Table | 可変 (最小128 bytes) | 128 bytes |
| PRDT Entry | 16 bytes | - |

### AHCI仕様書のバージョンと参照

このドライバは以下の仕様書に基づいて実装されています:

- **AHCI 1.3.1** - Serial ATA Advanced Host Controller Interface (AHCI) Revision 1.3.1, July 2011
- **SATA 3.x** - Serial ATA Revision 3.x
- **ATA8-ACS** - ATA/ATAPI Command Set

主要なセクション:
- Section 3: Register descriptions
- Section 4: System Memory Structures
- Section 5: Command List and Command Tables
- Section 10: Software Guidelines (重要)

---

## 関連ドキュメント

- [README.md](README.md) - ドライバの概要と使用方法
- [COMMAND_ISSUE_SPEC.md](COMMAND_ISSUE_SPEC.md) - ATAコマンド発行機能の詳細仕様
- [ahci_lld_ioctl.h](ahci_lld_ioctl.h) - IOCTLインターフェース定義

---

最終更新: 2025-12-20
