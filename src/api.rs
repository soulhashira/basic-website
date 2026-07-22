//! JSON API handlers — a behavioral port of the Node server's `/api/*` routes.
//! Documents are kept as `serde_json::Value` so unknown fields in existing
//! files pass through untouched, exactly like `JSON.parse`/`JSON.stringify`.

use std::collections::HashMap;
use std::sync::Arc;

use axum::extract::{Path, Query, State};
use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::Json;
use serde_json::{json, Value};

use crate::pages::html_404;
use crate::store;
use crate::AppState;

const ALLOWED_FONTS: [&str; 5] = [
    "Inter",
    "Merriweather",
    "JetBrains Mono",
    "Lora",
    "Space Grotesk",
];

fn err(status: StatusCode, msg: &str) -> Response {
    (status, Json(json!({ "error": msg }))).into_response()
}

fn today() -> String {
    chrono::Utc::now().format("%Y-%m-%d").to_string()
}

/// Node used truthiness (`if (data.title)`), so absent and `""` are equivalent.
fn nonempty<'a>(data: &'a Value, key: &str) -> Option<&'a str> {
    data.get(key).and_then(Value::as_str).filter(|s| !s.is_empty())
}

// ── Posts ────────────────────────────────────────────────────────────────

pub async fn list_posts(
    State(st): State<Arc<AppState>>,
    Query(params): Query<HashMap<String, String>>,
) -> Response {
    let mut posts = store::all_posts(&st.posts_dir).await;

    if let Some(wiki) = params.get("wiki").filter(|w| !w.is_empty()) {
        posts.retain(|p| p.get("wiki").and_then(Value::as_str) == Some(wiki.as_str()));
    }
    if let Some(q) = params.get("q").filter(|q| !q.is_empty()) {
        let q = q.to_lowercase();
        posts.retain(|p| {
            ["title", "excerpt", "body"]
                .iter()
                .any(|k| store::str_field(p, k).to_lowercase().contains(&q))
        });
    }
    Json(posts).into_response()
}

pub async fn create_post(State(st): State<Arc<AppState>>, body: String) -> Response {
    let Ok(data) = serde_json::from_str::<Value>(&body) else {
        return err(StatusCode::BAD_REQUEST, "Invalid JSON");
    };

    let title = nonempty(&data, "title");
    let post_body = nonempty(&data, "body");
    let (Some(title), Some(post_body)) = (title, post_body) else {
        return err(StatusCode::BAD_REQUEST, "Title and body are required");
    };

    let slug = store::slugify(title);
    if slug.is_empty() {
        return err(
            StatusCode::BAD_REQUEST,
            "Title must contain at least one letter or number",
        );
    }

    let file = st.posts_dir.join(format!("{slug}.json"));
    if tokio::fs::try_exists(&file).await.unwrap_or(false) {
        return err(StatusCode::CONFLICT, "A post with that title already exists");
    }

    let font = match nonempty(&data, "font") {
        Some(f) if ALLOWED_FONTS.contains(&f) => f,
        _ => "Inter",
    };
    let excerpt = match nonempty(&data, "excerpt") {
        Some(e) => e.to_string(),
        None => {
            let mut s: String = post_body.chars().take(120).collect();
            s.push_str("...");
            s
        }
    };
    let wiki = match nonempty(&data, "wiki") {
        Some(w) => Value::String(w.to_string()),
        None => Value::Null,
    };

    let mut post = json!({
        "title": title,
        "date": today(),
        "excerpt": excerpt,
        "body": post_body,
        "font": font,
        "wiki": wiki,
    });

    if store::write_json(&file, &post).await.is_err() {
        return err(StatusCode::INTERNAL_SERVER_ERROR, "Failed to save post");
    }

    post.as_object_mut()
        .expect("post is an object")
        .insert("slug".into(), Value::String(slug));
    (StatusCode::CREATED, Json(post)).into_response()
}

pub async fn get_post(State(st): State<Arc<AppState>>, Path(slug): Path<String>) -> Response {
    if !store::valid_slug(&slug) {
        return html_404(); // Node's route regex wouldn't match → static 404
    }
    match store::load(&st.posts_dir, &slug).await {
        Some(post) => Json(post).into_response(),
        None => err(StatusCode::NOT_FOUND, "Post not found"),
    }
}

