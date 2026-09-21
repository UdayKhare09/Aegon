const std = @import("std");
const swerver = @import("swerver");

const router = swerver.router;
const response_mod = swerver.response;

fn handlePlaintext(_: *router.HandlerContext) response_mod.Response {
    return .{
        .status = 200,
        .headers = &[_]response_mod.Header{
            .{ .name = "Content-Type", .value = "text/plain" },
        },
        .body = .{ .bytes = "Hello, World!" },
    };
}

const Message = struct {
    message: []const u8 = "Hello, World!",
};

fn handleJson(ctx: *router.HandlerContext) response_mod.Response {
    return ctx.jsonValue(200, Message{});
}

fn handleUserPost(ctx: *router.HandlerContext) response_mod.Response {
    const user_id = ctx.getParam("user_id") orelse "";
    const post_id = ctx.getParam("post_id") orelse "";
    var rb = ctx.respond() catch return .{
        .status = 500,
        .headers = &[_]response_mod.Header{},
        .body = .none,
    };
    const formatted = std.fmt.bufPrint(rb.handle.bytes, "User: {s}, Post: {s}", .{ user_id, post_id }) catch {
        if (ctx.buffer_ops) |ops| ops.release(ops.ctx, rb.handle);
        return .{
            .status = 500,
            .headers = &[_]response_mod.Header{},
            .body = .none,
        };
    };
    return rb.text(200, formatted) catch {
        if (ctx.buffer_ops) |ops| ops.release(ops.ctx, rb.handle);
        return .{
            .status = 500,
            .headers = &[_]response_mod.Header{},
            .body = .none,
        };
    };
}

pub fn main(init: std.process.Init) !void {
    const allocator = init.gpa;
    var num_workers: u16 = 2;
    var port: u16 = 18084;

    var it = try std.process.Args.Iterator.initAllocator(init.minimal.args, allocator);
    defer it.deinit();
    _ = it.next(); // skip program name
    if (it.next()) |w_arg| {
        const w_str = std.mem.sliceTo(w_arg, 0);
        num_workers = std.fmt.parseInt(u16, w_str, 10) catch 2;
    }
    if (it.next()) |p_arg| {
        const p_str = std.mem.sliceTo(p_arg, 0);
        port = std.fmt.parseInt(u16, p_str, 10) catch 18084;
    }

    var cfg = swerver.config.ServerConfig.default();
    cfg.address = "0.0.0.0";
    cfg.port = port;
    cfg.workers = num_workers;
    cfg.disable_middleware = true;
    cfg.cache_static_files = true;
    cfg.max_connections = 512;
    cfg.buffer_pool.buffer_size = 65536;
    cfg.buffer_pool.buffer_count = 1024;
    cfg.http2.initial_window_size = 1048576;
    cfg.http2.max_streams = 256;
    cfg.tls.cert_path = "benchmarks/http2/certs/server.crt";
    cfg.tls.key_path = "benchmarks/http2/certs/server.key";
    try cfg.validate();

    var app_router = router.Router.init(.{});
    try app_router.get("/plaintext", handlePlaintext);
    try app_router.get("/json", handleJson);
    try app_router.get("/users/:user_id/posts/:post_id", handleUserPost);

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
