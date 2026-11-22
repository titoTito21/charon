"""r407: build Charon's driver pack (\\EFI\\HAK\\drivers): network, input, storage, USB, virtual machine and file
system drivers, with a manifest, licence texts and the corresponding source.

  make-charon-driverpack.py fetch    DL_DIR
  make-charon-driverpack.py ipxe     DL_DIR QEMU_EXE [SERIAL_PORT]
  make-charon-driverpack.py edk2     DL_DIR EDK2_WORKSPACE NASM_DIR [all|network|more]
  make-charon-driverpack.py assemble DL_DIR QEMU_SHARE_DIR OUT_DIR
  make-charon-driverpack.py verify   OUT_DIR

fetch     downloads the pinned inputs (SHA-256 checked): the iPXE 2.0.0 source, the Alpine
          Linux ISO used only as the build machine, the official x64 release binaries of
          EfiFs 1.13 and of the ntfs-3g UEFI driver 1.9 with their source archives, and the
          GRUB source snapshot EfiFs 1.13 pins. Nothing downloaded is run on the host.
ipxe      builds iPXE's UEFI drivers (*.efidrv) from that source INSIDE a QEMU machine
          (Alpine Linux from the ISO, gcc from Alpine's package repository; iPXE needs
          GNU make + gcc + an ELF linker, which the Windows host does not have). The
          machine has user networking, no host folders and is deleted afterwards; input
          and output travel as tar archives on two raw virtual disks. Result:
          DL_DIR/ipxe-out/*.efi + build logs.
edk2      builds with MSVC in an existing edk2 workspace (work/ha): the ASIX AX88772 and
          AX88179 USB Ethernet drivers of edk2-platforms (pinned commit, every file SHA-256
          checked; the sources stay in DL_DIR and are found through PACKAGES_PATH) and the
          workspace's own USB network class drivers (MdeModulePkg/Bus/Usb/UsbNetwork:
          NetworkCommon, UsbRndis, UsbCdcEcm, UsbCdcNcm). Result: DL_DIR/edk2-out/*.efi and
          a copy of the compiled class driver sources ("network"). Then ("more") the generic
          drivers of EDK2X_MODULES - USB keyboard and mouse, NVMe, SATA/ATA, SCSI, SD/eMMC, UFS,
          xHCI/EHCI/UHCI, USB bus and mass storage, virtio, PVSCSI, LSI, ext4, PS/2, serial -
          from the workspace's own sources with a TimerLib that works on any firmware. Result:
          DL_DIR/edk2x-out/ and a copy of every compiled source folder. The workspace is
          shared: every build waits for and holds EDK2_WORKSPACE/.charon-build.lock, writes
          nothing outside its own Build/ folder and removes that folder afterwards.
assemble  writes OUT_DIR (must be new): nic-*.efi from DL_DIR/ipxe-out and edk2-out, the
          other classes from DL_DIR/edk2x-out, efifs and ntfs3g, net-*.efi (edk2 network
          stack) from QEMU's edk2-x86_64-code.fd as make-charon-netdrivers.py does, manual/
          (drivers no rule loads), MANIFEST.json, README.txt, DRIVER-PACK.txt, licences/, source/.
verify    checks every *.efi in OUT_DIR and OUT_DIR/manual: PE32+, machine x86-64, subsystem
          EFI boot service driver (11) or runtime driver (12), hashes equal to MANIFEST.json,
          manifest entry complete.

Loading rules: CharonWsNet.h LoadDrivers (nic-, wifi-, net-) and CharonDriverPack.h (fs-,
input-, disk-, usb-, virt-, nic-usb-, manual). See src/efi/DRIVER-PACK.txt.
"""
import hashlib,importlib.util,io,json,os,re,shutil,socket,struct,subprocess,sys,tarfile,time,urllib.request
from pathlib import Path

