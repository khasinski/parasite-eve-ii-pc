//! Names for the ids a save stores: items, key items, stages, areas, Parasite
//! Energy slots, game modes and named game flags.
//!
//! Sources in the decompilation (see ARCHITECTURE.md for the full list):
//! - item id ranges and categories: `include/gameplay/items.h` (`ITEM_DESCRIPTOR_COUNT`,
//!   `ITEM_KEY_DESCRIPTOR_COUNT`), `include/gameplay/inventory.h` (`ItemDesc::classification`)
//! - item names: the catalog behind `gItemDescriptors` / `gItemKeyDescriptors`
//!   (`src/gameplay/item_catalog.c`), see `item_names.rs`
//! - stages and areas: `include/main/areas.h`
//! - nibble flags: `include/main/event_flags.h`, `include/main/gameflag_ids.h`
//! - object slots: `src/mapui/map_shelter/map_shelter.c`, `src/mapui/map_neo_ark/map_neo_ark.c`

#[rustfmt::skip]
mod decomp_names;
#[rustfmt::skip]
mod item_names;

pub use decomp_names::{AREA_NAMES, NIBBLE_FLAGS, OBJECT_SLOTS};
pub use item_names::ITEM_NAMES;

/// What kind of thing an item id is, from the catalog's classification and id range.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ItemCategory {
    /// Recovery items (`ITEM_SUBTYPE_MEDICINE`): Recovery1..3, MP Boost, Cola, ...
    Consumable,
    /// Items that can be attached to armour for their effect (ids below 0x60).
    Attachment,
    /// Non-attachable tool items (Belt Pouch).
    Tool,
    /// Weapon add-ons: clip holders, snail magazine, M4A1 mounts.
    WeaponPart,
    /// Parasite Energy level entries 0x0F..0x32 (not carried as items).
    ParasiteEnergy,
    /// Armour, ids 0x60..0x7F.
    Armor,
    /// Weapons, ids 0x80..0x9F.
    Weapon,
    /// Ammunition, ids 0xA0..0xBF.
    Ammunition,
    /// Rechargeable built-in weapon supplies (batteries, fuel) in the 0xA0..0xBF range.
    BuiltInSupply,
    /// Key items, ids 0x100..0x17F, held as collection bits rather than rows.
    KeyItem,
}

impl ItemCategory {
    pub fn label(self) -> &'static str {
        match self {
            ItemCategory::Consumable => "Consumable",
            ItemCategory::Attachment => "Attachment",
            ItemCategory::Tool => "Tool",
            ItemCategory::WeaponPart => "Weapon add-on",
            ItemCategory::ParasiteEnergy => "Parasite Energy",
            ItemCategory::Armor => "Armor",
            ItemCategory::Weapon => "Weapon",
            ItemCategory::Ammunition => "Ammo",
            ItemCategory::BuiltInSupply => "Built-in supply",
            ItemCategory::KeyItem => "Key item",
        }
    }

    /// Categories that occupy inventory rows (everything but PE entries and key items).
    pub fn is_row_item(self) -> bool {
        !matches!(self, ItemCategory::ParasiteEnergy | ItemCategory::KeyItem)
    }

    /// Ammunition and supplies share one stack per id (`inventoryGiveItem`).
    pub fn stacks(self) -> bool {
        matches!(self, ItemCategory::Ammunition | ItemCategory::BuiltInSupply)
    }
}

/// Category implied by the id range alone, for ids the catalog leaves unnamed.
pub fn category_for_range(id: u16) -> Option<ItemCategory> {
    match id {
        0x01..=0x0E | 0x33..=0x5F => Some(ItemCategory::Attachment),
        0x0F..=0x32 => Some(ItemCategory::ParasiteEnergy),
        0x60..=0x7F => Some(ItemCategory::Armor),
        0x80..=0x9F => Some(ItemCategory::Weapon),
        0xA0..=0xBF => Some(ItemCategory::Ammunition),
        0x100..=0x17F => Some(ItemCategory::KeyItem),
        _ => None,
    }
}

/// Name and category of a catalogued item id.
pub fn item_info(id: u16) -> Option<(&'static str, ItemCategory)> {
    ITEM_NAMES
        .binary_search_by_key(&id, |(i, _, _)| *i)
        .ok()
        .map(|i| (ITEM_NAMES[i].1, ITEM_NAMES[i].2))
}

/// Name of an item id, if the catalog has one.
pub fn item_name(id: u16) -> Option<&'static str> {
    item_info(id).map(|(n, _)| n)
}

