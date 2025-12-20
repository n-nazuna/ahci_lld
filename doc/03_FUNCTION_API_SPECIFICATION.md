# AHCI LLD 関数API仕様書

**AHCI Low Level Driver - 内部関数API仕様**

Version: 1.0  
Date: 2025-12-20

---

## 目次

1. [概要](#概要)
2. [HBA操作関数](#hba操作関数)
3. [ポート操作関数](#ポート操作関数)
4. [コマンド実行関数](#コマンド実行関数)
5. [スロット管理関数](#スロット管理関数)
6. [DMAバッファ管理関数](#dmaバッファ管理関数)
7. [ユーティリティ関数](#ユーティリティ関数)
8. [デバイス管理関数](#デバイス管理関数)

---

## 概要

AHCI LLDドライバの内部関数APIドキュメント。カーネルモジュール内で使用される関数の仕様を定義します。

### エクスポート関数

主要な関数は`EXPORT_SYMBOL_GPL()`でエクスポートされ、他のカーネルモジュールから使用可能です。

### ファイル構成

| ファイル | 役割 |
|---------|------|
| `ahci_lld_main.c` | ドライバエントリ、デバイス管理、IOCTL |
| `ahci_lld_hba.c` | HBA初期化・制御 |
| `ahci_lld_port.c` | ポート初期化・制御 |
| `ahci_lld_cmd.c` | コマンド実行 |
| `ahci_lld_slot.c` | NCQスロット管理 |
| `ahci_lld_buffer.c` | DMAバッファ管理 |
| `ahci_lld_util.c` | ユーティリティ関数 |

---

## HBA操作関数

### ahci_hba_reset

**宣言:**
```c
int ahci_hba_reset(struct ahci_hba *hba);
```

**目的:** HBA（Host Bus Adapter）のハードウェアリセットを実行

**パラメータ:**
- `hba`: HBA構造体ポインタ

**動作:**
1. `GHC.HR`ビットを1に設定
2. `GHC.HR`が0になるまで待機（最大1秒）
3. タイムアウトチェック

**戻り値:**
- `0`: 成功
- `-ETIMEDOUT`: リセットタイムアウト（1秒以内に完了せず）

**副作用:**
- HBAの全状態がリセットされる
- 全ポートが停止
- DMA転送が中断される

**使用例:**
```c
ret = ahci_hba_reset(hba);
if (ret) {
    dev_err(&pdev->dev, "HBA reset failed\n");
    return ret;
}
```

**呼び出し元:** `ahci_lld_probe()`

---

### ahci_hba_enable

**宣言:**
```c
int ahci_hba_enable(struct ahci_hba *hba);
```

**目的:** AHCIモードを有効化

**パラメータ:**
- `hba`: HBA構造体ポインタ

**動作:**
1. `GHC.AE`（AHCI Enable）ビットを1に設定
2. 設定確認のため再読み込み
3. ビットがセットされていることを確認

**戻り値:**
- `0`: 成功
- `-EIO`: AHCI有効化失敗

**注意事項:**
- HBAリセット後は必ず再度実行が必要
- AHCIモードが有効でない場合、レジスタアクセス不可

**使用例:**
```c
ret = ahci_hba_enable(hba);
if (ret) {
    dev_err(&pdev->dev, "Failed to enable AHCI mode\n");
    return ret;
}
```

**呼び出し元:** `ahci_lld_probe()`, `ahci_hba_reset()`後

---

## ポート操作関数

### ahci_port_init

**宣言:**
```c
int ahci_port_init(struct ahci_port_device *port);
```

**目的:** ポートの初期化（AHCI 1.3.1 Section 10.3.1準拠）

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ

**動作:**
1. ポート停止（実行中の場合）
2. デバイス接続確認（`PxSSTS.DET=3`）
3. `PxSERR`クリア
4. `DIAG.X`ビットクリア（初回D2H FIS受信許可）
5. FIS受信有効化（`PxCMD.FRE=1`）
6. `PxCMD.FR=1`待機（最大500ms）
7. 割り込み有効化（`PxIE`設定）
8. `PxIS`クリア
9. NCQ関連変数初期化

**戻り値:**
- `0`: 成功
- `-ETIMEDOUT`: FIS受信有効化タイムアウト

**副作用:**
- ポートがコマンド受付可能状態になる
- NCQモードは無効（初期状態）

**使用例:**
```c
ret = ahci_port_init(port);
if (ret) {
    dev_err(port->device, "Port initialization failed\n");
    return ret;
}
```

**呼び出し元:** `ahci_create_port_device()`

---

### ahci_port_cleanup

**宣言:**
```c
void ahci_port_cleanup(struct ahci_port_device *port);
```

**目的:** ポートのクリーンアップ

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ

**動作:**
1. ポート停止（`ahci_port_stop()`）
2. 割り込み無効化（`PxIE=0`）
3. `PxIS`クリア

**戻り値:** なし（void）

**副作用:**
- ポートが使用不可になる
- 実行中のコマンドは中断される

**使用例:**
```c
ahci_port_cleanup(port);
```

**呼び出し元:** `ahci_destroy_port_device()`

---

### ahci_port_comreset

**宣言:**
```c
int ahci_port_comreset(struct ahci_port_device *port);
```

**目的:** COMRESET（通信リセット）実行

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ

**動作:**
1. ポート停止確認（`PxCMD.ST=0`, `CR=0`）
2. `PxSCTL.DET=1`設定（COMRESET開始）
3. 10ms待機
4. `PxSCTL.DET=0`設定（COMRESET解除）
5. `PxSSTS.DET=3`待機（最大1秒）
6. `PxSERR`クリア

**戻り値:**
- `0`: 成功
- `-ETIMEDOUT`: PHY確立タイムアウト

**副作用:**
- SATAリンクが再確立される
- デバイス状態がリセットされる

**使用例:**
```c
ret = ahci_port_comreset(port);
if (ret) {
    dev_err(port->device, "COMRESET failed\n");
    return ret;
}
```

**呼び出し元:** IOCTLハンドラ `AHCI_IOC_PORT_RESET`

---

### ahci_port_start

**宣言:**
```c
int ahci_port_start(struct ahci_port_device *port);
```

**目的:** ポートのコマンド処理を開始

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ

**動作:**
1. デバイス接続確認（`PxSSTS.DET=3`）
2. FIS受信有効化（`PxCMD.FRE=1`）
3. `PxCMD.FR=1`待機
4. `PxIS`クリア
5. コマンド処理開始（`PxCMD.ST=1`）
6. `PxCMD.CR=1`待機
7. デバイスレディ待機（`PxTFD: BSY=0, DRQ=0`、最大1秒）

**戻り値:**
- `0`: 成功
- `-ETIMEDOUT`: タイムアウト

**副作用:**
- ポートがコマンド受付可能になる

**使用例:**
```c
ret = ahci_port_start(port);
if (ret) {
    dev_err(port->device, "Port start failed\n");
    return ret;
}
```

**呼び出し元:** IOCTLハンドラ `AHCI_IOC_PORT_START`

---

### ahci_port_stop

**宣言:**
```c
int ahci_port_stop(struct ahci_port_device *port);
```

**目的:** ポートのコマンド処理を停止

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ

**動作:**
1. `PxCMD.ST=0`設定
2. `PxCMD.CR=0`待機（最大500ms）
3. `PxCMD.FRE=0`設定
4. `PxCMD.FR=0`待機（最大500ms）

**戻り値:**
- `0`: 成功
- `-ETIMEDOUT`: タイムアウト

**副作用:**
- 実行中のコマンドは中断される可能性
- ポートがコマンド受付不可になる

**使用例:**
```c
ret = ahci_port_stop(port);
if (ret) {
    dev_warn(port->device, "Port stop timeout\n");
}
```

**呼び出し元:** 
- IOCTLハンドラ `AHCI_IOC_PORT_STOP`
- `ahci_port_cleanup()`
- `ahci_port_init()`

---

## コマンド実行関数

### ahci_port_issue_cmd

**宣言:**
```c
int ahci_port_issue_cmd(struct ahci_port_device *port,
                        struct ahci_cmd_request *req,
                        void *buf);
```

**目的:** ATAコマンドを同期的に実行（Non-NCQ）

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ
- `req`: コマンドリクエスト構造体（入出力）
- `buf`: データバッファ（カーネル空間、READの場合結果格納）

**動作:**
1. ポート開始確認（`PxCMD.ST=1`）
2. スロット0のコマンドヘッダ設定
   - FIS長=5 DWORDs
   - Wビット（書き込みの場合）
   - PRDTL=SGバッファ数
   - CTBA=コマンドテーブルDMAアドレス
3. Command FIS（H2D）構築
   - FISタイプ=0x27
   - コマンドビット=1
   - ATA command, device, LBA, count, features設定
4. PRDT構築
   - WRITEの場合: `buf`からSGバッファへコピー
   - 各PRDTエントリにSGバッファDMAアドレス設定
5. `PxIS`クリア
6. コマンド発行（`PxCI`ビット0を1に）
7. 完了待機（`PxCI`ビット0が0になるまでポーリング）
   - 1msごとにチェック
   - `timeout_ms`でタイムアウト
   - `PxIS`でエラーチェック（TFES, HBFS, IFS）
8. 結果読み取り（D2H FIS、オフセット0x40）
   - status, error, device, LBA, count抽出
9. READの場合: SGバッファから`buf`へコピー
10. `PxIS`クリア

**戻り値:**
- `0`: 成功
- `-EINVAL`: 無効なパラメータ（ポート未開始等）
- `-ENOMEM`: SGバッファ割り当て失敗
- `-ETIMEDOUT`: コマンドタイムアウト
- `-EIO`: ハードウェアI/Oエラー

**副作用:**
- スロット0を占有（完了まで他のコマンド発行不可）
- SGバッファ使用

**注意事項:**
- ブロッキング関数（完了まで待機）
- スロット0専用（Non-NCQ）
- `buf`はカーネル空間ポインタ

**使用例:**
```c
struct ahci_cmd_request req;
uint8_t buffer[512];

memset(&req, 0, sizeof(req));
req.command = 0x25;  /* READ DMA EXT */
req.device = 0x40;
req.lba = 0x1000;
req.count = 1;
req.timeout_ms = 5000;

ret = ahci_port_issue_cmd(port, &req, buffer);
if (ret) {
    dev_err(port->device, "Command failed: %d\n", ret);
} else {
    pr_info("Status: 0x%02x\n", req.status);
    /* buffer contains read data */
}
```

**呼び出し元:** IOCTLハンドラ `AHCI_IOC_ISSUE_CMD`（同期モード）

---

### ahci_port_issue_identify

**宣言:**
```c
int ahci_port_issue_identify(struct ahci_port_device *port, void *buf);
```

**目的:** IDENTIFY DEVICEコマンドの便利ラッパー

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ
- `buf`: 512バイトバッファ（IDENTIFY結果格納）

**動作:**
1. `ahci_cmd_request`構造体を準備
   - command = 0xEC (IDENTIFY DEVICE)
   - device = 0x40
   - count = 1
   - timeout = 5000ms
2. `ahci_port_issue_cmd()`を呼び出し

**戻り値:**
- `0`: 成功
- その他: `ahci_port_issue_cmd()`のエラーコード

**使用例:**
```c
uint8_t identify_data[512];
ret = ahci_port_issue_identify(port, identify_data);
if (ret == 0) {
    /* Parse IDENTIFY data */
    uint16_t *words = (uint16_t *)identify_data;
    bool ncq_supported = !!(words[76] & (1 << 8));
}
```

**呼び出し元:** デバイス初期化、診断

---

### ahci_port_issue_cmd_async

**宣言:**
```c
int ahci_port_issue_cmd_async(struct ahci_port_device *port,
                               struct ahci_cmd_request *req,
                               void *buf);
```

**目的:** NCQコマンドを非同期的に発行

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ
- `req`: コマンドリクエスト（`req->tag`でスロット指定）
- `buf`: データバッファ（カーネル空間）

**動作:**
1. NCQモード有効化（初回のみ）
2. ユーザ指定タグ検証（0-31）
3. スロット空き確認
   - `slots_in_use`ビットマップでチェック
   - 使用中の場合`-EBUSY`
4. スロットマーク（使用中に設定）
5. スロット情報保存
   - リクエストコピー
   - バッファポインタ保存
6. コマンドテーブル割り当て（未割り当ての場合）
7. コマンドヘッダ設定
8. Command FIS（H2D）構築
   - ユーザ指定の`req->count`, `req->features`をそのまま使用
9. PRDT構築
   - WRITEの場合: データコピー
10. NCQコマンド発行
    - `PxSACT`にスロットビット設定
    - `PxCI`にスロットビット設定
11. **即座にリターン**（完了待機なし）

**戻り値:**
- `0`: 成功（コマンド発行完了、実行は非同期）
- `-EINVAL`: 無効なタグ
- `-EBUSY`: スロット使用中
- `-ENOMEM`: コマンドテーブル割り当て失敗

**副作用:**
- スロットを占有（`ahci_free_slot()`で解放必要）
- バッファはコマンド完了まで保持される

**注意事項:**
- ノンブロッキング（即座にリターン）
- 完了確認は`ahci_check_slot_completion()`で行う
- `buf`の寿命管理はドライバ責任

**使用例:**
```c
struct ahci_cmd_request req;
uint8_t *buffer = kmalloc(512, GFP_KERNEL);

memset(&req, 0, sizeof(req));
req.command = 0x60;        /* READ FPDMA QUEUED */
req.features = 1;          /* Sector count */
req.device = 0x40;
req.lba = 0x1000;
req.count = (5 << 3);      /* Tag 5 in bits 7:3 */
req.tag = 5;               /* Slot number */
req.flags = AHCI_CMD_FLAG_NCQ;

ret = ahci_port_issue_cmd_async(port, &req, buffer);
if (ret) {
    kfree(buffer);
    dev_err(port->device, "NCQ issue failed: %d\n", ret);
} else {
    /* Command issued, will complete asynchronously */
    /* Must call ahci_check_slot_completion() later */
}
```

**呼び出し元:** IOCTLハンドラ `AHCI_IOC_ISSUE_CMD`（NCQモード）

---

## スロット管理関数

### ahci_alloc_slot

**宣言:**
```c
int ahci_alloc_slot(struct ahci_port_device *port);
```

**目的:** 空きコマンドスロットを割り当て

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ

**動作:**
1. `slot_lock`スピンロック取得
2. `slots_in_use`ビットマップから最初の0ビットを検索
3. 空きスロットがない場合、`-EBUSY`
4. スロットビットを1に設定
5. `active_slots`カウンタをインクリメント
6. スピンロック解放
7. スロット番号を返す

**戻り値:**
- `0-31`: 割り当てられたスロット番号
- `-EBUSY`: 空きスロットなし（全32スロット使用中）

**副作用:**
- `slots_in_use`ビットマップ更新
- `active_slots`カウンタ更新

**使用例:**
```c
int slot = ahci_alloc_slot(port);
if (slot < 0) {
    dev_warn(port->device, "No free slots\n");
    return -EBUSY;
}
pr_info("Allocated slot %d\n", slot);
```

**呼び出し元:** 
- （現在は未使用、将来の自動割り当て用）
- `ahci_port_issue_cmd_async()`は`req->tag`を直接使用

---

### ahci_free_slot

**宣言:**
```c
void ahci_free_slot(struct ahci_port_device *port, int slot);
```

**目的:** 使用済みスロットを解放

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ
- `slot`: 解放するスロット番号（0-31）

**動作:**
1. スロット番号検証（0-31範囲外でエラーログ）
2. `slot_lock`スピンロック取得
3. スロット未使用チェック（警告ログ）
4. `slots_in_use`ビットクリア
5. `slots_completed`ビットクリア
6. `active_slots`カウンタをデクリメント
7. スロット情報を0クリア
8. スピンロック解放

**戻り値:** なし（void）

**副作用:**
- スロット情報削除
- ビットマップ更新

**使用例:**
```c
ahci_free_slot(port, slot);
```

**呼び出し元:** IOCTLハンドラ `AHCI_IOC_PROBE_CMD`（完了後）

---

### ahci_mark_slot_completed

**宣言:**
```c
void ahci_mark_slot_completed(struct ahci_port_device *port,
                               int slot,
                               int result);
```

**目的:** スロットを完了状態にマーク

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ
- `slot`: スロット番号
- `result`: 結果コード（0=成功、負=エラー）

**動作:**
1. スロット番号検証
2. `slot_lock`スピンロック取得
3. スロット使用中確認
4. `slots_completed`ビット設定
5. `port->slots[slot].completed = true`
6. `port->slots[slot].result = result`
7. `ncq_completed`カウンタをインクリメント
8. スピンロック解放

**戻り値:** なし（void）

**副作用:**
- 完了ビットマップ更新
- 統計カウンタ更新

**使用例:**
```c
ahci_mark_slot_completed(port, slot, 0);  /* Mark success */
```

**呼び出し元:** `ahci_check_slot_completion()`

---

### ahci_check_slot_completion

**宣言:**
```c
u32 ahci_check_slot_completion(struct ahci_port_device *port);
```

**目的:** NCQコマンドの完了をポーリング検出

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ

**動作:**
1. `PxSACT`レジスタ読み取り
2. `slot_lock`スピンロック取得
3. 全スロット（0-31）をスキャン:
   - `slots_in_use`で使用中スロットのみ
   - `completed`フラグでスキップ（既完了）
   - `PxSACT`でビットクリア確認 → 完了
4. 完了スロットごとに:
   - SDB FIS（オフセット0x58）読み取り
   - status, error抽出
   - `req`に格納
   - `lba_out`, `count_out`は元の値保持
   - `slots_completed`ビット設定
   - `completed`フラグ設定
   - `ncq_completed`インクリメント
   - `newly_completed`ビットマップに追加
5. スピンロック解放
6. 新規完了ビットマップを返す

**戻り値:**
- `u32`: 新規完了スロットのビットマップ

**副作用:**
- 完了スロットの`req`構造体更新
- 完了ビットマップ更新

**注意事項:**
- ノンブロッキング（即座にリターン）
- 完了コマンドがなくても正常（`0`を返す）
- 同じスロットは一度しか完了扱いにならない

**使用例:**
```c
u32 completed = ahci_check_slot_completion(port);
if (completed) {
    for (int i = 0; i < 32; i++) {
        if (completed & (1 << i)) {
            pr_info("Slot %d completed: status=0x%02x\n",
                    i, port->slots[i].req.status);
        }
    }
}
```

**呼び出し元:** IOCTLハンドラ `AHCI_IOC_PROBE_CMD`

---

## DMAバッファ管理関数

### ahci_port_alloc_dma_buffers

**宣言:**
```c
int ahci_port_alloc_dma_buffers(struct ahci_port_device *port);
```

**目的:** ポート用DMAバッファを割り当て

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ

**動作:**
1. **Command List割り当て** (1KB, 1KB-aligned)
   - `dma_alloc_coherent()`
   - `port->cmd_list`と`port->cmd_list_dma`に保存
2. **FIS Area割り当て** (256B, 256B-aligned)
   - `dma_alloc_coherent()`
   - `port->fis_area`と`port->fis_area_dma`に保存
3. **Command Table割り当て** (スロット0のみ、4KB)
   - `dma_alloc_coherent()`
   - `port->cmd_tables[0]`と`port->cmd_tables_dma[0]`に保存
4. **SG Buffer配列初期化**
   - 8個の128KBバッファ割り当て
   - `port->sg_buffers[]`と`port->sg_buffers_dma[]`に保存
   - `port->num_sg_buffers = 8`
5. **Mutex初期化**
   - `port->sg_mutex`

**戻り値:**
- `0`: 成功
- `-ENOMEM`: メモリ割り当て失敗

**副作用:**
- DMAメモリ割り当て
- 各種ポインタ設定

**エラー処理:**
- 途中失敗時、既に割り当てたメモリを解放してエラーリターン

**使用例:**
```c
ret = ahci_port_alloc_dma_buffers(port);
if (ret) {
    dev_err(port->device, "DMA buffer allocation failed\n");
    return ret;
}
```

**呼び出し元:** `ahci_create_port_device()`

---

### ahci_port_free_dma_buffers

**宣言:**
```c
void ahci_port_free_dma_buffers(struct ahci_port_device *port);
```

**目的:** ポート用DMAバッファを解放

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ

**動作:**
1. **SG Buffers解放**
   - 全`sg_buffers[]`を`dma_free_coherent()`
2. **Command Tables解放**
   - 全`cmd_tables[]`を`dma_free_coherent()`
3. **FIS Area解放**
   - `dma_free_coherent()`
4. **Command List解放**
   - `dma_free_coherent()`

**戻り値:** なし（void）

**副作用:**
- DMAメモリ解放
- ポインタはそのまま（再利用時は再割り当て必要）

**使用例:**
```c
ahci_port_free_dma_buffers(port);
```

**呼び出し元:** `ahci_destroy_port_device()`

---

### ahci_port_ensure_sg_buffers

**宣言:**
```c
int ahci_port_ensure_sg_buffers(struct ahci_port_device *port, int needed);
```

**目的:** 必要な数のSGバッファを確保（オンデマンド割り当て）

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ
- `needed`: 必要なSGバッファ数

**動作:**
1. `needed`が現在の`num_sg_buffers`以下なら即座にリターン
2. `needed`が最大値（2048）を超える場合、`-EINVAL`
3. `sg_mutex`取得
4. 不足分のバッファを追加割り当て:
   - 各128KBバッファを`dma_alloc_coherent()`
   - 失敗時は`-ENOMEM`
5. `num_sg_buffers`更新
6. `sg_mutex`解放

**戻り値:**
- `0`: 成功（既に十分、または追加割り当て成功）
- `-EINVAL`: `needed`が範囲外
- `-ENOMEM`: メモリ不足

**副作用:**
- SGバッファ追加割り当て
- `num_sg_buffers`更新

**使用例:**
```c
int needed = (buffer_size + 128*1024 - 1) / (128*1024);
ret = ahci_port_ensure_sg_buffers(port, needed);
if (ret) {
    dev_err(port->device, "SG buffer allocation failed\n");
    return ret;
}
```

**呼び出し元:** 
- `ahci_port_issue_cmd()`
- `ahci_port_issue_cmd_async()`

---

### ahci_port_setup_dma

**宣言:**
```c
int ahci_port_setup_dma(struct ahci_port_device *port);
```

**目的:** DMAアドレスをポートレジスタに設定

**パラメータ:**
- `port`: ポートデバイス構造体ポインタ

**動作:**
1. Command List DMAアドレスを`PxCLB/PxCLBU`に書き込み
2. FIS Area DMAアドレスを`PxFB/PxFBU`に書き込み

**戻り値:**
- `0`: 成功

**副作用:**
- ポートレジスタ更新

**注意事項:**
- ポート停止中に実行すること

**使用例:**
```c
ret = ahci_port_setup_dma(port);
```

**呼び出し元:** `ahci_port_init()`

---

## ユーティリティ関数

### ahci_wait_bit_clear

**宣言:**
```c
int ahci_wait_bit_clear(void __iomem *mmio,
                        u32 reg,
                        u32 mask,
                        int timeout_ms,
                        struct device *dev,
                        const char *bit_name);
```

**目的:** レジスタのビットがクリアされるまで待機

**パラメータ:**
- `mmio`: MMIOベースアドレス
- `reg`: レジスタオフセット
- `mask`: 監視するビットマスク
- `timeout_ms`: タイムアウト（ミリ秒）
- `dev`: デバイス構造体（ログ用、NULLも可）
- `bit_name`: ビット名（ログ用）

**動作:**
1. 1msごとにレジスタ読み取り
2. `(value & mask) == 0`なら成功
3. タイムアウトまで繰り返し
4. タイムアウト時、エラーログ出力

**戻り値:**
- `0`: ビットクリア確認
- `-ETIMEDOUT`: タイムアウト

**使用例:**
```c
ret = ahci_wait_bit_clear(port_mmio, AHCI_PORT_CMD,
                          AHCI_PORT_CMD_CR, 500, port->device, "CR");
if (ret) {
    dev_err(port->device, "CR bit did not clear\n");
    return ret;
}
```

**呼び出し元:** 
- `ahci_port_stop()`
- `ahci_hba_reset()`

---

### ahci_wait_bit_set

**宣言:**
```c
int ahci_wait_bit_set(void __iomem *mmio,
                      u32 reg,
                      u32 mask,
                      int timeout_ms,
                      struct device *dev,
                      const char *bit_name);
```

**目的:** レジスタのビットがセットされるまで待機

**パラメータ:**
- `mmio`: MMIOベースアドレス
- `reg`: レジスタオフセット
- `mask`: 監視するビットマスク
- `timeout_ms`: タイムアウト（ミリ秒）
- `dev`: デバイス構造体（ログ用）
- `bit_name`: ビット名（ログ用）

**動作:**
1. 1msごとにレジスタ読み取り
2. `(value & mask) == mask`なら成功
3. タイムアウトまで繰り返し
4. タイムアウト時、エラーログ出力

**戻り値:**
- `0`: ビットセット確認
- `-ETIMEDOUT`: タイムアウト

**使用例:**
```c
ret = ahci_wait_bit_set(port_mmio, AHCI_PORT_CMD,
                        AHCI_PORT_CMD_FR, 500, port->device, "FR");
if (ret) {
    dev_err(port->device, "FIS receive did not start\n");
    return ret;
}
```

**呼び出し元:**
- `ahci_port_start()`
- `ahci_port_init()`
- `ahci_port_comreset()`

---

## デバイス管理関数

### ahci_create_port_device

**宣言:**
```c
int ahci_create_port_device(struct ahci_hba *hba, int port_num);
```

**目的:** ポートデバイスノードの作成と初期化

**パラメータ:**
- `hba`: HBA構造体ポインタ
- `port_num`: ポート番号（0-31）

**動作:**
1. `ahci_port_device`構造体割り当て
2. スピンロック、mutexの初期化
3. ポートMMIOアドレス計算
4. キャラクタデバイス初期化（cdev）
5. デバイスノード作成（`/dev/ahci_lld_p<N>`）
6. DMAバッファ割り当て（`ahci_port_alloc_dma_buffers()`）
7. DMAアドレス設定（`ahci_port_setup_dma()`）
8. ポート初期化（`ahci_port_init()`）

**戻り値:**
- `0`: 成功
- `-ENOMEM`: メモリ不足
- その他: サブ関数のエラーコード

**副作用:**
- `/dev/ahci_lld_p<N>`デバイスノード作成
- `hba->port_devices[port_num]`に構造体ポインタ保存

**使用例:**
```c
for (port = 0; port < num_ports; port++) {
    if (pi & (1 << port)) {
        ret = ahci_create_port_device(hba, port);
        if (ret) {
            dev_err(&pdev->dev, "Failed to create port %d\n", port);
        }
    }
}
```

**呼び出し元:** `ahci_lld_probe()`

---

### ahci_destroy_port_device

**宣言:**
```c
void ahci_destroy_port_device(struct ahci_hba *hba, int port_num);
```

**目的:** ポートデバイスのクリーンアップと削除

**パラメータ:**
- `hba`: HBA構造体ポインタ
- `port_num`: ポート番号

**動作:**
1. ポート存在確認
2. ポートクリーンアップ（`ahci_port_cleanup()`）
3. DMAバッファ解放（`ahci_port_free_dma_buffers()`）
4. デバイスノード削除
5. cdev削除
6. `ahci_port_device`構造体解放
7. `hba->port_devices[port_num] = NULL`

**戻り値:** なし（void）

**副作用:**
- デバイスノード削除
- メモリ解放

**使用例:**
```c
for (port = 0; port < 32; port++) {
    if (hba->port_devices[port]) {
        ahci_destroy_port_device(hba, port);
    }
}
```

**呼び出し元:** `ahci_lld_remove()`

---

### ahci_create_ghc_device

**宣言:**
```c
int ahci_create_ghc_device(struct ahci_hba *hba);
```

**目的:** GHC（Global HBA Control）デバイスノード作成

**パラメータ:**
- `hba`: HBA構造体ポインタ

**動作:**
1. キャラクタデバイス初期化
2. デバイスノード作成（`/dev/ahci_lld_ghc`）

**戻り値:**
- `0`: 成功
- 負: エラー

**副作用:**
- `/dev/ahci_lld_ghc`作成

**使用例:**
```c
ret = ahci_create_ghc_device(hba);
```

**呼び出し元:** `ahci_lld_probe()`

---

### ahci_destroy_ghc_device

**宣言:**
```c
void ahci_destroy_ghc_device(struct ahci_hba *hba);
```

**目的:** GHCデバイスノード削除

**パラメータ:**
- `hba`: HBA構造体ポインタ

**動作:**
1. デバイスノード削除
2. cdev削除

**戻り値:** なし（void）

**使用例:**
```c
ahci_destroy_ghc_device(hba);
```

**呼び出し元:** `ahci_lld_remove()`

---

## まとめ

本ドキュメントでは、AHCI LLDドライバの内部関数APIを詳述しました。

**関数分類:**
- **HBA操作**: リセット、有効化
- **ポート操作**: 初期化、開始、停止、COMRESET
- **コマンド実行**: 同期（Non-NCQ）、非同期（NCQ）
- **スロット管理**: 割り当て、解放、完了検出
- **DMAバッファ管理**: 割り当て、解放、オンデマンド拡張
- **ユーティリティ**: ビット待機
- **デバイス管理**: 作成、削除

**重要ポイント:**
- 同期関数はブロッキング、非同期関数はノンブロッキング
- DMAメモリは適切なアライメント必須
- エラー処理は戻り値で一貫性
- スピンロックで並行アクセス保護

これらの関数を組み合わせることで、高性能なSATAデバイス制御を実現しています。