IPXE_VERSION='2.0.0'
INPUTS={
 'ipxe/ipxe-2.0.0.tar.gz':('https://github.com/ipxe/ipxe/archive/refs/tags/v2.0.0.tar.gz','9ed6d029be901a0ccc87cb2e5f9c774620f30f84ebdd507c6dd3e1e6229b7bd5'),
 'alpine/alpine-virt-3.24.4-x86_64.iso':('https://dl-cdn.alpinelinux.org/alpine/v3.24/releases/x86_64/alpine-virt-3.24.4-x86_64.iso','157d353f8e54252cad2d9812a0d56bb7fa2231fc154a110e1d180d7480ef8b4f'),
 # Official release binaries (never rebuilt here) and their corresponding source. The SHA-256 of every binary equals the
 # digest GitHub's release API reports for the asset (work/driverpack-dl/meta/*-release.json).
 'efifs/afs_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/afs_x64.efi','b34dbf472384bd31f17233ef852b4750e45b747fc284e22f8fb93d9afae1ddd5'),
 'efifs/bfs_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/bfs_x64.efi','c8cf2babe9e5dd358dec97aaa56e8a9391bb0f82ef5caba3ac130f5eef64c896'),
 'efifs/btrfs_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/btrfs_x64.efi','1712f61541cc4b308c9cbafe3361b7769d124ed430fb25337a05bb1b30df3862'),
 'efifs/cbfs_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/cbfs_x64.efi','558ca0f6ca8ad8eca210cc20e844284819014995b1074ea9d1a20edb8d5bb481'),
 'efifs/cpio_be_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/cpio_be_x64.efi','32763b8a51a2cd648069f96b7e14bbe27109ac9c7bfd3c7901a5a23468145d6a'),
 'efifs/cpio_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/cpio_x64.efi','1c44b08ee3d8e6d9df0c62a99b386183d9c07cf4e4dea033a4e05f8afcd3c60f'),
 'efifs/erofs_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/erofs_x64.efi','ea436ab66c379f824d54feb2b6d018f901e27275c2db84877b5ac348f17ae15f'),
 'efifs/exfat_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/exfat_x64.efi','3f16a8b72ea81a6e7f9dea0fe92a108cd9b980e29c21f058ddb2f2730027ec81'),
 'efifs/ext2_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/ext2_x64.efi','b9ef4af046ec60ac912e81f37e0c0a6739f70fae81031238ed2469958fdcb35f'),
 'efifs/f2fs_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/f2fs_x64.efi','b62b4d210e76bb2e18a1a6431878bc96b738f6bb81f761815185e4c7dbdc2929'),
 'efifs/fat_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/fat_x64.efi','70b72dd7c2e6033ad047af50ed01cb8b365c9e9bdcf4ae0d4f0f4074a7bd5f88'),
 'efifs/hfsplus_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/hfsplus_x64.efi','1fced68b912f3b67006aee74553301c72ccaa776e7d7cf7d8a7b02ccf607ae18'),
 'efifs/hfs_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/hfs_x64.efi','033ac6505d445e859ae477c938cb3e3c71866ed9a74b328c8a7bfa7b05ea96f2'),
 'efifs/iso9660_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/iso9660_x64.efi','a327b7ab1c742c8bbe98c2e20b47c61c6bbd7fb2fe600502a9db907d55ee9f3c'),
 'efifs/jfs_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/jfs_x64.efi','3163e321d0878554cc0d86c1db81b805a4d30ae3923a61e9e507ebf45d23f596'),
 'efifs/minix2_be_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/minix2_be_x64.efi','6d5bc2cab05e05557c42bd88a3a7ef27def8e4f345adaa23f796377a1cc581b4'),
 'efifs/minix2_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/minix2_x64.efi','c9ed4fd5009dc37ff777ec2272bee40eeadba8b2fb8eb4cd5bbce03bff265772'),
 'efifs/minix3_be_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/minix3_be_x64.efi','709c0c00eb50c41e579af8d4bb65f0ad90532c371a95a6d0df8ec4f42aa7b5f1'),
 'efifs/minix3_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/minix3_x64.efi','bd33cdddf920c7af0768340654522b399272d4043b4ab6ff78a4d12b509e291c'),
 'efifs/minix_be_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/minix_be_x64.efi','3eb286dc7d84a2e1ba07eb5a75487ac35db93ce8a91a3a75145cc8e8eb4c8cc9'),
 'efifs/minix_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/minix_x64.efi','c06686f35d050b6822d1532fda422ca784f56096bc465bcacbef020eeba8dbb2'),
 'efifs/newc_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/newc_x64.efi','8fc917fba9c2e861e1ed29d5b2d049a67b19b066313f10ffe7c27ad45fe7337d'),
 'efifs/nilfs2_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/nilfs2_x64.efi','f8aead6cba0cc99710ceaaada910b26615b8892dcdcfd86723148aba69db9fec'),
 'efifs/ntfs_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/ntfs_x64.efi','202aeb2cc078693e9c03f3c0a64e8c6c44f2c75eded8a32ada5e1589753b3317'),
 'efifs/odc_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/odc_x64.efi','2bad28d01e8229c7047850ee5531d4959f62991b15763f637a855fe87bfb125a'),
 'efifs/procfs_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/procfs_x64.efi','b15073536db146338097562f09012bae6b680d6b0274758cae5a3eee4e117625'),
 'efifs/reiserfs_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/reiserfs_x64.efi','04d646085428b4dd957b365fb70e65f734a5441e4c854a06af4f8d5015df4f91'),
 'efifs/romfs_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/romfs_x64.efi','d2c2714c162206ef58631e9ba907f4717fee74c4577019062b46cc1f41bfe3d1'),
 'efifs/sfs_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/sfs_x64.efi','df15f62f93af05b55f523aef7ca6fb9ea73e9807678d3b38d94c5a43cb4ae8bd'),
 'efifs/squash4_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/squash4_x64.efi','affabd362aee0c0801d1eed4425aa66e68d89e2bf3b6e1a5a866dfc2c3f4ba9b'),
 'efifs/tar_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/tar_x64.efi','b9ecfda05fa26463bdf76719ec9e602fa78f38daf4d3e3440a65df49e5885335'),
 'efifs/udf_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/udf_x64.efi','8c126fb13d7b7dc2525ba82cc72819dfdb7ef64a63fcaf0ff6a882458be2df76'),
 'efifs/ufs1_be_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/ufs1_be_x64.efi','7ea8a6997953b6074a0b0f6ce2fa55998ffbe2ee4773f94afdd8740738544e8e'),
 'efifs/ufs1_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/ufs1_x64.efi','9c473d66b1c24ee15e1050ad8796eeb028e0fc4955f8145c816047df7f2a59dd'),
 'efifs/ufs2_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/ufs2_x64.efi','99bfb4fb3fa5a61180c2e5d7b91815cd7264cd92f1b5b3d4ac7d1e03b96093da'),
 'efifs/xfs_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/xfs_x64.efi','e6634e30fa66416a617b039142cc90f8478f287143e978f013c0eb0ed7268dee'),
 'efifs/zfs_x64.efi':('https://github.com/pbatard/EfiFs/releases/download/v1.13/zfs_x64.efi','83b8eaa89435e1023b128d283caea6ecd5907f033bb952288474ece7b0440562'),
 'ntfs3g/ntfs_x64.efi':('https://github.com/pbatard/ntfs-3g/releases/download/1.9/ntfs_x64.efi','bea8e78bd1e68a0f3506faab9f2e88274b83cb159b1e7e06191f57228e8086cd'),
 'ntfs3g/ntfs_x64_ro.efi':('https://github.com/pbatard/ntfs-3g/releases/download/1.9/ntfs_x64_ro.efi','33a5b204e460a76832876059226cf98b313446fc15449275d7d72b67a09b9187'),
 'ntfs3g/ntfs_x64_signed.efi':('https://github.com/pbatard/ntfs-3g/releases/download/1.9/ntfs_x64_signed.efi','d77e7f1c317a42467d3f7ade7b3e0a20996b9bf541492fbc15d6245d8d46dcac'),
 'efifs/efifs-1.13.tar.gz':('https://github.com/pbatard/efifs/archive/refs/tags/v1.13.tar.gz','39f0e7f55e68d747ad66b1054cd1629e5b9ce9760a5d59020577ae2121f39723'),
 'ntfs3g/ntfs-3g-uefi-1.9.tar.gz':('https://github.com/pbatard/ntfs-3g/archive/refs/tags/1.9.tar.gz','c93fd51b8104075cd412823249a0b167d2d134c971a6ec8d9af5296b36638974'),
 'grub/grub-f10ad08fa2a02dce95774c1b5caee38254b30531.tar.gz':('https://gitlab.freedesktop.org/gnu-grub/grub/-/archive/f10ad08fa2a02dce95774c1b5caee38254b30531/grub-f10ad08fa2a02dce95774c1b5caee38254b30531.tar.gz','a93ab9c399635beb8e118608b407a82376e2b8661b777bf4660df84284018e3d'),
}
ASIX_COMMIT='4db2845d14425599238be86b8a73d288f7c0b4a6'
ASIX_BASE=f'https://raw.githubusercontent.com/tianocore/edk2-platforms/{ASIX_COMMIT}/'
ASIX_FILES={
 'License.txt':'50ce20c9cfdb0e19ee34fe0a51fc0afe961f743697b068359ab2f862b494df80',
 'Drivers/ASIX/Asix.dec':'1f9e8a753f353e4951346b79ff496fceaba88aa7f0f1f21516fa507a84ea08fb',
 'Drivers/ASIX/Asix.dsc':'4e3e9213b220b1358381c4883ec54a62d5883f3e1bff0454eb251c4ab94e0ae1',
 'Drivers/ASIX/Bus/Usb/UsbNetworking/Ax88772c/Ax88772.c':'f694b6f5915bb859c7838956d2375192e86e478750f3661a7dae9f9792719b70',
 'Drivers/ASIX/Bus/Usb/UsbNetworking/Ax88772c/Ax88772.h':'36d8b55e641fbfd5c0edfae7b5bfcfa1def427c085908f09da0ee82d5a22f880',
 'Drivers/ASIX/Bus/Usb/UsbNetworking/Ax88772c/Ax88772c.inf':'a3232d2d9c28290b93bdac860ef7fbd9691897b35a60f4bac3dab3780dde74a1',
 'Drivers/ASIX/Bus/Usb/UsbNetworking/Ax88772c/ComponentName.c':'7a90e4b2d7dc0292716d0e9d4b252d0e4230180b931e81ab23d7f40dad35ab2b',
 'Drivers/ASIX/Bus/Usb/UsbNetworking/Ax88772c/DriverBinding.c':'4aaa69e96d9c61998e808270ecb769d48909f494d95a19e4fd8b29868e88acca',
 'Drivers/ASIX/Bus/Usb/UsbNetworking/Ax88772c/SimpleNetwork.c':'4df6de8aac13b5c48e5f1ebdabc72b11ddc79de69fe7a568ffd598c437b20f2a',
 'Drivers/ASIX/Bus/Usb/UsbNetworking/Ax88179/Ax88179.c':'990897ca744101bde95cb43436689b6e0a1539a6091740a63e291798b5db943c',
 'Drivers/ASIX/Bus/Usb/UsbNetworking/Ax88179/Ax88179.h':'6b1d1cde72385aae72826daaea350a0a12c6ad33c067a9618a81e909ca5bfbab',
 'Drivers/ASIX/Bus/Usb/UsbNetworking/Ax88179/Ax88179.inf':'c6fb5d9988cb1f981051131337f4b3f9f53b70ed788f833970da3aa5ce81d0b0',
 'Drivers/ASIX/Bus/Usb/UsbNetworking/Ax88179/ComponentName.c':'8b0778a37c9f1a21dd8ad65f7575c0e2bcce61d86a140f124b5b92e93aaa13a0',
 'Drivers/ASIX/Bus/Usb/UsbNetworking/Ax88179/DriverBinding.c':'9b1ea0990e6363ce7f540e05bbed074e9e24120c7c64c5977736b57e3798f8b0',
 'Drivers/ASIX/Bus/Usb/UsbNetworking/Ax88179/SimpleNetwork.c':'abecdd0df6798eb652bb09e20b859b9365ea33895b4e5702cfd592889079c898',
}
# The USB class drivers of edk2 (MdeModulePkg, written for use on top of a firmware USB stack) are compiled from the
# edk2 workspace itself. iPXE's ecm/ncm/acm are NOT linked into nic-30-usb.efi: an iPXE driver binds first (its
# driver binding version is higher) and would keep the adapter even when its start-up fails.
USBNET_DIR='MdeModulePkg/Bus/Usb/UsbNetwork'
USBNET_MODULES=['NetworkCommon','UsbRndis','UsbCdcEcm','UsbCdcNcm']
# module, file name in the pack, description, devices
EDK2_MODULES=[
 ('Ax88772c','nic-usb-31-asix-ax88772.efi','ASIX AX88772/772A/772B/772C USB 2.0 10/100 Ethernet adapters (through the firmware USB stack)',
  ['usb:0b95:7720 AX88772','usb:0b95:772a AX88772A','usb:0b95:772b AX88772B/C','usb:0b95:7e2b AX88772B (ASUS)','usb:17ef:7203 AX88772B (Lenovo)']),
 ('Ax88179','nic-usb-32-asix-ax88179.efi','ASIX AX88179/AX88178A USB 3.0 gigabit Ethernet adapters (through the firmware USB stack)',['usb:0b95:1790 AX88179','usb:0b95:178a AX88178A']),
 ('NetworkCommon','nic-usb-33-class-common.efi','common part of the three USB class drivers below (turns their USB Ethernet protocol into UNDI)',[]),
 ('UsbRndis','nic-usb-34-class-rndis.efi','USB RNDIS network devices (Android USB tethering, some adapters and BMCs)',['usb-class:02/02/ff RNDIS over CDC-ACM','usb-class:ef/04/01 RNDIS']), # not e0/01/03 (older Android phones)
 ('UsbCdcEcm','nic-usb-35-class-cdc-ecm.efi','USB CDC-ECM network adapters',['usb-class:02/06/00 CDC Ethernet Control Model']),
 ('UsbCdcNcm','nic-usb-36-class-cdc-ncm.efi','USB CDC-NCM network adapters',['usb-class:02/0d/00 CDC Network Control Model']),
]
# ---- further edk2 drivers (input, storage, USB, virtual machines, ext4), built from the workspace itself -------------
# (INF path, pack path, description, devices). Files in manual/ are never loaded by a rule (see DRIVER-PACK.txt).
EFIFS_VERSION='1.13';EFIFS_GRUB='f10ad08fa2a02dce95774c1b5caee38254b30531';NTFS3G_VERSION='1.9'
EDK2X_MODULES=[
 ('MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf','input-10-usb-keyboard.efi','USB keyboards (HID boot protocol), through the firmware USB stack',['usb-class:03/01/01 HID boot keyboard']),
 ('MdeModulePkg/Bus/Usb/UsbMouseDxe/UsbMouseDxe.inf','input-20-usb-mouse.efi','USB mice (HID boot protocol; Simple Pointer)',['usb-class:03/01/02 HID boot mouse']),
 ('MdeModulePkg/Bus/Usb/UsbMouseAbsolutePointerDxe/UsbMouseAbsolutePointerDxe.inf','manual/UsbMouseAbsolutePointerDxe.efi','USB mice as an Absolute Pointer (alternative to input-20; binds the same devices)',['usb-class:03/01/02 HID boot mouse']),
 ('MdeModulePkg/Bus/Pci/NvmExpressDxe/NvmExpressDxe.inf','disk-10-nvme.efi','NVM Express solid state disks',['pci-class:01/08/02 NVM Express']),
 ('MdeModulePkg/Bus/Pci/SataControllerDxe/SataControllerDxe.inf','disk-20-sata-controller.efi','SATA (AHCI) and IDE controllers: controller initialisation',['pci-class:01/01 IDE','pci-class:01/06 SATA']),
 ('MdeModulePkg/Bus/Ata/AtaAtapiPassThru/AtaAtapiPassThru.inf','disk-21-ata-atapi-passthru.efi','ATA/ATAPI commands for the SATA/IDE controller driver',[]),
 ('MdeModulePkg/Bus/Ata/AtaBusDxe/AtaBusDxe.inf','disk-22-ata-bus.efi','ATA disks (Block I/O)',[]),
 ('MdeModulePkg/Bus/Scsi/ScsiBusDxe/ScsiBusDxe.inf','disk-30-scsi-bus.efi','SCSI bus (ATAPI drives, UFS, SCSI controllers)',[]),
 ('MdeModulePkg/Bus/Scsi/ScsiDiskDxe/ScsiDiskDxe.inf','disk-31-scsi-disk.efi','SCSI disks and CD/DVD drives (Block I/O)',[]),
 ('MdeModulePkg/Bus/Pci/SdMmcPciHcDxe/SdMmcPciHcDxe.inf','disk-40-sdmmc-controller.efi','SD/eMMC host controllers (SDHCI)',['pci-class:08/05/00 SD host controller','pci-class:08/05/01 SD host controller with DMA']),
 ('MdeModulePkg/Bus/Sd/SdDxe/SdDxe.inf','disk-41-sd-card.efi','SD cards (Block I/O)',[]),
 ('MdeModulePkg/Bus/Sd/EmmcDxe/EmmcDxe.inf','disk-42-emmc.efi','eMMC storage (Block I/O)',[]),
 ('MdeModulePkg/Bus/Pci/UfsPciHcDxe/UfsPciHcDxe.inf','disk-50-ufs-controller.efi','UFS host controllers',['pci-class:01/09 UFS']),
 ('MdeModulePkg/Bus/Ufs/UfsPassThruDxe/UfsPassThruDxe.inf','disk-51-ufs-passthru.efi','UFS storage (SCSI pass-through; disks appear through disk-30/31)',[]),
 ('MdeModulePkg/Bus/Pci/XhciDxe/XhciDxe.inf','usb-10-xhci.efi','USB 3 host controllers (xHCI)',['pci-class:0c/03/30 xHCI']),
 ('MdeModulePkg/Bus/Pci/EhciDxe/EhciDxe.inf','usb-11-ehci.efi','USB 2 host controllers (EHCI)',['pci-class:0c/03/20 EHCI']),
 ('MdeModulePkg/Bus/Pci/UhciDxe/UhciDxe.inf','usb-12-uhci.efi','USB 1.1 host controllers (UHCI; not OHCI)',['pci-class:0c/03/00 UHCI']),
 ('MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf','usb-20-bus.efi','USB bus (device enumeration, hubs)',[]),
 ('MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf','usb-30-mass-storage.efi','USB sticks, disks and card readers (bulk-only and CBI transport, not UAS)',['usb-class:08 mass storage']),
 ('CharonDriverPkg/Ext4Dxe/Ext4Dxe.inf','fs-20-ext4.efi','ext4 file system, read-only (Linux partitions)',[]),
 ('OvmfPkg/VirtioPciDeviceDxe/VirtioPciDeviceDxe.inf','virt-10-virtio-pci-legacy.efi','virtio 0.9.5 PCI transport',['pci:1af4:1000-103f']),
 ('OvmfPkg/Virtio10Dxe/Virtio10.inf','virt-11-virtio-pci-modern.efi','virtio 1.0 PCI transport',['pci:1af4:1040-107f']),
 ('OvmfPkg/VirtioBlkDxe/VirtioBlk.inf','virt-20-virtio-blk.efi','virtio block devices',['virtio:2']),
 ('OvmfPkg/VirtioScsiDxe/VirtioScsi.inf','virt-21-virtio-scsi.efi','virtio SCSI controllers',['virtio:8']),
 ('OvmfPkg/VirtioNetDxe/VirtioNet.inf','virt-22-virtio-net.efi','virtio network adapters (Simple Network)',['virtio:1']),
 ('OvmfPkg/VirtioRngDxe/VirtioRng.inf','virt-23-virtio-rng.efi','virtio random number source (EFI_RNG_PROTOCOL)',['virtio:4']),
 ('OvmfPkg/VirtioSerialDxe/VirtioSerial.inf','virt-24-virtio-serial.efi','virtio console (Serial I/O)',['virtio:3']),
 ('OvmfPkg/VirtioFsDxe/VirtioFsDxe.inf','virt-25-virtio-fs.efi','virtio-fs shared folders (Simple File System)',['virtio:26']),
 ('OvmfPkg/VirtioKeyboardDxe/VirtioKeyboard.inf','virt-26-virtio-keyboard.efi','virtio-input keyboards',['virtio:18']),
 ('OvmfPkg/PvScsiDxe/PvScsiDxe.inf','virt-30-vmware-pvscsi.efi','VMware paravirtual SCSI controller',['pci:15ad:07c0']),
 ('OvmfPkg/LsiScsiDxe/LsiScsiDxe.inf','virt-31-lsi-53c895a.efi','LSI 53C895A SCSI controller as emulated by QEMU',['pci:1000:0012']),
 ('OvmfPkg/MptScsiDxe/MptScsiDxe.inf','virt-32-lsi-fusion-mpt.efi','LSI Fusion-MPT SCSI/SAS controller as emulated by QEMU and VMware',['pci:1000:0030 53C1030','pci:1000:0054 SAS1068','pci:1000:0058 SAS1068E']),
 ('OvmfPkg/SioBusDxe/SioBusDxe.inf','manual/SioBusDxe.efi','BY HAND ONLY: fixed ISA devices (COM1, COM2, PS/2 keyboard and mouse) behind a PCI-ISA (LPC) bridge that no driver manages',['pci-class:06/01 PCI-ISA bridge']),
 ('MdeModulePkg/Bus/Isa/Ps2KeyboardDxe/Ps2KeyboardDxe.inf','manual/Ps2KeyboardDxe.efi','BY HAND ONLY: PS/2 keyboard; binds only to a handle with the Super I/O protocol and the ACPI node PNP0303 (SioBusDxe makes one)',['acpi:PNP0303']),
 ('MdeModulePkg/Bus/Isa/Ps2MouseDxe/Ps2MouseDxe.inf','manual/Ps2MouseDxe.efi','BY HAND ONLY: PS/2 mouse (Super I/O protocol + ACPI node PNP0F13)',['acpi:PNP0F13']),
 ('OpenCorePkg/Platform/OpenNtfsDxe/OpenNtfsDxe.inf','manual/OpenNtfsDxe.efi','NTFS, read-only, OpenCore implementation (BSD licensed alternative to fs-10-ntfs; not signed)',[]),
 # OpenCorePkg/Staging/OpenHfsPlus is not built: with MSVC it does not link in this DSC (unresolved memcpy from fsw_hfsplus.c).
 ('OpenCorePkg/Platform/OpenPartitionDxe/PartitionDxe.inf','manual/OpenPartitionDxe.efi','BY HAND ONLY: partition driver that also reads the Apple Partition Map (old Mac disks), beside GPT, MBR and El Torito',[]),
 ('MdeModulePkg/Bus/Pci/PciSioSerialDxe/PciSioSerialDxe.inf','manual/PciSioSerialDxe.efi','BY HAND ONLY: 16550 serial ports on PCI cards and behind a Super I/O protocol (Serial I/O)',['pci-class:07/00/02 16550 serial','acpi:PNP0501']),
]
# efifs release file -> pack path, description. Everything is read-only. manual/: archive formats, rare systems and
# duplicates of a driver that is loaded by rule.
EFIFS_FILES=[
 ('exfat','fs-11-exfat.efi','exFAT (large USB sticks and SD cards)'),('ext2','fs-21-ext2.efi','ext2/ext3/ext4 (GRUB; used for what fs-20-ext4 does not take)'),
 ('btrfs','fs-22-btrfs.efi','Btrfs (openSUSE, Fedora)'),('xfs','fs-23-xfs.efi','XFS (RHEL, Rocky, Alma)'),('f2fs','fs-24-f2fs.efi','F2FS'),('zfs','fs-25-zfs.efi','ZFS (single device pools GRUB can read)'),
 ('hfsplus','fs-30-hfsplus.efi','Apple HFS+ (macOS installers, old Mac disks)'),('hfs','fs-31-hfs.efi','Apple HFS'),
 ('udf','fs-40-udf.efi','UDF (DVD, Windows ISO images)'),('iso9660','fs-41-iso9660.efi','ISO 9660 with Joliet and Rock Ridge (CD, ISO images)'),
 ('jfs','fs-50-jfs.efi','IBM JFS'),('reiserfs','fs-51-reiserfs.efi','ReiserFS 3'),('nilfs2','fs-52-nilfs2.efi','NILFS2'),('ufs2','fs-53-ufs2.efi','UFS2 (FreeBSD)'),('ufs1','fs-54-ufs1.efi','UFS1 (old BSD)'),
 ('bfs','fs-55-bfs.efi','BeFS (Haiku, BeOS)'),('squash4','fs-60-squash4.efi','SquashFS 4 (live system images)'),('erofs','fs-61-erofs.efi','EROFS (live system images)'),
 ('ntfs','manual/efifs-ntfs.efi','NTFS, GRUB implementation (alternative to fs-10-ntfs, not signed)'),('fat','manual/efifs-fat.efi','FAT12/16/32, GRUB implementation (every firmware has its own FAT driver)'),
 ('afs','manual/efifs-afs.efi','AtheOS file system'),('sfs','manual/efifs-sfs.efi','Amiga Smart File System'),('cbfs','manual/efifs-cbfs.efi','coreboot file system'),('romfs','manual/efifs-romfs.efi','romfs'),
 ('minix','manual/efifs-minix.efi','Minix 1'),('minix2','manual/efifs-minix2.efi','Minix 2'),('minix3','manual/efifs-minix3.efi','Minix 3'),
 ('minix_be','manual/efifs-minix_be.efi','Minix 1, big endian'),('minix2_be','manual/efifs-minix2_be.efi','Minix 2, big endian'),('minix3_be','manual/efifs-minix3_be.efi','Minix 3, big endian'),
 ('ufs1_be','manual/efifs-ufs1_be.efi','UFS1, big endian'),('cpio','manual/efifs-cpio.efi','cpio archive as a volume'),('cpio_be','manual/efifs-cpio_be.efi','cpio archive, big endian'),
 ('newc','manual/efifs-newc.efi','cpio "newc" archive (initramfs) as a volume'),('odc','manual/efifs-odc.efi','cpio "odc" archive as a volume'),('tar','manual/efifs-tar.efi','tar archive as a volume'),
 ('procfs','manual/efifs-procfs.efi','GRUB procfs (of no use on a disk; shipped because the release has it)'),
]
NTFS3G_FILES=[
 ('ntfs_x64_signed.efi','fs-10-ntfs.efi','NTFS, read-only, Secure Boot signed build (Windows partitions and sticks)'),
 ('ntfs_x64_ro.efi','manual/ntfs-3g-readonly-unsigned.efi','NTFS, read-only, the same driver without the signature'),
 ('ntfs_x64.efi','manual/ntfs-3g-readwrite.efi','BY HAND ONLY: NTFS with WRITE support, not signed. Do not use on a hibernated or fast-startup Windows volume'),
]
EDK2X_CLASS_TEXT={'input':'input device driver','disk':'storage driver','usb':'USB host controller / bus / class driver','fs':'file system driver',
 'virt':'virtual machine device driver','manual':'driver loaded only on the user\'s request'}
