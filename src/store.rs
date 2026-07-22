//! File-backed store for posts and wikis: one JSON document per slug,
//! mirroring the layout the Node server used (`posts/*.json`, `wikis/*.json`).

use std::path::Path;

use serde_json::Value;

/// Matches the Node route regex `^[a-z0-9-]+$`.
pub fn valid_slug(s: &str) -> bool {
    !s.is_empty()
        && s.chars()
            .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '-')
}

/// Port of `slugify`: lowercase, collapse non-alphanumeric runs to `-`, trim.
pub fn slugify(title: &str) -> String {
    let mut out = String::with_capacity(title.len());
    let mut prev_dash = false;
    for c in title.chars() {
        let c = c.to_ascii_lowercase();
        if c.is_ascii_lowercase() || c.is_ascii_digit() {
            out.push(c);
            prev_dash = false;
        } else if !prev_dash && !out.is_empty() {
            out.push('-');
            prev_dash = true;
        }
    }
    while out.ends_with('-') {
        out.pop();
    }
    out
}

/// Read and parse one JSON document. `None` on missing file or bad JSON.
pub async fn read_json(path: &Path) -> Option<Value> {
    let raw = tokio::fs::read(path).await.ok()?;
    serde_json::from_slice(&raw).ok()
}

/// Write a document pretty-printed, like `JSON.stringify(data, null, 2)`.
///
/// Writes to a hidden temp file in the same directory, then renames over the
/// target. The rename is atomic, so concurrent readers never observe a
/// truncated or half-written document (Node got this for free by blocking
/// its only thread across the sync write).
pub async fn write_json(path: &Path, value: &Value) -> std::io::Result<()> {
    let raw = serde_json::to_string_pretty(value).expect("value is always serializable");
    let tmp = tmp_path(path);
    tokio::fs::write(&tmp, raw).await?;
    tokio::fs::rename(&tmp, path).await
}

/// `posts/foo.json` → `posts/.foo.json.tmp` — same filesystem (rename must
/// not cross devices) and invisible to `load_all`'s `*.json` filter.
fn tmp_path(path: &Path) -> std::path::PathBuf {
    let mut name = std::ffi::OsString::from(".");
    name.push(path.file_name().unwrap_or_default());
    name.push(".tmp");
    path.with_file_name(name)
}

pub enum CreateError {
    Exists,
    Io,
}

/// Create a new document, failing if one already exists. The existence check
/// is `O_CREAT|O_EXCL` at the syscall level — two concurrent creates for the
/// same slug cannot both succeed, unlike a `try_exists` + `write` pair.
pub async fn create_json(path: &Path, value: &Value) -> Result<(), CreateError> {
    match tokio::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path)
        .await
    {
        Ok(_) => {} // slot reserved; content lands via atomic rename below
        Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => {
            return Err(CreateError::Exists);
        }
        Err(_) => return Err(CreateError::Io),
    }
    if let Err(_) = write_json(path, value).await {
        let _ = tokio::fs::remove_file(path).await; // release the reservation
        return Err(CreateError::Io);
    }
    Ok(())
}

/// Load one record by slug, injecting `slug` into the returned object.
pub async fn load(dir: &Path, slug: &str) -> Option<Value> {
    if !valid_slug(slug) {
        return None;
    }
    let mut doc = read_json(&dir.join(format!("{slug}.json"))).await?;
    if let Some(map) = doc.as_object_mut() {
        map.insert("slug".into(), Value::String(slug.to_string()));
    }
    Some(doc)
}

/// Load every `*.json` record in a directory, each with `slug` injected.
/// Files that fail to parse are skipped (the Node server crashed on them).
pub async fn load_all(dir: &Path) -> Vec<Value> {
    let mut out = Vec::new();
    let Ok(mut rd) = tokio::fs::read_dir(dir).await else {
        return out;
    };
    while let Ok(Some(entry)) = rd.next_entry().await {
        let name = entry.file_name();
        let Some(name) = name.to_str() else { continue };
        let Some(slug) = name.strip_suffix(".json") else { continue };
        if let Some(doc) = load(dir, slug).await {
            out.push(doc);
        }
    }
    out
}

/// All posts, newest-first (dates are `YYYY-MM-DD`, so string order = date order).
pub async fn all_posts(dir: &Path) -> Vec<Value> {
    let mut posts = load_all(dir).await;
    posts.sort_by(|a, b| str_field(b, "date").cmp(str_field(a, "date")));
    posts
}

/// All wikis, alphabetical by name (case-insensitive, like `localeCompare`).
pub async fn all_wikis(dir: &Path) -> Vec<Value> {
    let mut wikis = load_all(dir).await;
    wikis.sort_by_key(|w| str_field(w, "name").to_lowercase());
    wikis
}

pub fn str_field<'a>(doc: &'a Value, key: &str) -> &'a str {
    doc.get(key).and_then(Value::as_str).unwrap_or("")
}
