//! egui GUI: slot browser on the left, tabbed editor on the right.

use anyhow::Result;
use eframe::egui;

use crate::card::CardContainer;
use crate::edit::SlotData;
use crate::names::{self, ItemCategory, Location};
use crate::save::{
    ItemRow, SaveImage, WeaponLoad, COLLECTED_BIT_WORDS, ITEM_ROW_COUNT, PE_LEVEL_COUNT,
    PE_USE_COUNT, WEAPON_LOAD_COUNT,
};

/// Application state.
pub struct EditorApp {
    container: Option<CardContainer>,
    container_path: String,
    status: String,
    active_slot: Option<usize>,
    /// Working copy of the active slot; edits land here until "Save changes to card".
    working: Option<SlotData>,
    tab: Tab,
    ui_state: UiState,
}

#[derive(PartialEq, Clone, Copy)]
enum Tab {
    Summary,
    Character,
    Inventory,
    KeyItems,
    Weapons,
    ParasiteEnergy,
    Progression,
    Flags,
    Options,
}

impl EditorApp {
    pub fn new() -> Self {
        Self {
            container: None,
            container_path: String::new(),
            status: "Open a card container (directory, raw slot, .mcs, .srm/.mcr/.mcd/.gme)".into(),
            active_slot: None,
            working: None,
            tab: Tab::Summary,
            ui_state: UiState::default(),
        }
    }

    fn load_container(&mut self, path: &str) {
        match CardContainer::open(path) {
            Ok(container) => {
                let count = container.slots().len();
                self.container = Some(container);
                self.container_path = path.to_string();
                self.active_slot = None;
                self.working = None;
                self.status = format!(
                    "Loaded {path} ({} PE2 slot{})",
                    count,
                    if count == 1 { "" } else { "s" }
                );
            }
            Err(e) => self.status = format!("Error: {e}"),
        }
    }

    fn select_slot(&mut self, index: usize) {
        let Some(container) = &self.container else {
            return;
        };
        match container.slots().into_iter().find(|(i, _)| *i == index) {
            Some((_, file)) => match SlotData::from_bytes(file.data.clone()) {
                Ok(data) => {
                    self.active_slot = Some(index);
                    self.working = Some(data);
                    self.status = format!("Editing slot {index}");
                }
                Err(e) => self.status = format!("Error: {e}"),
            },
            None => self.status = format!("Slot {index} not found"),
        }
    }

    fn commit(&mut self) {
        let Some(container) = self.container.as_mut() else {
            return;
        };
        let Some(index) = self.active_slot else {
            return;
        };
        let Some(data) = self.working.as_ref() else {
            return;
        };
        match container
            .write_slot(index, &data.data)
            .and_then(|()| container.save_to_path(&self.container_path))
        {
            Ok(()) => self.status = format!("Saved slot {index} to {}", self.container_path),
            Err(e) => self.status = format!("Error: {e}"),
        }
    }
}

impl eframe::App for EditorApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.top_panel(ctx);
        self.side_panel(ctx);
        self.central_panel(ctx);
    }
}

impl Default for EditorApp {
    fn default() -> Self {
        Self::new()
    }
}

impl EditorApp {
    fn top_panel(&mut self, ctx: &egui::Context) {
        egui::TopBottomPanel::top("top").show(ctx, |ui| {
            ui.horizontal(|ui| {
                if ui.button("Open card…").clicked() {
                    if let Some(path) = rfd::FileDialog::new()
                        .add_filter("Card containers", &["srm", "mcr", "mcd", "gme", "mcs"])
                        .add_filter("All files", &["*"])
                        .pick_file()
                    {
                        self.load_container(&path.to_string_lossy());
                    }
                }
                ui.separator();
                if ui.button("Open directory…").clicked() {
                    if let Some(path) = rfd::FileDialog::new().pick_folder() {
                        self.load_container(&path.to_string_lossy());
                    }
                }
                ui.separator();
                let can_save = self.active_slot.is_some()
                    && self.working.is_some()
                    && self.container.is_some();
                let save_button = egui::Button::new(
                    egui::RichText::new("Save changes to card")
                        .strong()
                        .color(egui::Color32::WHITE),
                )
                .fill(egui::Color32::from_rgb(26, 86, 160))
                .stroke(egui::Stroke::new(
                    1.5_f32,
                    egui::Color32::from_rgb(125, 195, 255),
                ))
                .min_size(egui::vec2(170.0, 32.0));
                if ui
                    .add_enabled(can_save, save_button)
                    .on_hover_text(
                        "Write changes in the selected slot to the open card or directory.",
                    )
                    .on_disabled_hover_text(
                        "Open a card or directory and select a slot to save changes.",
                    )
                    .clicked()
                {
                    self.commit();
                }
                if let Some(index) = self.active_slot {
                    ui.label(format!("slot {index}"));
                }
            });
        });
    }