TIMERLIB_C=r"""/** @file
  TimerLib for UEFI drivers that have to run on any firmware: delays go through the Stall() boot service, the
  performance counter is the CPU time stamp counter and its frequency is measured once against Stall().
  (The edk2 instances need a platform: ACPI timer address, local APIC bus clock or CPUID leaf 15h.)

  Copyright (c) 2026, Charon project. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>

STATIC UINT64  mFrequency = 1000000000ULL;

EFI_STATUS
EFIAPI
UefiStallTimerLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT64  Start;
  UINT64  Ticks;

  Start = AsmReadTsc ();
  SystemTable->BootServices->Stall (10000);
  Ticks = AsmReadTsc () - Start;
  if ((Ticks != 0) && (Ticks < 0x0100000000000000ULL)) {
    mFrequency = MultU64x32 (Ticks, 100);
  }

  return EFI_SUCCESS;
}

UINTN
EFIAPI
MicroSecondDelay (
  IN UINTN  MicroSeconds
  )
{
  gBS->Stall (MicroSeconds);
  return MicroSeconds;
}

UINTN
EFIAPI
NanoSecondDelay (
  IN UINTN  NanoSeconds
  )
{
  gBS->Stall (NanoSeconds / 1000 + 1);
  return NanoSeconds;
}

UINT64
EFIAPI
GetPerformanceCounter (
  VOID
  )
{
  return AsmReadTsc ();
}

UINT64
EFIAPI
GetPerformanceCounterProperties (
  OUT UINT64  *StartValue  OPTIONAL,
  OUT UINT64  *EndValue    OPTIONAL
  )
{
  if (StartValue != NULL) {
    *StartValue = 0;
  }

  if (EndValue != NULL) {
    *EndValue = MAX_UINT64;
  }

  return mFrequency;
}

UINT64
EFIAPI
GetTimeInNanoSecond (
  IN UINT64  Ticks
  )
{
  UINT64  Seconds;
  UINT64  Remainder;

  Seconds = DivU64x64Remainder (Ticks, mFrequency, &Remainder);
  return MultU64x32 (Seconds, 1000000000u) + DivU64x64Remainder (MultU64x32 (Remainder, 1000000000u), mFrequency, NULL);
}
"""
TIMERLIB_INF="""## @file
#  TimerLib through the Stall() boot service and the time stamp counter (see UefiStallTimerLib.c).
#  Copyright (c) 2026, Charon project. All rights reserved.
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##
[Defines]
  INF_VERSION                    = 0x00010005
  BASE_NAME                      = UefiStallTimerLib
  FILE_GUID                      = 6f0f4b6c-6a5f-4d0e-9d55-0d8f0c3b7a41
  MODULE_TYPE                    = UEFI_DRIVER
  VERSION_STRING                 = 1.0
  LIBRARY_CLASS                  = TimerLib|UEFI_DRIVER UEFI_APPLICATION DXE_DRIVER
  CONSTRUCTOR                    = UefiStallTimerLibConstructor

[Sources]
  UefiStallTimerLib.c

[Packages]
  MdePkg/MdePkg.dec

[LibraryClasses]
  BaseLib
  UefiBootServicesTableLib
"""
EDK2X_DEC="""## @file
#  Charon driver pack build package: carries the header of BaseUcs2Utf8Lib (copied unchanged from edk2 RedfishPkg).
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##
[Defines]
  DEC_SPECIFICATION              = 0x0001001b
  PACKAGE_NAME                   = CharonDriverPkg
  PACKAGE_GUID                   = 0c2c5b64-3c57-4f0e-b0a6-55f0c0c7d9a2
  PACKAGE_VERSION                = 0.1

[Includes]
  Include

[LibraryClasses]
  Ucs2Utf8Lib|Include/Library/BaseUcs2Utf8Lib.h
"""
EDK2X_DSC="""## @file
#  Charon driver pack: generic edk2 UEFI drivers built as separate images for loading on any x86-64 firmware.
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##
[Defines]
  PLATFORM_NAME                  = CharonDrivers
  PLATFORM_GUID                  = 5a1d5c0e-7b1e-4a3d-8a44-3c1b6d2f9e10
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = X64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT

!include MdePkg/MdeLibs.dsc.inc

[LibraryClasses]
  DebugLib|MdePkg/Library/UefiDebugLibStdErr/UefiDebugLibStdErr.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  TimerLib|CharonDriverPkg/Library/UefiStallTimerLib/UefiStallTimerLib.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  UefiUsbLib|MdePkg/Library/UefiUsbLib/UefiUsbLib.inf
  UefiRuntimeLib|MdePkg/Library/UefiRuntimeLib/UefiRuntimeLib.inf
  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf
  ReportStatusCodeLib|MdePkg/Library/BaseReportStatusCodeLibNull/BaseReportStatusCodeLibNull.inf
  PerformanceLib|MdePkg/Library/BasePerformanceLibNull/BasePerformanceLibNull.inf
  UefiScsiLib|MdePkg/Library/UefiScsiLib/UefiScsiLib.inf
  UefiHiiServicesLib|MdeModulePkg/Library/UefiHiiServicesLib/UefiHiiServicesLib.inf
  HiiLib|MdeModulePkg/Library/UefiHiiLib/UefiHiiLib.inf
  VirtioLib|OvmfPkg/Library/VirtioLib/VirtioLib.inf
  PciCapLib|OvmfPkg/Library/BasePciCapLib/BasePciCapLib.inf
  PciCapPciIoLib|OvmfPkg/Library/UefiPciCapPciIoLib/UefiPciCapPciIoLib.inf
  TimeBaseLib|EmbeddedPkg/Library/TimeBaseLib/TimeBaseLib.inf
  BaseUcs2Utf8Lib|CharonDriverPkg/Library/BaseUcs2Utf8Lib/BaseUcs2Utf8Lib.inf
  OcStringLib|OpenCorePkg/Library/OcStringLib/OcStringLib.inf
  SortLib|MdeModulePkg/Library/BaseSortLib/BaseSortLib.inf

[Components]
"""
ASIX_CMD=r'''@echo off
setlocal
rem Git Bash sets this; cmd would then not find edksetup.bat in the current directory.
set NoDefaultCurrentDirectoryInExePath=
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PYTHON_COMMAND=python3.15"
set "HOST_ARCH=X64"
set "VS2022_PREFIX=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\"
set "NASM_PREFIX=@NASM@\"
set "PATH=%NASM_PREFIX%;%PATH%"
set "WORKSPACE=@WORKSPACE@"
set "PACKAGES_PATH=@PACKAGES@"
set "EDK_TOOLS_PATH=%WORKSPACE%\BaseTools"
set "BASE_TOOLS_PATH=%EDK_TOOLS_PATH%"
set "EDK_TOOLS_BIN=%EDK_TOOLS_PATH%\Bin\Win64"
set "PATH=%EDK_TOOLS_BIN%;%PATH%"
cd /d "%WORKSPACE%"
call edksetup.bat
if errorlevel 1 exit /b 1
call build -a X64 -b RELEASE -t VS2022 -p Drivers/ASIX/CharonAsix.dsc -n 4
exit /b %errorlevel%
'''
# File name in the pack -> iPXE driver objects linked into that one UEFI driver image.
# USB host controller drivers (xhci, ehci, uhci) are deliberately NOT linked: they would take
# the USB controllers away from the firmware (keyboard, the stick Charon started from).
# USB network adapters go through "usbio", iPXE's client of the firmware's EFI_USB_IO_PROTOCOL.
GROUPS={
 'nic-10-intel':['intel'],
 'nic-11-realtek':['realtek'],
 'nic-12-broadcom':['tg3','bnx2','bnxt','b44'],
 'nic-13-aquantia':['aqc1xx'],
 'nic-14-marvell-atheros':['sky2','skge','atl1e','jme'],
 'nic-15-intel-10g':['intelx','intelxl','ice','igbvf','intelxvf','intelxlvf'],
 'nic-20-virtual':['virtio-net','vmxnet3','ena','gve'],
 'nic-30-usb':['usbio','smsc75xx','smsc95xx','lan78xx','dm96xx','iphone'],
 'nic-40-older':['forcedeth','rhine','velocity','sis190','sis900','pcnet32','eepro100','3c90x','tulip','natsemi','amd8111e','dmfe','davicom','sundance','epic100','w89c840','tlan','myson','icplus','rdc','pnic'],
}
GROUP_TEXT={
 'nic-10-intel':'Intel gigabit and 2.5G: e1000, e1000e (82574, I217/I218/I219), igb (I210/I211/I350), I225/I226',
 'nic-11-realtek':'Realtek RTL8139/8169/8168/8111/8136 (NOT RTL8125/8126)',
 'nic-12-broadcom':'Broadcom NetXtreme (tg3), NetXtreme II (bnx2), NetXtreme-C/E (bnxt), BCM44xx (b44)',
 'nic-13-aquantia':'Marvell/Aquantia AQtion AQC107/108/111/112/113 multi-gigabit',
 'nic-14-marvell-atheros':'Marvell Yukon (sky2, skge), Atheros L1E (atl1e), JMicron JMC250/260 (jme)',
 'nic-15-intel-10g':'Intel 10/40/100G (ixgbe, i40e, ice) and virtual functions',
 'nic-20-virtual':'virtio-net, VMware vmxnet3, Amazon ENA, Google gVNIC',
 'nic-30-usb':'USB network adapters with a maker-specific protocol, through the firmware USB stack: SMSC/Microchip LAN75xx/LAN95xx/LAN78xx, Davicom DM96xx, iPhone tethering',
 'nic-40-older':'older PCI cards: nVidia nForce, VIA Rhine/Velocity, SiS, AMD PCnet, Intel 10/100, 3Com 3c90x, DEC Tulip, NatSemi, ...',
}
# iPXE's default USB configuration links its own xHCI/EHCI/UHCI host controller drivers, a USB disk driver and an
# EFI_USB_IO provider into every image that has a USB network driver. This local configuration (src/config/local/usb.h)
# leaves only the pseudo host controller that works through the firmware's USB stack.
LOCAL_USB_H='''/* Charon driver pack: USB network adapters through the firmware's EFI_USB_IO_PROTOCOL only. */
#undef USB_HCD_XHCI
#undef USB_HCD_EHCI
#undef USB_HCD_UHCI
#undef USB_BLOCK
#undef USB_KEYBOARD
#undef USB_EFI
#define USB_HCD_USBIO
'''
# The only change to iPXE's code (GPL-2.0-or-later, shipped in source/ipxe-build/): when the firmware's USB stack
# refuses the configuration iPXE prefers (it can only use the one the firmware selected), iPXE tries the other
# configurations its drivers accept instead of giving the device up. Needed for adapters with several configurations.
IPXE_PATCH=Path(__file__).with_name('charon-driverpack-ipxe-usb.patch')
STACK=['DpcDxe','SnpDxe','MnpDxe','ArpDxe','Ip4Dxe','Udp4Dxe','Dhcp4Dxe','TcpDxe','DnsDxe','HttpUtilitiesDxe','HttpDxe']

