nvdaControllerClient.dll (\EFI\HAK\addon)
==========================================
NV Access's controller client for NVDA, x64, unchanged, from
https://download.nvaccess.org/releases/2026.3/nvda_2026.3_controllerClient.zip
SHA-256 of the DLL: 7ef3aaa056c8b3b37db9f8cd82da94ad6254f1726184f5fc17d8c90510f54908
Licence: GNU Lesser General Public License, version 2.1 (LGPL-2.1.txt).
Source: the NVDA repository, https://github.com/nvaccess/nvda , release 2026.3,
folder nvdaHelper (the client is built from it); the archive above also holds the
header and the licence. Charon's scripts load the library to hand their messages to
a running NVDA; it may be replaced by any other build of the same interface.
NVDA itself is not shipped.