    fn side_panel(&mut self, ctx: &egui::Context) {
        egui::SidePanel::left("slots").show(ctx, |ui| {
            ui.heading("Slots");
            ui.separator();
            let Some(container) = &self.container else {
                ui.label(&self.status);
                return;
            };
            let slots: Vec<(usize, String, String)> = container
                .slots()
                .into_iter()
                .map(|(i, f)| {
                    let summary = SlotData::from_bytes(f.data.clone())
                        .and_then(|d| d.save())
                        .map(|s| crate::cli::describe(&s))
                        .unwrap_or_else(|e| format!("unreadable: {e}"));
                    (i, f.filename.clone(), summary)
                })
                .collect();
            for (index, filename, summary) in slots {
                let selected = self.active_slot == Some(index);
                if ui
                    .selectable_label(selected, format!("[{index}] {filename}"))
                    .clicked()
                {
                    self.select_slot(index);
                }
                ui.label(egui::RichText::new(format!("    {summary}")).small().weak());
            }
        });
    }

    fn central_panel(&mut self, ctx: &egui::Context) {
        egui::CentralPanel::default().show(ctx, |ui| {
            if self.working.is_none() {
                ui.label("Select a slot to edit.");
                return;
            }
            let tab = self.tab;
            let mut ui_state = std::mem::take(&mut self.ui_state);
            let data = self.working.as_mut().unwrap();
            let Ok(mut save) = data.save() else {
                ui.label("Save image unreadable.");
                return;
            };
            ui.horizontal_wrapped(|ui| {
                for (page, label) in [
                    (Tab::Summary, "Summary"),
                    (Tab::Character, "Character"),
                    (Tab::Inventory, "Inventory"),
                    (Tab::KeyItems, "Key Items"),
                    (Tab::Weapons, "Weapons"),
                    (Tab::ParasiteEnergy, "Parasite Energy"),
                    (Tab::Progression, "Progression"),
                    (Tab::Flags, "Game Flags"),
                    (Tab::Options, "Options"),
                ] {
                    ui.selectable_value(&mut self.tab, page, label);
                }
            });
            ui.separator();
            // Tabs edit the save image copy (`save`) and report `changed`; tabs that
            // touch other sections (player record, flag banks) call `SlotData`
            // methods directly, which refresh checksums themselves and never touch
            // the save section.
            let mut changed = false;
            let mut after: Vec<SlotAction> = Vec::new();
            egui::ScrollArea::vertical().show(ui, |ui| {
                changed |= match tab {
                    Tab::Summary => show_summary(ui, &save, data),
                    Tab::Character => show_character(ui, &mut save),
                    Tab::Inventory => show_inventory(ui, &mut save, &mut ui_state),
                    Tab::KeyItems => show_key_items(ui, &mut save, &mut after),
                    Tab::Weapons => show_weapons(ui, &mut save, data, &mut ui_state),
                    Tab::ParasiteEnergy => show_pe(ui, &mut save),
                    Tab::Progression => show_progression(ui, &mut save, data),
                    Tab::Flags => show_flags(ui, data, &mut ui_state),
                    Tab::Options => show_options(ui, &mut save),
                };
            });
            self.ui_state = ui_state;
            if changed {
                // `edit` also mirrors changed EXP/BP into the gameplay record.
                let bytes = save.raw.clone();
                if let Err(e) = data.edit(|image| image.raw.copy_from_slice(&bytes)) {
                    self.status = format!("Error: {e}");
                }
            }
            for action in after {
                let result = match action {
                    SlotAction::RestartIceBagTimer => data.restart_ice_bag_timer(),
                };
                if let Err(e) = result {
                    self.status = format!("Error: {e}");
                }
            }
            ui.separator();
            ui.label(&self.status);
        });
    }
}

/// Follow-up edits outside the save section, run after the save image is written back.
enum SlotAction {
    RestartIceBagTimer,
}

/// Transient widget state that is not part of the save.
#[derive(Default)]
struct UiState {
    add_item: u8,
    add_qty: u16,
    show_all_weapons: bool,
    flag_filter: String,
    show_unnamed_flags: bool,
}

fn opt_item_label(id: Option<u8>) -> String {
    id.map(|i| names::item_label(i as u16))
        .unwrap_or_else(|| "none".to_string())
}

