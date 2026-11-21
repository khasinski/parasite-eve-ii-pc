use pe2_save_editor::{
    edit::SlotData,
    layout::{FILE_SIZE, PLAYER_SECTION},
};
use std::{fs, process::Command};

#[test]
fn cli_edit_persists_player_currency_in_standalone_file() {
    let root = std::env::temp_dir().join(format!("pe2-cli-persist-{}", std::process::id()));
    fs::create_dir_all(&root).unwrap();
    let path = root.join("copy.slot");
    let mut data = vec![0; FILE_SIZE];
    data[..2].copy_from_slice(b"SC");
    fs::write(&path, data).unwrap();
    let output = Command::new(env!("CARGO_BIN_EXE_pe2-save-editor"))
        .arg("edit")
        .arg(&path)
        .args(["--set", "bp=4321", "--set", "exp=777"])
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let slot = SlotData::from_bytes(fs::read(&path).unwrap()).unwrap();
    assert_eq!(slot.save().unwrap().player_bp(), 4321);
    let offset = PLAYER_SECTION.live_offset;
    assert_eq!(&slot.data[offset + 8..offset + 12], &777i32.to_le_bytes());
    assert_eq!(&slot.data[offset + 12..offset + 16], &4321i32.to_le_bytes());
    let (sections, header, summary) = slot.verify();
    assert!(sections.iter().all(|ok| *ok) && header && summary);
    fs::remove_dir_all(root).unwrap();
}

fn run(args: &[&str], path: &std::path::Path) -> String {
    let output = Command::new(env!("CARGO_BIN_EXE_pe2-save-editor"))
        .arg(args[0])
        .arg(path)
        .args(&args[1..])
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{:?}: {}",
        args,
        String::from_utf8_lossy(&output.stderr)
    );
    String::from_utf8_lossy(&output.stdout).into_owned()
}

#[test]
fn cli_named_items_key_items_and_weapons_round_trip() {
    let root = std::env::temp_dir().join(format!("pe2-cli-named-{}", std::process::id()));
    fs::create_dir_all(&root).unwrap();
    let path = root.join("copy.slot");
    let mut data = vec![0; FILE_SIZE];
    data[..2].copy_from_slice(b"SC");
    // Akropolis Square, carried window rows 0..20, as a fresh Akropolis save has it.
    let save = pe2_save_editor::layout::SAVE_SECTION.live_offset;
    data[save + 4..save + 10].copy_from_slice(&[6, 1, 1, 1, 1, 1]);
    data[save + 0x5BC..save + 0x5BE].copy_from_slice(&[0, 20]);
    fs::write(&path, data).unwrap();
    run(
        &[
            "edit",
            "--add-item",
            "M93R",
            "--add-item",
            "9mm P.B.:100",
            "--add-item",
            "0x02",
            "--add-key-item",
            "MIST Badge",
            "--add-key-item",
            "monkey wrench",
            "--equip",
            "M93R:9mm P.B.",
            "--set-flag",
            "0x7A=1",
        ],
        &path,
    );
    let slot = SlotData::from_bytes(fs::read(&path).unwrap()).unwrap();
    let (sections, header, summary) = slot.verify();
    assert!(sections.iter().all(|ok| *ok) && header && summary);
    let image = slot.save().unwrap();
    assert!(image.has_key_item(0x106) && image.has_key_item(0x111));
    assert_eq!(slot.player().weapon, Some(0x81));
    assert_eq!(slot.player().weapon_slot_item, Some(0xA0));
    assert_eq!(slot.nibble(0x7A), 1);
    let text = run(&["inspect"], &path);
    for needle in [
        "Akropolis Tower / MIST - Akropolis Square",
        "M93R",
        "9mm P.B.",
        "x100",
        "Recovery2",
        "MIST Badge",
        "Monkey Wrench",
        "equipped: weapon M93R (0x81) | ammo 9mm P.B. (0xa0)",
        "story: chapter Akropolis",
    ] {
        assert!(text.contains(needle), "inspect lacks {needle:?}:\n{text}");
    }
    run(
        &[
            "edit",
            "--remove-key-item",
            "0x111",
            "--remove-item",
            "recovery2",
        ],
        &path,
    );
    let slot = SlotData::from_bytes(fs::read(&path).unwrap()).unwrap();
    assert!(!slot.save().unwrap().has_key_item(0x111));
    let text = run(&["inspect", "--flags"], &path);
    assert!(!text.contains("Recovery2"));
    assert!(text.contains("EVENT_FLAG_STORY_CHAPTER"));
    // Unknown names fail without touching the file.
    let before = fs::read(&path).unwrap();
    let output = Command::new(env!("CARGO_BIN_EXE_pe2-save-editor"))
        .args(["edit"])
        .arg(&path)
        .args(["--add-item", "M93R", "--add-item", "no such thing"])
        .output()
        .unwrap();
    assert!(!output.status.success());
    assert_eq!(fs::read(&path).unwrap(), before);
    let catalog = Command::new(env!("CARGO_BIN_EXE_pe2-save-editor"))
        .args(["items", "--category", "key"])
        .output()
        .unwrap();
    assert!(String::from_utf8_lossy(&catalog.stdout).contains("Parthenon Key"));
    fs::remove_dir_all(root).unwrap();
}
