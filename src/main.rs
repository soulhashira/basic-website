mod api;
mod pages;
mod store;

use std::net::SocketAddr;
use std::path::PathBuf;
use std::sync::Arc;

use axum::routing::get;
use axum::Router;

pub struct AppState {
    pub posts_dir: PathBuf,
    pub wikis_dir: PathBuf,
    pub public_dir: PathBuf,
    pub port: u16,
}

#[tokio::main]
async fn main() {
    let base = std::env::current_dir().expect("cannot resolve working directory");
    let port: u16 = std::env::var("PORT")
        .ok()
        .and_then(|p| p.parse().ok())
        .unwrap_or(3000);

    let state = Arc::new(AppState {
        posts_dir: base.join("posts"),
        wikis_dir: base.join("wikis"),
        public_dir: base.join("public"),
        port,
    });

    let app = Router::new()
        .route("/api/posts", get(api::list_posts).post(api::create_post))
        .route(
            "/api/posts/{slug}",
            get(api::get_post).put(api::update_post).delete(api::delete_post),
        )
        .route("/api/wikis", get(api::list_wikis).post(api::create_wiki))
        .route("/api/wikis/{slug}", get(api::get_wiki))
        .route("/wiki", get(pages::wiki_index))
        .route("/wiki/{*rest}", get(pages::wiki_view))
        .route("/sim", get(pages::sim))
        .route("/explainers/{slug}", get(pages::explainer))
        .route("/edit/{*rest}", get(pages::edit))
        .route("/new", get(pages::new_page))
        .route("/about", get(pages::about))
        .route("/post/{*slug}", get(pages::post_page))
        .fallback(pages::static_file)
        .with_state(state);

    let addr = SocketAddr::from(([0, 0, 0, 0], port));
    let listener = tokio::net::TcpListener::bind(addr).await.expect("bind failed");
    println!("Blog running at http://localhost:{port}");
    axum::serve(listener, app).await.expect("server error");
}