fn show_summary(ui: &mut egui::Ui, save: &SaveImage, data: &SlotData) -> bool {
    ui.heading("Save summary");
    let loc = Location::from_key(save.location());
    egui::Grid::new("summary").num_columns(2).show(ui, |ui| {
        ui.label("Location");
        ui.label(loc.label()).on_hover_text(format!(
            "stage {} area {} room state {} view {} warp {} variant {}",
            loc.stage, loc.area, loc.room_state, loc.view, loc.warp, loc.variant
        ));
        ui.end_row();
        ui.label("Save point");
        ui.label(format!(
            "{} (#{}), save label #{}",
            save.save_point_label(),
            save.save_point(),
            save.save_number()
        ));
        ui.end_row();
        let minutes = save.play_time_minutes();
        ui.label("Play time");
        ui.label(format!(
            "{}:{:02} ({} minutes)",
            minutes / 60,
            minutes % 60,
            minutes
        ));
        ui.end_row();
        ui.label("Mode");
        ui.label(format!(
            "{} (clears {}, saves {})",
            names::game_mode_label(save.game_mode(), save.clear_count()),
            save.clear_count(),
            save.save_count()
        ));
        ui.end_row();
        let chapter = data.nibble(names::STORY_CHAPTER_FLAG as usize) as usize;
        ui.label("Story chapter");
        ui.label(
            names::STORY_CHAPTERS
                .get(chapter)
                .map(|s| s.to_string())
                .unwrap_or_else(|| format!("{chapter}")),
        );
        ui.end_row();
        ui.label("Objective");
        ui.label(names::objective_label(
            data.nibble_byte(names::OBJECTIVE_FLAG as usize),
        ));
        ui.end_row();
        let p = data.player();
        ui.label("HP / MP");
        ui.label(format!("{}/{}  {}/{}", p.hp, p.hp_max, p.mp, p.mp_max));
        ui.end_row();
        ui.label("EXP / BP");
        ui.label(format!(
            "{} / {} (max {} / {})",
            p.exp,
            p.bp,
            save.max_exp(),
            save.max_bp()
        ));
        ui.end_row();
        ui.label("Equipped");
        ui.label(format!(
            "{} with {}, wearing {}",
            opt_item_label(p.weapon),
            opt_item_label(p.weapon_slot_item),
            opt_item_label(p.armor)
        ));
        ui.end_row();
        ui.label("Battles");
        ui.label(format!(
            "won {} / escaped {}",
            save.battles_won(),
            save.battles_escaped()
        ));
        ui.end_row();
        let keys: Vec<&str> = names::key_items()
            .filter(|(id, _)| save.has_key_item(*id))
            .map(|(_, n)| n)
            .collect();
        ui.label("Key items");
        ui.label(if keys.is_empty() {
            "none".to_string()
        } else {
            keys.join(", ")
        });
        ui.end_row();
    });
    false
}

/// Dropdown of catalogued items in `categories`; `extra` adds fixed entries first.
fn item_combo(
    ui: &mut egui::Ui,
    salt: impl std::hash::Hash,
    value: &mut u8,
    categories: &[ItemCategory],
    extra: &[(u8, &str)],
) -> bool {
    let before = *value;
    let selected = extra
        .iter()
        .find(|(v, _)| *v == *value)
        .map(|(_, n)| n.to_string())
        .unwrap_or_else(|| names::item_label(*value as u16));
    egui::ComboBox::from_id_salt(salt)
        .selected_text(selected)
        .width(190.0)
        .height(420.0)
        .show_ui(ui, |ui| {
            for (v, n) in extra {
                ui.selectable_value(value, *v, *n);
            }
            let mut last = None;
            for (id, name, cat) in names::ITEM_NAMES {
                if *id > 0xFF || !categories.contains(cat) {
                    continue;
                }
                if last != Some(*cat) {
                    ui.label(egui::RichText::new(cat.label()).weak().small());
                    last = Some(*cat);
                }
                ui.selectable_value(value, *id as u8, format!("{name}  ({id:#04x})"));
            }
        });
    *value != before
}

const ROW_CATEGORIES: [ItemCategory; 8] = [
    ItemCategory::Consumable,
    ItemCategory::Attachment,
    ItemCategory::Tool,
    ItemCategory::WeaponPart,
    ItemCategory::Armor,
    ItemCategory::Weapon,
    ItemCategory::Ammunition,
    ItemCategory::BuiltInSupply,
];
const AMMO_CATEGORIES: [ItemCategory; 2] = [ItemCategory::Ammunition, ItemCategory::BuiltInSupply];

fn item_rows_grid(
    ui: &mut egui::Ui,
    save: &mut SaveImage,
    id: &str,
    rows: Vec<usize>,
    carried: bool,
) -> bool {
    let mut changed = false;
    egui::Grid::new(id).striped(true).show(ui, |ui| {
        ui.strong("row");
        ui.strong("item");
        ui.strong("category");
        ui.strong("qty");
        ui.strong("loaded");
        ui.strong("armor slot");
        ui.end_row();
        for i in rows {
            let row = save.item_row(i);
            ui.label(format!("{i}"));
            let mut item = row.item_id;
            if item_combo(ui, (id, i), &mut item, &ROW_CATEGORIES, &[]) {
                save.set_item_row(
                    i,
                    ItemRow {
                        item_id: item,
                        ..row
                    },
                );
                changed = true;
            }
            ui.label(
                names::item_category(row.item_id as u16)
                    .map(|c| c.label())
                    .unwrap_or("?"),
            );
            let mut qty = row.qty;
            if ui
                .add(egui::DragValue::new(&mut qty).range(0..=65535))
                .changed()
            {
                save.set_item_row(i, ItemRow { qty, ..row });
                changed = true;
            }
            let loaded = if carried {
                save.loaded_qty(row.item_id)
            } else {
                0
            };
            ui.label(if loaded > 0 {
                format!("{loaded}")
            } else {
                String::new()
            })
            .on_hover_text("Rounds loaded into carried weapons; the row quantity includes them.");
            let mut attach = row.attach_slot as i32;
            if ui
                .add(
                    egui::DragValue::new(&mut attach)
                        .range(-1..=10)
                        .custom_formatter(|v, _| match v as i32 {
                            -1 => "equipped".into(),
                            0 => "-".into(),
                            n => format!("{n}"),
                        }),
                )
                .on_hover_text("-1 = the equipped armour, 1..10 = attached to its slots")
                .changed()
            {
                save.set_item_row(
                    i,
                    ItemRow {
                        attach_slot: attach as i8,
                        ..row
                    },
                );
                changed = true;
            }
            if ui.button("✕").on_hover_text("Empty this row").clicked() {
                save.set_item_row(i, ItemRow::default());
                changed = true;
            }
            ui.end_row();
        }
    });
    changed
}

