# NCQ (Native Command Queuing) Implementation

This driver supports AHCI Native Command Queuing for concurrent command execution.

## Overview

NCQ allows issuing multiple ATA commands concurrently (up to 32 commands) without waiting for each command to complete. This significantly improves performance for workloads with multiple concurrent I/O operations.

## Features

- **Asynchronous Command Execution**: Issue commands with `AHCI_CMD_FLAG_ASYNC` flag
- **Multi-slot Support**: Up to 32 concurrent commands (AHCI specification limit)
- **Non-blocking Completion Check**: Poll for completed commands using `AHCI_IOC_PROBE_CMD`
- **Backward Compatible**: Synchronous execution still works without async flag

## IOCTL Interface

### 1. Issue Command (Extended)

```c
struct ahci_cmd_request req;
memset(&req, 0, sizeof(req));

req.command = 0x25;  // READ DMA EXT
req.lba = 100;
req.count = 1;
req.device = 0x40;
req.buffer = (uint64_t)buffer;
req.buffer_len = 512;
req.flags = AHCI_CMD_FLAG_ASYNC;  // Async execution

if (ioctl(fd, AHCI_IOC_ISSUE_CMD, &req) == 0) {
    printf("Command issued with tag %u\n", req.tag);
}
```

### 2. Probe Completed Commands

```c
struct ahci_sdb sdb;
memset(&sdb, 0, sizeof(sdb));

if (ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb) == 0) {
    for (int tag = 0; tag < 32; tag++) {
        if (sdb.completed & (1 << tag)) {
            printf("Tag %d completed: status=0x%02x, error=0x%02x\n",
                   tag, sdb.status[tag], sdb.error[tag]);
        }
    }
}
```

## Data Structures

### `struct ahci_cmd_request`

Extended with:
- `tag` (output): Assigned slot number for async commands (0-31)
- `flags`: Add `AHCI_CMD_FLAG_ASYNC` for async execution

### `struct ahci_sdb` (Set Device Bits)

AHCI-compliant completion information:
- `sactive`: Currently active slots (from PxSACT register)
- `completed`: Bitmap of newly completed commands
- `status[32]`: ATA status for each completed slot
- `error[32]`: ATA error for each completed slot
- `buffer[32]`: User buffer pointers

## Implementation Details

### Slot Management

- **Allocation**: `ahci_alloc_slot()` - Bitmap-based slot allocation
- **Deallocation**: `ahci_free_slot()` - Cleanup and bitmap clearing
- **Completion Marking**: `ahci_mark_slot_completed()` - Mark slot as done
- **Completion Check**: `ahci_check_slot_completion()` - Poll PxCI/PxSACT

### Command Execution

1. **Synchronous** (`flags = 0`):
   - Calls `ahci_port_issue_cmd()`
   - Waits for completion
   - Copies data to user buffer
   - Returns immediately with data

2. **Asynchronous** (`flags = AHCI_CMD_FLAG_ASYNC`):
   - Calls `ahci_port_issue_cmd_async()`
   - Allocates slot dynamically
   - Stores request/buffer in slot structure
   - Issues command to hardware (writes to PxCI)
   - Returns immediately with assigned tag
   - Data remains in kernel until PROBE

### Data Flow

```
[Userspace]                [Kernel]                   [Hardware]

ISSUE_CMD (async) ───────► Allocate slot
                          Store request/buffer
                          Build Command Table
                          Write PxCI ────────────────► Start DMA
◄──────────────────────── Return tag

       ... (non-blocking) ...

                                                      Complete ──► Interrupt
                                                      Clear PxCI bit

PROBE_CMD ───────────────► Check PxCI/PxSACT
                          Mark completed slots
                          Copy data to user
◄──────────────────────── Return SDB

```

## Test Programs

### `test_ncq`

Comprehensive NCQ test suite:

```bash
cd tests
make test_ncq
sudo ./test_ncq
```

Tests include:
1. **Sync IDENTIFY**: Compatibility test (existing behavior)
2. **Single Async READ**: Single command async execution
3. **Multi Async READ**: 4 and 8 concurrent READ commands
4. **Mixed R/W**: Concurrent READ and WRITE operations

### Expected Output

```
=== Test 1: Synchronous IDENTIFY ===
Command completed synchronously
Status: 0x50, Error: 0x00

=== Test 2: Single Async READ ===
Command issued asynchronously, tag=0
Command completed: tag=0, status=0x50, error=0x00

=== Test 3: Multiple Concurrent Async READ (4 commands) ===
Command 0 issued: tag=0, lba=0
Command 1 issued: tag=1, lba=8
Command 2 issued: tag=2, lba=16
Command 3 issued: tag=3, lba=24
Command 0 completed: tag=0, status=0x50, error=0x00
Command 1 completed: tag=1, status=0x50, error=0x00
Command 2 completed: tag=2, status=0x50, error=0x00
Command 3 completed: tag=3, status=0x50, error=0x00
All 4 commands completed successfully
```

## Performance Considerations

### Advantages

- **Parallelism**: Multiple commands in-flight simultaneously
- **Reduced Latency**: No waiting between command submissions
- **Better Queue Depth**: Hardware can optimize command ordering

### Limitations

- **Polling-based**: Currently uses polling instead of interrupts
- **User-space Buffering**: Each async command allocates kernel buffer
- **No Priority**: All commands treated equally (AHCI limitation)

## Future Enhancements

1. **Interrupt Support**: Replace polling with interrupt-driven completion
2. **NCQ FIS Construction**: Support FPDMA READ/WRITE commands (0x60/0x61)
3. **Error Recovery**: Better handling of slot errors
4. **Statistics**: Per-port NCQ performance counters
5. **Queue Full Handling**: Automatic retry when all slots occupied

## References

- AHCI Specification 1.3.1, Section 8 (Native Command Queuing)
- ATA/ATAPI Command Set 3 (ACS-3), Section 13 (NCQ)
- `IOCTL_SPEC.md`: Detailed IOCTL documentation
- `NCQ_IMPLEMENTATION_SPEC.md`: Implementation design document
