//! Command-line interface: container inspection, typed edits and JSON transfer.

use std::path::PathBuf;

use anyhow::{bail, Context, Result};
use clap::{Args, Parser, Subcommand};

use crate::card::{write_slot_file, CardContainer};
use crate::edit::SlotData;
use crate::names::{self, Location};
use crate::save::{
    SaveImage, SaveTransfer, COLLECTED_BIT_WORDS, ITEM_ROW_COUNT, WEAPON_LOAD_COUNT,
};

#[derive(Parser)]
pub struct Cli {
    #[command(subcommand)]
    pub command: Option<Command>,
}

#[derive(Subcommand)]
pub enum Command {
    /// List PE2 saves in a container.
    List { path: PathBuf },
    /// Show one slot's contents (add --verbose for all typed fields).
    Inspect {
        path: PathBuf,
        #[arg(long)]
        slot: Option<usize>,
        #[arg(long)]
        verbose: bool,
        /// Also decode the stage flag banks and the nibble flag bank.
        #[arg(long)]
        flags: bool,
    },
    /// List the item catalog: id, name, category (optionally filtered by category).
    Items {
        #[arg(long)]
        category: Option<String>,
    },
    /// Verify every live checksum of one slot.
    Verify {
        path: PathBuf,
        #[arg(long)]
        slot: Option<usize>,
    },
    /// Edit typed fields of one slot; unknown bytes are preserved.
    Edit(EditArgs),
    /// Export one slot's typed fields as JSON.
    Export {
        path: PathBuf,
        #[arg(long)]
        slot: Option<usize>,
        #[arg(long)]
        out: PathBuf,
    },
    /// Apply a JSON transfer onto one slot (None fields are skipped).
    Import {
        path: PathBuf,
        #[arg(long)]
        slot: Option<usize>,
        #[arg(long)]
        input: PathBuf,
    },
    /// Export one slot's raw 0x2000-byte file.
    Extract {
        path: PathBuf,
        #[arg(long)]
        slot: Option<usize>,
        #[arg(long)]
        out: PathBuf,
    },
}

#[derive(Args)]
pub struct EditArgs {
    pub path: PathBuf,
    #[arg(long)]
    slot: Option<usize>,
    /// Field=value pairs; run `edit --help-fields` style listing via `fields`.
    #[arg(long = "set")]
    sets: Vec<String>,
    /// Print the supported field names instead of editing.
    #[arg(long)]
    fields: bool,
    /// Add an item to the carried window: NAME or ID, optionally `:QTY` (e.g. `"9mm Hydra:50"`).
    #[arg(long = "add-item")]
    add_items: Vec<String>,
    /// Remove every carried row holding an item (NAME or ID).
    #[arg(long = "remove-item")]
    remove_items: Vec<String>,
    /// Give a key item (NAME or ID 0x100..0x17F).
    #[arg(long = "add-key-item")]
    add_key_items: Vec<String>,
    /// Take a key item away (NAME or ID 0x100..0x17F).
    #[arg(long = "remove-key-item")]
    remove_key_items: Vec<String>,
    /// Equip a weapon: NAME or ID, optionally `:AMMO` (e.g. `"M93R:9mm P.B."`), or `none`.
    #[arg(long)]
    equip: Option<String>,
    /// Set a nibble-bank flag: INDEX=VALUE (index hex/decimal, value 0..15).
    #[arg(long = "set-flag")]
    set_flags: Vec<String>,
}

fn resolve_slot(
    container: &CardContainer,
    slot: Option<usize>,
) -> Result<(usize, crate::card::SlotFile)> {
    let slots = container.slots();
    if slots.is_empty() {
        bail!("no PE2 save files found");
    }
    let available: Vec<usize> = slots.iter().map(|(i, _)| *i).collect();
    match slot {
        Some(index) => slots.into_iter().find(|(i, _)| *i == index).ok_or_else(|| {
            anyhow::anyhow!("slot {} not found (available: {:?})", index, available)
        }),
        None if slots.len() == 1 => Ok(slots.into_iter().next().unwrap()),
        None => bail!(
            "multiple slots found; pass --slot (available: {:?})",
            available
        ),
    }
}

fn open_slot(path: &PathBuf, slot: Option<usize>) -> Result<(CardContainer, usize, SlotData)> {
    let container = CardContainer::open(path).context("opening container")?;
    let (index, file) = resolve_slot(&container, slot)?;
    let data = SlotData::from_bytes(file.data.clone())?;
    Ok((container, index, data))
}