fn show_inventory(ui: &mut egui::Ui, save: &mut SaveImage, st: &mut UiState) -> bool {
    let mut changed = false;
    let carried = save.carried_row_indices();
    ui.heading(format!(
        "Carried items (rows {}..{})",
        carried.start, carried.end
    ));
    let rows: Vec<usize> = carried
        .clone()
        .filter(|i| save.item_row(*i).item_id != 0)
        .collect();
    changed |= item_rows_grid(ui, save, "carried", rows, true);
    ui.horizontal(|ui| {
        ui.label("Add");
        if st.add_item == 0 {
            st.add_item = 0x01;
        }
        item_combo(ui, "add-item", &mut st.add_item, &ROW_CATEGORIES, &[]);
        st.add_qty = st.add_qty.max(1);
        ui.add(
            egui::DragValue::new(&mut st.add_qty)
                .range(1..=999)
                .prefix("x"),
        );
        if ui
            .button("add to inventory")
            .on_hover_text("Ammunition joins its stack; other items take one free row each.")
            .clicked()
        {
            changed |= save.give_item(st.add_item, st.add_qty).is_ok();
        }
    });
    let free = carried
        .clone()
        .filter(|i| save.item_row(*i).item_id == 0)
        .count();
    ui.label(egui::RichText::new(format!("{free} free rows in the carried window")).weak());
    ui.separator();
    let stored: Vec<usize> = (0..ITEM_ROW_COUNT)
        .filter(|i| !carried.contains(i) && save.item_row(*i).item_id != 0)
        .collect();
    egui::CollapsingHeader::new(format!(
        "Other rows outside the carried window ({})",
        stored.len()
    ))
    .show(ui, |ui| {
        changed |= item_rows_grid(ui, save, "stored", stored, false);
    });
    egui::CollapsingHeader::new("Carried window").show(ui, |ui| {
        let mut range = save.carried_range();
        ui.add(
            egui::DragValue::new(&mut range.first_row)
                .prefix("first row ")
                .range(0..=255),
        );
        ui.add(
            egui::DragValue::new(&mut range.row_count)
                .prefix("row count ")
                .range(0..=255),
        );
        if ui.button("apply carried range").clicked() {
            save.set_carried_range(range);
            changed = true;
        }
    });
    changed
}

fn show_key_items(ui: &mut egui::Ui, save: &mut SaveImage, after: &mut Vec<SlotAction>) -> bool {
    ui.heading("Key items");
    ui.label(
        egui::RichText::new(
            "Key items are not inventory rows: each is one collection bit (item id 0x100 + bit).",
        )
        .weak(),
    );
    let mut changed = false;
    egui::Grid::new("keys").striped(true).show(ui, |ui| {
        for (id, name) in names::key_items() {
            let mut held = save.has_key_item(id);
            if ui.checkbox(&mut held, name).changed() {
                save.set_key_item(id, held);
                if held && crate::edit::is_ice_bag(id) {
                    after.push(SlotAction::RestartIceBagTimer);
                }
                changed = true;
            }
            ui.label(egui::RichText::new(format!("{id:#x}")).weak());
            ui.end_row();
        }
    });
    ui.separator();
    ui.label("Unnamed collection bits (events and unused ids)");
    egui::Grid::new("collected").show(ui, |ui| {
        let mut n = 0;
        for bit in 0..COLLECTED_BIT_WORDS * 32 {
            if names::item_name(0x100 + bit as u16).is_some() {
                continue;
            }
            let mut on = save.collected_bit(bit);
            if ui.toggle_value(&mut on, format!("{bit}")).changed() {
                save.set_collected_bit(bit, on);
                changed = true;
            }
            n += 1;
            if n % 16 == 0 {
                ui.end_row();
            }
        }
    });
    changed
}

