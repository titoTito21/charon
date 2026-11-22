/* HAK HDA machine-quirk matching (Charon r408): pure table walking, no I/O.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Follows Linux v7.3 sound/hda/common/auto_parser.c: hda_quirk_match(),
 * snd_hda_pick_fixup() and __snd_hda_apply_fixup(), and
 * sound/hda/common/bind.c hda_codec_match() for the revision rule.
 * Needs only the types of HakHdaQuirks.h and a table mHakFamilies[]. */
#ifndef HAK_HDA_QUIRK_MATCH_H
#define HAK_HDA_QUIRK_MATCH_H

#define HAK_HDA_CHAIN_MAX  16

/* The fixup table of the kernel driver that binds this codec, or NULL.
 * An entry with a revision wins over an entry for any revision. */
static CONST HAK_HDA_FAMILY *
HakQuirkFamily (
  IN UINT32  VendorId,
  IN UINT32  RevisionId
  )
{
  UINTN                 f;
  UINTN                 i;
  CONST HAK_HDA_FAMILY  *any;

  any = NULL;
  for (f = 0; f < sizeof (mHakFamilies) / sizeof (mHakFamilies[0]); f++) {
    for (i = 0; i < mHakFamilies[f].IdCount; i++) {
      if (mHakFamilies[f].Ids[i].Id != VendorId) {
        continue;
      }

      if (mHakFamilies[f].Ids[i].Rev == 0) {
        if (any == NULL) {
          any = &mHakFamilies[f];
        }
      } else if (mHakFamilies[f].Ids[i].Rev == RevisionId) {
        return &mHakFamilies[f];
      }
    }
  }

  return any;
}

static BOOLEAN
HakQuirkRowMatches (
  IN CONST HAK_HDA_QUIRK  *Row,
  IN UINT16               Vendor,
  IN UINT16               Device
  )
{
  if (Row->SubVendor != Vendor) {
    return FALSE;
  }

  return (BOOLEAN)(Row->SubDevice == 0 || (Device & Row->Mask) == Row->SubDevice);
}

/* The kernel's order: every row against the PCI SSID (rows marked "codec
 * SSID" against the codec SSID; all rows against it when there is no PCI
 * SSID), then every row against the codec SSID. Returns the row or NULL. */
static CONST HAK_HDA_QUIRK *
HakQuirkPick (
  IN CONST HAK_HDA_FAMILY  *Family,
  IN UINT16                PciVendor,
  IN UINT16                PciDevice,
  IN UINT16                CodecVendor,
  IN UINT16                CodecDevice
  )
{
  UINTN                q;
  CONST HAK_HDA_QUIRK  *row;

  for (q = 0; q < Family->QuirkCount; q++) {
    row = &Family->Quirks[q];
    if ((PciVendor == 0) || (PciDevice == 0) || ((row->Fixup & HAK_HDA_QUIRK_CODEC_SSID) != 0)) {
      if (HakQuirkRowMatches (row, CodecVendor, CodecDevice)) {
        return row;
      }
    } else if (HakQuirkRowMatches (row, PciVendor, PciDevice)) {
      return row;
    }
  }

  for (q = 0; q < Family->QuirkCount; q++) {
    row = &Family->Quirks[q];
    if (HakQuirkRowMatches (row, CodecVendor, CodecDevice)) {
      return row;
    }
  }

  return NULL;
}

/* The fixups __snd_hda_apply_fixup() visits for Start, in its order. */
static VOID
HakQuirkChainWalk (
  IN     CONST HAK_HDA_FAMILY  *Family,
  IN     UINTN                 Id,
  IN     UINTN                 Depth,
  IN OUT UINT16                *Order,
  IN OUT UINTN                 *Count
  )
{
  CONST HAK_HDA_FIXUP  *fix;

  while (Id < Family->FixupCount) {
    if (++Depth > 10) {
      break;
    }

    fix = &Family->Fixups[Id];
    if ((fix->Flags & HAK_HDA_FIX_CHAINED_BEFORE) != 0) {
      HakQuirkChainWalk (Family, fix->Chain, Depth + 1, Order, Count);
    }

    if (*Count < HAK_HDA_CHAIN_MAX) {
      Order[(*Count)++] = (UINT16)Id;
    }

    if (((fix->Flags & HAK_HDA_FIX_CHAINED) == 0) || ((fix->Flags & HAK_HDA_FIX_CHAINED_BEFORE) != 0)) {
      break;
    }

    Id = fix->Chain;
  }
}

#endif
