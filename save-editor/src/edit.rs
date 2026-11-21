//! Save editing operations and full-file checksum maintenance.
//!
//! Mutating operations act on [`SaveImage`]; `refresh_checksums` then updates
//! the whole slot file the way the game's save walk does: section checksums
//! (live copies only), the summary pair, the header checksum and the card
//! file header sum. Loading in-game only reads live copies, so backups are
//! deliberately left untouched.

use anyhow::{bail, Result};

use crate::checksum::{
    verify_save_header_checksum, verify_section_checksum, write_all_section_checksums,
    write_card_header_checksum, write_save_header_checksum, write_section_checksum_summary,
};
use crate::layout::{
    SectionLayout, AKROPOLIS_SECTION, CHECKSUM_SECTIONS, DRYFIELD_NIGHT_SECTION, DRYFIELD_SECTION,
    MINE_SHELTER_SECTION, NEO_ARK_SECTION, NIBBLE_SECTION, PLAYER_SECTION, SAVE_SECTION,
};
use crate::save::{SaveImage, SAVE_IMAGE_SIZE};

/// `PlayerStatus` byte offsets (include/main/wipsys_types.h:91..94).
pub const PLAYER_OFF_WEAPON: usize = 0x21;
pub const PLAYER_OFF_WEAPON_SLOT_ITEM: usize = 0x22;
pub const PLAYER_OFF_ARMOR: usize = 0x23;
/// Four-bit positions in the nibble bank (`GAME_FLAG_NIBBLE_COUNT`).
pub const NIBBLE_FLAG_COUNT: usize = 504;
/// `GameFlagNibbleBank::payload.state.playTimeMark` (static assert at 0x38).
pub const NIBBLE_OFF_PLAY_TIME_MARK: usize = 0x38;
/// Nibble positions shared with the play-time mark (`EVENT_FLAG_RESERVED_PLAY_TIME_*`).
pub const NIBBLE_PLAY_TIME_POSITIONS: std::ops::RangeInclusive<usize> = 0x68..=0x6B;
/// Ice Bag collection bit (`INVENTORY_COLLECTED_ICE_BAG_BIT`, item 0x119).
const ICE_BAG_BIT: u16 = 0x19;

/// Whether a key-item id (or bit index) is the Ice Bag, whose timer restarts when given.
pub fn is_ice_bag(id: u16) -> bool {
    (id & 0x7F) == ICE_BAG_BIT
}

/// Decoded live `PlayerStatus` fields the editor names.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PlayerRecord {
    pub exp: i32,
    pub bp: i32,
    pub hp: i16,
    pub hp_max: i16,
    pub mp: i16,
    pub mp_max: i16,
    /// Equipped weapon item id (0x80..0x9F), `None` when unarmed.
    pub weapon: Option<u8>,
    /// Consumable selected in the weapon's primary slot (0xA0..0xBF).
    pub weapon_slot_item: Option<u8>,
    /// Equipped armour item id (0x60..0x7F).
    pub armor: Option<u8>,
    /// `PLAYER_STATUS_*` timed effects (poison, paralysis, ...).
    pub status_flags: u8,
}

/// Section holding stage `stage`'s flag bank.
pub fn stage_section(stage: u8) -> Option<SectionLayout> {
    Some(match stage {
        1 => AKROPOLIS_SECTION,
        2 => DRYFIELD_SECTION,
        3 => DRYFIELD_NIGHT_SECTION,
        4 => MINE_SHELTER_SECTION,
        5 => NEO_ARK_SECTION,
        _ => return None,
    })
}

/// Placement-record count of each stage bank (`GameFlag*Bank::areas`).
pub fn stage_area_slots(stage: u8) -> usize {
    match stage {
        1 => 18,
        2 => 36,
        4 => 48,
        5 => 32,
        _ => 0,
    }
}

/// Read-only view of one live stage bank (`GameFlagStageHeader` + area records).
pub struct StageBank<'a> {
    pub stage: u8,
    pub bytes: &'a [u8],
}