def sha256(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for block in iter(lambda:f.read(1<<20),b''):h.update(block)
    return h.hexdigest()

def fetch(dl):
    for rel,(url,digest) in INPUTS.items():
        target=dl/rel
        if not target.exists():
            target.parent.mkdir(parents=True,exist_ok=True)
            print('download',url)
            with urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'driverpack-fetch'})) as r,open(target,'wb') as f:
                for block in iter(lambda:r.read(1<<20),b''):f.write(block)
        got=sha256(target)
        if got!=digest:raise SystemExit(f'{rel}: SHA-256 {got}, expected {digest}')
        print('ok',rel)

# ---- iPXE build inside a throw-away QEMU machine ---------------------------
BUILD_SH=r'''#!/bin/sh
# Runs inside the Alpine build machine. /w holds this script and the iPXE source archive.
cd /w
ip link set eth0 up
udhcpc -i eth0 -q -n || { echo CHARON-BUILD-DONE rc=10; exit 10; }
printf 'https://dl-cdn.alpinelinux.org/alpine/v3.24/main\n' >/etc/apk/repositories
apk add --no-cache build-base perl xz-dev >apk.log 2>&1 || { tail -5 apk.log; echo CHARON-BUILD-DONE rc=11; exit 11; }
echo "@SHA@  ipxe-@VER@.tar.gz" | sha256sum -c || { echo CHARON-BUILD-DONE rc=12; exit 12; }
mkdir -p /w/out
tar xzf ipxe-@VER@.tar.gz
cd ipxe-@VER@/src
cp /w/local-usb.h config/local/usb.h
patch -p1 -d .. </w/ipxe-usb.patch || { echo CHARON-BUILD-DONE rc=13; exit 13; }
B=bin-x86_64-efi
singles="@SINGLES@"
t=""; for d in $singles; do t="$t $B/$d.efidrv"; done
echo building single drivers
make -k -j4 VERSION=@VER@ $t >/w/out/make-singles.log 2>&1
ok=""; bad=""
for d in $singles; do if [ -f $B/$d.efidrv ]; then ok="$ok $d"; else bad="$bad $d"; fi; done
echo "built:$ok" >/w/out/built.txt
echo "failed:$bad" >>/w/out/built.txt
while read name members; do
  t=""
  for m in $members; do case " $ok " in *" $m "*) t="${t:+$t--}$m";; esac; done
  [ -n "$t" ] || continue
  echo "group $name = $t"
  if make -j4 VERSION=@VER@ $B/$t.efidrv >>/w/out/make-groups.log 2>&1; then
    cp $B/$t.efidrv /w/out/$name.efi; echo "$name $t" >>/w/out/built.txt
  else echo "$name FAILED $t" >>/w/out/built.txt; fi
done </w/groups.txt
gcc --version | head -1 >/w/out/toolchain.txt
ld --version | head -1 >>/w/out/toolchain.txt
make --version | head -1 >>/w/out/toolchain.txt
apk info -v 2>/dev/null | sort >>/w/out/toolchain.txt
cat /w/out/built.txt
cd /w/out && tar cf /dev/vdb . && sync
echo CHARON-BUILD-DONE rc=0
'''
def ipxe(dl,qemu,port=4493):
    fetch(dl)
    area=dl/'ipxe-build-vm';area.mkdir(exist_ok=False)
    source=dl/f'ipxe/ipxe-{IPXE_VERSION}.tar.gz'
    singles=sorted({m for members in GROUPS.values() for m in members})
    script=BUILD_SH.replace('@SHA@',INPUTS[f'ipxe/ipxe-{IPXE_VERSION}.tar.gz'][1]).replace('@VER@',IPXE_VERSION).replace('@SINGLES@',' '.join(singles))
    groups=''.join(f'{name} {" ".join(members)}\n' for name,members in GROUPS.items())
    with tarfile.open(area/'in.img','w') as tar:
        for name,data in (('build.sh',script.encode()),('groups.txt',groups.encode()),('local-usb.h',LOCAL_USB_H.encode()),('ipxe-usb.patch',IPXE_PATCH.read_bytes())):
            info=tarfile.TarInfo(name);info.size=len(data);info.mode=0o755;tar.addfile(info,io.BytesIO(data))
        tar.add(source,arcname=source.name)
    with open(area/'in.img','ab') as f:f.write(bytes(1<<20))
    with open(area/'out.img','wb') as f:f.truncate(64<<20)
    with socket.socket() as probe:
        if probe.connect_ex(('127.0.0.1',port))==0:raise SystemExit(f'port {port} is in use')
    command=[str(qemu),'-name','charon-driverpack-build','-machine','q35,accel=whpx,kernel-irqchip=off','-cpu','max','-smp','4','-m','6144','-display','none',
        '-cdrom',str((dl/'alpine/alpine-virt-3.24.4-x86_64.iso').resolve()),'-boot','d',
        '-drive','if=virtio,format=raw,file=in.img,readonly=on','-drive','if=virtio,format=raw,file=out.img',
        '-nic','user,model=virtio-net-pci','-serial',f'tcp:127.0.0.1:{port},server=on,wait=off']
    child=subprocess.Popen(command,cwd=area,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=(area/'stderr.log').open('wb'))
    log=(area/'serial.log').open('wb')
    try:
        for _ in range(50):
            try:s=socket.create_connection(('127.0.0.1',port),timeout=2);break
            except OSError:time.sleep(0.2)
        else:raise SystemExit('no serial port')
        seen=b''
        def wait(pattern,seconds,poke=None):
            nonlocal seen
            end=time.time()+seconds;s.settimeout(5)
            while time.time()<end:
                found=re.search(pattern,seen)
                if found:seen=seen[found.end():];return found
                try:data=s.recv(65536)
                except socket.timeout:
                    if poke:s.sendall(poke)
                    continue
                if not data:break
                log.write(data);log.flush();seen=(seen+data)[-65536:]
                if child.poll() is not None:break
            raise SystemExit(f'build machine: no {pattern!r} (see {area/"serial.log"})')
        wait(rb'login: ',240,poke=b'\n')
        s.sendall(b'root\n');wait(rb'[#$] ',30)
        s.sendall(b'mkdir /w && tar xf /dev/vda -C /w && sh /w/build.sh\n')
        done=wait(rb'CHARON-BUILD-DONE rc=(\d+)',3000)
        rc=int(done.group(1))
        s.sendall(b'poweroff\n')
        try:child.wait(60)
        except subprocess.TimeoutExpired:pass
    finally:
        if child.poll() is None:child.kill();child.wait()
        log.close()
    if rc:raise SystemExit(f'build failed in the machine, rc={rc}')
    out=dl/'ipxe-out';out.mkdir(exist_ok=True)
    with tarfile.open(area/'out.img') as tar:
        for member in tar:
            name=Path(member.name).name
            if not member.isfile() or not re.fullmatch(r'[A-Za-z0-9._-]+',name):continue
            (out/name).write_bytes(tar.extractfile(member).read());print(name,member.size)
    (out/'build.sh').write_text(script,encoding='utf-8',newline='\n');(out/'groups.txt').write_text(groups,encoding='utf-8',newline='\n')
    (out/'local-usb.h').write_text(LOCAL_USB_H,encoding='utf-8',newline='\n');(out/'ipxe-usb.patch').write_bytes(IPXE_PATCH.read_bytes())
    for name in ('in.img','out.img'):(area/name).unlink()
    print('kept logs in',area)

