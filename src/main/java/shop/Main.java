package shop;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.javalin.Javalin;
import io.javalin.http.Context;
import io.javalin.http.staticfiles.Location;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.sql.*;
import java.util.*;

public class Main {

    static final String DB_URL = "jdbc:sqlite:shop.db";
    static final ObjectMapper MAPPER = new ObjectMapper();

    static Connection conn() throws SQLException {
        return DriverManager.getConnection(DB_URL);
    }

    static String sha256(String s) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] d = md.digest(s.getBytes(StandardCharsets.UTF_8));
            StringBuilder sb = new StringBuilder();
            for (byte b : d) sb.append(String.format("%02X", b));
            return sb.toString();
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    static Map<String, Object> result(String status, String message) {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("status", status);
        if (message != null) m.put("message", message);
        return m;
    }

    // readTree throws checked exception -> catch here so handlers compile
    static JsonNode body(Context ctx) {
        try {
            JsonNode n = MAPPER.readTree(ctx.body());
            return (n != null && n.isObject()) ? n : null;
        } catch (Exception e) {
            return null;
        }
    }

    static boolean has(JsonNode j, String... keys) {
        if (j == null) return false;
        for (String k : keys) if (!j.has(k)) return false;
        return true;
    }

    static void tryExec(Statement st, String sql) {
        try { st.execute(sql); } catch (SQLException ignored) { }
    }

    static void initDatabase() {
        try {
            Class.forName("org.sqlite.JDBC");
        } catch (ClassNotFoundException e) {
            System.err.println("[DB] SQLite driver missing!");
        }
        try (Connection c = conn(); Statement st = c.createStatement()) {
            st.execute("CREATE TABLE IF NOT EXISTS users ("
                    + "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    + "name TEXT, email TEXT UNIQUE, password TEXT, role TEXT);");
            st.execute("CREATE TABLE IF NOT EXISTS products ("
                    + "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    + "name TEXT, price REAL, seller_email TEXT, "
                    + "category TEXT DEFAULT 'General', image_url TEXT, "
                    + "rating REAL DEFAULT 4.8);");
            st.execute("CREATE TABLE IF NOT EXISTS orders ("
                    + "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    + "buyer_email TEXT, product_name TEXT, quantity INTEGER, "
                    + "total_price REAL, status TEXT DEFAULT 'Pending', "
                    + "order_date DATETIME DEFAULT CURRENT_TIMESTAMP);");
            tryExec(st, "ALTER TABLE products ADD COLUMN category TEXT DEFAULT 'General';");
            tryExec(st, "ALTER TABLE products ADD COLUMN image_url TEXT;");
            tryExec(st, "ALTER TABLE products ADD COLUMN rating REAL DEFAULT 4.8;");
            tryExec(st, "ALTER TABLE orders ADD COLUMN status TEXT DEFAULT 'Pending';");
            // Demo accounts for viva (passwords SHA-256, like register()):
            // admin@rabbimart.com / admin123, seller@rabbimart.com / seller123, buyer@rabbimart.com / buyer123
            tryExec(st, "INSERT OR IGNORE INTO users (name, email, password, role) VALUES "
                    + "('Admin', 'admin@rabbimart.com', "
                    + "'240BE518FABD2724DDB6F04EEB1DA5967448D7E831C08C8FA822809F74C720A9', 'admin');");
            tryExec(st, "INSERT OR IGNORE INTO users (name, email, password, role) VALUES "
                    + "('Demo Seller', 'seller@rabbimart.com', "
                    + "'2A76110D06BCC4FD437337B984131CFA82DB9F792E3E2340ACEF9F3066B264E0', 'seller');");
            tryExec(st, "INSERT OR IGNORE INTO users (name, email, password, role) VALUES "
                    + "('Demo Buyer', 'buyer@rabbimart.com', "
                    + "'E547BD13228250DFB4C7DF1D1EBB78CFD9F2ADA56EBB0C425D35829DD3AC4AE8', 'buyer');");
            System.out.println("[DB] SQLite Database Connected & Initialized!");
        } catch (SQLException e) {
            System.err.println("[DB] Database Error: " + e.getMessage());
        }
    }

    public static void main(String[] args) {
        initDatabase();

        Javalin app = Javalin.create(config -> {
            config.staticFiles.add(sf -> {
                sf.hostedPath = "/";
                sf.directory = "public";
                sf.location = Location.EXTERNAL;
            });
        });

        app.post("/api/register", Main::register);
        app.post("/api/login", Main::login);
        app.post("/api/products/add", Main::addProduct);
        app.get("/api/products", Main::getProducts);
        app.post("/api/products/delete", Main::deleteProduct);
        app.post("/api/orders/place", Main::placeOrder);
        app.get("/api/orders", Main::getOrders);
        // Frontend calls /api/orders/update-status (admin.html) - keep exact path
        app.post("/api/orders/update-status", Main::updateOrderStatus);

        app.start(8080);
        System.out.println("=================================================");
        System.out.println("RabbiMart Server is running on: http://localhost:8080");
        System.out.println("=================================================");
    }

    static void register(Context ctx) {
        JsonNode j = body(ctx);
        if (!has(j, "email", "password", "name", "role")) {
            ctx.json(result("error", "All fields are required!"));
            return;
        }
        String name = j.get("name").asText();
        String email = j.get("email").asText();
        String hashed = sha256(j.get("password").asText());
        String role = j.get("role").asText();
        String sql = "INSERT INTO users (name, email, password, role) VALUES (?, ?, ?, ?);";
        try (Connection c = conn(); PreparedStatement ps = c.prepareStatement(sql)) {
            ps.setString(1, name);
            ps.setString(2, email);
            ps.setString(3, hashed);
            ps.setString(4, role);
            try {
                ps.executeUpdate();
                ctx.json(result("success", "User registered successfully!"));
            } catch (SQLException e) {
                ctx.json(result("error", "Email already registered!"));
            }
        } catch (SQLException e) {
            ctx.json(result("error", "Database statement error!"));
        }
    }

    static void login(Context ctx) {
        JsonNode j = body(ctx);
        if (!has(j, "email", "password")) {
            ctx.json(result("error", "Email and password required!"));
            return;
        }
        String email = j.get("email").asText();
        String password = j.get("password").asText();
        String hashed = sha256(password);
        String sql = "SELECT name, role FROM users WHERE email = ? "
                + "AND (password = ? OR password = ? OR password = ?);";
        try (Connection c = conn(); PreparedStatement ps = c.prepareStatement(sql)) {
            ps.setString(1, email);
            ps.setString(2, hashed);
            ps.setString(3, hashed.toLowerCase());
            ps.setString(4, password);
            try (ResultSet rs = ps.executeQuery()) {
                if (rs.next()) {
                    Map<String, Object> ret = result("success", null);
                    ret.put("name", rs.getString(1));
                    ret.put("role", rs.getString(2));
                    ret.put("email", email);
                    ctx.json(ret);
                } else {
                    ctx.json(result("error", "Invalid email or password!"));
                }
            }
        } catch (SQLException e) {
            ctx.json(result("error", "Database error!"));
        }
    }

    static void addProduct(Context ctx) {
        JsonNode j = body(ctx);
        if (!has(j, "name", "price", "seller_email")) {
            ctx.json(result("error", "Product details missing!"));
            return;
        }
        String name = j.get("name").asText();
        double price = j.get("price").asDouble();
        String sellerEmail = j.get("seller_email").asText();
        String category = j.has("category") ? j.get("category").asText() : "General";
        String imageUrl = j.has("image_url") ? j.get("image_url").asText() : "";
        String sql = "INSERT INTO products (name, price, seller_email, category, image_url, rating) "
                + "VALUES (?, ?, ?, ?, ?, 4.9);";
        try (Connection c = conn(); PreparedStatement ps = c.prepareStatement(sql)) {
            ps.setString(1, name);
            ps.setDouble(2, price);
            ps.setString(3, sellerEmail);
            ps.setString(4, category);
            ps.setString(5, imageUrl);
            try {
                ps.executeUpdate();
                ctx.json(result("success", "Product added successfully!"));
            } catch (SQLException e) {
                ctx.json(result("error", "Failed to add product!"));
            }
        } catch (SQLException e) {
            ctx.json(result("error", "Database error!"));
        }
    }

    static void getProducts(Context ctx) {
        String sql = "SELECT id, name, price, seller_email, COALESCE(category, 'General'), "
                + "COALESCE(image_url, ''), COALESCE(rating, 4.8) FROM products;";
        List<Map<String, Object>> products = new ArrayList<>();
        try (Connection c = conn(); PreparedStatement ps = c.prepareStatement(sql);
             ResultSet rs = ps.executeQuery()) {
            while (rs.next()) {
                Map<String, Object> item = new LinkedHashMap<>();
                item.put("id", rs.getInt(1));
                item.put("name", rs.getString(2));
                item.put("price", rs.getDouble(3));
                item.put("seller_email", rs.getString(4));
                item.put("category", rs.getString(5));
                item.put("image_url", rs.getString(6));
                item.put("rating", rs.getDouble(7));
                products.add(item);
            }
            Map<String, Object> ret = new LinkedHashMap<>();
            ret.put("products", products);
            ret.put("status", "success");
            ctx.json(ret);
        } catch (SQLException e) {
            Map<String, Object> ret = result("error", "Failed to fetch products!");
            ret.put("products", products);
            ctx.json(ret);
        }
    }

    static void deleteProduct(Context ctx) {
        JsonNode j = body(ctx);
        if (!has(j, "id")) {
            ctx.json(result("error", "Product ID is required!"));
            return;
        }
        int id = j.get("id").asInt();
        try (Connection c = conn();
             PreparedStatement ps = c.prepareStatement("DELETE FROM products WHERE id = ?;")) {
            ps.setInt(1, id);
            ps.executeUpdate();
            ctx.json(result("success", "Product deleted successfully!"));
        } catch (SQLException e) {
            ctx.json(result("error", "Failed to delete product!"));
        }
    }

    static void placeOrder(Context ctx) {
        JsonNode j = body(ctx);
        // Frontend (buyer.html) sends {buyer_email, items:[{name,price,quantity}]}
        if (!has(j, "buyer_email", "items") || !j.get("items").isArray()) {
            ctx.json(result("error", "Invalid order data!"));
            return;
        }
        String buyerEmail = j.get("buyer_email").asText();
        String sql = "INSERT INTO orders (buyer_email, product_name, quantity, total_price, status) "
                + "VALUES (?, ?, ?, ?, 'Pending');";
        try (Connection c = conn()) {
            c.setAutoCommit(false);
            try (PreparedStatement ps = c.prepareStatement(sql)) {
                for (JsonNode item : j.get("items")) {
                    String name = item.path("name").asText();
                    int qty = item.path("quantity").asInt();
                    double total = item.path("price").asDouble() * qty;
                    ps.setString(1, buyerEmail);
                    ps.setString(2, name);
                    ps.setInt(3, qty);
                    ps.setDouble(4, total);
                    ps.addBatch();
                }
                ps.executeBatch();
                c.commit();
                ctx.json(result("success", "Order placed successfully!"));
            } catch (SQLException e) {
                c.rollback();
                ctx.json(result("error", "Failed to place order!"));
            }
        } catch (SQLException e) {
            ctx.json(result("error", "Database error!"));
        }
    }

    static void getOrders(Context ctx) {
        String sql = "SELECT id, buyer_email, product_name, quantity, total_price, "
                + "COALESCE(status, 'Pending'), order_date FROM orders ORDER BY id DESC;";
        List<Map<String, Object>> orders = new ArrayList<>();
        try (Connection c = conn(); PreparedStatement ps = c.prepareStatement(sql);
             ResultSet rs = ps.executeQuery()) {
            while (rs.next()) {
                Map<String, Object> item = new LinkedHashMap<>();
                item.put("id", rs.getInt(1));
                item.put("buyer_email", rs.getString(2));
                item.put("product_name", rs.getString(3));
                item.put("quantity", rs.getInt(4));
                item.put("total_price", rs.getDouble(5));
                item.put("status", rs.getString(6));
                item.put("order_date", rs.getString(7));
                orders.add(item);
            }
            Map<String, Object> ret = new LinkedHashMap<>();
            ret.put("orders", orders);
            ret.put("status", "success");
            ctx.json(ret);
        } catch (SQLException e) {
            Map<String, Object> ret = result("error", "Failed to fetch orders!");
            ret.put("orders", orders);
            ctx.json(ret);
        }
    }

    static void updateOrderStatus(Context ctx) {
        JsonNode j = body(ctx);
        if (!has(j, "id", "status")) {
            ctx.json(result("error", "ID and status required!"));
            return;
        }
        int id = j.get("id").asInt();
        String status = j.get("status").asText();
        try (Connection c = conn();
             PreparedStatement ps = c.prepareStatement("UPDATE orders SET status = ? WHERE id = ?;")) {
            ps.setString(1, status);
            ps.setInt(2, id);
            ps.executeUpdate();
            ctx.json(result("success", "Order status updated!"));
        } catch (SQLException e) {
            ctx.json(result("error", "Failed to update order status!"));
        }
    }
}