/// Display label: the name, or `unknown 0xNN` for ids the catalog does not name.
pub fn item_label(id: u16) -> String {
    match id {
        0 => "(empty)".to_string(),
        _ => match item_name(id) {
            Some(name) => name.to_string(),
            None => format!("unknown {id:#04x}"),
        },
    }
}

/// Category label for any id, falling back to the id range.
pub fn item_category(id: u16) -> Option<ItemCategory> {
    item_info(id)
        .map(|(_, c)| c)
        .or_else(|| category_for_range(id))
}

/// Catalogued items of the given categories, in id order.
pub fn items_of(categories: &[ItemCategory]) -> impl Iterator<Item = (u16, &'static str)> + '_ {
    ITEM_NAMES
        .iter()
        .filter(move |(_, _, c)| categories.contains(c))
        .map(|(i, n, _)| (*i, *n))
}

/// Every item that can occupy an inventory row, in id order.
pub fn row_items() -> impl Iterator<Item = (u16, &'static str, ItemCategory)> {
    ITEM_NAMES
        .iter()
        .filter(|(_, _, c)| c.is_row_item())
        .copied()
}

/// Key items (ids 0x100..0x17F), in id order.
pub fn key_items() -> impl Iterator<Item = (u16, &'static str)> {
    items_of(&[ItemCategory::KeyItem])
}

/// Parse an item reference: `0x81`, `129`, or a case-insensitive name.
pub fn parse_item(text: &str) -> Option<u16> {
    let t = text.trim();
    if let Some(hex) = t.strip_prefix("0x").or_else(|| t.strip_prefix("0X")) {
        return u16::from_str_radix(hex, 16).ok();
    }
    if let Ok(v) = t.parse::<u16>() {
        return Some(v);
    }
    let norm = |s: &str| {
        s.chars()
            .filter(|c| c.is_alphanumeric())
            .collect::<String>()
            .to_lowercase()
    };
    let want = norm(t);
    ITEM_NAMES
        .iter()
        .find(|(_, n, _)| norm(n) == want)
        .map(|(i, _, _)| *i)
}

/// Stage names (`GameLocationKey.stage`, `include/main/areas.h` `GAME_STAGE_*`).
pub const STAGE_NAMES: [&str; 6] = [
    "No stage",
    "Akropolis Tower / MIST",
    "Dryfield",
    "Dryfield (night)",
    "Mine / Shelter B1-B4",
    "Shelter 1F & B6 / Neo Ark",
];

pub fn stage_name(stage: u8) -> String {
    STAGE_NAMES
        .get(stage as usize)
        .map(|s| s.to_string())
        .unwrap_or_else(|| format!("stage {stage}"))
}

/// Area (room folder) name for a stage/area pair.
pub fn area_name(stage: u8, area: u8) -> Option<&'static str> {
    AREA_NAMES
        .iter()
        .find(|(s, a, _, _)| *s == stage && *a == area)
        .map(|(_, _, n, _)| *n)
}

/// Decoded `GameLocationKey` (`include/main/session_types.h`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Location {
    pub view: u8,
    pub room_state: u8,
    pub area: u8,
    pub stage: u8,
    pub warp: u8,
    pub variant: u8,
}

impl Location {
    pub fn from_key(key: [u8; 8]) -> Self {
        Self {
            view: key[0],
            room_state: key[1],
            area: key[2],
            stage: key[3],
            warp: key[4],
            variant: key[5],
        }
    }

    /// `Stage - Area` with raw ids for anything unnamed.
    pub fn label(&self) -> String {
        if self.stage == 0 {
            return "no stage".to_string();
        }
        let area = area_name(self.stage, self.area)
            .map(|s| s.to_string())
            .unwrap_or_else(|| format!("area {}", self.area));
        format!("{} - {}", stage_name(self.stage), area)
    }
}

/// Parasite Energy names for save slots 0..11 (`attachLevels[i]` levels catalog item
/// `0x0F + 3 * i`, src/gameplay/item_panels.c:1177). Slots 12..17 have no ability.
pub const PE_NAMES: [&str; 12] = [
    "Pyrokinesis",
    "Combustion",
    "Inferno",
    "Necrosis",
    "Plasma",
    "Apobiosis",
    "Metabolism",
    "Healing",
    "Lifedrain",
    "Antibody",
    "Energyshot",
    "Energyball",
];

pub fn pe_name(slot: usize) -> String {
    PE_NAMES
        .get(slot)
        .map(|s| s.to_string())
        .unwrap_or_else(|| format!("slot {slot} (unused)"))
}