/// Print the human-readable summary shown by `list` and `inspect`.
pub fn describe(save: &SaveImage) -> String {
    let minutes = save.play_time_minutes();
    format!(
        "save #{} at {} ({}) | {} | {}:{:02} played | EXP {} BP {} | battles {}/{}",
        save.save_number(),
        save.save_point_label(),
        Location::from_key(save.location()).label(),
        names::game_mode_label(save.game_mode(), save.clear_count()),
        minutes / 60,
        minutes % 60,
        save.player_exp(),
        save.player_bp(),
        save.battles_won(),
        save.battles_escaped(),
    )
}

fn opt_item(id: Option<u8>) -> String {
    id.map(|i| format!("{} ({:#04x})", names::item_label(i as u16), i))
        .unwrap_or_else(|| "none".to_string())
}

fn category_label(id: u16) -> &'static str {
    names::item_category(id).map(|c| c.label()).unwrap_or("?")
}

fn print_rows(save: &SaveImage, rows: impl Iterator<Item = usize>, carried: bool) {
    for index in rows {
        let row = save.item_row(index);
        if row.item_id == 0 {
            continue;
        }
        let attach = match row.attach_slot {
            -1 => " [equipped armor]".to_string(),
            0 => String::new(),
            n => format!(" [armor slot {n}]"),
        };
        let loaded = if carried {
            save.loaded_qty(row.item_id)
        } else {
            0
        };
        let loaded = if loaded > 0 {
            format!(" ({} loaded)", loaded)
        } else {
            String::new()
        };
        println!(
            "  row {:3}: {:<22} {:<15} x{}{}{} ({:#04x})",
            index,
            names::item_label(row.item_id as u16),
            category_label(row.item_id as u16),
            row.qty,
            loaded,
            attach,
            row.item_id
        );
    }
}