impl StageBank<'_> {
    fn u32_at(&self, off: usize) -> u32 {
        u32::from_le_bytes(self.bytes[off..off + 4].try_into().unwrap())
    }
    /// `visitedAreas`: bit `area - 1` (1..64).
    pub fn area_visited(&self, area: u8) -> bool {
        if !(1..=64).contains(&area) {
            return false;
        }
        let bit = (area - 1) as usize;
        self.u32_at(4 + (bit / 32) * 4) & (1 << (bit % 32)) != 0
    }
    /// `objectStates`: sixteen two-bit states per word, low pair first (0..63).
    pub fn object_state(&self, slot: usize) -> u8 {
        assert!(slot < 64);
        ((self.u32_at(0x0C + (slot / 16) * 4) >> ((slot % 16) * 2)) & 3) as u8
    }
    /// `(variant, spawnFlags)` of placement record `slot` (`AreaSavedState`).
    pub fn area_slot(&self, slot: usize) -> Option<(i8, u8)> {
        let off = 0x20 + slot * 4;
        if slot >= stage_area_slots(self.stage) || off + 2 > self.bytes.len() {
            return None;
        }
        Some((self.bytes[off] as i8, self.bytes[off + 1]))
    }
}

/// A whole slot file (0x2000 bytes): card header + all sections.
#[derive(Debug, Clone)]
pub struct SlotData {
    pub data: Vec<u8>,
}

impl SlotData {
    pub fn from_bytes(data: Vec<u8>) -> Result<Self> {
        if data.len() != crate::layout::FILE_SIZE {
            bail!(
                "slot file must be {} bytes, got {}",
                crate::layout::FILE_SIZE,
                data.len()
            );
        }
        Ok(Self { data })
    }

    /// Typed view of the live save image.
    pub fn save(&self) -> Result<SaveImage> {
        SaveImage::from_bytes(
            self.data[SAVE_SECTION.live_offset..SAVE_SECTION.live_offset + SAVE_IMAGE_SIZE]
                .to_vec(),
        )
    }

    /// Mutable typed view source: the live image bytes.
    fn image_mut(&mut self) -> &mut [u8] {
        &mut self.data[SAVE_SECTION.live_offset..SAVE_SECTION.live_offset + SAVE_IMAGE_SIZE]
    }

    /// Run an edit closure against the live save image, then refresh checksums.
    pub fn edit(&mut self, f: impl FnOnce(&mut SaveImage)) -> Result<()> {
        let mut image = self.save()?;
        let old_exp = image.player_exp();
        let old_bp = image.player_bp();
        f(&mut image);
        // McSaveData carries the menu preview. Gameplay restores EXP and BP
        // from the separate PlayerStatus record, so edits must update both.
        // Only propagate changed fields: unrelated edits must preserve the
        // live player record even when the preview was already stale.
        for (old, new, offset) in [
            (old_exp, image.player_exp(), 0x08),
            (old_bp, image.player_bp(), 0x0c),
        ] {
            if old != new {
                let start = PLAYER_SECTION.live_offset + offset;
                self.data[start..start + 4].copy_from_slice(&new.to_le_bytes());
            }
        }
        let bytes = image.raw;
        self.image_mut().copy_from_slice(&bytes);
        self.refresh_checksums();
        Ok(())
    }

    /// Recompute all checksum pairs the way the game's save walk writes them.
    ///
    /// Order matters: the header, title and (zeroed) buffer checksum pairs sit
    /// inside the save section's checksummed payload, so they are written
    /// before the section checksums; the summary is written last.
    pub fn refresh_checksums(&mut self) {
        let mut image = std::mem::take(&mut self.data);
        let header: Vec<u8> = image[..SAVE_SECTION.live_offset].to_vec();
        write_card_header_checksum(&header, &mut image[SAVE_SECTION.live_offset..]);
        write_save_header_checksum(&mut image[SAVE_SECTION.live_offset..]);
        // Zero the buffer-checksum pair before section sums cover it.
        const BUFFER_CHECKSUM_OFFSET: usize = 0x940;
        let live = SAVE_SECTION.live_offset;
        image[live + BUFFER_CHECKSUM_OFFSET..live + BUFFER_CHECKSUM_OFFSET + 2]
            .copy_from_slice(&0u16.to_le_bytes());
        image[live + BUFFER_CHECKSUM_OFFSET + 2..live + BUFFER_CHECKSUM_OFFSET + 4]
            .copy_from_slice(&crate::checksum::CHECKSUM_MASK.to_le_bytes());
        write_all_section_checksums(&mut image);
        write_section_checksum_summary(&mut image);
        self.data = image;
    }

