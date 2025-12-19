/*
 * AHCI Low Level Driver - Slot Management for NCQ
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/bitmap.h>
#include "ahci_lld.h"
#include "ahci_lld_fis.h"

/**
 * ahci_alloc_slot - Allocate a free command slot
 * @port: Port device structure
 *
 * Finds and allocates a free slot for command execution.
 * 
 * Return: Slot number (0-31) on success, negative error code on failure
 */
int ahci_alloc_slot(struct ahci_port_device *port)
{
    unsigned long flags;
    int slot;
    
    spin_lock_irqsave(&port->slot_lock, flags);
    
    /* Find first zero bit (free slot) */
    slot = find_first_zero_bit(&port->slots_in_use, 32);
    
    if (slot >= 32) {
        spin_unlock_irqrestore(&port->slot_lock, flags);
        dev_warn(port->device, "No free slots available\n");
        return -EBUSY;
    }
    
    /* Mark slot as in use */
    set_bit(slot, &port->slots_in_use);
    atomic_inc(&port->active_slots);
    
    spin_unlock_irqrestore(&port->slot_lock, flags);
    
    dev_dbg(port->device, "Allocated slot %d\n", slot);
    return slot;
}
EXPORT_SYMBOL_GPL(ahci_alloc_slot);

/**
 * ahci_free_slot - Free an allocated command slot
 * @port: Port device structure
 * @slot: Slot number to free
 */
void ahci_free_slot(struct ahci_port_device *port, int slot)
{
    unsigned long flags;
    
    if (slot < 0 || slot >= 32) {
        dev_err(port->device, "Invalid slot number: %d\n", slot);
        return;
    }
    
    spin_lock_irqsave(&port->slot_lock, flags);
    
    if (!test_bit(slot, &port->slots_in_use)) {
        dev_warn(port->device, "Slot %d is not in use\n", slot);
        spin_unlock_irqrestore(&port->slot_lock, flags);
        return;
    }
    
    /* Clear slot */
    clear_bit(slot, &port->slots_in_use);
    clear_bit(slot, &port->slots_completed);
    atomic_dec(&port->active_slots);
    
    /* Clear slot information */
    memset(&port->slots[slot], 0, sizeof(port->slots[slot]));
    
    spin_unlock_irqrestore(&port->slot_lock, flags);
    
    dev_dbg(port->device, "Freed slot %d\n", slot);
}
EXPORT_SYMBOL_GPL(ahci_free_slot);

/**
 * ahci_mark_slot_completed - Mark a slot as completed
 * @port: Port device structure
 * @slot: Slot number
 * @result: Result code (0 = success, negative = error)
 */
void ahci_mark_slot_completed(struct ahci_port_device *port, int slot, int result)
{
    unsigned long flags;
    
    if (slot < 0 || slot >= 32)
        return;
    
    spin_lock_irqsave(&port->slot_lock, flags);
    
    if (!test_bit(slot, &port->slots_in_use)) {
        spin_unlock_irqrestore(&port->slot_lock, flags);
        return;
    }
    
    /* Mark as completed */
    set_bit(slot, &port->slots_completed);
    port->slots[slot].completed = true;
    port->slots[slot].result = result;
    port->ncq_completed++;
    
    spin_unlock_irqrestore(&port->slot_lock, flags);
    
    dev_dbg(port->device, "Slot %d marked as completed (result=%d)\n", slot, result);
}
EXPORT_SYMBOL_GPL(ahci_mark_slot_completed);

/**
 * ahci_check_slot_completion - Check if command slots have completed
 * @port: Port device structure
 *
 * Polls PxSACT register to detect completed NCQ commands.
 * Updates slot completion status accordingly.
 * This is only used for NCQ (asynchronous) commands.
 *
 * Return: Bitmap of newly completed slots
 */
u32 ahci_check_slot_completion(struct ahci_port_device *port)
{
    void __iomem *port_mmio = port->port_mmio;
    unsigned long flags;
    u32 sact;
    u32 newly_completed = 0;
    int slot;
    
    /* Read PxSACT register (NCQ active slots) */
    sact = ioread32(port_mmio + AHCI_PORT_SACT);
    
    spin_lock_irqsave(&port->slot_lock, flags);
    
    for (slot = 0; slot < 32; slot++) {
        u32 slot_bit = (1 << slot);
        
        /* Skip if slot is not in use */
        if (!test_bit(slot, &port->slots_in_use))
            continue;
        
        /* Skip if already marked completed */
        if (port->slots[slot].completed)
            continue;
        
        /* Check if NCQ slot has completed (PxSACT bit cleared) */
        if (!(sact & slot_bit)) {
            /* NCQ: Read SDB (Set Device Bits) FIS */
            struct fis_set_dev_bits *sdb_fis;
            sdb_fis = (struct fis_set_dev_bits *)((u8 *)port->fis_area + AHCI_RX_FIS_SDB);
            
            /* SDB FIS contains the actual status value */
            port->slots[slot].req.status = sdb_fis->status;
            port->slots[slot].req.error = sdb_fis->error;
            port->slots[slot].req.device_out = 0;  /* SDB FIS doesn't have device */
            
            /* SDB FIS doesn't contain LBA/count, keep original values */
            port->slots[slot].req.lba_out = port->slots[slot].req.lba;
            port->slots[slot].req.count_out = port->slots[slot].req.count;
            
            set_bit(slot, &port->slots_completed);
            port->slots[slot].completed = true;
            port->slots[slot].result = 0;
            port->ncq_completed++;
            newly_completed |= slot_bit;
            
            dev_dbg(port->device, "Slot %d completed: status=0x%02x error=0x%02x (SACT=0x%08x)\n",
                    slot, port->slots[slot].req.status, port->slots[slot].req.error, sact);
        }
    }
    
    spin_unlock_irqrestore(&port->slot_lock, flags);
    
    return newly_completed;
}
EXPORT_SYMBOL_GPL(ahci_check_slot_completion);