# ---- ASIX AX88772 (edk2-platforms) with the host's edk2 workspace -----------
def asix(dl,workspace,nasm):
    with WorkspaceLock(workspace,"make-charon-driverpack.py edk2 (Build/CharonAsix)"):_asix(dl,workspace,nasm)
def _asix(dl,workspace,nasm):
    area=dl/'asix';src=area/'src'
    for rel,digest in ASIX_FILES.items():
        target=area/rel if rel=='License.txt' else src/rel
        if not target.exists():
            target.parent.mkdir(parents=True,exist_ok=True)
            with urllib.request.urlopen(urllib.request.Request(ASIX_BASE+rel,headers={'User-Agent':'driverpack-fetch'})) as r:target.write_bytes(r.read())
        if sha256(target)!=digest:raise SystemExit(f'{rel}: SHA-256 differs from the pinned value')
    dsc=(src/'Drivers/ASIX/Asix.dsc').read_text()
    assert dsc.count('  UefiRuntimeLib|')==1
    dsc=dsc.replace('PLATFORM_NAME                  = Asix','PLATFORM_NAME                  = CharonAsix')
    dsc=dsc.replace('  UefiRuntimeLib|','  UefiUsbLib|MdePkg/Library/UefiUsbLib/UefiUsbLib.inf\n  UefiRuntimeLib|')
    dsc=dsc.rstrip('\n')+'\n'+''.join(f'{USBNET_DIR}/{m}/{m}.inf\n' for m in USBNET_MODULES)
    (src/'Drivers/ASIX/CharonAsix.dsc').write_text(dsc,newline='\n')
    script=ASIX_CMD.replace('@NASM@',str(nasm.resolve())).replace('@WORKSPACE@',str(workspace.resolve())).replace('@PACKAGES@',str(src.resolve()))
    (area/'build.cmd').write_text(script,newline='\r\n')
    with (area/'build.log').open('wb') as log:
        rc=subprocess.run(['cmd','/c',str((area/'build.cmd').resolve())],stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL).returncode
    if rc:raise SystemExit(f'edk2 build failed, see {area/"build.log"}')
    out=dl/'edk2-out';out.mkdir(exist_ok=True)
    for module,*_ in EDK2_MODULES:
        data=(workspace/f'Build/CharonAsix/RELEASE_VS2022/X64/{module}.efi').read_bytes();(out/f'{module}.efi').write_bytes(data)
        print(f'{module}.efi',len(data),hashlib.sha256(data).hexdigest())
    # The USB class drivers come from the workspace itself; keep the exact sources that were compiled.
    for module in USBNET_MODULES:
        for path in sorted((workspace/USBNET_DIR/module).glob('*')):
            if path.is_file():
                target=out/'usbnetwork-source'/module/path.name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(path.read_bytes())
    shutil.rmtree(workspace/'Build/CharonAsix',ignore_errors=True)