    // -- player record (`PlayerStatus`, include/main/wipsys_types.h:76) ------------

    /// Typed view of the live `PlayerStatus` record.
    pub fn player(&self) -> PlayerRecord {
        let p = &self.data[PLAYER_SECTION.live_offset..PLAYER_SECTION.live_offset + 0x40];
        let s16 = |o: usize| i16::from_le_bytes([p[o], p[o + 1]]);
        let item = |raw: u8, base: u8| {
            if raw == 0 {
                None
            } else {
                Some(raw.wrapping_add(base))
            }
        };
        PlayerRecord {
            exp: i32::from_le_bytes(p[0x08..0x0C].try_into().unwrap()),
            bp: i32::from_le_bytes(p[0x0C..0x10].try_into().unwrap()),
            hp: s16(0x18),
            hp_max: s16(0x1A),
            mp: s16(0x1C),
            mp_max: s16(0x1E),
            weapon: item(p[PLAYER_OFF_WEAPON], 0x7F),
            weapon_slot_item: item(p[PLAYER_OFF_WEAPON_SLOT_ITEM], 0x9F),
            armor: item(p[PLAYER_OFF_ARMOR], 0x5F),
            status_flags: p[0x25],
        }
    }

    /// Select the equipped weapon (0x80..0x9F, or `None` for unarmed) and its
    /// selected consumable (0xA0..0xBF or `None`), then refresh checksums.
    ///
    /// The game stores `weapon` as id - 0x7F and `weaponSlotItem` as id - 0x9F.
    pub fn set_equipped_weapon(&mut self, weapon: Option<u8>, slot_item: Option<u8>) -> Result<()> {
        let w = match weapon {
            None => 0,
            Some(id @ 0x80..=0x9F) => id - 0x7F,
            Some(id) => bail!("{id:#04x} is not a weapon id (0x80..0x9F)"),
        };
        let a = match slot_item {
            None => 0,
            Some(id @ 0xA0..=0xBF) => id - 0x9F,
            Some(id) => bail!("{id:#04x} is not an ammunition id (0xA0..0xBF)"),
        };
        let base = PLAYER_SECTION.live_offset;
        self.data[base + PLAYER_OFF_WEAPON] = w;
        self.data[base + PLAYER_OFF_WEAPON_SLOT_ITEM] = a;
        self.refresh_checksums();
        Ok(())
    }

    // -- nibble flag bank (`GameFlagNibbleBank`, include/main/gameflag_types.h:34) --

    /// Four-bit flag `index` (0..503): high half of byte index/2 for even indices.
    pub fn nibble(&self, index: usize) -> u8 {
        assert!(index < NIBBLE_FLAG_COUNT);
        let byte = self.data[NIBBLE_SECTION.live_offset + 4 + index / 2];
        if index.is_multiple_of(2) {
            byte >> 4
        } else {
            byte & 0x0F
        }
    }

    /// Set one four-bit flag, preserving its neighbour, then refresh checksums.
    pub fn set_nibble(&mut self, index: usize, value: u8) {
        assert!(index < NIBBLE_FLAG_COUNT);
        let off = NIBBLE_SECTION.live_offset + 4 + index / 2;
        let byte = self.data[off];
        self.data[off] = if index.is_multiple_of(2) {
            (byte & 0x0F) | ((value & 0x0F) << 4)
        } else {
            (byte & 0xF0) | (value & 0x0F)
        };
        self.refresh_checksums();
    }

    /// Whole byte of two nibble positions (`gameFlagGetPackedByte`).
    pub fn nibble_byte(&self, index: usize) -> u8 {
        self.data[NIBBLE_SECTION.live_offset + 4 + index / 2]
    }