fn print_inspect(data: &SlotData, save: &SaveImage, verbose: bool, flags: bool) {
    let loc = Location::from_key(save.location());
    println!(
        "location: {} (stage {} area {} room state {} view {} warp {} variant {})",
        loc.label(),
        loc.stage,
        loc.area,
        loc.room_state,
        loc.view,
        loc.warp,
        loc.variant
    );
    let minutes = save.play_time_minutes();
    println!(
        "mode: {} | clears {} | play time {}:{:02} | saves {}",
        names::game_mode_label(save.game_mode(), save.clear_count()),
        save.clear_count(),
        minutes / 60,
        minutes % 60,
        save.save_count()
    );
    let p = data.player();
    println!(
        "player: HP {}/{} MP {}/{} | EXP {} BP {} (gameplay record; preview EXP {} BP {})",
        p.hp,
        p.hp_max,
        p.mp,
        p.mp_max,
        p.exp,
        p.bp,
        save.player_exp(),
        save.player_bp()
    );
    println!(
        "equipped: weapon {} | ammo {} | armor {}",
        opt_item(p.weapon),
        opt_item(p.weapon_slot_item),
        opt_item(p.armor)
    );
    let chapter = data.nibble(names::STORY_CHAPTER_FLAG as usize) as usize;
    println!(
        "story: chapter {} | objective {}",
        names::STORY_CHAPTERS
            .get(chapter)
            .map(|s| s.to_string())
            .unwrap_or_else(|| format!("{chapter}")),
        names::objective_label(data.nibble_byte(names::OBJECTIVE_FLAG as usize))
    );
    let rows = save.carried_row_indices();
    println!(
        "inventory: {} items in rows {}..{}",
        carried_item_count(save),
        rows.start,
        rows.end
    );
    print_rows(save, rows.clone(), true);
    let stored: Vec<usize> = (0..ITEM_ROW_COUNT)
        .filter(|i| !rows.contains(i) && save.item_row(*i).item_id != 0)
        .collect();
    if !stored.is_empty() {
        println!("other rows (outside the carried window):");
        print_rows(save, stored.into_iter(), false);
    }
    let keys: Vec<String> = names::key_items()
        .filter(|(id, _)| save.has_key_item(*id))
        .map(|(id, n)| format!("{n} ({id:#x})"))
        .collect();
    println!(
        "key items: {}",
        if keys.is_empty() {
            "none".to_string()
        } else {
            keys.join(", ")
        }
    );
    let unknown_bits: Vec<usize> = (0..COLLECTED_BIT_WORDS * 32)
        .filter(|b| save.collected_bit(*b) && names::item_name(0x100 + *b as u16).is_none())
        .collect();
    if !unknown_bits.is_empty() {
        println!("unnamed collection bits set: {:?}", unknown_bits);
    }
    println!("weapons:");
    for i in 0..WEAPON_LOAD_COUNT {
        let id = (0x80 + i) as u8;
        let held = save
            .carried_row_indices()
            .any(|r| save.item_row(r).item_id == id);
        let load = save.weapon_load(i);
        let uses = save.weapon_use_count(i);
        if !held && !verbose {
            continue;
        }
        if !held && load.primary_qty == 0 && load.secondary_qty == 0 && uses == 0 {
            continue;
        }
        println!(
            "  {:<18} {} primary {} x{} | secondary {} | used {}",
            names::item_label(id as u16),
            if held { "(carried)" } else { "         " },
            load_item(load.primary_item_id),
            load.primary_qty,
            match load.secondary_item_id {
                0xFF => "unavailable".to_string(),
                sid => format!("{} x{}", load_item(sid), load.secondary_qty),
            },
            uses
        );
    }
    let pe: Vec<String> = (0..names::PE_NAMES.len())
        .filter(|i| save.pe_level(*i) > 0)
        .map(|i| format!("{} Lv{}", names::pe_name(i), save.pe_level(i)))
        .collect();
    println!(
        "parasite energy: {}",
        if pe.is_empty() {
            "none".to_string()
        } else {
            pe.join(", ")
        }
    );
    let visited: Vec<&str> = (1..6)
        .filter(|b| save.visit_flags() & (1 << b) != 0)
        .map(|b| names::STAGE_NAMES[b])
        .collect();
    println!(
        "stages visited: {}",
        if visited.is_empty() {
            "none".to_string()
        } else {
            visited.join(", ")
        }
    );
    println!(
        "companion: type {} variant {} hp {}/{}",
        save.companion_type(),
        save.companion_variant(),
        save.companion_hp(),
        save.companion_hp_max()
    );
    if verbose {
        println!("options: vibration_off {} run {} layout {} mono {} music {} cursor_reset {} interlace {} cheat {}",
            save.vibration_off(), save.run_default(), save.button_layout(), save.mono_sound(),
            save.music_volume(), save.cursor_reset(), save.interlaced(), save.cheat_mode());
        println!(
            "hpBonus {} mpBonus {} maxExp {} maxBp {} shop tiers {:#x} stock {:#x}",
            save.hp_bonus(),
            save.mp_bonus(),
            save.max_exp(),
            save.max_bp(),
            save.shop_tiers(),
            save.shop_stock()
        );
    }
    if flags {
        print_flags(data);
    }
}

fn load_item(id: u8) -> String {
    match id {
        0 => "none".to_string(),
        _ => names::item_label(id as u16),
    }
}

/// Decoded stage banks and nibble flags (`inspect --flags`).
fn print_flags(data: &SlotData) {
    for stage in 1..=5u8 {
        let Some(bank) = data.stage_bank(stage) else {
            continue;
        };
        println!("stage bank {} — {}", stage, names::stage_name(stage));
        let visited: Vec<String> = (1..=64u8)
            .filter(|a| bank.area_visited(*a))
            .map(|a| {
                names::area_name(stage, a)
                    .map(|n| n.to_string())
                    .unwrap_or_else(|| format!("area {a}"))
            })
            .collect();
        println!(
            "  visited areas: {}",
            if visited.is_empty() {
                "none".into()
            } else {
                visited.join(", ")
            }
        );
        if stage != 3 {
            for slot in 0..64 {
                let state = bank.object_state(slot);
                if state == 0 && names::object_slot_name(stage, slot as u8).is_none() {
                    continue;
                }
                println!(
                    "  object {:2}: {:<14} {}",
                    slot,
                    names::OBJECT_STATE_LABELS[state as usize],
                    names::object_slot_name(stage, slot as u8).unwrap_or("(unnamed)")
                );
            }
        }
    }
    println!("nibble flags (named, then unnamed non-zero):");
    for flag in names::NIBBLE_FLAGS {
        let v = data.nibble(flag.index as usize);
        if v != 0 {
            println!("  {:#05x} = {:2}  {}", flag.index, v, flag.name);
        }
    }
    let unnamed: Vec<String> = (0..crate::edit::NIBBLE_FLAG_COUNT)
        .filter(|i| !crate::edit::NIBBLE_PLAY_TIME_POSITIONS.contains(i))
        // The objective is a whole byte: its low nibble belongs to 0x0A2.
        .filter(|i| *i != names::OBJECTIVE_FLAG as usize + 1)
        .filter(|i| names::nibble_flag(*i as u16).is_none() && data.nibble(*i) != 0)
        .map(|i| format!("{:#05x}={}", i, data.nibble(i)))
        .collect();
    println!(
        "  unnamed: {}",
        if unnamed.is_empty() {
            "none".into()
        } else {
            unnamed.join(" ")
        }
    );
    println!("  play-time mark: {} min", data.play_time_mark());
}

