mod api;
mod pages;
mod store;

use std::net::SocketAddr;
use std::path::PathBuf;
use std::sync::Arc;

use axum::routing::{any, get};
use axum::Router;
use tokio::sync::Mutex;

pub struct AppState {
    pub posts_dir: PathBuf,
    pub wikis_dir: PathBuf,
    pub public_dir: PathBuf,
    pub port: u16,
    /// Serializes post read-modify-write cycles (PUT) and deletes.
    /// Creates don't need it: `store::create_json` is atomic via O_EXCL.
    pub write_lock: Mutex<()>,
}

#[tokio::main]
async fn main() {
    // Node used `__dirname` (the server file's directory); a compiled binary
    // has no such anchor, so the data root is BASE_DIR or the working
    // directory — validated loudly instead of serving empty lists.
    let base = match std::env::var_os("BASE_DIR") {
        Some(dir) => PathBuf::from(dir),
        None => std::env::current_dir().expect("cannot resolve working directory"),
    };
    for required in ["posts", "wikis", "public"] {
        if !base.join(required).is_dir() {
            eprintln!(
                "error: {}/{required} not found — run from the site root or set BASE_DIR",
                base.display()
            );
            std::process::exit(1);
        }
    }

    let port: u16 = std::env::var("PORT")
        .ok()
        .and_then(|p| p.parse().ok())
        .unwrap_or(3000);

    let state = Arc::new(AppState {
        posts_dir: base.join("posts"),
        wikis_dir: base.join("wikis"),
        public_dir: base.join("public"),
        port,
        write_lock: Mutex::new(()),
    });

    // Page routes use `any()`: the Node server matched pages on pathname
    // alone, so e.g. POST /about served the page. The bare "/wiki/",
    // "/edit/", "/post/" routes exist because `{*x}` won't match an empty
    // remainder, while Node's startsWith("/wiki/") did.
    let app = Router::new()
        .route("/api/posts", get(api::list_posts).post(api::create_post))
        .route(
            "/api/posts/{slug}",
            get(api::get_post).put(api::update_post).delete(api::delete_post),
        )
        .route("/api/wikis", get(api::list_wikis).post(api::create_wiki))
        .route("/api/wikis/{slug}", get(api::get_wiki))
        .route("/wiki", any(pages::wiki_index))
        .route("/wiki/", any(pages::wiki_view))
        .route("/wiki/{*rest}", any(pages::wiki_view))
        .route("/sim", any(pages::sim))
        .route("/explainers/{slug}", any(pages::explainer))
        .route("/edit/", any(pages::edit))
        .route("/edit/{*rest}", any(pages::edit))
        .route("/new", any(pages::new_page))
        .route("/about", any(pages::about))
        .route("/post/", any(pages::post_page_bare))
        .route("/post/{*slug}", any(pages::post_page))
        .fallback(pages::static_file)
        // Wrong method on an API route fell through to the static handler
        // in Node (e.g. PATCH /api/posts → 404 page), never a 405.
        .method_not_allowed_fallback(pages::static_file)
        .with_state(state);

    let addr = SocketAddr::from(([0, 0, 0, 0], port));
    let listener = tokio::net::TcpListener::bind(addr).await.expect("bind failed");
    println!("Blog running at http://localhost:{port}");
    axum::serve(listener, app).await.expect("server error");
}
