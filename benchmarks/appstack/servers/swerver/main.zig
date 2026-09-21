const std = @import("std");
const swerver = @import("swerver");

const router = swerver.router;
const response_mod = swerver.response;

fn handleBaseline(ctx: *router.HandlerContext) response_mod.Response {
    var sum: i64 = 0;
    if (std.mem.indexOfScalar(u8, ctx.request.path, '?')) |q_start| {
        const query = ctx.request.path[q_start + 1 ..];
        var it = std.mem.splitScalar(u8, query, '&');
        while (it.next()) |pair| {
            if (std.mem.indexOfScalar(u8, pair, '=')) |eq| {
                sum += std.fmt.parseInt(i64, pair[eq + 1 ..], 10) catch 0;
            }
        }
    }
    if (ctx.request.method == .POST and ctx.request.body.len() > 0) {
        const body_bytes = ctx.request.body.sliceOrNull() orelse "";
        const trimmed = std.mem.trim(u8, body_bytes, " \t\r\n");
        sum += std.fmt.parseInt(i64, trimmed, 10) catch 0;
    }
    const body = std.fmt.bufPrint(ctx.response_buf, "{d}", .{sum}) catch "0";
    return .{
        .status = 200,
        .headers = &[_]response_mod.Header{
            .{ .name = "Content-Type", .value = "text/plain" },
        },
        .body = .{ .bytes = body },
    };
}

const Rating = struct { score: i64 = 0, count: i64 = 0 };

const ParseItem = struct {
    id: i64,
    name: []const u8,
    category: []const u8,
    price: i64,
    quantity: i64,
    active: bool = false,
    tags: []const []const u8 = &.{},
    rating: Rating = .{},
};

const DatasetItem = struct {
    id: i64,
    name: []const u8,
    category: []const u8,
    price: i64,
    quantity: i64,
    active: bool,
    tags: []const []const u8,
    rating: Rating,
};

const JsonItem = struct {
    id: i64,
    name: []const u8,
    category: []const u8,
    price: i64,
    quantity: i64,
    active: bool,
    tags: []const []const u8,
    rating: Rating,
    total: i64,
};

const MAX_ITEMS = 64;
var dataset_items: [MAX_ITEMS]DatasetItem = undefined;
var dataset_len: usize = 0;

var name_pool: [4096]u8 = undefined;
var name_pool_off: usize = 0;
var cat_pool: [4096]u8 = undefined;
var cat_pool_off: usize = 0;
var tags_pool: [16384]u8 = undefined;
var tags_pool_off: usize = 0;
var tag_slices: [256][]const u8 = undefined;
var tag_slice_off: usize = 0;

var gzip_out: [65536]u8 = undefined;
var json_buf: [65536]u8 = undefined;

const c_io = struct {
    extern "c" fn open(path: [*:0]const u8, flags: c_int, ...) c_int;
    extern "c" fn read(fd: c_int, buf: [*]u8, count: usize) isize;
    extern "c" fn close(fd: c_int) c_int;
};

fn loadDataset(allocator: std.mem.Allocator) void {
    var env_slice: []const u8 = "";
    if (std.c.getenv("DATASET_PATH")) |ptr| {
        env_slice = std.mem.sliceTo(ptr, 0);
    }
    const candidate_paths = [_][]const u8{
        env_slice,
        "data/dataset.json",
        "benchmarks/appstack/data/dataset.json",
        "../../data/dataset.json",
    };

    var content: ?[]u8 = null;
    for (candidate_paths) |path| {
        if (path.len == 0 or path.len >= 127) continue;
        var path_z: [128:0]u8 = undefined;
        @memcpy(path_z[0..path.len], path);
        path_z[path.len] = 0;

        const fd = c_io.open(&path_z, 0);
        if (fd < 0) continue;
        defer _ = c_io.close(fd);

        const buf = allocator.alloc(u8, 65536) catch continue;
        const n = c_io.read(fd, buf.ptr, 65536);
        if (n <= 0) {
            allocator.free(buf);
            continue;
        }
        content = buf[0..@intCast(n)];
        std.debug.print("[Swerver] Loaded dataset from {s} ({d} bytes)\n", .{ path, content.?.len });
        break;
    }

    if (content == null) {
        std.debug.print("[Swerver] WARNING: Could not find dataset.json!\n", .{});
        return;
    }
    defer allocator.free(content.?);

    var arena = std.heap.ArenaAllocator.init(allocator);
    defer arena.deinit();

    const items = std.json.parseFromSliceLeaky(
        []ParseItem,
        arena.allocator(),
        content.?,
        .{ .ignore_unknown_fields = true },
    ) catch |err| {
        std.debug.print("[Swerver] JSON parse error: {}\n", .{err});
        return;
    };

    const count = @min(items.len, MAX_ITEMS);
    for (items[0..count], 0..) |item, i| {
        const ns = name_pool_off;
        @memcpy(name_pool[ns .. ns + item.name.len], item.name);
        name_pool_off += item.name.len;

        const cs = cat_pool_off;
        @memcpy(cat_pool[cs .. cs + item.category.len], item.category);
        cat_pool_off += item.category.len;

        const tags_start = tag_slice_off;
        for (item.tags) |tag| {
            const s = tags_pool_off;
            @memcpy(tags_pool[s .. s + tag.len], tag);
            tags_pool_off += tag.len;
            tag_slices[tag_slice_off] = tags_pool[s .. s + tag.len];
            tag_slice_off += 1;
        }

        dataset_items[i] = .{
            .id = item.id,
            .name = name_pool[ns .. ns + item.name.len],
            .category = cat_pool[cs .. cs + item.category.len],
            .price = item.price,
            .quantity = item.quantity,
            .active = item.active,
            .tags = tag_slices[tags_start..tag_slice_off],
            .rating = item.rating,
        };
    }
    dataset_len = count;
    std.debug.print("[Swerver] Parsed {d} items into memory\n", .{dataset_len});
}