fn carried_item_count(save: &SaveImage) -> usize {
    save.carried_row_indices()
        .filter(|i| save.item_row(*i).item_id != 0)
        .count()
}

pub fn run(cli: Cli) -> Result<()> {
    match cli.command.expect("main dispatches None to the GUI") {
        Command::List { path } => {
            let container = CardContainer::open(&path).context("opening container")?;
            let slots = container.slots();
            if slots.is_empty() {
                println!("no PE2 save files in {}", path.display());
            }
            for (index, file) in &slots {
                let data = SlotData::from_bytes(file.data.clone())?;
                let save = data.save()?;
                let (sections, header, summary) = data.verify();
                let status = if sections.iter().all(|ok| *ok) && header && summary {
                    "OK"
                } else {
                    "CHECKSUM MISMATCH"
                };
                println!(
                    "[{}] {} — {} [{}]",
                    index,
                    file.filename,
                    describe(&save),
                    status
                );
            }
        }
        Command::Inspect {
            path,
            slot,
            verbose,
            flags,
        } => {
            let (_, index, data) = open_slot(&path, slot)?;
            let save = data.save()?;
            println!("slot {}", index);
            println!("{}", describe(&save));
            print_inspect(&data, &save, verbose, flags);
        }
        Command::Items { category } => {
            for (id, name, cat) in names::ITEM_NAMES {
                if let Some(want) = &category {
                    let want = want.to_lowercase();
                    if !cat.label().to_lowercase().contains(&want) {
                        continue;
                    }
                }
                println!("{:#05x}  {:<24} {}", id, name, cat.label());
            }
        }
        Command::Verify { path, slot } => {
            let (_, index, data) = open_slot(&path, slot)?;
            let (sections, header, summary) = data.verify();
            let names = [
                "save",
                "player",
                "akropolis",
                "dryfield",
                "dryfield-night",
                "mine-shelter",
                "neo-ark",
                "nibble",
            ];
            let mut all_ok = header && summary;
            for (ok, name) in sections.iter().zip(names) {
                println!(
                    "section {:<16}: {}",
                    name,
                    if *ok { "OK" } else { "MISMATCH" }
                );
                all_ok &= *ok;
            }
            println!(
                "header checksum : {}",
                if header { "OK" } else { "MISMATCH" }
            );
            println!(
                "section summary : {}",
                if summary { "OK" } else { "MISMATCH" }
            );
            if !all_ok {
                bail!("slot {} has checksum mismatches", index);
            }
        }
        Command::Edit(args) => run_edit(args)?,
        Command::Export { path, slot, out } => {
            let (_, _, data) = open_slot(&path, slot)?;
            let transfer = data.save()?.to_transfer();
            std::fs::write(&out, serde_json::to_string_pretty(&transfer)?)
                .with_context(|| format!("writing {}", out.display()))?;
            println!("exported to {}", out.display());
        }
        Command::Import { path, slot, input } => {
            let json = std::fs::read_to_string(&input)
                .with_context(|| format!("reading {}", input.display()))?;
            let transfer: SaveTransfer =
                serde_json::from_str(&json).context("parsing transfer JSON")?;
            let (mut container, index, mut data) = open_slot(&path, slot)?;
            data.edit(|save| save.apply_transfer(&transfer))
                .expect("edit cannot fail");
            container.write_slot(index, &data.data)?;
            container.save_to_path(&path)?;
            println!("imported into slot {}", index);
        }
        Command::Extract { path, slot, out } => {
            let (_, _, data) = open_slot(&path, slot)?;
            write_slot_file(&out, &data.data)?;
            println!("extracted to {}", out.display());
        }
    }
    Ok(())
}

