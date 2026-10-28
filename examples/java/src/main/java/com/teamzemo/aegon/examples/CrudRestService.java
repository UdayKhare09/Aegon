package com.teamzemo.aegon.examples;

import com.teamzemo.aegon.AegonServer;
import com.teamzemo.aegon.http.StatusCode;

import java.util.Collection;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

/**
 * RESTful CRUD microservice demonstrating:
 * <ul>
 *   <li>Project Loom Virtual Thread concurrency</li>
 *   <li>Fastjson2 record mapping</li>
 *   <li>Path parameter extraction</li>
 *   <li>RFC 7807 Problem Details on invalid requests or missing records</li>
 * </ul>
 */
public final class CrudRestService {

    /**
     * Product entity record.
     */
    public record Product(long id, String name, double price, int stock) {}

    /**
     * DTO for product creation and updates.
     */
    public record CreateProductRequest(String name, Double price, Integer stock) {}

    // In-memory thread-safe store
    private static final ConcurrentHashMap<Long, Product> DB = new ConcurrentHashMap<>();
    private static final AtomicLong ID_GEN = new AtomicLong(100);

    static {
        DB.put(1L, new Product(1L, "Mechanical Keyboard", 149.99, 25));
        DB.put(2L, new Product(2L, "Wireless Mouse", 79.99, 50));
        DB.put(3L, new Product(3L, "4K Monitor", 399.00, 10));
    }

    public static void main(String[] args) {
        AegonServer server = AegonServer.create();

        server.router().group("/api/v1/products", products -> {

            // LIST: GET /api/v1/products
            products.getAsync("", ctx -> {
                Collection<Product> all = DB.values();
                ctx.json(all);
            });

            // GET BY ID: GET /api/v1/products/:id
            products.getAsync("/:id", ctx -> {
                long id;
                try {
                    id = Long.parseLong(ctx.param("id"));
                } catch (NumberFormatException e) {
                    ctx.problem(StatusCode.BAD_REQUEST, "Invalid ID", "Product ID must be numeric");
                    return;
                }

                Product p = DB.get(id);
                if (p == null) {
                    ctx.problem(StatusCode.NOT_FOUND, "Product Not Found", "No product with ID " + id);
                    return;
                }
                ctx.json(p);
            });

            // CREATE: POST /api/v1/products
            products.postAsync("", ctx -> {
                CreateProductRequest req = ctx.bindJson(CreateProductRequest.class);
                if (req.name() == null || req.name().isBlank()) {
                    ctx.problem(StatusCode.BAD_REQUEST, "Validation Error", "Product 'name' is required");
                    return;
                }
                if (req.price() == null || req.price() < 0) {
                    ctx.problem(StatusCode.BAD_REQUEST, "Validation Error", "Product 'price' must be non-negative");
                    return;
                }

                long newId = ID_GEN.incrementAndGet();
                Product created = new Product(newId, req.name(), req.price(), req.stock() != null ? req.stock() : 0);
                DB.put(newId, created);

                ctx.status(StatusCode.CREATED).json(created);
            });

            // UPDATE: PUT /api/v1/products/:id
            products.putAsync("/:id", ctx -> {
                long id;
                try {
                    id = Long.parseLong(ctx.param("id"));
                } catch (NumberFormatException e) {
                    ctx.problem(StatusCode.BAD_REQUEST, "Invalid ID", "Product ID must be numeric");
                    return;
                }

                if (!DB.containsKey(id)) {
                    ctx.problem(StatusCode.NOT_FOUND, "Product Not Found", "Cannot update non-existent product " + id);
                    return;
                }

                CreateProductRequest req = ctx.bindJson(CreateProductRequest.class);
                Product updated = new Product(id, req.name(), req.price(), req.stock() != null ? req.stock() : 0);
                DB.put(id, updated);

                ctx.json(updated);
            });

            // DELETE: DELETE /api/v1/products/:id
            products.delAsync("/:id", ctx -> {
                long id;
                try {
                    id = Long.parseLong(ctx.param("id"));
                } catch (NumberFormatException e) {
                    ctx.problem(StatusCode.BAD_REQUEST, "Invalid ID", "Product ID must be numeric");
                    return;
                }

                Product removed = DB.remove(id);
                if (removed == null) {
                    ctx.problem(StatusCode.NOT_FOUND, "Product Not Found", "No product with ID " + id);
                    return;
                }

                ctx.status(StatusCode.NO_CONTENT);
            });
        });

        int port = 8081;
        server.listen(port, "0.0.0.0");
        System.out.printf("Aegon CRUD REST Service listening on http://localhost:%d/api/v1/products%n", port);
        server.run();
    }
}

