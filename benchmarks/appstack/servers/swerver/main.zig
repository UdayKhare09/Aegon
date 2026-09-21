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
