package com.teamzemo.aegon.benchmarks.springboot;

import org.apache.catalina.connector.Connector;
import org.apache.coyote.http2.Http2Protocol;
import org.apache.tomcat.util.net.SSLHostConfig;
import org.apache.tomcat.util.net.SSLHostConfigCertificate;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.tomcat.servlet.TomcatServletWebServerFactory;
import org.springframework.boot.web.server.WebServerFactoryCustomizer;
import org.springframework.http.MediaType;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.socket.WebSocketMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;
import org.springframework.web.socket.handler.AbstractWebSocketHandler;
import org.springframework.http.CacheControl;
import org.springframework.web.servlet.config.annotation.ResourceHandlerRegistry;
import org.springframework.web.servlet.config.annotation.WebMvcConfigurer;
import tools.jackson.core.type.TypeReference;
import tools.jackson.databind.json.JsonMapper;

import java.io.File;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

@SpringBootApplication
@EnableWebSocket
@RestController
public class Application implements WebServerFactoryCustomizer<TomcatServletWebServerFactory>, WebSocketConfigurer, WebMvcConfigurer {

    // --- Domain Models ---
    public record Rating(int score, long count) {}
    public record Item(long id, String name, String category, int price, int quantity, boolean active, List<String> tags, Rating rating) {}
    public record TotalItem(long id, String name, String category, int price, int quantity, boolean active, List<String> tags, Rating rating, long total) {}
    public record Page(List<TotalItem> items, int count) {}

    // --- State ---
    private static int basePort = 18100;
    private static String certFile = "benchmarks/certs/server.crt";
    private static String keyFile = "benchmarks/certs/server.key";
    private final List<Item> dataset;

    public Application(JsonMapper mapper) {
        List<Item> loaded = Collections.emptyList();
        String[] searchPaths = {
            "benchmarks/data/dataset.json",
            "data/dataset.json",
            "../data/dataset.json",
            "/data/dataset.json"
        };
        for (String p : searchPaths) {
            Path path = Path.of(p);
            if (Files.exists(path)) {
                try {
                    loaded = mapper.readValue(Files.readAllBytes(path), new TypeReference<List<Item>>() {});
                    System.out.println("[Spring Boot] Loaded " + loaded.size() + " items from " + p);
                    break;
                } catch (Exception ignored) {}
            }
        }
        this.dataset = loaded;
    }

    // --- 1. Baseline (/baseline) ---
    @RequestMapping(value = "/baseline", method = {RequestMethod.GET, RequestMethod.POST})
    public String baseline(@RequestParam(value = "a", defaultValue = "0") int a,
                           @RequestParam(value = "b", defaultValue = "0") int b,
                           @RequestBody(required = false) String body) {
        int bodyNum = 0;
        if (body != null && !body.isEmpty()) {
            try { bodyNum = Integer.parseInt(body.trim()); } catch (Exception ignored) {}
        }
        return String.valueOf(a + b + bodyNum);
    }

    // --- 2. Pipeline (/pipeline) ---
    @GetMapping("/pipeline")
    public String pipeline() {
        return "ok";
    }

    // --- 3. Dynamic JSON with Totals (/json/{count}) ---
    @GetMapping("/json/{count}")
    public Page json(@PathVariable int count, @RequestParam(value = "m", defaultValue = "1") int m) {
        int n = Math.min(Math.max(count, 0), dataset.size());
        List<TotalItem> list = new ArrayList<>(n);
        for (int i = 0; i < n; i++) {
            Item item = dataset.get(i);
            long total = (long) item.price() * item.quantity() * m;
            list.add(new TotalItem(item.id(), item.name(), item.category(), item.price(), item.quantity(), item.active(), item.tags(), item.rating(), total));
        }
        return new Page(list, n);
    }

    // --- 4. Non-blocking Async Delay on Virtual Threads (/delay/{ms}) ---
    @GetMapping(value = "/delay/{ms}", produces = MediaType.TEXT_PLAIN_VALUE)
    public String delay(@PathVariable int ms) throws InterruptedException {
        if (ms > 0) {
            Thread.sleep(ms);
        }
        return String.valueOf(ms);
    }

    // --- 5. Stream Echo (/echo) ---
    @PostMapping(value = "/echo", consumes = MediaType.ALL_VALUE, produces = MediaType.APPLICATION_OCTET_STREAM_VALUE)
    public byte[] echo(InputStream in) throws Exception {
        return in.readAllBytes();
    }

