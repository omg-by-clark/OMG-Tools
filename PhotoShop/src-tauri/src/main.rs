#![cfg_attr(target_os = "windows", windows_subsystem = "windows")]

use std::{fs, io::{Read, Write}, path::{Path, PathBuf}, process::Command};

use base64::{engine::general_purpose::STANDARD, Engine as _};
use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Manager, Window};
use zip::{write::FileOptions, ZipArchive, ZipWriter};

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct LoadedProject { data_url: String, name: String, source_path: String, ocg: Option<String> }

#[derive(Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
struct Session { source_path: String, ocg: String }

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct SaveProject { source_path: String, original_data_url: String, ocg: String }

fn mime_for(path: &Path) -> &'static str {
    match path.extension().and_then(|value| value.to_str()).unwrap_or("").to_lowercase().as_str() {
        "jpg" | "jpeg" => "image/jpeg", "png" => "image/png", "webp" => "image/webp",
        "gif" => "image/gif", "bmp" => "image/bmp", _ => "application/octet-stream",
    }
}

fn mime_from_bytes(bytes: &[u8], fallback: &'static str) -> &'static str {
    if bytes.starts_with(&[0xFF, 0xD8, 0xFF]) { "image/jpeg" }
    else if bytes.starts_with(b"\x89PNG\r\n\x1a\n") { "image/png" }
    else if bytes.starts_with(b"GIF87a") || bytes.starts_with(b"GIF89a") { "image/gif" }
    else if bytes.starts_with(b"BM") { "image/bmp" }
    else if bytes.len() > 12 && &bytes[0..4] == b"RIFF" && &bytes[8..12] == b"WEBP" { "image/webp" }
    else { fallback }
}

fn extension_from_data_url(data_url: &str) -> &'static str {
    if data_url.starts_with("data:image/jpeg") { "jpg" }
    else if data_url.starts_with("data:image/png") { "png" }
    else if data_url.starts_with("data:image/webp") { "webp" }
    else if data_url.starts_with("data:image/gif") { "gif" }
    else if data_url.starts_with("data:image/bmp") { "bmp" }
    else { "png" }
}

fn app_data(app: &AppHandle) -> Result<PathBuf, String> {
    let dir = app.path().app_data_dir().map_err(|error| error.to_string())?;
    fs::create_dir_all(&dir).map_err(|error| error.to_string())?;
    Ok(dir)
}

fn project_from_image(path: &Path, ocg: Option<String>) -> Result<LoadedProject, String> {
    let bytes = fs::read(path).map_err(|error| error.to_string())?;
    Ok(LoadedProject {
        data_url: format!("data:{};base64,{}", mime_for(path), STANDARD.encode(bytes)),
        name: path.file_stem().and_then(|name| name.to_str()).unwrap_or("Untitled").to_string(),
        source_path: path.to_string_lossy().to_string(), ocg,
    })
}

fn source_from_ocg(content: &str) -> Option<PathBuf> {
    content.lines().find_map(|line| line.strip_prefix("source_path: ")).map(PathBuf::from)
}

#[tauri::command]
fn open_file_dialog() -> Option<String> {
    rfd::FileDialog::new().add_filter("PhotoShop 项目与图片", &["oimg", "ocg", "jpg", "jpeg", "png", "webp", "gif", "bmp"])
        .pick_file().map(|path| path.to_string_lossy().to_string())
}

