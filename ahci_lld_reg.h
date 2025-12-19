/*
 * AHCI Low Level Driver - Register Definitions
 * 
 * Based on:
 * - AHCI Specification Rev 1.3.1 (Serial ATA Advanced Host Controller Interface)
 * - SATA Specification Rev 3.x
 * 
 * This header defines all HBA registers, port registers, and bit masks
 * according to AHCI specification.
 */

#ifndef AHCI_LLD_REG_H
#define AHCI_LLD_REG_H

/* ========================================================================
 * Generic Host Control Registers (AHCI 1.3.1 Section 3.1)
 * Base offset: 0x00
 * ======================================================================== */

/* AHCI Generic Host Control レジスタオフセット */
#define AHCI_CAP        0x00    /* Host Capabilities */
#define AHCI_GHC        0x04    /* Global HBA Control */
#define AHCI_IS         0x08    /* Interrupt Status */
#define AHCI_PI         0x0C    /* Ports Implemented */
#define AHCI_VS         0x10    /* Version */
#define AHCI_CCC_CTL    0x14    /* Command Completion Coalescing Control */
#define AHCI_CCC_PORTS  0x18    /* Command Completion Coalescing Ports */
#define AHCI_EM_LOC     0x1C    /* Enclosure Management Location */
#define AHCI_EM_CTL     0x20    /* Enclosure Management Control */
#define AHCI_CAP2       0x24    /* Host Capabilities Extended */
#define AHCI_BOHC       0x28    /* BIOS/OS Handoff Control and Status */

/* CAP - Host Capabilities ビットマスク */
#define AHCI_CAP_S64A   (1 << 31)  /* Supports 64-bit Addressing */
#define AHCI_CAP_SNCQ   (1 << 30)  /* Supports Native Command Queuing */
#define AHCI_CAP_SSNTF  (1 << 29)  /* Supports SNotification Register */
#define AHCI_CAP_SMPS   (1 << 28)  /* Supports Mechanical Presence Switch */
#define AHCI_CAP_SSS    (1 << 27)  /* Supports Staggered Spin-up */
#define AHCI_CAP_SALP   (1 << 26)  /* Supports Aggressive Link Power Management */
#define AHCI_CAP_SAL    (1 << 25)  /* Supports Activity LED */
#define AHCI_CAP_SCLO   (1 << 24)  /* Supports Command List Override */
#define AHCI_CAP_ISS    (0x0F << 20)  /* Interface Speed Support */
#define AHCI_CAP_SAM    (1 << 18)  /* Supports AHCI mode only */
#define AHCI_CAP_SPM    (1 << 17)  /* Supports Port Multiplier */
#define AHCI_CAP_FBSS   (1 << 16)  /* FIS-based Switching Supported */
#define AHCI_CAP_PMD    (1 << 15)  /* PIO Multiple DRQ Block */
#define AHCI_CAP_SSC    (1 << 14)  /* Slumber State Capable */
#define AHCI_CAP_PSC    (1 << 13)  /* Partial State Capable */
#define AHCI_CAP_NCS    (0x1F << 8)   /* Number of Command Slots */
#define AHCI_CAP_CCCS   (1 << 7)   /* Command Completion Coalescing Supported */
#define AHCI_CAP_EMS    (1 << 6)   /* Enclosure Management Supported */
#define AHCI_CAP_SXS    (1 << 5)   /* Supports External SATA */
#define AHCI_CAP_NP     (0x1F << 0)   /* Number of Ports */

/* GHC - Global HBA Control ビットマスク */
#define AHCI_GHC_AE     (1 << 31)  /* AHCI Enable */
#define AHCI_GHC_MRSM   (1 << 2)   /* MSI Revert to Single Message */
#define AHCI_GHC_IE     (1 << 1)   /* Interrupt Enable */
#define AHCI_GHC_HR     (1 << 0)   /* HBA Reset */

