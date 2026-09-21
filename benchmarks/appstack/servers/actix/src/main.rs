use actix_web::http::header::{ContentType, HeaderValue, SERVER};
use actix_web::{web, App, HttpResponse, HttpServer};
use mimalloc::MiMalloc;
use serde::Deserialize;
use std::env;

#[global_allocator]
static GLOBAL: MiMalloc = MiMalloc;

static SERVER_HDR: HeaderValue = HeaderValue::from_static("actix");

#[derive(Deserialize)]
struct BaselineQuery {
    a: Option<i64>,
    b: Option<i64>,
}

async fn baseline11_get(query: web::Query<BaselineQuery>) -> HttpResponse {
    let sum = query.a.unwrap_or(0) + query.b.unwrap_or(0);
    HttpResponse::Ok()
        .insert_header((SERVER, SERVER_HDR.clone()))
        .content_type(ContentType::plaintext())
        .body(sum.to_string())
}

async fn baseline11_post(query: web::Query<BaselineQuery>, body: web::Bytes) -> HttpResponse {
    let mut sum = query.a.unwrap_or(0) + query.b.unwrap_or(0);
    if let Ok(s) = std::str::from_utf8(&body) {
        if let Ok(n) = s.trim().parse::<i64>() {
            sum += n;
        }
    }
    HttpResponse::Ok()
        .insert_header((SERVER, SERVER_HDR.clone()))
        .content_type(ContentType::plaintext())
        .body(sum.to_string())
}

async fn baseline2(query: web::Query<BaselineQuery>) -> HttpResponse {
    let sum = query.a.unwrap_or(0) + query.b.unwrap_or(0);
    HttpResponse::Ok()
        .insert_header((SERVER, SERVER_HDR.clone()))
        .content_type(ContentType::plaintext())
        .body(sum.to_string())
}

async fn delay(path: web::Path<u64>) -> HttpResponse {
    let ms = path.into_inner();
    if ms > 0 {
        actix_web::rt::time::sleep(std::time::Duration::from_millis(ms)).await;
    }
    HttpResponse::Ok()
        .insert_header((SERVER, SERVER_HDR.clone()))
        .content_type(ContentType::plaintext())
        .body(ms.to_string())
}

#[actix_web::main]
async fn main() -> std::io::Result<()> {
    let args: Vec<String> = env::args().collect();
    let workers: usize = args.get(1).and_then(|s| s.parse().ok()).unwrap_or(2);
    let port: u16 = args.get(2).and_then(|s| s.parse().ok()).unwrap_or(18091);

    println!("Actix appstack benchmark listening on http://0.0.0.0:{} with {} workers", port, workers);

    HttpServer::new(|| {
        App::new()
            .route("/baseline11", web::get().to(baseline11_get))
            .route("/baseline11", web::post().to(baseline11_post))
            .route("/baseline2", web::get().to(baseline2))
            .route("/baseline2", web::post().to(baseline11_post))
            .route("/delay/{ms}", web::get().to(delay))
    })
    .workers(workers)
    .bind(("0.0.0.0", port))?
    .run()
    .await
}