fn show_weapons(
    ui: &mut egui::Ui,
    save: &mut SaveImage,
    data: &mut SlotData,
    st: &mut UiState,
) -> bool {
    let mut changed = false;
    ui.heading("Equipped");
    let player = data.player();
    let carried_weapons: Vec<(u8, &str)> = save
        .carried_row_indices()
        .map(|i| save.item_row(i).item_id)
        .filter(|id| (0x80..=0x9F).contains(id))
        .map(|id| (id, names::item_name(id as u16).unwrap_or("unknown weapon")))
        .collect();
    ui.horizontal(|ui| {
        let mut weapon = player.weapon.unwrap_or(0);
        ui.label("weapon");
        let before = weapon;
        egui::ComboBox::from_id_salt("equip-weapon")
            .selected_text(opt_item_label(player.weapon))
            .show_ui(ui, |ui| {
                ui.selectable_value(&mut weapon, 0, "none");
                for (id, name) in &carried_weapons {
                    ui.selectable_value(&mut weapon, *id, *name);
                }
            });
        let mut ammo = player.weapon_slot_item.unwrap_or(0);
        ui.label("ammo");
        let ammo_before = ammo;
        item_combo(
            ui,
            "equip-ammo",
            &mut ammo,
            &AMMO_CATEGORIES,
            &[(0, "none")],
        );
        if weapon != before || ammo != ammo_before {
            if weapon != before && weapon != 0 {
                // Follow the weapon's saved primary selection, as the game shows it.
                let load = save.weapon_load((weapon - 0x80) as usize);
                ammo = load.primary_item_id;
            }
            let w = (weapon != 0).then_some(weapon);
            let a = (ammo != 0 && w.is_some()).then_some(ammo);
            if data.set_equipped_weapon(w, a).is_err() {
                ui.label("invalid selection");
            }
        }
        ui.label(format!("armor: {}", opt_item_label(player.armor)))
            .on_hover_text("Change armour in game: equipping recomputes HP/MP and attachments.");
    });
    ui.separator();
    ui.heading("Weapon loads");
    ui.checkbox(
        &mut st.show_all_weapons,
        "show weapons that are not carried",
    );
    egui::Grid::new("weapons").striped(true).show(ui, |ui| {
        ui.strong("weapon");
        ui.strong("primary");
        ui.strong("qty");
        ui.strong("secondary");
        ui.strong("qty");
        ui.strong("uses");
        ui.end_row();
        for i in 0..WEAPON_LOAD_COUNT {
            let id = (0x80 + i) as u8;
            let held = carried_weapons.iter().any(|(w, _)| *w == id);
            if !held && !st.show_all_weapons {
                continue;
            }
            let load = save.weapon_load(i);
            ui.label(if held {
                egui::RichText::new(names::item_label(id as u16)).strong()
            } else {
                egui::RichText::new(names::item_label(id as u16))
            });
            let mut p = load.primary_item_id;
            if item_combo(ui, ("primary", i), &mut p, &AMMO_CATEGORIES, &[(0, "none")]) {
                save.set_weapon_load(
                    i,
                    WeaponLoad {
                        primary_item_id: p,
                        ..load
                    },
                );
                changed = true;
            }
            let mut pq = load.primary_qty;
            if ui.add(egui::DragValue::new(&mut pq)).changed() {
                save.set_weapon_load(
                    i,
                    WeaponLoad {
                        primary_qty: pq,
                        ..load
                    },
                );
                changed = true;
            }
            let mut s = load.secondary_item_id;
            if item_combo(
                ui,
                ("secondary", i),
                &mut s,
                &AMMO_CATEGORIES,
                &[(0, "none"), (0xFF, "unavailable")],
            ) {
                save.set_weapon_load(
                    i,
                    WeaponLoad {
                        secondary_item_id: s,
                        ..load
                    },
                );
                changed = true;
            }
            let mut sq = load.secondary_qty;
            if ui.add(egui::DragValue::new(&mut sq)).changed() {
                save.set_weapon_load(
                    i,
                    WeaponLoad {
                        secondary_qty: sq,
                        ..load
                    },
                );
                changed = true;
            }
            let mut uses = save.weapon_use_count(i);
            if ui.add(egui::DragValue::new(&mut uses)).changed() {
                save.set_weapon_use_count(i, uses);
                changed = true;
            }
            ui.end_row();
        }
    });
    changed
}

fn show_pe(ui: &mut egui::Ui, save: &mut SaveImage) -> bool {
    ui.heading("Parasite Energy");
    let mut changed = false;
    egui::Grid::new("pe").striped(true).show(ui, |ui| {
        ui.strong("ability");
        ui.strong("level");
        ui.strong("uses");
        ui.end_row();
        for i in 0..PE_LEVEL_COUNT {
            let mut level = save.pe_level(i);
            ui.label(names::pe_name(i));
            if ui
                .add(egui::DragValue::new(&mut level).range(0..=3))
                .changed()
            {
                save.set_pe_level(i, level);
                changed = true;
            }
            if i < PE_USE_COUNT {
                let mut uses = save.pe_use_count(i);
                if ui.add(egui::DragValue::new(&mut uses)).changed() {
                    save.set_pe_use_count(i, uses);
                    changed = true;
                }
            } else {
                ui.label("—");
            }
            ui.end_row();
        }
    });
    changed
}