/* CAP2 - Host Capabilities Extended ビットマスク */
#define AHCI_CAP2_DESO  (1 << 5)   /* DevSleep Entrance from Slumber Only */
#define AHCI_CAP2_SADM  (1 << 4)   /* Supports Aggressive Device Sleep Management */
#define AHCI_CAP2_SDS   (1 << 3)   /* Supports Device Sleep */
#define AHCI_CAP2_APST  (1 << 2)   /* Automatic Partial to Slumber Transitions */
#define AHCI_CAP2_NVMP  (1 << 1)   /* NVMHCI Present */
#define AHCI_CAP2_BOH   (1 << 0)   /* BIOS/OS Handoff */

/* ========================================================================
 * Port Registers (Section 3.3)
 * ======================================================================== */

/* AHCI Port レジスタオフセット */
#define AHCI_PORT_BASE  0x100
#define AHCI_PORT_SIZE  0x80
#define AHCI_PORT_OFFSET(port)  (AHCI_PORT_BASE + (port) * AHCI_PORT_SIZE)

/* Port レジスタオフセット (ポートベースからの相対オフセット) */
#define AHCI_PORT_CLB   0x00    /* Command List Base Address */
#define AHCI_PORT_CLBU  0x04    /* Command List Base Address Upper 32-bits */
#define AHCI_PORT_FB    0x08    /* FIS Base Address */
#define AHCI_PORT_FBU   0x0C    /* FIS Base Address Upper 32-bits */
#define AHCI_PORT_IS    0x10    /* Interrupt Status */
#define AHCI_PORT_IE    0x14    /* Interrupt Enable */
#define AHCI_PORT_CMD   0x18    /* Command and Status */
#define AHCI_PORT_TFD   0x20    /* Task File Data */
#define AHCI_PORT_SIG   0x24    /* Signature */
#define AHCI_PORT_SSTS  0x28    /* SATA Status (SCR0: SStatus) */
#define AHCI_PORT_SCTL  0x2C    /* SATA Control (SCR2: SControl) */
#define AHCI_PORT_SERR  0x30    /* SATA Error (SCR1: SError) */
#define AHCI_PORT_SACT  0x34    /* SATA Active (SCR3: SActive) */
#define AHCI_PORT_CI    0x38    /* Command Issue */
#define AHCI_PORT_SNTF  0x3C    /* SATA Notification (SCR4: SNotification) */
#define AHCI_PORT_FBS   0x40    /* FIS-based Switching Control */
#define AHCI_PORT_DEVSLP 0x44   /* Device Sleep */

/* ========================================================================
 * ATA Command Codes (ATA8-ACS)
 * These are standard ATA command codes sent in the Command field of H2D FIS
 * ======================================================================== */
#define ATA_CMD_IDENTIFY_DEVICE     0xEC    /* IDENTIFY DEVICE */
#define ATA_CMD_READ_DMA_EXT        0x25    /* READ DMA EXT (48-bit LBA) */
#define ATA_CMD_WRITE_DMA_EXT       0x35    /* WRITE DMA EXT (48-bit LBA) */
#define ATA_CMD_READ_FPDMA_QUEUED   0x60    /* READ FPDMA QUEUED (NCQ) */
#define ATA_CMD_WRITE_FPDMA_QUEUED  0x61    /* WRITE FPDMA QUEUED (NCQ) */
#define ATA_CMD_READ_SECTORS_EXT    0x24    /* READ SECTORS EXT (PIO) */
#define ATA_CMD_WRITE_SECTORS_EXT   0x34    /* WRITE SECTORS EXT (PIO) */

/* ATA Status Register bits (returned in D2H FIS) */
#define ATA_STATUS_BSY      0x80    /* Busy */
#define ATA_STATUS_DRDY     0x40    /* Device Ready */
#define ATA_STATUS_DF       0x20    /* Device Fault */
#define ATA_STATUS_DRQ      0x08    /* Data Request */
#define ATA_STATUS_ERR      0x01    /* Error */

/* ATA Device Register bits */
#define ATA_DEV_LBA         0x40    /* LBA mode (bit 6) */

/* ========================================================================
 * Port Interrupt Status/Enable Registers
 * ======================================================================== */