/// `McSaveState::gameMode` (`include/main/mc_types.h:139`). Mode 0 is a normal
/// game until the first clear and a replay afterwards.
pub fn game_mode_label(mode: u8, clear_count: i8) -> &'static str {
    match mode {
        0 if clear_count > 0 => "Replay",
        0 => "Normal",
        1 => "Bounty",
        2 => "Scavenger",
        3 => "Nightmare",
        _ => "unknown mode",
    }
}

/// `McSaveState::visitFlags` bits (`include/main/mc_types.h:140`).
pub const VISIT_FLAG_LABELS: [&str; 6] = [
    "new-game setup done",
    "visited Akropolis / MIST",
    "visited Dryfield",
    "visited Dryfield (night)",
    "visited Mine / Shelter",
    "visited Shelter 1F / Neo Ark",
];

/// `EVENT_FLAG_STORY_CHAPTER` values (`include/main/event_flags.h:48`).
pub const STORY_CHAPTERS: [&str; 7] = [
    "MIST arrival",
    "Akropolis",
    "Dryfield",
    "Dryfield (night)",
    "End of Dryfield night",
    "Shelter",
    "Shelter 1F bulwark",
];

/// Named `EVENT_FLAG_CURRENT_OBJECTIVE` values (`include/main/event_flags.h:59`).
pub const OBJECTIVES: &[(u8, &str)] = &[
    (0x01, "Akropolis entered"),
    (0x02, "Akropolis: cafeteria key received"),
    (0x03, "Akropolis: cafeteria boss killed"),
    (0x04, "Akropolis: cafeteria boss examined"),
    (0x05, "MIST contacted"),
    (0x06, "Akropolis: sanctuary entered"),
    (0x07, "Akropolis: roof garden bomb seen"),
    (0x08, "Akropolis: No. 9 defeated"),
    (0x09, "Dryfield arrived"),
    (0x3A, "MIST shooting gallery start"),
    (0x3B, "MIST shooting gallery briefed"),
    (0x3C, "MIST returned"),
];

/// Nibble index of `EVENT_FLAG_STORY_CHAPTER`.
pub const STORY_CHAPTER_FLAG: u16 = 0x07A;
/// Nibble index of `EVENT_FLAG_CURRENT_OBJECTIVE` (a whole byte with index + 1).
pub const OBJECTIVE_FLAG: u16 = 0x0A2;

pub fn objective_label(value: u8) -> String {
    if value == 0 {
        return "none".to_string();
    }
    OBJECTIVES
        .iter()
        .find(|(v, _)| *v == value)
        .map(|(_, n)| n.to_string())
        .unwrap_or_else(|| format!("objective {value:#04x}"))
}

/// One named four-bit flag of the `GameFlagNibbleBank`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NamedFlag {
    /// Nibble index 0..503 (even: high half of byte index/2, odd: low half).
    pub index: u16,
    /// Constant name in the decompilation.
    pub name: &'static str,
    /// Header and line of the definition.
    pub source: &'static str,
    /// Summary of the decompilation's documentation.
    pub doc: &'static str,
}

pub fn nibble_flag(index: u16) -> Option<&'static NamedFlag> {
    NIBBLE_FLAGS
        .binary_search_by_key(&index, |f| f.index)
        .ok()
        .map(|i| &NIBBLE_FLAGS[i])
}

/// One named object-state slot of a stage bank.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ObjectSlot {
    pub stage: u8,
    pub slot: u8,
    pub name: &'static str,
    pub source: &'static str,
}

pub fn object_slot_name(stage: u8, slot: u8) -> Option<&'static str> {
    // Night Dryfield's object states live in the daytime bank.
    let stage = if stage == 3 { 2 } else { stage };
    OBJECT_SLOTS
        .iter()
        .find(|o| o.stage == stage && o.slot == slot)
        .map(|o| o.name)
}