fn show_progression(ui: &mut egui::Ui, save: &mut SaveImage, data: &mut SlotData) -> bool {
    ui.heading("Progression");
    let mut changed = false;
    ui.horizontal(|ui| {
        let mut chapter = data.nibble(names::STORY_CHAPTER_FLAG as usize);
        let label = names::STORY_CHAPTERS
            .get(chapter as usize)
            .map(|s| s.to_string())
            .unwrap_or_else(|| format!("{chapter}"));
        ui.label("story chapter");
        let before = chapter;
        egui::ComboBox::from_id_salt("chapter")
            .selected_text(label)
            .show_ui(ui, |ui| {
                for (i, name) in names::STORY_CHAPTERS.iter().enumerate() {
                    ui.selectable_value(&mut chapter, i as u8, *name);
                }
            });
        if chapter != before {
            data.set_nibble(names::STORY_CHAPTER_FLAG as usize, chapter);
        }
        let objective = data.nibble_byte(names::OBJECTIVE_FLAG as usize);
        ui.label(format!("objective: {}", names::objective_label(objective)));
    });
    ui.separator();
    ui.label("Stages visited (visitFlags)");
    ui.horizontal_wrapped(|ui| {
        let mut flags = save.visit_flags();
        for (bit, label) in names::VISIT_FLAG_LABELS.iter().enumerate() {
            let mut on = flags & (1 << bit) != 0;
            if ui.checkbox(&mut on, *label).changed() {
                flags ^= 1 << bit;
                save.set_visit_flags(flags);
                changed = true;
            }
        }
    });
    ui.horizontal(|ui| {
        if ui.button("mark all items seen (0x180)").clicked() {
            save.mark_items_seen(0x180);
            changed = true;
        }
        let mut tiers = save.shop_tiers();
        if ui
            .add(
                egui::DragValue::new(&mut tiers)
                    .hexadecimal(4, false, true)
                    .prefix("shop tiers "),
            )
            .changed()
        {
            save.set_shop_tiers(tiers);
            changed = true;
        }
        let mut stock = save.shop_stock();
        if ui
            .add(
                egui::DragValue::new(&mut stock)
                    .hexadecimal(8, false, true)
                    .prefix("shop stock "),
            )
            .changed()
        {
            save.set_shop_stock(stock);
            changed = true;
        }
    });
    ui.separator();
    ui.heading("Stage flag banks");
    for stage in 1..=5u8 {
        egui::CollapsingHeader::new(format!("{} (bank {stage})", names::stage_name(stage)))
            .id_salt(("stage", stage))
            .show(ui, |ui| show_stage_bank(ui, data, stage));
    }
    changed
}

fn show_stage_bank(ui: &mut egui::Ui, data: &mut SlotData, stage: u8) {
    let Some(bank) = data.stage_bank(stage) else {
        return;
    };
    let mut visits: Vec<(u8, String, bool)> = Vec::new();
    for area in 1..=64u8 {
        let on = bank.area_visited(area);
        match names::area_name(stage, area) {
            Some(n) => visits.push((area, n.to_string(), on)),
            None if on => visits.push((area, format!("area {area} (unnamed)"), on)),
            None => {}
        }
    }
    let objects: Vec<(usize, u8, Option<&str>)> = (0..64)
        .map(|s| {
            (
                s,
                bank.object_state(s),
                names::object_slot_name(stage, s as u8),
            )
        })
        .collect();
    let slots: Vec<(usize, (i8, u8))> = (0..crate::edit::stage_area_slots(stage))
        .filter_map(|s| bank.area_slot(s).map(|v| (s, v)))
        .collect();
    ui.label("Visited areas (map)");
    egui::Grid::new(("visits", stage)).show(ui, |ui| {
        for (n, (area, name, on)) in visits.into_iter().enumerate() {
            let mut v = on;
            if ui.checkbox(&mut v, name).changed() {
                let _ = data.set_area_visited(stage, area, v);
            }
            if n % 3 == 2 {
                ui.end_row();
            }
        }
    });
    if stage == 3 {
        ui.label(
            egui::RichText::new(
                "Night Dryfield keeps its object states and placement records in the Dryfield bank.",
            )
            .weak(),
        );
        return;
    }
    ui.label("Object states (pickups, item boxes, save points)");
    egui::Grid::new(("objects", stage))
        .striped(true)
        .show(ui, |ui| {
            for (slot, state, name) in objects {
                if state == 0 && name.is_none() {
                    continue;
                }
                ui.label(format!("{slot}"));
                ui.label(name.unwrap_or("(unnamed)"));
                let mut v = state;
                egui::ComboBox::from_id_salt(("object", stage, slot))
                    .selected_text(names::OBJECT_STATE_LABELS[v as usize])
                    .show_ui(ui, |ui| {
                        for (i, l) in names::OBJECT_STATE_LABELS.iter().enumerate() {
                            ui.selectable_value(&mut v, i as u8, *l);
                        }
                    });
                if v != state {
                    let _ = data.set_object_state(stage, slot, v);
                }
                ui.end_row();
            }
        });
    egui::CollapsingHeader::new("Area placement records (variant, spawn flags) — raw")
        .id_salt(("slots", stage))
        .show(ui, |ui| {
            for (slot, (variant, flags)) in slots {
                ui.label(format!(
                    "slot {slot:2}: variant {variant} flags {flags:#04x}{}",
                    if flags & 0x04 != 0 { " (map mark)" } else { "" }
                ));
            }
        });
}

