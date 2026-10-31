// SPDX-License-Identifier: GPL-3.0-or-later
//! Round-trip tests for image parsing, cpio handling and vendor_boot patching.
//!
//! The images are assembled here from the on-disk layout documented in the
//! AOSP `bootimg.h`, independently of the bindgen structs the parser uses.

use bootparse::compress::{CompressFormat, compress, decompress};
use bootparse::cpio::{Cpio, CpioEntry};
use bootparse::parse::BootImage;
use bootparse::patcher::VendorBootPatcher;

const PAGE: usize = 4096;

fn pad_to_page(buf: &mut Vec<u8>) {
    while buf.len() % PAGE != 0 {
        buf.push(0);
    }
}

fn put_u32(buf: &mut [u8], offset: usize, value: u32) {
    buf[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

fn put_u64(buf: &mut [u8], offset: usize, value: u64) {
    buf[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
}

fn sample_cpio(extra: &str) -> Vec<u8> {
    let mut cpio = Cpio::new();
    cpio.add("init", CpioEntry::regular(0o755, b"init".to_vec()))
        .unwrap();
    cpio.add("lib", CpioEntry::dir(0o755)).unwrap();
    cpio.add("lib/modules", CpioEntry::dir(0o755)).unwrap();
    cpio.add(
        "lib/modules/modules.load",
        CpioEntry::regular(0o644, extra.as_bytes().to_vec()),
    )
    .unwrap();
    cpio.dump()
}

/// v4 vendor_boot image with `ramdisks` as the vendor ramdisk section.
fn build_vendor_boot_v4(ramdisks: &[Vec<u8>], dtb: &[u8], bootconfig: &[u8], avb: bool) -> Vec<u8> {
    let mut section = Vec::new();
    let mut table = Vec::new();

    for (index, ramdisk) in ramdisks.iter().enumerate() {
        table.extend_from_slice(&(ramdisk.len() as u32).to_le_bytes());
        table.extend_from_slice(&(section.len() as u32).to_le_bytes());
        table.extend_from_slice(&1u32.to_le_bytes()); // VENDOR_RAMDISK_TYPE_PLATFORM
        let mut name = [0u8; 32];
        let text = format!("ramdisk{index}");
        name[..text.len()].copy_from_slice(text.as_bytes());
        table.extend_from_slice(&name);
        table.extend_from_slice(&[0u8; 64]); // board_id
        section.extend_from_slice(ramdisk);
    }

    let mut image = vec![0u8; PAGE];
    image.extend_from_slice(&section);
    pad_to_page(&mut image);
    image.extend_from_slice(dtb);
    pad_to_page(&mut image);
    let table_offset = image.len();
    image.extend_from_slice(&table);
    pad_to_page(&mut image);
    let bootconfig_offset = image.len();
    image.extend_from_slice(bootconfig);
    pad_to_page(&mut image);

    image[0..8].copy_from_slice(b"VNDRBOOT");
    put_u32(&mut image, 8, 4); // header_version
    put_u32(&mut image, 12, PAGE as u32); // page_size
    put_u32(&mut image, 16, 0x1000_0000); // kernel_addr
    put_u32(&mut image, 20, 0x1100_0000); // ramdisk_addr
    put_u32(&mut image, 24, section.len() as u32); // vendor_ramdisk_size
    put_u32(&mut image, 2076, 0x1000_1000); // tags_addr
    put_u32(&mut image, 2096, 2128); // header_size
    put_u32(&mut image, 2100, dtb.len() as u32); // dtb_size
    put_u64(&mut image, 2104, 0x1200_0000); // dtb_addr
    put_u32(&mut image, 2112, table.len() as u32); // vendor_ramdisk_table_size
    put_u32(&mut image, 2116, ramdisks.len() as u32); // entry_num
    put_u32(&mut image, 2120, 108); // entry_size
    put_u32(&mut image, 2124, bootconfig.len() as u32); // bootconfig_size

    let _ = table_offset;
    let _ = bootconfig_offset;

    if avb {
        append_avb(&mut image, 256);
    }
    image
}

fn append_avb(image: &mut Vec<u8>, vbmeta_size: usize) {
    let original_image_size = image.len();
    let vbmeta_offset = (image.len() + PAGE - 1) / PAGE * PAGE;

    image.resize(vbmeta_offset, 0);
    image.extend_from_slice(b"AVB0");
    image.resize(vbmeta_offset + vbmeta_size, 0);
    // Slack so the patched payload still fits before the vbmeta blob.
    image.resize(image.len() + 4 * PAGE, 0);

    let mut footer = [0u8; 64];
    footer[0..4].copy_from_slice(b"AVBf");
    footer[12..20].copy_from_slice(&(original_image_size as u64).to_be_bytes());
    footer[20..28].copy_from_slice(&(vbmeta_offset as u64).to_be_bytes());
    footer[28..36].copy_from_slice(&(vbmeta_size as u64).to_be_bytes());
    image.extend_from_slice(&footer);
}

/// v3 vendor_boot image with a single ramdisk.
fn build_vendor_boot_v3(ramdisk: &[u8], dtb: &[u8]) -> Vec<u8> {
    let mut image = vec![0u8; PAGE];
    image.extend_from_slice(ramdisk);
    pad_to_page(&mut image);
    image.extend_from_slice(dtb);
    pad_to_page(&mut image);

    image[0..8].copy_from_slice(b"VNDRBOOT");
    put_u32(&mut image, 8, 3);
    put_u32(&mut image, 12, PAGE as u32);
    put_u32(&mut image, 24, ramdisk.len() as u32);
    put_u32(&mut image, 2096, 2112);
    put_u32(&mut image, 2100, dtb.len() as u32);
    put_u64(&mut image, 2104, 0x1200_0000);
    image
}

/// v3 boot image with a gzip kernel and a gzip ramdisk.
fn build_boot_v3(kernel: &[u8], ramdisk: &[u8]) -> Vec<u8> {
    let mut image = vec![0u8; PAGE];
    image.extend_from_slice(kernel);
    pad_to_page(&mut image);
    image.extend_from_slice(ramdisk);
    pad_to_page(&mut image);

    image[0..8].copy_from_slice(b"ANDROID!");
    put_u32(&mut image, 8, kernel.len() as u32);
    put_u32(&mut image, 12, ramdisk.len() as u32);
    put_u32(&mut image, 20, 1580); // header_size
    put_u32(&mut image, 40, 3); // header_version
    image
}

#[test]
fn cpio_round_trip() {
    let archive = sample_cpio("nksu.ko\n");
    let cpio = Cpio::load_from_data(&archive).unwrap();

    assert!(cpio.exists("lib/modules"));
    assert_eq!(
        cpio.get("lib/modules/modules.load").unwrap().data(),
        Some(b"nksu.ko\n".as_slice())
    );
    assert!(cpio.get("lib/modules").unwrap().is_dir());

    let reparsed = Cpio::load_from_data(&cpio.dump()).unwrap();
    assert_eq!(reparsed.entries().len(), cpio.entries().len());
}

#[test]
fn cpio_concatenated_archives_are_merged() {
    let mut data = sample_cpio("first\n");
    data.extend_from_slice(&sample_cpio("second\n"));

    let cpio = Cpio::load_from_data(&data).unwrap();
    assert_eq!(
        cpio.get("lib/modules/modules.load").unwrap().data(),
        Some(b"second\n".as_slice())
    );
}

#[test]
fn vendor_boot_v4_parses_its_table() {
    let ramdisks = vec![
        compress(&sample_cpio("a\n"), CompressFormat::GZIP).unwrap(),
        b"not-a-cpio".to_vec(),
    ];
    let image = build_vendor_boot_v4(&ramdisks, b"dtb", b"bootconfig", false);

    let parsed = BootImage::parse(&image).unwrap();
    assert_eq!(parsed.vendor_boot_version(), Some(4));

    let entries = parsed.ramdisks().unwrap();
    assert_eq!(entries.len(), 2);
    assert_eq!(entries[0].name, "ramdisk0");
    assert_eq!(entries[0].format(), CompressFormat::GZIP);
    assert_eq!(entries[0].decompress().unwrap(), sample_cpio("a\n"));
    assert_eq!(entries[1].data, b"not-a-cpio");
}

#[test]
fn vendor_boot_v4_patch_rewrites_table_and_keeps_other_blocks() {
    let original = vec![
        compress(&sample_cpio("a\n"), CompressFormat::GZIP).unwrap(),
        b"second".to_vec(),
    ];
    let image = build_vendor_boot_v4(&original, b"dtb-payload", b"bootconfig", false);

    let replacement = sample_cpio("replaced\n");
    let boot = BootImage::parse(&image).unwrap();
    let mut patcher = VendorBootPatcher::new(&boot).unwrap();
    patcher.replace_vendor_ramdisk(0, replacement.clone());
    let patched = patcher.patch().unwrap();

    let parsed = BootImage::parse(&patched).unwrap();
    let entries = parsed.ramdisks().unwrap();
    assert_eq!(entries.len(), 2);
    assert_eq!(entries[0].decompress().unwrap(), replacement);
    assert_eq!(entries[1].data, b"second");
    assert_eq!(parsed.dtb().unwrap().raw(), b"dtb-payload");
    assert_eq!(parsed.bootconfig().unwrap().raw(), b"bootconfig");
    // The untouched entry is copied verbatim; the replaced one is recompressed
    // with its original format.
    assert_eq!(entries[0].format(), CompressFormat::GZIP);
}

#[test]
fn vendor_boot_v3_patch_replaces_the_single_ramdisk() {
    let original = compress(&sample_cpio("v3\n"), CompressFormat::GZIP).unwrap();
    let image = build_vendor_boot_v3(&original, b"dtb3");

    let replacement = sample_cpio("v3-replaced\n");
    let boot = BootImage::parse(&image).unwrap();
    let mut patcher = VendorBootPatcher::new(&boot).unwrap();
    patcher.replace_vendor_ramdisk(0, replacement.clone());
    let patched = patcher.patch().unwrap();

    let parsed = BootImage::parse(&patched).unwrap();
    let entries = parsed.ramdisks().unwrap();
    assert_eq!(entries.len(), 1);
    assert_eq!(entries[0].decompress().unwrap(), replacement);
    assert_eq!(parsed.dtb().unwrap().raw(), b"dtb3");
}

#[test]
fn vendor_boot_patch_preserves_avb_structures() {
    let ramdisk = compress(&sample_cpio("avb\n"), CompressFormat::GZIP).unwrap();
    let image = build_vendor_boot_v4(&[ramdisk], b"dtb", b"", true);

    let replacement = sample_cpio("avb-replaced\n");
    let boot = BootImage::parse(&image).unwrap();
    let mut patcher = VendorBootPatcher::new(&boot).unwrap();
    patcher.replace_vendor_ramdisk(0, replacement.clone());
    let patched = patcher.patch().unwrap();

    assert_eq!(patched.len(), image.len());

    let footer = &patched[patched.len() - 64..];
    assert!(footer.starts_with(b"AVBf"));
    let vbmeta_offset = u64::from_be_bytes(footer[20..28].try_into().unwrap()) as usize;
    assert!(patched[vbmeta_offset..].starts_with(b"AVB0"));
    let original_image_size = u64::from_be_bytes(footer[12..20].try_into().unwrap()) as usize;
    assert!(original_image_size <= vbmeta_offset);

    let parsed = BootImage::parse(&patched).unwrap();
    assert_eq!(
        parsed.ramdisks().unwrap()[0].decompress().unwrap(),
        replacement
    );
}

#[test]
fn boot_image_kernel_is_decompressed() {
    let kernel = b"kernel payload".repeat(64);
    let compressed = compress(&kernel, CompressFormat::GZIP).unwrap();
    let image = build_boot_v3(&compressed, b"ramdisk");

    let parsed = BootImage::parse(&image).unwrap();
    assert_eq!(parsed.boot_version(), Some(3));
    assert_eq!(parsed.kernel().unwrap().format(), CompressFormat::GZIP);
    assert_eq!(parsed.kernel().unwrap().decompress().unwrap(), kernel);
    assert_eq!(decompress(parsed.kernel().unwrap().raw()).unwrap(), kernel);
}

#[test]
fn every_supported_format_round_trips() {
    let payload = b"nekosu ramdisk payload".repeat(200);
    for format in [
        CompressFormat::GZIP,
        CompressFormat::XZ,
        CompressFormat::LZMA,
        CompressFormat::BZIP2,
        CompressFormat::LZ4,
        CompressFormat::LZ4_LEGACY,
        CompressFormat::LZOP,
    ] {
        let compressed = compress(&payload, format).unwrap();
        assert_eq!(
            bootparse::compress::parse_compress_format(&compressed),
            format,
            "detection failed for {format:?}"
        );
        assert_eq!(decompress(&compressed).unwrap(), payload, "{format:?}");
    }
}

#[test]
fn lzop_round_trips() {
    let payload = b"nekosu lzop payload ".repeat(500);
    let compressed = compress(&payload, CompressFormat::LZOP).unwrap();

    assert_eq!(
        bootparse::compress::parse_compress_format(&compressed),
        CompressFormat::LZOP
    );
    assert!(compressed.starts_with(b"\x89LZO"));
    assert_eq!(decompress(&compressed).unwrap(), payload);
}

/// Cross-checks both directions against the reference `lzop` tool.  Skipped
/// when the tool is not installed (CI only needs the self-contained tests).
#[test]
fn lzop_interops_with_the_reference_tool() {
    let payload = b"nekosu lzop interop payload ".repeat(2000);
    let compressed = compress(&payload, CompressFormat::LZOP).unwrap();

    let dir = std::env::temp_dir().join(format!("bootparse-lzop-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let lzo_path = dir.join("ours.lzo");
    let bin_path = dir.join("plain.bin");
    std::fs::write(&lzo_path, &compressed).unwrap();
    std::fs::write(&bin_path, &payload).unwrap();

    // What we write must be readable by the reference decompressor.
    let decoded = match std::process::Command::new("lzop")
        .arg("-d")
        .arg("-c")
        .arg(&lzo_path)
        .output()
    {
        Ok(output) => output,
        Err(_) => {
            let _ = std::fs::remove_dir_all(&dir);
            return;
        }
    };
    assert!(
        decoded.status.success(),
        "lzop -d: {}",
        String::from_utf8_lossy(&decoded.stderr)
    );
    assert_eq!(decoded.stdout, payload);

    // What the reference compressor writes must be readable by us.
    let encoded = std::process::Command::new("lzop")
        .arg("-c")
        .arg(&bin_path)
        .output()
        .unwrap();
    assert!(encoded.status.success());
    assert_eq!(decompress(&encoded.stdout).unwrap(), payload);

    let _ = std::fs::remove_dir_all(&dir);
}