    /// Play-time mark (minutes) sharing nibble positions 104..107.
    pub fn play_time_mark(&self) -> u16 {
        let off = NIBBLE_SECTION.live_offset + NIBBLE_OFF_PLAY_TIME_MARK;
        u16::from_le_bytes([self.data[off], self.data[off + 1]])
    }

    /// Give or take a key item. Giving the Ice Bag also restarts its melting
    /// timer from the save's play time, as `inventorySetCollectedBit` does
    /// (src/gameplay/item_boost.c:819). Checksums are refreshed.
    pub fn set_key_item(&mut self, id: u16, held: bool) -> Result<()> {
        if !(0x100..=0x17F).contains(&id) {
            bail!("{id:#x} is not a key item id (0x100..0x17F)");
        }
        self.edit(|save| save.set_key_item(id, held))?;
        if held && is_ice_bag(id) {
            self.restart_ice_bag_timer()?;
        }
        Ok(())
    }

    /// Store the save's play time as the Ice Bag's melting-timer start
    /// (`inventoryResetIceBagTimer`), then refresh checksums.
    pub fn restart_ice_bag_timer(&mut self) -> Result<()> {
        let play_time = self.save()?.play_time_minutes();
        let off = NIBBLE_SECTION.live_offset + NIBBLE_OFF_PLAY_TIME_MARK;
        self.data[off..off + 2].copy_from_slice(&play_time.to_le_bytes());
        self.refresh_checksums();
        Ok(())
    }

    // -- per-stage flag banks (include/main/gameflag_types.h:57..228) --------------