/* PxIS/PxIE - Port Interrupt Status/Enable ビットマスク */
#define AHCI_PORT_INT_CPDS  (1 << 31)  /* Cold Port Detect Status */
#define AHCI_PORT_INT_TFES  (1 << 30)  /* Task File Error Status */
#define AHCI_PORT_INT_HBFS  (1 << 29)  /* Host Bus Fatal Error Status */
#define AHCI_PORT_INT_HBDS  (1 << 28)  /* Host Bus Data Error Status */
#define AHCI_PORT_INT_IFS   (1 << 27)  /* Interface Fatal Error Status */
#define AHCI_PORT_INT_INFS  (1 << 26)  /* Interface Non-fatal Error Status */
#define AHCI_PORT_INT_OFS   (1 << 24)  /* Overflow Status */
#define AHCI_PORT_INT_IPMS  (1 << 23)  /* Incorrect Port Multiplier Status */
#define AHCI_PORT_INT_PRCS  (1 << 22)  /* PhyRdy Change Status */
#define AHCI_PORT_INT_DMPS  (1 << 7)   /* Device Mechanical Presence Status */
#define AHCI_PORT_INT_PCS   (1 << 6)   /* Port Connect Change Status */
#define AHCI_PORT_INT_DPS   (1 << 5)   /* Descriptor Processed */
#define AHCI_PORT_INT_UFS   (1 << 4)   /* Unknown FIS Interrupt */
#define AHCI_PORT_INT_SDBS  (1 << 3)   /* Set Device Bits Interrupt */
#define AHCI_PORT_INT_DSS   (1 << 2)   /* DMA Setup FIS Interrupt */
#define AHCI_PORT_INT_PSS   (1 << 1)   /* PIO Setup FIS Interrupt */
#define AHCI_PORT_INT_DHRS  (1 << 0)   /* Device to Host Register FIS Interrupt */

/* エラー割り込みマスク */
#define AHCI_PORT_INT_ERROR  (AHCI_PORT_INT_TFES | AHCI_PORT_INT_HBFS | \
                              AHCI_PORT_INT_HBDS | AHCI_PORT_INT_IFS)

/* PxCMD - Port Command and Status ビットマスク */
#define AHCI_PORT_CMD_ICC   (0x0F << 28)  /* Interface Communication Control */
#define AHCI_PORT_CMD_ASP   (1 << 27)  /* Aggressive Slumber / Partial */
#define AHCI_PORT_CMD_ALPE  (1 << 26)  /* Aggressive Link Power Management Enable */
#define AHCI_PORT_CMD_DLAE  (1 << 25)  /* Drive LED on ATAPI Enable */
#define AHCI_PORT_CMD_ATAPI (1 << 24)  /* Device is ATAPI */
#define AHCI_PORT_CMD_APSTE (1 << 23)  /* Automatic Partial to Slumber Transitions Enable */
#define AHCI_PORT_CMD_FBSCP (1 << 22)  /* FIS-based Switching Capable Port */
#define AHCI_PORT_CMD_ESP   (1 << 21)  /* External SATA Port */
#define AHCI_PORT_CMD_CPD   (1 << 20)  /* Cold Presence Detection */
#define AHCI_PORT_CMD_MPSP  (1 << 19)  /* Mechanical Presence Switch Attached to Port */
#define AHCI_PORT_CMD_HPCP  (1 << 18)  /* Hot Plug Capable Port */
#define AHCI_PORT_CMD_PMA   (1 << 17)  /* Port Multiplier Attached */
#define AHCI_PORT_CMD_CPS   (1 << 16)  /* Cold Presence State */
#define AHCI_PORT_CMD_CR    (1 << 15)  /* Command List Running */
#define AHCI_PORT_CMD_FR    (1 << 14)  /* FIS Receive Running */
#define AHCI_PORT_CMD_MPSS  (1 << 13)  /* Mechanical Presence Switch State */
#define AHCI_PORT_CMD_CCS   (0x1F << 8)   /* Current Command Slot */
#define AHCI_PORT_CMD_FRE   (1 << 4)   /* FIS Receive Enable */
#define AHCI_PORT_CMD_CLO   (1 << 3)   /* Command List Override */
#define AHCI_PORT_CMD_POD   (1 << 2)   /* Power On Device */
#define AHCI_PORT_CMD_SUD   (1 << 1)   /* Spin-Up Device */
#define AHCI_PORT_CMD_ST    (1 << 0)   /* Start */