class WorkspaceLock:
    """work/ha is shared with other builds: one build at a time (file .charon-build.lock in the workspace)."""
    def __init__(self,workspace,purpose,wait=1800):self.path=workspace/'.charon-build.lock';self.purpose=purpose;self.wait=wait
    def __enter__(self):
        end=time.time()+self.wait
        while True:
            try:
                with open(self.path,'x',encoding='utf-8') as f:f.write(f'{self.purpose} pid={os.getpid()} {time.strftime("%Y-%m-%d %H:%M:%S")}\n')
                return self
            except FileExistsError:
                if time.time()>end:raise SystemExit(f'{self.path} still exists: {self.path.read_text(errors="replace").strip()}')
                time.sleep(5)
    def __exit__(self,*_):
        try:self.path.unlink()
        except FileNotFoundError:pass

def edk2x(dl,workspace,nasm):
    """Generic edk2 drivers (EDK2X_MODULES) from the workspace's own sources -> DL_DIR/edk2x-out/<pack path> and a copy
    of every compiled module's source folder."""
    area=dl/'edk2x';src=area/'src';lib=src/'CharonDriverPkg/Library/UefiStallTimerLib';lib.mkdir(parents=True,exist_ok=True)
    (lib/'UefiStallTimerLib.c').write_text(TIMERLIB_C,newline='\n');(lib/'UefiStallTimerLib.inf').write_text(TIMERLIB_INF,newline='\n')
    # Ext4Dxe names RedfishPkg.dec only for one small library, and that package description does not parse in this
    # workspace (its jansson submodule is absent). The workspace is shared, so nothing is changed there: Ext4Dxe, the
    # library and its header are copied unchanged into CharonDriverPkg and only the package reference is replaced.
    pkg=src/'CharonDriverPkg'
    if (pkg/'Ext4Dxe').exists():shutil.rmtree(pkg/'Ext4Dxe')
    shutil.copytree(workspace/'Ext4Pkg/Ext4Dxe',pkg/'Ext4Dxe')
    inf=(pkg/'Ext4Dxe/Ext4Dxe.inf').read_text();assert inf.count('RedfishPkg/RedfishPkg.dec')==1
    (pkg/'Ext4Dxe/Ext4Dxe.inf').write_text(inf.replace('RedfishPkg/RedfishPkg.dec','CharonDriverPkg/CharonDriverPkg.dec'),newline='\n')
    (pkg/'Library/BaseUcs2Utf8Lib').mkdir(parents=True,exist_ok=True);(pkg/'Include/Library').mkdir(parents=True,exist_ok=True)
    (pkg/'Library/BaseUcs2Utf8Lib/BaseUcs2Utf8Lib.c').write_bytes((workspace/'RedfishPkg/Library/BaseUcs2Utf8Lib/BaseUcs2Utf8Lib.c').read_bytes())
    (pkg/'Include/Library/BaseUcs2Utf8Lib.h').write_bytes((workspace/'RedfishPkg/Include/Library/BaseUcs2Utf8Lib.h').read_bytes())
    inf=(workspace/'RedfishPkg/Library/BaseUcs2Utf8Lib/BaseUcs2Utf8Lib.inf').read_text();assert inf.count('RedfishPkg/RedfishPkg.dec')==1
    (pkg/'Library/BaseUcs2Utf8Lib/BaseUcs2Utf8Lib.inf').write_text(inf.replace('RedfishPkg/RedfishPkg.dec','CharonDriverPkg/CharonDriverPkg.dec'),newline='\n')
    (pkg/'CharonDriverPkg.dec').write_text(EDK2X_DEC,newline='\n')
    (pkg/'CharonDrivers.dsc').write_text(EDK2X_DSC+''.join(f'  {inf}\n' for inf,*_ in EDK2X_MODULES),newline='\n')
    script=ASIX_CMD.replace('@NASM@',str(nasm.resolve())).replace('@WORKSPACE@',str(workspace.resolve())).replace('@PACKAGES@',str(src.resolve())).replace('Drivers/ASIX/CharonAsix.dsc -n 4','CharonDriverPkg/CharonDrivers.dsc -n 8')
    (area/'build.cmd').write_text(script,newline='\r\n')
    built=workspace/'Build/CharonDrivers'
    with WorkspaceLock(workspace,'make-charon-driverpack.py edk2 (Build/CharonDrivers)'):
        try:
            with (area/'build.log').open('wb') as log:
                rc=subprocess.run(['cmd','/c',str((area/'build.cmd').resolve())],stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL).returncode
            if rc:raise SystemExit(f'edk2 build failed, see {area/"build.log"}')
            out=dl/'edk2x-out'
            if out.exists():shutil.rmtree(out)
            for inf,target,*_ in EDK2X_MODULES:
                root=src if (src/inf).exists() else workspace
                base=re.search(r'^\s*BASE_NAME\s*=\s*(\S+)',(root/inf).read_text(errors='replace'),re.M).group(1)
                data=(built/f'RELEASE_VS2022/X64/{base}.efi').read_bytes()
                (out/target).parent.mkdir(parents=True,exist_ok=True);(out/target).write_bytes(data)
                print(f'{target:44} {base:28} {len(data):7} {hashlib.sha256(data).hexdigest()}')
                folder=(root/inf).parent
                for path in sorted(folder.rglob('*')):
                    if path.is_file():
                        copy=out/'source'/Path(inf).parent/path.relative_to(folder);copy.parent.mkdir(parents=True,exist_ok=True);copy.write_bytes(path.read_bytes())
            for path in sorted(pkg.rglob('*')):
                if path.is_file() and 'Ext4Dxe' not in path.relative_to(pkg).parts[:1]:
                    copy=out/'source/CharonDriverPkg'/path.relative_to(pkg);copy.parent.mkdir(parents=True,exist_ok=True);copy.write_bytes(path.read_bytes())
            (out/'source/build.cmd').write_bytes((area/'build.cmd').read_bytes())
            for rel in ('License.txt','OvmfPkg/License.txt','OpenCorePkg/LICENSE.txt'):
                if (workspace/rel).exists():
                    copy=out/'source'/rel;copy.parent.mkdir(parents=True,exist_ok=True);copy.write_bytes((workspace/rel).read_bytes())
        finally:
            shutil.rmtree(built,ignore_errors=True)

# ---- PE check ---------------------------------------------------------------
def pe_info(data):
    if data[:2]!=b'MZ' or len(data)<0x100:return None
    at=struct.unpack_from('<I',data,0x3c)[0]
    if at+0x78>len(data) or data[at:at+4]!=b'PE\0\0':return None
    machine,sections=struct.unpack_from('<HH',data,at+4);magic=struct.unpack_from('<H',data,at+24)[0]
    return dict(machine=machine,sections=sections,magic=magic,subsystem=struct.unpack_from('<H',data,at+24+68)[0],
        entry=struct.unpack_from('<I',data,at+24+16)[0],image_size=struct.unpack_from('<I',data,at+24+56)[0])
def check_driver(path):
    info=pe_info(Path(path).read_bytes())
    if not info:return 'not a PE image'
    if info['magic']!=0x20b:return 'not PE32+'
    if info['machine']!=0x8664:return f'machine {info["machine"]:#x}, not x86-64'
    if info['subsystem'] not in (11,12):return f'subsystem {info["subsystem"]}, not an EFI driver'
    if not info['entry'] or not info['sections']:return 'no entry point or sections'
    return None

# ---- device IDs from the iPXE source ---------------------------------------
def ipxe_ids(source_tar,drivers):
    """driver object -> list of 'pci:vvvv:dddd name' / 'usb:vvvv:pppp name' / 'usb-class:cc/ss/pp' from its .c file."""
    wanted={d.replace('-','_'):d for d in drivers};wanted.update({d:d for d in drivers});wanted['igbvf_main']='igbvf';wanted['virtio-net']='virtio-net'
    found={d:[] for d in drivers}
    with tarfile.open(source_tar) as tar:
        for member in tar:
            if not member.isfile() or not member.name.endswith('.c') or '/src/drivers/' not in member.name:continue
            stem=Path(member.name).stem
            if stem not in wanted:continue
            text=tar.extractfile(member).read().decode('utf-8','replace');driver=wanted[stem];ids=found[driver]
            for m in re.finditer(r'PCI_ROM\s*\(\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*"([^"]*)"',text):
                ids.append(f'pci:{int(m.group(1),16):04x}:{int(m.group(2),16):04x} {m.group(3)}')
            for m in re.finditer(r'USB_(?:ROM|ID)\s*\(\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*"([^"]*)"',text):
                ids.append(f'usb:{int(m.group(1),16):04x}:{int(m.group(2),16):04x} {m.group(3)}')
            for m in re.finditer(r'USB_CLASS_ID\s*\(([^)]*)\)',text):
                ids.append('usb-class:'+re.sub(r'\s+','',m.group(1)))
    return found