/// Supported `--set field=value` names for the edit command.
fn apply_field(save: &mut SaveImage, field: &str, value: &str) -> Result<()> {
    macro_rules! int_field {
        ($name:literal, $setter:ident, $ty:ty) => {
            if field == $name {
                let v: $ty = value.parse().context(concat!("bad value for ", $name))?;
                save.$setter(v);
                return Ok(());
            }
        };
    }
    macro_rules! bool_field {
        ($name:literal, $setter:ident) => {
            if field == $name {
                let v = matches!(value, "1" | "true" | "yes" | "on");
                save.$setter(v);
                return Ok(());
            }
        };
    }
    int_field!("play-time", set_play_time_minutes, u16);
    int_field!("exp", set_player_exp, i32);
    int_field!("bp", set_player_bp, i32);
    int_field!("max-exp", set_max_exp, i32);
    int_field!("max-bp", set_max_bp, i32);
    int_field!("hp-bonus", set_hp_bonus, u8);
    int_field!("mp-bonus", set_mp_bonus, u8);
    int_field!("battles-won", set_battles_won, u16);
    int_field!("battles-escaped", set_battles_escaped, u16);
    int_field!("companion-hp", set_companion_hp, i16);
    int_field!("companion-hp-max", set_companion_hp_max, i16);
    int_field!("shop-tiers", set_shop_tiers, u32);
    int_field!("shop-stock", set_shop_stock, u32);
    int_field!("save-number", set_save_number, i8);
    int_field!("save-point", set_save_point, u8);
    int_field!("clear-count", set_clear_count, i8);
    int_field!("game-mode", set_game_mode, u8);
    int_field!("save-count", set_save_count, u8);
    int_field!("replay-rank", set_replay_rank, i8);
    int_field!("companion-type", set_companion_type, i8);
    int_field!("companion-variant", set_companion_variant, i8);
    int_field!("character-id", set_character_id, i8);
    int_field!("button-layout", set_button_layout, u8);
    int_field!("music-volume", set_music_volume, u8);
    bool_field!("vibration-off", set_vibration_off);
    bool_field!("run", set_run_default);
    bool_field!("mono", set_mono_sound);
    bool_field!("cursor-reset", set_cursor_reset);
    bool_field!("interlace", set_interlaced);
    bool_field!("cheat", set_cheat_mode);
    bool_field!("freeze-actors", set_actors_frozen);
    if field == "mark-items-seen" {
        let max: usize = value.parse().context("bad value for mark-items-seen")?;
        save.mark_items_seen(max);
        return Ok(());
    }
    bail!("unknown field {:?}; supported: play-time, exp, bp, max-exp, max-bp, hp-bonus, mp-bonus, battles-won, battles-escaped, companion-hp, companion-hp-max, shop-tiers, shop-stock, save-number, save-point, clear-count, game-mode, save-count, replay-rank, companion-type, companion-variant, character-id, button-layout, music-volume, vibration-off, run, mono, cursor-reset, interlace, cheat, freeze-actors, mark-items-seen", field)
}

fn parse_item_arg(text: &str) -> Result<u16> {
    names::parse_item(text)
        .ok_or_else(|| anyhow::anyhow!("unknown item {text:?}; run `items` for the catalog"))
}

fn parse_num(text: &str) -> Result<usize> {
    let t = text.trim();
    Ok(match t.strip_prefix("0x") {
        Some(hex) => usize::from_str_radix(hex, 16)?,
        None => t.parse()?,
    })
}

