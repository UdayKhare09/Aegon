use actix_web::{get, web, App, HttpResponse, HttpServer, Responder};
use mimalloc::MiMalloc;
use serde::Serialize;
use std::env;
use std::time::{SystemTime, UNIX_EPOCH};
use uuid::Uuid;

#[global_allocator]
static GLOBAL: MiMalloc = MiMalloc;

#[derive(Serialize)]
struct JsonMessage {
    message: &'static str,
    id: String,
    timestamp: u128,
}

#[derive(Serialize)]
struct PostInfo {
    user_id: u64,
    post_id: u64,
}

#[get("/plaintext")]
async fn plaintext() -> impl Responder {
    HttpResponse::Ok()
        .content_type("text/plain; charset=utf-8")
        .body("Hello, World!")
}

#[get("/json")]
async fn json() -> impl Responder {
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_micros();

    let msg = JsonMessage {
        message: "Hello, World!",
        id: Uuid::new_v4().to_string(),
        timestamp: now,
    };

    HttpResponse::Ok()
        .content_type("application/json; charset=utf-8")
        .json(msg)
}

#[get("/users/{id}/posts/{post_id}")]
async fn user_post(path: web::Path<(u64, u64)>) -> impl Responder {
    let (user_id, post_id) = path.into_inner();
    let info = PostInfo { user_id, post_id };

    HttpResponse::Ok()
        .content_type("application/json; charset=utf-8")
        .json(info)
}

#[actix_web::main]
async fn main() -> std::io::Result<()> {
    let args: Vec<String> = env::args().collect();
    let workers: usize = args.get(1).and_then(|s| s.parse().ok()).unwrap_or(4);
    let port: u16 = args.get(2).and_then(|s| s.parse().ok()).unwrap_or(18081);

    let mut builder = openssl::ssl::SslAcceptor::mozilla_intermediate(openssl::ssl::SslMethod::tls())
        .expect("Failed to create SSL acceptor");
    builder.set_private_key_file("benchmarks/http1_tls/certs/server.key", openssl::ssl::SslFiletype::PEM)
        .expect("Failed to set private key");
    builder.set_certificate_chain_file("benchmarks/http1_tls/certs/server.crt")
        .expect("Failed to set certificate chain");

    println!("Actix-web TLS listening on https://0.0.0.0:{} with {} workers", port, workers);

    HttpServer::new(|| {
        App::new()
            .service(plaintext)
            .service(json)
            .service(user_post)
    })
    .workers(workers)
    .bind_openssl(("0.0.0.0", port), builder)?
    .run()
    .await
}