/// Two-bit object states (`areaGetCurrentObjectState`, include/gameplay/items.h;
/// `MAP_SHELTER_STATE_*`, src/mapui/map_shelter/map_shelter.c:477).
pub const OBJECT_STATE_LABELS: [&str; 4] = ["inactive", "available", "taken", "replenishable"];

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tables_are_sorted_and_unique() {
        assert!(ITEM_NAMES.windows(2).all(|w| w[0].0 < w[1].0));
        assert!(NIBBLE_FLAGS.windows(2).all(|w| w[0].index < w[1].index));
        assert!(NIBBLE_FLAGS.iter().all(|f| f.index < 504));
        let mut names: Vec<_> = ITEM_NAMES.iter().map(|(_, n, _)| *n).collect();
        names.sort();
        let len = names.len();
        names.dedup();
        assert_eq!(len, names.len(), "item names must be unique for parsing");
    }

    #[test]
    fn categories_follow_id_ranges() {
        for (id, _, cat) in ITEM_NAMES {
            let range = category_for_range(*id).expect("catalogued id in a known range");
            match range {
                ItemCategory::Attachment => assert!(matches!(
                    cat,
                    ItemCategory::Attachment
                        | ItemCategory::Consumable
                        | ItemCategory::Tool
                        | ItemCategory::WeaponPart
                )),
                ItemCategory::Ammunition => assert!(matches!(
                    cat,
                    ItemCategory::Ammunition | ItemCategory::BuiltInSupply
                )),
                other => assert_eq!(*cat, other, "id {id:#x}"),
            }
        }
    }

    #[test]
    fn known_items() {
        assert_eq!(
            item_info(0x01),
            Some(("Recovery1", ItemCategory::Consumable))
        );
        assert_eq!(
            item_info(0x60),
            Some(("Leather Jacket", ItemCategory::Armor))
        );
        assert_eq!(item_info(0x81), Some(("M93R", ItemCategory::Weapon)));
        assert_eq!(
            item_info(0xA0),
            Some(("9mm P.B.", ItemCategory::Ammunition))
        );
        assert_eq!(
            item_info(0xA1),
            Some(("9mm Hydra", ItemCategory::Ammunition))
        );
        assert_eq!(item_info(0xB5).unwrap().1, ItemCategory::BuiltInSupply);
        assert_eq!(item_info(0x40).unwrap().0, "GPS");
        assert_eq!(item_info(0x92).unwrap().0, "Tonfa Baton");
        // Key items agree with the collection ids named in include/gameplay/items.h.
        assert_eq!(item_name(0x101), Some("Parthenon Key"));
        assert_eq!(item_name(0x106), Some("MIST Badge"));
        assert_eq!(item_name(0x111), Some("Monkey Wrench"));
        assert_eq!(item_name(0x119), Some("Ice Bag"));
        assert_eq!(item_name(0x121), Some("Bowman's Card"));
        assert_eq!(item_category(0x105), Some(ItemCategory::KeyItem));
        assert_eq!(item_label(0x33), "unknown 0x33");
        assert_eq!(item_label(0), "(empty)");
        assert_eq!(
            item_info(0x10),
            Some(("Pyrokinesis Lv2", ItemCategory::ParasiteEnergy))
        );
    }

    #[test]
    fn parse_item_by_name_or_id() {
        assert_eq!(parse_item("m93r"), Some(0x81));
        assert_eq!(parse_item("9mm hydra"), Some(0xA1));
        assert_eq!(parse_item("9MM P.B."), Some(0xA0));
        assert_eq!(parse_item("bowmans card"), Some(0x121));
        assert_eq!(parse_item("0x81"), Some(0x81));
        assert_eq!(parse_item("129"), Some(0x81));
        assert_eq!(parse_item("nonsense"), None);
    }

    #[test]
    fn locations_and_flags() {
        let loc = Location::from_key([6, 1, 1, 1, 1, 1, 0, 0]);
        assert_eq!(loc.label(), "Akropolis Tower / MIST - Akropolis Square");
        assert_eq!(area_name(2, 15), Some("Dryfield Parking Lot"));
        assert_eq!(area_name(5, 33), Some("Neo Ark Substation"));
        assert_eq!(
            area_name(4, 41),
            Some("Shelter B3 Incinerator Control Room")
        );
        assert_eq!(nibble_flag(0x07A).unwrap().name, "EVENT_FLAG_STORY_CHAPTER");
        assert_eq!(
            nibble_flag(0x02E).unwrap().name,
            "GAME_FLAG_MOTEL_ROOM_6_DOOR_UNLOCKED"
        );
        assert!(nibble_flag(0x068).is_none(), "play-time mark is not a flag");
        assert_eq!(
            object_slot_name(4, 0x1),
            Some("Mine Forked Tunnel Oak Board")
        );
        assert_eq!(object_slot_name(5, 46), Some("Tent Save Point"));
        assert_eq!(game_mode_label(0, 0), "Normal");
        assert_eq!(game_mode_label(0, 1), "Replay");
        assert_eq!(game_mode_label(3, 1), "Nightmare");
        assert_eq!(pe_name(0), "Pyrokinesis");
        assert_eq!(pe_name(11), "Energyball");
    }
}