fn show_flags(ui: &mut egui::Ui, data: &mut SlotData, st: &mut UiState) -> bool {
    ui.heading("Game flags (nibble bank)");
    ui.label(
        egui::RichText::new(
            "504 four-bit values. Names come from include/main/event_flags.h and \
             include/main/gameflag_ids.h; hover a name for its documentation.",
        )
        .weak(),
    );
    ui.horizontal(|ui| {
        ui.label("filter");
        ui.text_edit_singleline(&mut st.flag_filter);
        ui.checkbox(&mut st.show_unnamed_flags, "show unnamed positions");
    });
    let filter = st.flag_filter.to_lowercase();
    egui::Grid::new("flags").striped(true).show(ui, |ui| {
        ui.strong("index");
        ui.strong("value");
        ui.strong("name");
        ui.end_row();
        for index in 0..crate::edit::NIBBLE_FLAG_COUNT {
            if crate::edit::NIBBLE_PLAY_TIME_POSITIONS.contains(&index) {
                continue;
            }
            let named = names::nibble_flag(index as u16);
            let value = data.nibble(index);
            let label = match named {
                Some(f) => f.name.to_string(),
                None if index == names::OBJECTIVE_FLAG as usize + 1 => {
                    "(low half of EVENT_FLAG_CURRENT_OBJECTIVE)".to_string()
                }
                None => "(unnamed)".to_string(),
            };
            if named.is_none() && !st.show_unnamed_flags && value == 0 {
                continue;
            }
            if !filter.is_empty()
                && !label.to_lowercase().contains(&filter)
                && !format!("{index:#05x}").contains(&filter)
            {
                continue;
            }
            ui.label(format!("{index:#05x}"));
            let mut v = value;
            if ui.add(egui::DragValue::new(&mut v).range(0..=15)).changed() {
                data.set_nibble(index, v);
            }
            let response = ui.label(label);
            if let Some(f) = named {
                response.on_hover_text(format!("{}\n{}", f.doc, f.source));
            }
            ui.end_row();
        }
    });
    ui.label(format!("play-time mark: {} min", data.play_time_mark()));
    false
}

fn show_character(ui: &mut egui::Ui, save: &mut SaveImage) -> bool {
    ui.heading("Character");
    let mut changed = false;
    let mut exp = save.player_exp();
    if ui
        .add(egui::DragValue::new(&mut exp).prefix("EXP "))
        .changed()
    {
        save.set_player_exp(exp);
        changed = true;
    }
    let mut bp = save.player_bp();
    if ui
        .add(egui::DragValue::new(&mut bp).prefix("BP "))
        .changed()
    {
        save.set_player_bp(bp);
        changed = true;
    }
    let mut max_exp = save.max_exp();
    if ui
        .add(egui::DragValue::new(&mut max_exp).prefix("max EXP "))
        .changed()
    {
        save.set_max_exp(max_exp);
        changed = true;
    }
    let mut max_bp = save.max_bp();
    if ui
        .add(egui::DragValue::new(&mut max_bp).prefix("max BP "))
        .changed()
    {
        save.set_max_bp(max_bp);
        changed = true;
    }
    let mut hp = save.hp_bonus();
    if ui
        .add(
            egui::DragValue::new(&mut hp)
                .prefix("HP bonus ")
                .range(0..=255),
        )
        .changed()
    {
        save.set_hp_bonus(hp);
        changed = true;
    }
    let mut mp = save.mp_bonus();
    if ui
        .add(
            egui::DragValue::new(&mut mp)
                .prefix("MP bonus ")
                .range(0..=255),
        )
        .changed()
    {
        save.set_mp_bonus(mp);
        changed = true;
    }
    let mut minutes = save.play_time_minutes();
    if ui
        .add(egui::DragValue::new(&mut minutes).prefix("play time (min) "))
        .changed()
    {
        save.set_play_time_minutes(minutes);
        changed = true;
    }
    let mut companion_hp = save.companion_hp();
    if ui
        .add(egui::DragValue::new(&mut companion_hp).prefix("companion HP "))
        .changed()
    {
        save.set_companion_hp(companion_hp);
        changed = true;
    }
    let mut companion_hp_max = save.companion_hp_max();
    if ui
        .add(egui::DragValue::new(&mut companion_hp_max).prefix("companion HP max "))
        .changed()
    {
        save.set_companion_hp_max(companion_hp_max);
        changed = true;
    }
    changed
}

fn show_options(ui: &mut egui::Ui, save: &mut SaveImage) -> bool {
    ui.heading("Options");
    let mut changed = false;
    let mut v = save.vibration_off();
    if ui.checkbox(&mut v, "vibration off").changed() {
        save.set_vibration_off(v);
        changed = true;
    }
    let mut r = save.run_default();
    if ui.checkbox(&mut r, "run by default").changed() {
        save.set_run_default(r);
        changed = true;
    }
    let mut layout = save.button_layout();
    if ui
        .add(
            egui::DragValue::new(&mut layout)
                .prefix("button layout ")
                .range(0..=2),
        )
        .changed()
    {
        save.set_button_layout(layout);
        changed = true;
    }
    let mut mono = save.mono_sound();
    if ui.checkbox(&mut mono, "mono sound").changed() {
        save.set_mono_sound(mono);
        changed = true;
    }
    let mut volume = save.music_volume();
    if ui
        .add(
            egui::DragValue::new(&mut volume)
                .prefix("music volume (0 full .. 3 off) ")
                .range(0..=3),
        )
        .changed()
    {
        save.set_music_volume(volume);
        changed = true;
    }
    let mut cursor = save.cursor_reset();
    if ui.checkbox(&mut cursor, "reset menu cursor").changed() {
        save.set_cursor_reset(cursor);
        changed = true;
    }
    let mut interlace = save.interlaced();
    if ui.checkbox(&mut interlace, "interlaced display").changed() {
        save.set_interlaced(interlace);
        changed = true;
    }
    let mut cheat = save.cheat_mode();
    if ui
        .checkbox(&mut cheat, "cheat mode (nothing is spent)")
        .changed()
    {
        save.set_cheat_mode(cheat);
        changed = true;
    }
    let mut frozen = save.actors_frozen();
    if ui.checkbox(&mut frozen, "actors frozen").changed() {
        save.set_actors_frozen(frozen);
        changed = true;
    }
    changed
}

