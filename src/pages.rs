//! HTML page routes, per-post SEO injection, and the static-file fallback.

use std::path::Component;
use std::sync::Arc;

use axum::extract::{Path, State};
use axum::http::header::{CONTENT_TYPE, HOST};
use axum::http::{HeaderMap, StatusCode, Uri};
use axum::response::{IntoResponse, Response};

use crate::store;
use crate::AppState;

/// Node emitted a bare `text/html` (no charset) on every page; axum's
/// `Html` wrapper would append `; charset=utf-8`, so headers are set by hand.
pub fn html_404() -> Response {
    (StatusCode::NOT_FOUND, [(CONTENT_TYPE, "text/html")], "<h1>404</h1>").into_response()
}

/// Serve a fixed HTML file from `public/`. Missing file = 500, like the
/// Node server's uncaught `readFileSync` throw.
async fn public_html(st: &AppState, rel: &str) -> Response {
    match tokio::fs::read(st.public_dir.join(rel)).await {
        Ok(data) => ([(CONTENT_TYPE, "text/html")], data).into_response(),
        Err(_) => StatusCode::INTERNAL_SERVER_ERROR.into_response(),
    }
}

pub async fn wiki_index(State(st): State<Arc<AppState>>) -> Response {
    public_html(&st, "wiki.html").await
}

pub async fn wiki_view(State(st): State<Arc<AppState>>) -> Response {
    public_html(&st, "wiki-view.html").await
}

pub async fn sim(State(st): State<Arc<AppState>>) -> Response {
    public_html(&st, "sim/index.html").await
}

pub async fn edit(State(st): State<Arc<AppState>>) -> Response {
    public_html(&st, "edit.html").await
}

pub async fn new_page(State(st): State<Arc<AppState>>) -> Response {
    public_html(&st, "new.html").await
}

pub async fn about(State(st): State<Arc<AppState>>) -> Response {
    public_html(&st, "about.html").await
}

pub async fn explainer(State(st): State<Arc<AppState>>, Path(slug): Path<String>) -> Response {
    if !store::valid_slug(&slug) {
        return html_404();
    }
    match tokio::fs::read(st.public_dir.join("explainers").join(format!("{slug}.html"))).await {
        Ok(data) => ([(CONTENT_TYPE, "text/html")], data).into_response(),
        Err(_) => html_404(),
    }
}

// ── /post/{slug} with server-side SEO injection ─────────────────────────

fn escape_html(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '"' => out.push_str("&quot;"),
            _ => out.push(c),
        }
    }
    out
}

fn build_post_seo(post: &serde_json::Value, slug: &str, origin: &str) -> String {
    let title = escape_html(store::str_field(post, "title"));
    let desc = escape_html(store::str_field(post, "excerpt"));
    let date = escape_html(store::str_field(post, "date"));
    let url = format!("{origin}/post/{slug}");
    [
        format!("<title>{title}</title>"),
        format!(r#"<meta name="description" content="{desc}">"#),
        format!(r#"<link rel="canonical" href="{url}">"#),
        r#"<meta property="og:type" content="article">"#.to_string(),
        format!(r#"<meta property="og:title" content="{title}">"#),
        format!(r#"<meta property="og:description" content="{desc}">"#),
        format!(r#"<meta property="og:url" content="{url}">"#),
        format!(r#"<meta property="article:published_time" content="{date}">"#),
        r#"<meta name="twitter:card" content="summary">"#.to_string(),
        format!(r#"<meta name="twitter:title" content="{title}">"#),
        format!(r#"<meta name="twitter:description" content="{desc}">"#),
    ]
    .join("\n  ")
}

pub async fn post_page(
    State(st): State<Arc<AppState>>,
    Path(slug): Path<String>,
    headers: HeaderMap,
) -> Response {
    post_page_impl(&st, &slug, &headers).await
}

/// `/post/` with no slug: Node's startsWith match served post.html untouched.
pub async fn post_page_bare(State(st): State<Arc<AppState>>, headers: HeaderMap) -> Response {
    post_page_impl(&st, "", &headers).await
}

async fn post_page_impl(st: &AppState, slug: &str, headers: &HeaderMap) -> Response {
    let Ok(mut html) =
        tokio::fs::read_to_string(st.public_dir.join("post.html")).await
    else {
        return StatusCode::INTERNAL_SERVER_ERROR.into_response();
    };

    if let Some(post) = store::load(&st.posts_dir, slug).await {
        let default_host = format!("localhost:{}", st.port);
        let host = headers
            .get(HOST)
            .and_then(|v| v.to_str().ok())
            .unwrap_or(&default_host);
        let origin = format!("http://{host}");
        html = html.replace("<title>Post</title>", &build_post_seo(&post, slug, &origin));
    }
    ([(CONTENT_TYPE, "text/html")], html).into_response()
}

// ── Static fallback ──────────────────────────────────────────────────────

fn content_type_for(path: &std::path::Path) -> &'static str {
    match path.extension().and_then(|e| e.to_str()).unwrap_or("") {
        "html" => "text/html",
        "css" => "text/css",
        "js" => "text/javascript",
        "json" => "application/json",
        "png" => "image/png",
        "jpg" => "image/jpeg",
        "svg" => "image/svg+xml",
        "ico" => "image/x-icon",
        "wasm" => "application/wasm",
        _ => "application/octet-stream",
    }
}

pub async fn static_file(State(st): State<Arc<AppState>>, uri: Uri) -> Response {
    let rel = match uri.path() {
        "/" => "/index.html",
        p => p,
    };

    // Traversal guard: only plain path segments may extend public/.
    let mut full = st.public_dir.clone();
    for comp in std::path::Path::new(rel).components() {
        match comp {
            Component::Normal(seg) => full.push(seg),
            Component::RootDir => {}
            _ => return (StatusCode::FORBIDDEN, "Forbidden").into_response(),
        }
    }

    match tokio::fs::read(&full).await {
        Ok(data) => ([(CONTENT_TYPE, content_type_for(&full))], data).into_response(),
        Err(_) => html_404(),
    }
}