    /// Typed view over stage `stage`'s live bank (1..5).
    pub fn stage_bank(&self, stage: u8) -> Option<StageBank<'_>> {
        let section = stage_section(stage)?;
        Some(StageBank {
            stage,
            bytes: &self.data[section.live_offset..section.live_offset + section.record_bytes],
        })
    }

    /// Set one two-bit object state of a stage bank, then refresh checksums.
    pub fn set_object_state(&mut self, stage: u8, slot: usize, state: u8) -> Result<()> {
        let section = stage_section(stage).ok_or_else(|| anyhow::anyhow!("no stage {stage}"))?;
        if slot >= 64 {
            bail!("object slot {slot} out of range (0..63)");
        }
        let off = section.live_offset + 0x0C + (slot / 16) * 4;
        let mut word = u32::from_le_bytes(self.data[off..off + 4].try_into().unwrap());
        let shift = (slot % 16) * 2;
        word = (word & !(3 << shift)) | (((state & 3) as u32) << shift);
        self.data[off..off + 4].copy_from_slice(&word.to_le_bytes());
        self.refresh_checksums();
        Ok(())
    }

    /// Mark an area visited (or not) in its stage bank, then refresh checksums.
    pub fn set_area_visited(&mut self, stage: u8, area: u8, visited: bool) -> Result<()> {
        let section = stage_section(stage).ok_or_else(|| anyhow::anyhow!("no stage {stage}"))?;
        if !(1..=64).contains(&area) {
            bail!("area {area} out of range (1..64)");
        }
        let bit = (area - 1) as usize;
        let off = section.live_offset + 4 + (bit / 32) * 4;
        let mut word = u32::from_le_bytes(self.data[off..off + 4].try_into().unwrap());
        if visited {
            word |= 1 << (bit % 32);
        } else {
            word &= !(1 << (bit % 32));
        }
        self.data[off..off + 4].copy_from_slice(&word.to_le_bytes());
        self.refresh_checksums();
        Ok(())
    }

    /// Verify every live section checksum and the summary pair.
    ///
    /// Returns `(section_results, header_ok, summary_ok)` in section order.
    pub fn verify(&self) -> (Vec<bool>, bool, bool) {
        let mut sections = Vec::new();
        for section in CHECKSUM_SECTIONS {
            let record =
                &self.data[section.live_offset..section.live_offset + section.record_bytes];
            sections.push(verify_section_checksum(record));
        }
        let image =
            &self.data[SAVE_SECTION.live_offset..SAVE_SECTION.live_offset + SAVE_IMAGE_SIZE];
        let header_ok = verify_save_header_checksum(image);
        let mut sum: u16 = 0;
        for section in CHECKSUM_SECTIONS {
            sum = sum.wrapping_add(self.data[section.live_offset] as u16);
        }
        let stored = u16::from_le_bytes(
            self.data[SAVE_SECTION.live_offset + 0x940..SAVE_SECTION.live_offset + 0x942]
                .try_into()
                .unwrap(),
        );
        let summary_ok = stored == sum;
        (sections, header_ok, summary_ok)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::save::ItemRow;

    /// Slot file shaped like the game writes it: 'SC' header section, zeroed
    /// sections. Checksums start invalid until refreshed.
    fn synthetic_slot() -> SlotData {
        let mut data = vec![0u8; crate::layout::FILE_SIZE];
        data[0] = b'S';
        data[1] = b'C';
        SlotData::from_bytes(data).unwrap()
    }

    #[test]
    fn edit_updates_checksums_and_verify_passes() {
        let mut slot = synthetic_slot();
        slot.edit(|save| {
            save.set_player_exp(4242);
            save.set_item_row(
                0,
                ItemRow {
                    item_id: 0x85,
                    attach_slot: 0,
                    qty: 3,
                },
            );
        })
        .unwrap();
        let (sections, header_ok, summary_ok) = slot.verify();
        assert!(
            sections.iter().all(|ok| *ok),
            "all live section checksums must verify"
        );
        assert!(header_ok);
        assert!(summary_ok);
        let save = slot.save().unwrap();
        assert_eq!(save.player_exp(), 4242);
        assert_eq!(save.item_row(0).item_id, 0x85);
    }

    #[test]
    fn unmodeled_bytes_survive_edit() {
        let mut slot = synthetic_slot();
        // Plant a byte in a region the editor does not model (enemy poses).
        slot.data[SAVE_SECTION.live_offset + 0x30] = 0x5A;
        slot.edit(|save| save.set_player_bp(99)).unwrap();
        assert_eq!(slot.data[SAVE_SECTION.live_offset + 0x30], 0x5A);
        assert_eq!(slot.save().unwrap().player_bp(), 99);
    }

    fn all_ok(slot: &SlotData) -> bool {
        let (sections, header, summary) = slot.verify();
        sections.iter().all(|ok| *ok) && header && summary
    }

    #[test]
    fn player_record_decodes_equipment() {
        let mut slot = synthetic_slot();
        let base = PLAYER_SECTION.live_offset;
        // Bytes as found in a real Akropolis save: M93R, 9mm P.B., Leather Jacket.
        slot.data[base + 0x18..base + 0x20].copy_from_slice(&[0x6e, 0, 0x6e, 0, 0x1e, 0, 0x1e, 0]);
        slot.data[base + 0x21..base + 0x24].copy_from_slice(&[0x02, 0x01, 0x01]);
        let p = slot.player();
        assert_eq!((p.hp, p.hp_max, p.mp, p.mp_max), (110, 110, 30, 30));
        assert_eq!(p.weapon, Some(0x81));
        assert_eq!(p.weapon_slot_item, Some(0xA0));
        assert_eq!(p.armor, Some(0x60));
        slot.data[base + 0x30] = 0x77; // unmodeled player byte
        slot.set_equipped_weapon(Some(0x8D), Some(0xAC)).unwrap();
        let p = slot.player();
        assert_eq!((p.weapon, p.weapon_slot_item), (Some(0x8D), Some(0xAC)));
        assert_eq!(slot.data[base + 0x30], 0x77);
        assert!(all_ok(&slot));
        assert!(slot.set_equipped_weapon(Some(0x60), None).is_err());
        slot.set_equipped_weapon(None, None).unwrap();
        assert_eq!(slot.player().weapon, None);
    }

    #[test]
    fn nibble_flags_pack_high_then_low() {
        let mut slot = synthetic_slot();
        slot.set_nibble(0x7A, 5);
        slot.set_nibble(0x7B, 0xC);
        assert_eq!(slot.nibble_byte(0x7A), 0x5C);
        assert_eq!((slot.nibble(0x7A), slot.nibble(0x7B)), (5, 0xC));
        slot.set_nibble(0x7A, 0);
        assert_eq!(slot.nibble(0x7B), 0xC, "neighbour nibble preserved");
        assert!(all_ok(&slot));
    }

    #[test]
    fn key_items_toggle_collection_bits_and_ice_bag_timer() {
        let mut slot = synthetic_slot();
        slot.edit(|s| s.set_play_time_minutes(321)).unwrap();
        slot.set_key_item(0x111, true).unwrap(); // Monkey Wrench
        assert!(slot.save().unwrap().has_key_item(0x111));
        assert!(slot.save().unwrap().collected_bit(0x11));
        assert_eq!(
            slot.play_time_mark(),
            0,
            "only the Ice Bag restarts the timer"
        );
        slot.set_key_item(0x119, true).unwrap();
        assert_eq!(slot.play_time_mark(), 321);
        slot.set_key_item(0x111, false).unwrap();
        assert!(!slot.save().unwrap().has_key_item(0x111));
        assert!(slot.set_key_item(0x81, true).is_err());
        assert!(all_ok(&slot));
    }

    #[test]
    fn stage_banks_decode_and_edit() {
        let mut slot = synthetic_slot();
        let base = crate::layout::AKROPOLIS_SECTION.live_offset;
        // Visited Square (1) and Plaza (5); object slot 1 = 3 (replenishable).
        slot.data[base + 4] = 0x11;
        slot.data[base + 0x0C] = 0b1100;
        let bank = slot.stage_bank(1).unwrap();
        assert!(bank.area_visited(1) && bank.area_visited(5) && !bank.area_visited(2));
        assert_eq!(bank.object_state(1), 3);
        assert_eq!(bank.object_state(0), 0);
        slot.set_object_state(1, 40, 2).unwrap();
        slot.set_area_visited(1, 33, true).unwrap();
        let bank = slot.stage_bank(1).unwrap();
        assert_eq!(bank.object_state(40), 2);
        assert_eq!(bank.object_state(1), 3);
        assert!(bank.area_visited(33));
        assert!(slot.stage_bank(6).is_none());
        assert!(all_ok(&slot));
    }

    #[test]
    fn verify_detects_corruption() {
        let mut slot = synthetic_slot();
        slot.edit(|save| save.set_player_exp(1)).unwrap();
        // Corrupt a payload byte.
        slot.data[SAVE_SECTION.live_offset + 0x14] ^= 0xFF;
        let (sections, _, _) = slot.verify();
        assert!(
            !sections[0],
            "save section must fail verification after corruption"
        );
    }
    #[test]
    fn currency_edit_updates_gameplay_record_and_preserves_unrelated_bytes() {
        let mut slot = synthetic_slot();
        let base = PLAYER_SECTION.live_offset;
        slot.data[base + 0x10] = 0x5a;
        slot.data[PLAYER_SECTION.backup_offset() + 0x0c] = 0x42;
        slot.edit(|s| {
            s.set_player_exp(777);
            s.set_player_bp(4321);
        })
        .unwrap();
        assert_eq!(&slot.data[base + 8..base + 12], &777i32.to_le_bytes());
        assert_eq!(&slot.data[base + 12..base + 16], &4321i32.to_le_bytes());
        assert_eq!(slot.data[base + 0x10], 0x5a);
        assert_eq!(slot.data[PLAYER_SECTION.backup_offset() + 0x0c], 0x42);
        // A stale preview must not overwrite actual gameplay BP on another edit.
        slot.data[base + 12..base + 16].copy_from_slice(&987i32.to_le_bytes());
        slot.edit(|s| s.set_player_exp(778)).unwrap();
        assert_eq!(&slot.data[base + 12..base + 16], &987i32.to_le_bytes());
        assert!(slot.verify().0.iter().all(|ok| *ok));
    }
}