fn jsonError() response_mod.Response {
    return .{
        .status = 500,
        .headers = &[_]response_mod.Header{
            .{ .name = "Content-Type", .value = "application/json" },
        },
        .body = .{ .bytes = "{\"error\":\"json/gzip failed\"}" },
    };
}

fn handleJson(ctx: *router.HandlerContext) response_mod.Response {
    const count_str = ctx.getParam("count") orelse "50";
    const count = @min(
        std.fmt.parseInt(usize, count_str, 10) catch 50,
        dataset_len,
    );

    var m: i64 = 1;
    if (std.mem.indexOfScalar(u8, ctx.request.path, '?')) |q_start| {
        const query = ctx.request.path[q_start + 1 ..];
        var it = std.mem.splitScalar(u8, query, '&');
        while (it.next()) |pair| {
            if (std.mem.startsWith(u8, pair, "m=")) {
                m = std.fmt.parseInt(i64, pair[2..], 10) catch 1;
            }
        }
    }

    var items: [MAX_ITEMS]JsonItem = undefined;
    for (dataset_items[0..count], 0..) |item, i| {
        items[i] = .{
            .id = item.id,
            .name = item.name,
            .category = item.category,
            .price = item.price,
            .quantity = item.quantity,
            .active = item.active,
            .tags = item.tags,
            .rating = item.rating,
            .total = item.price * item.quantity * m,
        };
    }

    const payload = .{ .items = items[0..count], .count = count };

    if (ctx.request.getHeader("accept-encoding")) |ae| {
        if (std.mem.indexOf(u8, ae, "gzip") != null) {
            var w = std.Io.Writer.fixed(json_buf[0..]);
            std.json.Stringify.value(payload, .{}, &w) catch return jsonError();
            if (swerver.compress.gzipCompress(w.buffered(), &gzip_out)) |clen| {
                return .{
                    .status = 200,
                    .headers = &[_]response_mod.Header{
                        .{ .name = "Content-Type", .value = "application/json" },
                        .{ .name = "Content-Encoding", .value = "gzip" },
                        .{ .name = "Vary", .value = "Accept-Encoding" },
                    },
                    .body = .{ .bytes = gzip_out[0..clen] },
                };
            }
        }
    }

    return ctx.jsonValue(200, payload);
}

pub fn main(init: std.process.Init) !void {
    const allocator = init.gpa;
    var num_workers: u16 = 2;
    var port: u16 = 18094;

    var it = try std.process.Args.Iterator.initAllocator(init.minimal.args, allocator);
    defer it.deinit();
    _ = it.next(); // skip program name
    if (it.next()) |w_arg| {
        const w_str = std.mem.sliceTo(w_arg, 0);
        num_workers = std.fmt.parseInt(u16, w_str, 10) catch 2;
    }
    if (it.next()) |p_arg| {
        const p_str = std.mem.sliceTo(p_arg, 0);
        port = std.fmt.parseInt(u16, p_str, 10) catch 18094;
    }

    loadDataset(allocator);

    var cfg = swerver.config.ServerConfig.default();
    cfg.address = "0.0.0.0";
    cfg.port = port;
    cfg.workers = num_workers;
    cfg.disable_middleware = true;
    cfg.cache_static_files = true;
    cfg.max_connections = 4096;
    cfg.buffer_pool.buffer_size = 65536;
    cfg.buffer_pool.buffer_count = 8192;
    try cfg.validate();

    var app_router = router.Router.init(.{});
    try app_router.get("/baseline11", handleBaseline);
    try app_router.post("/baseline11", handleBaseline);
    try app_router.get("/baseline2", handleBaseline);
    try app_router.post("/baseline2", handleBaseline);
    try app_router.get("/json/:count", handleJson);

    std.debug.print("Swerver appstack benchmark listening on http://0.0.0.0:{d} with {d} workers\n", .{ port, num_workers });

    if (cfg.workers != 1) {
        var master = try swerver.Master.init(allocator, cfg, app_router, null);
        defer master.deinit();
        try master.run(null);
    } else {
        const srv = try swerver.ServerBuilder
            .config(cfg)
            .router(app_router)
            .build(allocator);
        defer {
            srv.deinit();
            allocator.destroy(srv);
        }
        try srv.run(null);
    }
}