/// Apply every requested change to `data`; any error leaves the caller's copy untouched.
fn apply_edits(data: &mut SlotData, args: &EditArgs) -> Result<()> {
    let mut probe = SaveImage::blank();
    for pair in &args.sets {
        let (field, value) = pair
            .split_once('=')
            .ok_or_else(|| anyhow::anyhow!("expected field=value, got {:?}", pair))?;
        apply_field(&mut probe, field, value)?;
    }
    data.edit(|save| {
        for pair in &args.sets {
            let (field, value) = pair.split_once('=').expect("validated above");
            apply_field(save, field, value).expect("validated above");
        }
    })?;
    for spec in &args.add_items {
        let (item, qty) = match spec.rsplit_once(':') {
            Some((item, qty)) if qty.trim().parse::<u16>().is_ok() => {
                (item, qty.trim().parse::<u16>().unwrap())
            }
            _ => (spec.as_str(), 1),
        };
        let id = parse_item_arg(item)?;
        match names::item_category(id) {
            Some(c) if c.is_row_item() && id <= 0xFF => {}
            Some(names::ItemCategory::KeyItem) => {
                bail!(
                    "{} is a key item; use --add-key-item",
                    names::item_label(id)
                )
            }
            _ => bail!(
                "{} cannot be held as an inventory row",
                names::item_label(id)
            ),
        }
        let mut result = Ok(0);
        data.edit(|save| result = save.give_item(id as u8, qty))?;
        let row = result?;
        println!("added {} x{} (row {})", names::item_label(id), qty, row);
    }
    for spec in &args.remove_items {
        let id = parse_item_arg(spec)?;
        if id == 0 || id > 0xFF {
            bail!("{} is not a row item", names::item_label(id));
        }
        let mut cleared = 0;
        data.edit(|save| cleared = save.remove_item(id as u8))?;
        if cleared == 0 {
            bail!("{} is not in the carried window", names::item_label(id));
        }
        println!("removed {} ({} rows)", names::item_label(id), cleared);
    }
    for (specs, held) in [(&args.add_key_items, true), (&args.remove_key_items, false)] {
        for spec in specs {
            let id = parse_item_arg(spec)?;
            data.set_key_item(id, held)?;
            println!(
                "{} key item {}",
                if held { "gave" } else { "took" },
                names::item_label(id)
            );
        }
    }
    if let Some(spec) = &args.equip {
        if spec.eq_ignore_ascii_case("none") {
            data.set_equipped_weapon(None, None)?;
        } else {
            let (weapon, ammo) = match spec.split_once(':') {
                Some((w, a)) => (w, Some(a)),
                None => (spec.as_str(), None),
            };
            let weapon = parse_item_arg(weapon)?;
            if !(0x80..=0x9F).contains(&weapon) {
                bail!("{} is not a weapon", names::item_label(weapon));
            }
            let ammo = match ammo {
                Some(a) => Some(parse_item_arg(a)? as u8),
                None => {
                    let load = data.save()?.weapon_load((weapon - 0x80) as usize);
                    (load.primary_item_id != 0).then_some(load.primary_item_id)
                }
            };
            data.set_equipped_weapon(Some(weapon as u8), ammo)?;
        }
    }
    for pair in &args.set_flags {
        let (index, value) = pair
            .split_once('=')
            .ok_or_else(|| anyhow::anyhow!("expected INDEX=VALUE, got {:?}", pair))?;
        let index = parse_num(index)?;
        let value = parse_num(value)?;
        if index >= crate::edit::NIBBLE_FLAG_COUNT
            || crate::edit::NIBBLE_PLAY_TIME_POSITIONS.contains(&index)
            || value > 15
        {
            bail!("flag {index:#x}={value} out of range (index 0..0x1F7 except the play-time mark 0x68..0x6B, value 0..15)");
        }
        data.set_nibble(index, value as u8);
    }
    Ok(())
}

fn run_edit(args: EditArgs) -> Result<()> {
    if args.fields {
        println!("play-time exp bp max-exp max-bp hp-bonus mp-bonus battles-won battles-escaped");
        println!("companion-hp companion-hp-max shop-tiers shop-stock save-number save-point");
        println!("clear-count game-mode save-count replay-rank companion-type companion-variant");
        println!("character-id button-layout music-volume vibration-off run mono cursor-reset");
        println!("interlace cheat freeze-actors mark-items-seen");
        println!("(items: --add-item, --remove-item, --add-key-item, --remove-key-item, --equip, --set-flag)");
        return Ok(());
    }
    let nothing = args.sets.is_empty()
        && args.add_items.is_empty()
        && args.remove_items.is_empty()
        && args.add_key_items.is_empty()
        && args.remove_key_items.is_empty()
        && args.equip.is_none()
        && args.set_flags.is_empty();
    if nothing {
        bail!("nothing to change; pass --set field=value, --add-item, --add-key-item, --equip or --set-flag (or --fields for the list)");
    }
    let (mut container, index, data) = open_slot(&args.path, args.slot)?;
    // Work on a copy so a failing change never partially applies or persists.
    let mut work = data.clone();
    apply_edits(&mut work, &args)?;
    container.write_slot(index, &work.data)?;
    container.save_to_path(&args.path)?;
    println!("edited slot {}", index);
    Ok(())
}