#[tauri::command]
fn read_project(path: String) -> Result<LoadedProject, String> {
    let project_path = PathBuf::from(path);
    let extension = project_path.extension().and_then(|value| value.to_str()).unwrap_or("").to_lowercase();
    if extension == "ocg" {
        let ocg = fs::read_to_string(&project_path).map_err(|error| error.to_string())?;
        let source = source_from_ocg(&ocg).ok_or("OCG 中没有可打开的原图路径")?;
        return project_from_image(&source, Some(ocg));
    }
    if extension != "oimg" { return project_from_image(&project_path, None); }
    let file = fs::File::open(&project_path).map_err(|error| error.to_string())?;
    let mut archive = ZipArchive::new(file).map_err(|error| error.to_string())?;
    let original_extension = (0..archive.len()).find_map(|index| archive.by_index(index).ok()
        .and_then(|entry| entry.name().strip_prefix("original.").map(str::to_string))).ok_or("OIMG 中缺少原图")?;
    let mut original = Vec::new();
    archive.by_name(&format!("original.{original_extension}")).map_err(|error| error.to_string())?
        .read_to_end(&mut original).map_err(|error| error.to_string())?;
    let ocg = archive.by_name("changes.ocg").ok().and_then(|mut entry| {
        let mut content = String::new(); entry.read_to_string(&mut content).ok().map(|_| content)
    });
    let original_path = PathBuf::from(format!("original.{original_extension}"));
    Ok(LoadedProject {
        data_url: format!("data:{};base64,{}", mime_from_bytes(&original, mime_for(&original_path)), STANDARD.encode(original)),
        name: project_path.file_stem().and_then(|name| name.to_str()).unwrap_or("Untitled").to_string(),
        source_path: project_path.to_string_lossy().to_string(), ocg,
    })
}

#[tauri::command]
fn save_project(input: SaveProject) -> Result<String, String> {
    let source_path = PathBuf::from(&input.source_path);
    let extension = extension_from_data_url(&input.original_data_url);
    let payload = input.original_data_url.split_once(',').map(|(_, data)| data).ok_or("原图数据无效")?;
    let original = STANDARD.decode(payload).map_err(|error| error.to_string())?;
    let target = source_path.with_extension("oimg");
    let file = fs::File::create(&target).map_err(|error| error.to_string())?;
    let mut archive = ZipWriter::new(file);
    let options = FileOptions::default().compression_method(zip::CompressionMethod::Deflated);
    archive.start_file(format!("original.{extension}"), options).map_err(|error| error.to_string())?;
    archive.write_all(&original).map_err(|error| error.to_string())?;
    archive.start_file("changes.ocg", options).map_err(|error| error.to_string())?;
    archive.write_all(input.ocg.as_bytes()).map_err(|error| error.to_string())?;
    archive.finish().map_err(|error| error.to_string())?;
    Ok(target.to_string_lossy().to_string())
}

#[tauri::command]
fn persist_session(app: AppHandle, session: Session) -> Result<(), String> {
    let file = app_data(&app)?.join("last-session.json");
    fs::write(file, serde_json::to_vec(&session).map_err(|error| error.to_string())?).map_err(|error| error.to_string())
}

#[tauri::command]
fn load_last_session(app: AppHandle) -> Option<Session> {
    let file = app_data(&app).ok()?.join("last-session.json");
    serde_json::from_slice(&fs::read(file).ok()?).ok()
}

#[tauri::command]
fn register_oimg_association() -> Result<(), String> {
    #[cfg(target_os = "windows")]
    {
        let executable = std::env::current_exe().map_err(|error| error.to_string())?;
        let command = format!("\"{}\" \"%1\"", executable.to_string_lossy());
        for (key, value) in [
            (r"HKCU\Software\Classes\.oimg", "PhotoShop.OIMG"),
            (r"HKCU\Software\Classes\PhotoShop.OIMG", "PhotoShop Image Project"),
            (r"HKCU\Software\Classes\PhotoShop.OIMG\shell\open\command", command.as_str()),
        ] {
            Command::new("reg").args(["add", key, "/ve", "/d", value, "/f"]).output().map_err(|error| error.to_string())?;
        }
    }
    Ok(())
}

#[tauri::command]
fn launch_target() -> Option<String> { std::env::args().skip(1).find(|arg| Path::new(arg).exists()) }

#[tauri::command]
fn minimize_window(window: Window) { let _ = window.minimize(); }
#[tauri::command]
fn maximize_window(window: Window) { if window.is_maximized().unwrap_or(false) { let _ = window.unmaximize(); } else { let _ = window.maximize(); } }
#[tauri::command]
fn close_window(window: Window) { let _ = window.close(); }

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![open_file_dialog, read_project, save_project, persist_session, load_last_session, register_oimg_association, launch_target, minimize_window, maximize_window, close_window])
        .run(tauri::generate_context!()).expect("PhotoShop could not start");
}