    // --- 6. WebSocket Echo (/ws) ---
    @Override
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
        registry.addHandler(new AbstractWebSocketHandler() {
            @Override
            public void handleMessage(WebSocketSession session, WebSocketMessage<?> message) throws Exception {
                session.sendMessage(message);
            }
        }, "/ws");
    }

    // --- 7. Static Files (/static/**) ---
    @Override
    public void addResourceHandlers(ResourceHandlerRegistry registry) {
        String[] searchPaths = {
            "benchmarks/data/static",
            "data/static",
            "../data/static",
            "/data/static"
        };
        for (String p : searchPaths) {
            File dir = new File(p);
            if (dir.exists() && dir.isDirectory()) {
                registry.addResourceHandler("/static/**")
                    .addResourceLocations("file:" + dir.getAbsolutePath() + "/")
                    .setCacheControl(CacheControl.noCache());
                System.out.println("[Spring Boot] Serving static files from " + dir.getAbsolutePath());
                break;
            }
        }
    }

    // --- Multi-Port Listeners (Plain, TLS, H2) ---
    @Override
    public void customize(TomcatServletWebServerFactory factory) {
        int portPlain = basePort;
        int portTls = basePort + 1;
        int portH2 = basePort + 2;

        factory.setPort(portPlain);

        boolean hasCerts = new File(certFile).exists() && new File(keyFile).exists();
        if (hasCerts) {
            String absCert = new File(certFile).getAbsolutePath();
            String absKey = new File(keyFile).getAbsolutePath();

            // Port + 1: TLS 1.3 HTTP/1.1
            Connector tlsH1 = new Connector("org.apache.coyote.http11.Http11NioProtocol");
            tlsH1.setPort(portTls);
            tlsH1.setScheme("https");
            tlsH1.setSecure(true);
            tlsH1.setProperty("SSLEnabled", "true");
            SSLHostConfig sslConfigH1 = new SSLHostConfig();
            SSLHostConfigCertificate certH1 = new SSLHostConfigCertificate(sslConfigH1, SSLHostConfigCertificate.Type.UNDEFINED);
            certH1.setCertificateFile(absCert);
            certH1.setCertificateKeyFile(absKey);
            sslConfigH1.addCertificate(certH1);
            tlsH1.addSslHostConfig(sslConfigH1);
            factory.addAdditionalConnectors(tlsH1);

            // Port + 2: TLS 1.3 HTTP/2 ALPN
            Connector tlsH2 = new Connector("org.apache.coyote.http11.Http11NioProtocol");
            tlsH2.setPort(portH2);
            tlsH2.setScheme("https");
            tlsH2.setSecure(true);
            tlsH2.setProperty("SSLEnabled", "true");
            SSLHostConfig sslConfigH2 = new SSLHostConfig();
            SSLHostConfigCertificate certH2 = new SSLHostConfigCertificate(sslConfigH2, SSLHostConfigCertificate.Type.UNDEFINED);
            certH2.setCertificateFile(absCert);
            certH2.setCertificateKeyFile(absKey);
            sslConfigH2.addCertificate(certH2);
            tlsH2.addSslHostConfig(sslConfigH2);
            tlsH2.addUpgradeProtocol(new Http2Protocol());
            factory.addAdditionalConnectors(tlsH2);
        }

        System.out.println("===================================================================");
        System.out.println(" [Spring Boot Unified Server] (Spring MVC + Java 21 Virtual Threads)");
        System.out.println("   -> Plain HTTP/1.1 & WS : http://0.0.0.0:" + portPlain);
        if (hasCerts) {
            System.out.println("   -> TLS 1.3 HTTP/1.1    : https://0.0.0.0:" + portTls);
            System.out.println("   -> TLS 1.3 HTTP/2      : https://0.0.0.0:" + portH2);
        }
        System.out.println("===================================================================");
    }

    public static void main(String[] args) {
        if (args.length > 1) {
            try { basePort = Integer.parseInt(args[1]); } catch (Exception ignored) {}
        }
        if (args.length > 2) certFile = args[2];
        if (args.length > 3) keyFile = args[3];

        if (!new File(certFile).exists() || !new File(keyFile).exists()) {
            if (new File("certs/server.crt").exists() && new File("certs/server.key").exists()) {
                certFile = "certs/server.crt";
                keyFile = "certs/server.key";
            }
        }

        SpringApplication app = new SpringApplication(Application.class);
        app.run(args);
    }
}