pub async fn update_post(
    State(st): State<Arc<AppState>>,
    Path(slug): Path<String>,
    body: String,
) -> Response {
    if !store::valid_slug(&slug) {
        return html_404();
    }
    let file = st.posts_dir.join(format!("{slug}.json"));
    let Some(mut existing) = store::read_json(&file).await else {
        return err(StatusCode::NOT_FOUND, "Post not found");
    };
    let Ok(data) = serde_json::from_str::<Value>(&body) else {
        return err(StatusCode::BAD_REQUEST, "Invalid JSON");
    };

    let doc = existing.as_object_mut().expect("post file is an object");
    for key in ["title", "excerpt", "body"] {
        if let Some(v) = nonempty(&data, key) {
            doc.insert(key.into(), Value::String(v.to_string()));
        }
    }
    if let Some(f) = nonempty(&data, "font") {
        if ALLOWED_FONTS.contains(&f) {
            doc.insert("font".into(), Value::String(f.to_string()));
        }
    }
    // Node checked `"wiki" in data` — key presence, not truthiness.
    if data.get("wiki").is_some() {
        let wiki = match nonempty(&data, "wiki") {
            Some(w) => Value::String(w.to_string()),
            None => Value::Null,
        };
        doc.insert("wiki".into(), wiki);
    }

    if store::write_json(&file, &existing).await.is_err() {
        return err(StatusCode::INTERNAL_SERVER_ERROR, "Failed to save post");
    }

    existing
        .as_object_mut()
        .expect("post file is an object")
        .insert("slug".into(), Value::String(slug));
    Json(existing).into_response()
}

pub async fn delete_post(State(st): State<Arc<AppState>>, Path(slug): Path<String>) -> Response {
    if !store::valid_slug(&slug) {
        return html_404();
    }
    let file = st.posts_dir.join(format!("{slug}.json"));
    if tokio::fs::remove_file(&file).await.is_err() {
        return err(StatusCode::NOT_FOUND, "Post not found");
    }
    Json(json!({ "deleted": slug })).into_response()
}

// ── Wikis ────────────────────────────────────────────────────────────────

pub async fn list_wikis(State(st): State<Arc<AppState>>) -> Response {
    Json(store::all_wikis(&st.wikis_dir).await).into_response()
}

pub async fn create_wiki(State(st): State<Arc<AppState>>, body: String) -> Response {
    let Ok(data) = serde_json::from_str::<Value>(&body) else {
        return err(StatusCode::BAD_REQUEST, "Invalid JSON");
    };
    let Some(name) = nonempty(&data, "name") else {
        return err(StatusCode::BAD_REQUEST, "Name is required");
    };

    let slug = store::slugify(name);
    if slug.is_empty() {
        return err(
            StatusCode::BAD_REQUEST,
            "Name must contain at least one letter or number",
        );
    }

    let file = st.wikis_dir.join(format!("{slug}.json"));
    if tokio::fs::try_exists(&file).await.unwrap_or(false) {
        return err(StatusCode::CONFLICT, "A wiki with that name already exists");
    }

    let mut wiki = json!({
        "name": name,
        "description": data.get("description").and_then(Value::as_str).unwrap_or(""),
        "created": today(),
    });

    if store::write_json(&file, &wiki).await.is_err() {
        return err(StatusCode::INTERNAL_SERVER_ERROR, "Failed to save wiki");
    }

    wiki.as_object_mut()
        .expect("wiki is an object")
        .insert("slug".into(), Value::String(slug));
    (StatusCode::CREATED, Json(wiki)).into_response()
}

pub async fn get_wiki(State(st): State<Arc<AppState>>, Path(slug): Path<String>) -> Response {
    if !store::valid_slug(&slug) {
        return html_404();
    }
    let Some(mut wiki) = store::load(&st.wikis_dir, &slug).await else {
        return err(StatusCode::NOT_FOUND, "Wiki not found");
    };

    let posts: Vec<Value> = store::all_posts(&st.posts_dir)
        .await
        .into_iter()
        .filter(|p| p.get("wiki").and_then(Value::as_str) == Some(slug.as_str()))
        .collect();
    wiki.as_object_mut()
        .expect("wiki file is an object")
        .insert("posts".into(), Value::Array(posts));
    Json(wiki).into_response()
}
