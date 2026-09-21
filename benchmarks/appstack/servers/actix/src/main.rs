use actix_web::http::header;
use actix_web::http::header::{ContentType, HeaderValue, SERVER};
use actix_web::{web, App, HttpRequest, HttpResponse, HttpServer};
use mimalloc::MiMalloc;
use serde::{Deserialize, Serialize};
use std::env;
use std::sync::Arc;

#[global_allocator]
static GLOBAL: MiMalloc = MiMalloc;

static SERVER_HDR: HeaderValue = HeaderValue::from_static("actix");

#[derive(Deserialize)]
struct BaselineQuery {
    a: Option<i64>,
    b: Option<i64>,
}

#[derive(Deserialize)]
struct JsonQuery {
    m: Option<i64>,
}

#[derive(Deserialize, Clone)]
struct Rating {
    score: i64,
    count: i64,
}

#[derive(Deserialize, Clone)]
struct DatasetItem {
    id: i64,
    name: String,
    category: String,
    price: i64,
    quantity: i64,
    active: bool,
    tags: Vec<String>,
    rating: Rating,
}

#[derive(Serialize, Clone)]
struct RatingOut {
    score: i64,
    count: i64,
}

#[derive(Serialize, Clone)]
struct ProcessedItem {
    id: i64,
    name: String,
    category: String,
    price: i64,
    quantity: i64,
    active: bool,
    tags: Vec<String>,
    rating: RatingOut,
    total: i64,
}

#[derive(Serialize)]
struct JsonResponse {
    items: Vec<ProcessedItem>,
    count: usize,
}

struct AppState {
    dataset: Arc<Vec<DatasetItem>>,
}

fn build_json_body(dataset: &[DatasetItem], count: usize, m: i64) -> Vec<u8> {
    let count = count.min(dataset.len());
    let items: Vec<ProcessedItem> = dataset[..count]
        .iter()
        .map(|d| ProcessedItem {
            id: d.id,
            name: d.name.clone(),
            category: d.category.clone(),
            price: d.price,
            quantity: d.quantity,
            active: d.active,
            tags: d.tags.clone(),
            rating: RatingOut {
                score: d.rating.score,
                count: d.rating.count,
            },
            total: d.price * d.quantity * m,
        })
        .collect();
    let resp = JsonResponse { items, count };
    serde_json::to_vec(&resp).unwrap_or_default()
}

fn load_dataset() -> Vec<DatasetItem> {
    let candidate_paths = [
        env::var("DATASET_PATH").unwrap_or_default(),
        "data/dataset.json".to_string(),
        "benchmarks/appstack/data/dataset.json".to_string(),
        "../../data/dataset.json".to_string(),
    ];
    for path in &candidate_paths {
        if path.is_empty() {
            continue;
        }
        if let Ok(content) = std::fs::read_to_string(path) {
            if let Ok(items) = serde_json::from_str::<Vec<DatasetItem>>(&content) {
                println!("[Actix] Loaded {} items from {}", items.len(), path);
                return items;
            }
        }
    }
    eprintln!("[Actix] WARNING: Could not find or parse dataset.json!");
    Vec::new()
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

async fn json_endpoint(
    state: web::Data<AppState>,
    req: HttpRequest,
    path: web::Path<usize>,
    query: web::Query<JsonQuery>,
) -> HttpResponse {
    use std::io::Write as _;

    let count = path.into_inner().min(state.dataset.len());
    let m = query.m.unwrap_or(1);
    let body = build_json_body(&state.dataset, count, m);

    let accepts_gzip = req
        .headers()
        .get(header::ACCEPT_ENCODING)
        .and_then(|v| v.to_str().ok())
        .is_some_and(|v| v.contains("gzip"));

    if accepts_gzip {
        let mut enc = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::new(1));
        if enc.write_all(&body).is_ok() {
            if let Ok(compressed) = enc.finish() {
                return HttpResponse::Ok()
                    .insert_header((SERVER, SERVER_HDR.clone()))
                    .insert_header((header::CONTENT_ENCODING, "gzip"))
                    .insert_header((header::VARY, "Accept-Encoding"))
                    .content_type(ContentType::json())
                    .body(compressed);
            }
        }
    }

    HttpResponse::Ok()
        .insert_header((SERVER, SERVER_HDR.clone()))
        .content_type(ContentType::json())
        .body(body)
}

#[actix_web::main]
async fn main() -> std::io::Result<()> {
    let args: Vec<String> = env::args().collect();
    let workers: usize = args.get(1).and_then(|s| s.parse().ok()).unwrap_or(2);
    let port: u16 = args.get(2).and_then(|s| s.parse().ok()).unwrap_or(18091);

    let dataset = Arc::new(load_dataset());
    println!("Actix appstack benchmark listening on http://0.0.0.0:{} with {} workers", port, workers);

    let app_state = web::Data::new(AppState {
        dataset: dataset.clone(),
    });

    HttpServer::new(move || {
        App::new()
            .app_data(app_state.clone())
            .route("/baseline11", web::get().to(baseline11_get))
            .route("/baseline11", web::post().to(baseline11_post))
            .route("/baseline2", web::get().to(baseline2))
            .route("/baseline2", web::post().to(baseline11_post))
            .route("/delay/{ms}", web::get().to(delay))
            .route("/json/{count}", web::get().to(json_endpoint))
    })
    .workers(workers)
    .bind(("0.0.0.0", port))?
    .run()
    .await
}