CLASS_RULES={
 'nic-':'network adapter drivers: loaded only when the firmware drives no network adapter (CharonWsNet.h LoadDrivers)',
 'nic-usb-':'USB network drivers that follow the UEFI driver model: as nic-, and also for a USB network interface nothing drives while another adapter works (CharonDriverPack.h LoadUsbNetwork)',
 'wifi-':'Wi-Fi drivers: loaded when no adapter is driven or no Wi-Fi connection protocol exists (none in the pack)',
 'net-':'IPv4 network stack: loaded only when adapters exist without an IPv4 service',
 'fs-':'file system drivers: loaded when a block device with a medium has no file system driver; only those devices are connected (CharonDriverPack.h LoadFileSystems)',
 'input-':'USB keyboard and mouse drivers: loaded only when a USB HID boot keyboard or mouse interface exists that nothing drives (LoadInput)',
 'disk-':'storage drivers: loaded only when a PCI mass storage or SD host controller exists that nothing drives; only that controller is connected (LoadDisks)',
 'usb-':'USB host controller, bus and mass storage drivers: loaded only when a UHCI/EHCI/xHCI controller exists that nothing drives and firmware legacy support does not own (LoadUsbControllers)',
 'virt-':'virtual machine devices: loaded only when a virtio, VMware PVSCSI or emulated LSI device exists that nothing drives (LoadVirtual)',
 'audio-':'reserved: no loadable sound driver exists besides AudioDxe; nothing loads this class',
 'manual/':'never loaded by a rule: only when the user picks the file (LoadManual); then every controller is connected',
}
def assemble(dl,share,out):
    out.mkdir(parents=True,exist_ok=False)
    built=dl/'ipxe-out';source=dl/f'ipxe/ipxe-{IPXE_VERSION}.tar.gz'
    spec=importlib.util.spec_from_file_location('netdrivers',Path(__file__).with_name('make-charon-netdrivers.py'))
    net=importlib.util.module_from_spec(spec);spec.loader.exec_module(net)
    entries=[]
    members={}
    for line in (built/'built.txt').read_text().splitlines():
        parts=line.split()
        if len(parts)==2 and parts[0] in GROUPS:members[parts[0]]=parts[1].split('--')
    ids=ipxe_ids(source,sorted({m for v in members.values() for m in v}))
    for name,drivers in members.items():
        data=(built/f'{name}.efi').read_bytes();(out/f'{name}.efi').write_bytes(data)
        entries.append(dict(file=f'{name}.efi',kind='network adapter driver (SNP)',name='iPXE UEFI driver: '+', '.join(drivers),version=f'iPXE {IPXE_VERSION} + source/ipxe-build/ipxe-usb.patch (USB core) + local-usb.h (configuration)',
            description=GROUP_TEXT[name],source=INPUTS[f'ipxe/ipxe-{IPXE_VERSION}.tar.gz'][0],source_sha256=INPUTS[f'ipxe/ipxe-{IPXE_VERSION}.tar.gz'][1],
            built='from source by tools/make-charon-driverpack.py ipxe (gcc in an Alpine Linux 3.24 machine); see source/ipxe-build/',
            licence='GPL-2.0-or-later (iPXE; some files GPL-2.0-only, UBDL or BSD - see licences/ipxe-COPYING*)',
            sha256=hashlib.sha256(data).hexdigest(),size=len(data),devices={d:ids.get(d,[]) for d in drivers}))
    edk2=dl/'edk2-out'
    for module,name,text,devices in EDK2_MODULES:
        if not (edk2/f'{module}.efi').exists():print('not built:',module);continue
        data=(edk2/f'{module}.efi').read_bytes();(out/name).write_bytes(data)
        asix=module.startswith('Ax')
        hashes={rel:digest for rel,digest in ASIX_FILES.items() if f'/{module}/' in rel} if asix else {p.name:sha256(p) for p in sorted((edk2/'usbnetwork-source'/module).glob('*'))}
        entries.append(dict(file=name,kind='network adapter driver (SNP)' if asix else 'USB network class driver (UNDI through NetworkCommon; needs the firmware\'s or the pack\'s SnpDxe)',
            name=('edk2-platforms ' if asix else 'edk2 MdeModulePkg UsbNetwork ')+module,version='edk2-platforms '+ASIX_COMMIT[:12] if asix else 'as in the edk2 (AUDK) workspace work/ha',
            description=text,source=f'https://github.com/tianocore/edk2-platforms/tree/{ASIX_COMMIT}/Drivers/ASIX/Bus/Usb/UsbNetworking/{module}' if asix else f'https://github.com/tianocore/edk2/tree/master/{USBNET_DIR}/{module} (compiled files: source/edk2-usbnetwork/{module})',
            source_sha256=hashes,built='from source by tools/make-charon-driverpack.py edk2 (MSVC, edk2 workspace work/ha); see source/asix/build.cmd',
            licence='BSD-2-Clause-Patent',sha256=hashlib.sha256(data).hexdigest(),size=len(data),devices={module:devices}))
    edk2x=dl/'edk2x-out'
    for inf,target,text,devices in EDK2X_MODULES:
        if not (edk2x/target).exists():print('not built:',target);continue
        data=(edk2x/target).read_bytes();(out/target).parent.mkdir(parents=True,exist_ok=True);(out/target).write_bytes(data)
        where=Path(inf).parent.as_posix();folder=edk2x/'source'/where;cls='manual' if target.startswith('manual/') else target.split('-')[0]
        own=where.startswith('CharonDriverPkg/');opencore=where.startswith('OpenCorePkg/')
        url=('https://github.com/acidanthera/audk/tree/master/Ext4Pkg/Ext4Dxe (first published as https://github.com/tianocore/edk2-platforms/tree/master/Features/Ext4Pkg)'
             if own else 'https://github.com/acidanthera/OpenCorePkg/tree/master/'+where[len('OpenCorePkg/'):] if opencore else 'https://github.com/tianocore/edk2/tree/master/'+where)
        entries.append(dict(file=target,kind=EDK2X_CLASS_TEXT[cls],name=('' if opencore else 'edk2 ')+('Ext4Pkg/Ext4Dxe' if own else where),
            version='as in the edk2 (AUDK) workspace work/ha; the compiled files are in source/edk2/'+where,description=text,source=url,
            source_sha256={q.relative_to(folder).as_posix():sha256(q) for q in sorted(folder.rglob('*')) if q.is_file()},
            built='from source by tools/make-charon-driverpack.py edk2 (MSVC, edk2 workspace work/ha; TimerLib = source/edk2/CharonDriverPkg/Library/UefiStallTimerLib); see source/edk2/build.cmd',
            licence='BSD-3-Clause (OpenCorePkg; some files BSD-2-Clause-Patent or a BSD style notice of their own; licences/OpenCorePkg-LICENSE.txt)' if opencore else 'BSD-2-Clause-Patent',
            sha256=hashlib.sha256(data).hexdigest(),size=len(data),devices={Path(inf).stem:devices}))
    def release(rel,target,kind,name,version,text,page,sources,licence):
        url,digest=INPUTS[rel];data=(dl/rel).read_bytes()
        if hashlib.sha256(data).hexdigest()!=digest:raise SystemExit(f'{rel}: SHA-256 differs from the pinned value')
        (out/target).parent.mkdir(parents=True,exist_ok=True);(out/target).write_bytes(data)
        entries.append(dict(file=target,kind=kind,name=name,version=version,description=text,source=url,source_sha256={INPUTS[r][0]:INPUTS[r][1] for r in sources},
            built=f'official release binary from {page} (SHA-256 equal to the digest GitHub reports for the release asset), not rebuilt; corresponding source: '+', '.join('source/'+Path(r).name for r in sources),
            licence=licence,sha256=digest,size=len(data),devices={}))
    efifsSources=[f'efifs/efifs-{EFIFS_VERSION}.tar.gz',f'grub/grub-{EFIFS_GRUB}.tar.gz'];ntfsSources=[f'ntfs3g/ntfs-3g-uefi-{NTFS3G_VERSION}.tar.gz']
    for fs,target,text in EFIFS_FILES:
        release(f'efifs/{fs}_x64.efi',target,EDK2X_CLASS_TEXT['manual' if target.startswith('manual/') else 'fs'],f'EfiFs {fs}',f'EfiFs {EFIFS_VERSION} (GRUB 2.16, commit {EFIFS_GRUB[:12]})',
            text+'; read-only',f'https://github.com/pbatard/efifs/releases/tag/v{EFIFS_VERSION}',efifsSources,'GPL-3.0-or-later')
    for asset,target,text in NTFS3G_FILES:
        release(f'ntfs3g/{asset}',target,EDK2X_CLASS_TEXT['manual' if target.startswith('manual/') else 'fs'],'ntfs-3g UEFI driver '+asset,f'pbatard/ntfs-3g {NTFS3G_VERSION}',
            text,f'https://github.com/pbatard/ntfs-3g/releases/tag/{NTFS3G_VERSION}',ntfsSources,'GPL-2.0-or-later (libntfs-3g parts LGPL-2.0-or-later, see licences/ntfs-3g-COPYING*)')
    found={};net.modules((share/'edk2-x86_64-code.fd').read_bytes(),found)
    firmware=sha256(share/'edk2-x86_64-code.fd')
    for order,module in enumerate(STACK):
        if module not in found:print('missing in firmware image:',module);continue
        data=found[module];name=f'net-{10+order}-{module}.efi';(out/name).write_bytes(data)
        entries.append(dict(file=name,kind='network stack module',name='edk2 '+module,version='as built into QEMU\'s edk2-x86_64-code.fd',
            description='edk2 NetworkPkg/'+module,source='https://github.com/tianocore/edk2 (NetworkPkg); binary taken from the firmware image shipped with QEMU for Windows',
            source_sha256=firmware,built='extracted from the firmware volume, not rebuilt',licence='BSD-2-Clause-Patent',
            sha256=hashlib.sha256(data).hexdigest(),size=len(data),devices={}))
    licences=out/'licences';licences.mkdir()
    with tarfile.open(source) as tar:
        for member in tar:
            if member.isfile() and re.fullmatch(rf'ipxe-{IPXE_VERSION}/COPYING[A-Za-z0-9.]*',member.name):
                (licences/('ipxe-'+Path(member.name).name+'.txt')).write_bytes(tar.extractfile(member).read())
    if (share/'edk2-licenses.txt').exists():(licences/'edk2-licenses.txt').write_bytes((share/'edk2-licenses.txt').read_bytes())
    src=out/'source';(src/'ipxe-build').mkdir(parents=True)
    (src/source.name).write_bytes(source.read_bytes())
    for name in ('build.sh','groups.txt','local-usb.h','ipxe-usb.patch','built.txt','toolchain.txt','make-singles.log','make-groups.log'):
        if (built/name).exists():(src/'ipxe-build'/name).write_bytes((built/name).read_bytes())
    if edk2.exists():
        (licences/'edk2-platforms-License.txt').write_bytes((dl/'asix/License.txt').read_bytes())
        for rel in ASIX_FILES:
            if rel=='License.txt':continue
            target=src/'asix'/rel;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes((dl/'asix/src'/rel).read_bytes())
        (src/'asix/Drivers/ASIX/CharonAsix.dsc').write_bytes((dl/'asix/src/Drivers/ASIX/CharonAsix.dsc').read_bytes())
        (src/'asix/build.cmd').write_bytes((dl/'asix/build.cmd').read_bytes())
        for path in sorted((edk2/'usbnetwork-source').rglob('*')):
            if path.is_file():
                target=src/'edk2-usbnetwork'/path.relative_to(edk2/'usbnetwork-source');target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(path.read_bytes())
    if edk2x.exists():
        shutil.copytree(edk2x/'source',src/'edk2')
        for rel,name in (('License.txt','edk2-License.txt'),('OvmfPkg/License.txt','edk2-OvmfPkg-License.txt'),('OpenCorePkg/LICENSE.txt','OpenCorePkg-LICENSE.txt')):
            if (edk2x/'source'/rel).exists():(licences/name).write_bytes((edk2x/'source'/rel).read_bytes())
    for rel in efifsSources+ntfsSources:
        if sha256(dl/rel)!=INPUTS[rel][1]:raise SystemExit(f'{rel}: SHA-256 differs from the pinned value')
        (src/Path(rel).name).write_bytes((dl/rel).read_bytes())
    for rel,member,name in ((efifsSources[0],'LICENSE','efifs-LICENSE-GPL-3.0.txt'),(efifsSources[1],'COPYING','grub-COPYING-GPL-3.0.txt'),
            (ntfsSources[0],'COPYING','ntfs-3g-COPYING-GPL-2.0.txt'),(ntfsSources[0],'COPYING.LIB','ntfs-3g-COPYING.LIB-LGPL-2.0.txt')):
        with tarfile.open(dl/rel) as tar:
            for item in tar:
                if item.isfile() and item.name.count('/')==1 and item.name.split('/')[1]==member:
                    (licences/name).write_bytes(tar.extractfile(item).read());break
            else:raise SystemExit(f'{rel}: no {member}')
    (src/'make-charon-driverpack.py').write_bytes(Path(__file__).read_bytes())
    (src/'make-charon-netdrivers.py').write_bytes(Path(__file__).with_name('make-charon-netdrivers.py').read_bytes())
    for entry in entries:
        problem=check_driver(out/entry['file'])
        if problem:raise SystemExit(f'{entry["file"]}: {problem}')
    (out/'MANIFEST.json').write_text(json.dumps(dict(pack='charon-drivers-r407',format=2,
        note='Every file here is redistributable. Nothing was run on a physical computer; what was run in a virtual machine at all is listed in src/efi/DRIVER-PACK.txt (copied here as DRIVER-PACK.txt).',
        classes=CLASS_RULES,drivers=entries),indent=1),encoding='utf-8')
    doc=Path(__file__).resolve().parents[1]/'src/efi/DRIVER-PACK.txt'
    if doc.exists():(out/'DRIVER-PACK.txt').write_bytes(doc.read_bytes())
    lines=['Charon driver pack r407. Copy everything in this folder that ends in .efi, and the folder "manual", to',
     '\\EFI\\HAK\\drivers on Charon\'s stick or on any FAT volume of the computer. Details: DRIVER-PACK.txt.','',
     'Charon decides by the beginning of the file name when a driver is loaded:']
    lines+=[f'  {prefix:10} {rule}' for prefix,rule in CLASS_RULES.items()]
    lines+=['','Nothing here was run on a physical computer. Files are unsigned (except fs-10-ntfs.efi): with Secure Boot on,','the firmware refuses them and Charon says how many were refused.','']
    for entry in entries:lines.append(f'{entry["file"]:42} {entry["description"]}')
    lines+=['','Licences (texts in licences/, sources in source/):',
     'nic-10..40-*.efi: iPXE '+IPXE_VERSION+' (https://ipxe.org), GPL-2.0-or-later; complete corresponding source source/'+source.name+',',
     '  build recipe and changes source/ipxe-build/, licence texts licences/ipxe-COPYING*.',
     'nic-usb-31/32-*.efi: edk2-platforms, BSD-2-Clause-Patent (licences/edk2-platforms-License.txt, source/asix).',
     'nic-usb-33..36-*.efi: edk2 MdeModulePkg UsbNetwork, BSD-2-Clause-Patent (source/edk2-usbnetwork).',
     'net-*.efi: edk2 NetworkPkg, BSD-2-Clause-Patent (licences/edk2-licenses.txt).',
     'input-, disk-, usb-, virt-*.efi, fs-20-ext4.efi and the edk2 files in manual/: edk2, BSD-2-Clause-Patent',
     '  (licences/edk2-License.txt, licences/edk2-OvmfPkg-License.txt; the compiled sources are in source/edk2).',
     'manual/Open*.efi: Acidanthera OpenCorePkg, BSD-3-Clause (licences/OpenCorePkg-LICENSE.txt; source/edk2/OpenCorePkg).',
     f'fs-*.efi except fs-10 and fs-20, manual/efifs-*.efi: EfiFs {EFIFS_VERSION} by Pete Batard (https://github.com/pbatard/efifs), built on GRUB 2.16,',
     f'  GPL-3.0-or-later (licences/efifs-LICENSE-GPL-3.0.txt); corresponding source source/efifs-{EFIFS_VERSION}.tar.gz and',
     f'  source/grub-{EFIFS_GRUB}.tar.gz (the GRUB commit the release pins as a submodule).',
     f'fs-10-ntfs.efi, manual/ntfs-3g-*.efi: ntfs-3g UEFI driver {NTFS3G_VERSION} (https://github.com/pbatard/ntfs-3g), GPL-2.0-or-later',
     f'  (licences/ntfs-3g-COPYING*); corresponding source source/ntfs-3g-uefi-{NTFS3G_VERSION}.tar.gz.',
     'These drivers are separate programs loaded through the firmware; they are shipped beside Charon, not linked into it.',
     'MANIFEST.json: version, source, SHA-256, licence and PCI/USB IDs of every file.','',
     'Drivers of the card makers (Realtek RTL8125/8126 and USB, Intel, Wi-Fi) are NOT in this pack: their licences do',
     'not allow passing them on. Download one from the maker, name it nic-<anything>.efi (wired or USB) or',
     'wifi-<anything>.efi and put it in this folder. Other classes the same way: disk-, usb-, input-, fs-.','']
    (out/'README.txt').write_text('\n'.join(lines),encoding='utf-8')
    print(len(entries),'drivers in',out)