/// Run the GUI.
pub fn run_gui() -> Result<()> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([1180.0, 720.0]),
        ..Default::default()
    };
    eframe::run_native(
        "Parasite Eve II Save Editor",
        options,
        Box::new(|_cc| Ok(Box::new(EditorApp::new()))),
    )
    .map_err(|e| anyhow::anyhow!("eframe error: {e}"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::layout::{FILE_SIZE, PLAYER_SECTION};
    use std::fs;

    #[test]
    fn every_tab_renders_named_contents_headless() {
        let mut raw = vec![0u8; FILE_SIZE];
        raw[..2].copy_from_slice(b"SC");
        let mut slot = SlotData::from_bytes(raw).unwrap();
        slot.edit(|s| {
            s.set_location([6, 1, 1, 1, 1, 1, 0, 0]);
            s.set_carried_range(crate::save::CarriedRange {
                first_row: 0,
                row_count: 20,
                table_id: 0,
                field_3: 0,
            });
            s.give_item(0x81, 1).unwrap();
            s.give_item(0xA0, 100).unwrap();
            s.give_item(0x60, 1).unwrap();
            s.set_key_item(0x106, true);
        })
        .unwrap();
        slot.set_equipped_weapon(Some(0x81), Some(0xA0)).unwrap();
        slot.set_nibble(0x7A, 1);
        let mut app = EditorApp::new();
        app.working = Some(slot);
        app.active_slot = Some(0);
        let ctx = egui::Context::default();
        for tab in [
            Tab::Summary,
            Tab::Character,
            Tab::Inventory,
            Tab::KeyItems,
            Tab::Weapons,
            Tab::ParasiteEnergy,
            Tab::Progression,
            Tab::Flags,
            Tab::Options,
        ] {
            app.tab = tab;
            let output = ctx.run(egui::RawInput::default(), |ctx| app.central_panel(ctx));
            assert!(!output.shapes.is_empty());
        }
        let slot = app.working.as_ref().unwrap();
        let (sections, header_ok, summary_ok) = slot.verify();
        assert!(sections.iter().all(|ok| *ok) && header_ok && summary_ok);
        assert_eq!(slot.player().weapon, Some(0x81));
    }

    #[test]
    fn gui_exp_edit_reaches_gameplay_record() {
        let mut raw = vec![0u8; FILE_SIZE];
        raw[..2].copy_from_slice(b"SC");
        let mut app = EditorApp::new();
        app.working = Some(SlotData::from_bytes(raw).unwrap());
        app.active_slot = Some(0);
        // Simulate the central panel's write-back of an edited save copy.
        let data = app.working.as_mut().unwrap();
        let mut save = data.save().unwrap();
        save.set_player_exp(5555);
        let bytes = save.raw.clone();
        data.edit(|image| image.raw.copy_from_slice(&bytes))
            .unwrap();
        assert_eq!(data.player().exp, 5555);
    }

    #[test]
    fn standalone_save_commit_survives_reopen_without_native_windows() {
        let root = std::env::temp_dir().join(format!("pe2-editor-gui-{}", std::process::id()));
        fs::create_dir_all(&root).unwrap();
        for mcs in [false, true] {
            let path = root.join(if mcs { "copy.mcs" } else { "copy.slot" });
            let mut raw = vec![0u8; FILE_SIZE];
            raw[..2].copy_from_slice(b"SC");
            let mut header = vec![0u8; 128];
            header[..2].copy_from_slice(b"SC");
            header[0x0a..0x0a + 20].copy_from_slice(b"BASLUS-01042B1234567");
            let bytes = if mcs {
                [header.clone(), raw].concat()
            } else {
                raw
            };
            fs::write(&path, bytes).unwrap();
            let mut app = EditorApp::new();
            app.load_container(path.to_str().unwrap());
            app.select_slot(if mcs { 1 } else { 0 });
            app.working
                .as_mut()
                .unwrap()
                .edit(|save| save.set_player_bp(4321))
                .unwrap();
            let ctx = egui::Context::default();
            let output = ctx.run(egui::RawInput::default(), |ctx| {
                app.top_panel(ctx);
                app.side_panel(ctx);
                app.central_panel(ctx);
            });
            assert!(!output.shapes.is_empty());
            app.commit();
            assert!(app.status.starts_with("Saved"), "{}", app.status);
            let reopened = CardContainer::open(&path).unwrap();
            let file = reopened.slots().remove(0).1;
            let slot = SlotData::from_bytes(file.data).unwrap();
            assert_eq!(slot.save().unwrap().player_bp(), 4321);
            let offset = PLAYER_SECTION.live_offset + 0x0c;
            assert_eq!(
                i32::from_le_bytes(slot.data[offset..offset + 4].try_into().unwrap()),
                4321
            );
            let (sections, header_ok, summary_ok) = slot.verify();
            assert!(sections.iter().all(|ok| *ok) && header_ok && summary_ok);
            if mcs {
                assert_eq!(&fs::read(&path).unwrap()[..128], header);
            }
        }
        fs::remove_dir_all(root).unwrap();
    }
}
