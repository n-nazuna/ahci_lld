savedcmd_ahci_lld.mod := printf '%s\n'   ahci_lld_main.o ahci_lld_hba.o ahci_lld_port.o ahci_lld_util.o | awk '!x[$$0]++ { print("./"$$0) }' > ahci_lld.mod