/* PxTFD - Port Task File Data ビットマスク */
#define AHCI_PORT_TFD_ERR   (0xFF << 8)   /* Error */
#define AHCI_PORT_TFD_STS   (0xFF << 0)   /* Status */
#define AHCI_PORT_TFD_STS_BSY  (1 << 7)   /* Interface is busy */
#define AHCI_PORT_TFD_STS_DRQ  (1 << 3)   /* Data transfer requested */
#define AHCI_PORT_TFD_STS_ERR  (1 << 0)   /* Error during transfer */

/* PxSSTS - Port SATA Status (SCR0: SStatus) ビットマスク */
#define AHCI_PORT_SSTS_IPM  (0x0F << 8)   /* Interface Power Management */
#define AHCI_PORT_SSTS_SPD  (0x0F << 4)   /* Current Interface Speed */
#define AHCI_PORT_SSTS_DET  (0x0F << 0)   /* Device Detection */

/* PxSSTS.DET 値 */
#define AHCI_PORT_DET_NONE      0   /* No device detected */
#define AHCI_PORT_DET_PRESENT   1   /* Device presence detected but no Phy communication */
#define AHCI_PORT_DET_ESTABLISHED 3 /* Device presence detected and Phy communication established */

/* PxSSTS.SPD 値 */
#define AHCI_PORT_SPD_NONE  0   /* No device detected */
#define AHCI_PORT_SPD_GEN1  1   /* Generation 1 (1.5 Gbps) */
#define AHCI_PORT_SPD_GEN2  2   /* Generation 2 (3 Gbps) */
#define AHCI_PORT_SPD_GEN3  3   /* Generation 3 (6 Gbps) */

/* PxSSTS.IPM 値 */
#define AHCI_PORT_IPM_NONE    0   /* Device not present or communication not established */
#define AHCI_PORT_IPM_ACTIVE  1   /* Interface in active state */
#define AHCI_PORT_IPM_PARTIAL 2   /* Interface in Partial power management state */
#define AHCI_PORT_IPM_SLUMBER 6   /* Interface in Slumber power management state */
#define AHCI_PORT_IPM_DEVSLEEP 8  /* Interface in DevSleep power management state */

/* PxSCTL - Port SATA Control (SCR2: SControl) ビットマスク */
#define AHCI_PORT_SCTL_IPM  (0x0F << 8)   /* Interface Power Management Transitions Allowed */
#define AHCI_PORT_SCTL_SPD  (0x0F << 4)   /* Speed Allowed */
#define AHCI_PORT_SCTL_DET  (0x0F << 0)   /* Device Detection Initialization */

/* PxSCTL.DET 値 */
#define AHCI_PORT_SCTL_DET_NONE     0   /* No device detection or initialization action */
#define AHCI_PORT_SCTL_DET_INIT     1   /* Perform interface communication initialization */
#define AHCI_PORT_SCTL_DET_DISABLE  4   /* Disable SATA interface and put Phy in offline mode */

/* PxSERR - Port SATA Error (SCR1: SError) ビットマスク */
#define AHCI_PORT_SERR_DIAG_X  (1 << 26)  /* Exchanged */
#define AHCI_PORT_SERR_DIAG_F  (1 << 25)  /* Unknown FIS Type */
#define AHCI_PORT_SERR_DIAG_T  (1 << 24)  /* Transport state transition error */
#define AHCI_PORT_SERR_DIAG_S  (1 << 23)  /* Link sequence error */
#define AHCI_PORT_SERR_DIAG_H  (1 << 22)  /* Handshake Error */
#define AHCI_PORT_SERR_DIAG_C  (1 << 21)  /* CRC Error */
#define AHCI_PORT_SERR_DIAG_D  (1 << 20)  /* Disparity Error */
#define AHCI_PORT_SERR_DIAG_B  (1 << 19)  /* 10B to 8B Decode Error */
#define AHCI_PORT_SERR_DIAG_W  (1 << 18)  /* Comm Wake */
#define AHCI_PORT_SERR_DIAG_I  (1 << 17)  /* Phy Internal Error */
#define AHCI_PORT_SERR_DIAG_N  (1 << 16)  /* PhyRdy Change */