def verify(out):
    manifest=json.loads((out/'MANIFEST.json').read_text(encoding='utf-8'));listed={e['file']:e for e in manifest['drivers']};bad=0
    for path in sorted(q for q in out.rglob('*.efi') if q.relative_to(out).parts[0]!='source'):
        name=path.relative_to(out).as_posix();problem=check_driver(path)
        if not problem and name not in listed:problem='not in MANIFEST.json'
        if not problem and sha256(path)!=listed[name]['sha256']:problem='SHA-256 differs from MANIFEST.json'
        if not problem and not all(listed[name].get(key) for key in ('name','version','source','licence','sha256')):problem='manifest entry incomplete'
        info=pe_info(path.read_bytes()) or {}
        print(f'{"FAIL" if problem else "ok":4} {name:44} machine={info.get("machine",0):#06x} subsystem={info.get("subsystem")} size={path.stat().st_size} {problem or ""}')
        bad+=bool(problem)
    for name in ('README.txt','licences','source'):
        if not (out/name).exists():print('FAIL',name,'missing');bad+=1
    for name in listed:
        if not (out/name).exists():print('FAIL',name,'listed but missing');bad+=1
    if bad:raise SystemExit(f'{bad} problem(s)')
    print('all',len(listed),'images are PE32+ x86-64 EFI drivers and match the manifest')

def main():
    if len(sys.argv)<3:raise SystemExit(__doc__)
    stage=sys.argv[1]
    if stage=='fetch':fetch(Path(sys.argv[2]))
    elif stage=='ipxe':ipxe(Path(sys.argv[2]).resolve(),Path(sys.argv[3]),int(sys.argv[4]) if len(sys.argv)>4 else 4493)
    elif stage=='edk2':
        which=sys.argv[5] if len(sys.argv)>5 else 'all'
        if which not in ('all','network','more'):raise SystemExit(__doc__)
        if which in ('all','network'):asix(Path(sys.argv[2]).resolve(),Path(sys.argv[3]),Path(sys.argv[4]))
        if which in ('all','more'):edk2x(Path(sys.argv[2]).resolve(),Path(sys.argv[3]),Path(sys.argv[4]))
    elif stage=='assemble':assemble(Path(sys.argv[2]),Path(sys.argv[3]),Path(sys.argv[4]))
    elif stage=='verify':verify(Path(sys.argv[2]))
    else:raise SystemExit(__doc__)
if __name__=='__main__':main()