#define AHCI_PORT_SERR_ERR_E   (1 << 11)  /* Internal Error */
#define AHCI_PORT_SERR_ERR_P   (1 << 10)  /* Protocol Error */
#define AHCI_PORT_SERR_ERR_C   (1 << 9)   /* Persistent Communication or Data Integrity Error */
#define AHCI_PORT_SERR_ERR_T   (1 << 8)   /* Transient Data Integrity Error */
#define AHCI_PORT_SERR_ERR_M   (1 << 1)   /* Recovered Communications Error */
#define AHCI_PORT_SERR_ERR_I   (1 << 0)   /* Recovered Data Integrity Error */

/* PxFBS - Port FIS-based Switching Control ビットマスク */
#define AHCI_PORT_FBS_DWE   (0x0F << 16)  /* Device With Error */
#define AHCI_PORT_FBS_ADO   (0x0F << 12)  /* Active Device Optimization */
#define AHCI_PORT_FBS_DEV   (0x0F << 8)   /* Device To Issue */
#define AHCI_PORT_FBS_SDE   (1 << 2)   /* Single Device Error */
#define AHCI_PORT_FBS_DEC   (1 << 1)   /* Device Error Clear */
#define AHCI_PORT_FBS_EN    (1 << 0)   /* Enable */

/* ========================================================================
 * Device Signatures (Section 3.3.9)
 * ======================================================================== */

#define AHCI_SIG_ATA        0x00000101  /* SATA drive */
#define AHCI_SIG_ATAPI      0xEB140101  /* SATAPI drive */
#define AHCI_SIG_SEMB       0xC33C0101  /* Enclosure management bridge */
#define AHCI_SIG_PM         0x96690101  /* Port multiplier */

/* ========================================================================
 * System Memory Structures Sizes (Section 4)
 * ======================================================================== */

#define AHCI_CMD_HEADER_SIZE    32      /* Command Header size in bytes */
#define AHCI_CMD_LIST_SIZE      1024    /* Command List size (32 slots * 32 bytes) */
#define AHCI_RCV_FIS_SIZE       256     /* Received FIS structure size in bytes */
#define AHCI_CMD_TBL_HDR_SIZE   0x80    /* Command Table header size (128 bytes) */
#define AHCI_CMD_TBL_CDB_SIZE   0x40    /* ATAPI CDB area size (64 bytes) */

/* Command Table PRDT Entry size */
#define AHCI_PRDT_ENTRY_SIZE    16      /* PRD Table Entry size in bytes */

/* Maximum PRD entries per command table */
#define AHCI_MAX_PRDT_ENTRIES   65535   /* Maximum number of PRD entries */

/* Alignment requirements */
#define AHCI_CMD_LIST_ALIGN     1024    /* Command List 1K byte aligned */
#define AHCI_RCV_FIS_ALIGN      256     /* Received FIS 256 byte aligned */
#define AHCI_CMD_TBL_ALIGN      128     /* Command Table 128 byte aligned */

/* ========================================================================
 * Command Header Bits (Section 4.2.2)
 * ======================================================================== */

/* Command Header DW0 bits */
#define AHCI_CMD_HDR_CFL    (0x1F << 0)   /* Command FIS Length */
#define AHCI_CMD_HDR_A      (1 << 5)   /* ATAPI */
#define AHCI_CMD_HDR_W      (1 << 6)   /* Write */
#define AHCI_CMD_HDR_P      (1 << 7)   /* Prefetchable */
#define AHCI_CMD_HDR_R      (1 << 8)   /* Reset */
#define AHCI_CMD_HDR_B      (1 << 9)   /* BIST */
#define AHCI_CMD_HDR_C      (1 << 10)  /* Clear Busy upon R_OK */
#define AHCI_CMD_HDR_PMP    (0x0F << 12)  /* Port Multiplier Port */
#define AHCI_CMD_HDR_PRDTL  (0xFFFF << 16) /* Physical Region Descriptor Table Length */

#endif /* AHCI_LLD_REG_H */
