#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <sqlite3.h>
#include <iostream>
#include <string>

using namespace drogon;

sqlite3* db = nullptr;

void initDatabase() {
    if (sqlite3_open("shop.db", &db) != SQLITE_OK) {
        std::cerr << "[DB] Failed to open DB: " << sqlite3_errmsg(db) << "\n";
        return;
    }

    const char* createUsersTable = 
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT, "
        "email TEXT UNIQUE, "
        "password TEXT, "
        "role TEXT);";

    const char* createProductsTable = 
        "CREATE TABLE IF NOT EXISTS products ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT, "
        "price REAL, "
        "seller_email TEXT, "
        "category TEXT DEFAULT 'General', "
        "image_url TEXT, "
        "rating REAL DEFAULT 4.8);";

    const char* createOrdersTable = 
        "CREATE TABLE IF NOT EXISTS orders ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "buyer_email TEXT, "
        "product_name TEXT, "
        "quantity INTEGER, "
        "total_price REAL, "
        "status TEXT DEFAULT 'Pending', "
        "order_date DATETIME DEFAULT CURRENT_TIMESTAMP);";

    char* errMsg = nullptr;
    sqlite3_exec(db, createUsersTable, nullptr, nullptr, &errMsg);
    sqlite3_exec(db, createProductsTable, nullptr, nullptr, &errMsg);
    sqlite3_exec(db, createOrdersTable, nullptr, nullptr, &errMsg);

    // Auto-migrate any older table structure
    sqlite3_exec(db, "ALTER TABLE products ADD COLUMN category TEXT DEFAULT 'General';", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE products ADD COLUMN image_url TEXT;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE products ADD COLUMN rating REAL DEFAULT 4.8;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE orders ADD COLUMN status TEXT DEFAULT 'Pending';", nullptr, nullptr, nullptr);

    std::cout << "[DB] SQLite Database, SHA-256 Auth & Full Suite Initialized!\n";
}

int main() {
    initDatabase();

    // 1. Register API (With SHA-256 Hashing)
    app().registerHandler(
        "/api/register",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
            auto json = req->getJsonObject();
            Json::Value ret;

            if (!json || !(*json).isMember("email") || !(*json).isMember("password") || !(*json).isMember("name") || !(*json).isMember("role")) {
                ret["status"] = "error";
                ret["message"] = "All fields are required!";
                callback(HttpResponse::newHttpJsonResponse(ret));
                return;
            }

            std::string name = (*json)["name"].asString();
            std::string email = (*json)["email"].asString();
            std::string rawPass = (*json)["password"].asString();
            std::string role = (*json)["role"].asString();
            std::string hashedPass = drogon::utils::getSha256(rawPass);

            const char* sql = "INSERT INTO users (name, email, password, role) VALUES (?, ?, ?, ?);";
            sqlite3_stmt* stmt = nullptr;

            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, hashedPass.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, role.c_str(), -1, SQLITE_TRANSIENT);

                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    ret["status"] = "success";
                    ret["message"] = "User registered successfully!";
                } else {
                    ret["status"] = "error";
                    ret["message"] = "Email already registered!";
                }
                sqlite3_finalize(stmt);
            } else {
                ret["status"] = "error";
                ret["message"] = "Database statement error!";
            }

            callback(HttpResponse::newHttpJsonResponse(ret));
        },
        {Post}
    );

    // 2. Login API (Supports Hashed & Legacy Passwords)
    app().registerHandler(
        "/api/login",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
            auto json = req->getJsonObject();
            Json::Value ret;

            if (!json || !(*json).isMember("email") || !(*json).isMember("password")) {
                ret["status"] = "error";
                ret["message"] = "Email and password required!";
                callback(HttpResponse::newHttpJsonResponse(ret));
                return;
            }

            std::string email = (*json)["email"].asString();
            std::string password = (*json)["password"].asString();
            std::string hashedPass = drogon::utils::getSha256(password);

            const char* sql = "SELECT name, role FROM users WHERE email = ? AND (password = ? OR password = ?);";
            sqlite3_stmt* stmt = nullptr;

            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, hashedPass.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, password.c_str(), -1, SQLITE_TRANSIENT);

                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    ret["status"] = "success";
                    ret["name"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    ret["role"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    ret["email"] = email;
                } else {
                    ret["status"] = "error";
                    ret["message"] = "Invalid email or password!";
                }
                sqlite3_finalize(stmt);
            } else {
                ret["status"] = "error";
                ret["message"] = "Database error!";
            }

            callback(HttpResponse::newHttpJsonResponse(ret));
        },
        {Post}
    );

    // 3. Add Product API
    app().registerHandler(
        "/api/products/add",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
            auto json = req->getJsonObject();
            Json::Value ret;

            if (!json || !(*json).isMember("name") || !(*json).isMember("price") || !(*json).isMember("seller_email")) {
                ret["status"] = "error";
                ret["message"] = "Product details missing!";
                callback(HttpResponse::newHttpJsonResponse(ret));
                return;
            }

            std::string name = (*json)["name"].asString();
            double price = (*json)["price"].asDouble();
            std::string seller_email = (*json)["seller_email"].asString();
            std::string category = (*json).isMember("category") ? (*json)["category"].asString() : "General";
            std::string image_url = (*json).isMember("image_url") ? (*json)["image_url"].asString() : "";

            const char* sql = "INSERT INTO products (name, price, seller_email, category, image_url, rating) VALUES (?, ?, ?, ?, ?, 4.9);";
            sqlite3_stmt* stmt = nullptr;

            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(stmt, 2, price);
                sqlite3_bind_text(stmt, 3, seller_email.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, category.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 5, image_url.c_str(), -1, SQLITE_TRANSIENT);

                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    ret["status"] = "success";
                    ret["message"] = "Product added successfully!";
                } else {
                    ret["status"] = "error";
                    ret["message"] = "Failed to add product!";
                }
                sqlite3_finalize(stmt);
            } else {
                ret["status"] = "error";
                ret["message"] = "Database error!";
            }

            callback(HttpResponse::newHttpJsonResponse(ret));
        },
        {Post}
    );

    // 4. Get Products API
    app().registerHandler(
        "/api/products",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
            Json::Value ret;
            ret["products"] = Json::arrayValue;

            const char* sql = "SELECT id, name, price, seller_email, COALESCE(category, 'General'), COALESCE(image_url, ''), COALESCE(rating, 4.8) FROM products;";
            sqlite3_stmt* stmt = nullptr;

            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    Json::Value item;
                    item["id"] = sqlite3_column_int(stmt, 0);
                    item["name"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    item["price"] = sqlite3_column_double(stmt, 2);
                    item["seller_email"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                    item["category"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                    item["image_url"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                    item["rating"] = sqlite3_column_double(stmt, 6);
                    ret["products"].append(item);
                }
                sqlite3_finalize(stmt);
                ret["status"] = "success";
            } else {
                ret["status"] = "error";
                ret["message"] = "Failed to fetch products!";
            }

            callback(HttpResponse::newHttpJsonResponse(ret));
        },
        {Get}
    );

    // 5. Delete Product API
    app().registerHandler(
        "/api/products/delete",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
            auto json = req->getJsonObject();
            Json::Value ret;

            if (!json || !(*json).isMember("id")) {
                ret["status"] = "error";
                ret["message"] = "Product ID is required!";
                callback(HttpResponse::newHttpJsonResponse(ret));
                return;
            }

            int productId = (*json)["id"].asInt();
            const char* sql = "DELETE FROM products WHERE id = ?;";
            sqlite3_stmt* stmt = nullptr;

            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, productId);
                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    ret["status"] = "success";
                    ret["message"] = "Product deleted successfully!";
                } else {
                    ret["status"] = "error";
                    ret["message"] = "Failed to delete product!";
                }
                sqlite3_finalize(stmt);
            }
            callback(HttpResponse::newHttpJsonResponse(ret));
        },
        {Post}
    );

    // 6. Place Orders API
    app().registerHandler(
        "/api/orders/place",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
            auto json = req->getJsonObject();
            Json::Value ret;

            if (!json || !(*json).isMember("buyer_email") || !(*json).isMember("items") || !(*json)["items"].isArray()) {
                ret["status"] = "error";
                ret["message"] = "Invalid order data!";
                callback(HttpResponse::newHttpJsonResponse(ret));
                return;
            }

            std::string buyer_email = (*json)["buyer_email"].asString();
            auto items = (*json)["items"];

            const char* sql = "INSERT INTO orders (buyer_email, product_name, quantity, total_price, status) VALUES (?, ?, ?, ?, 'Pending');";
            sqlite3_stmt* stmt = nullptr;

            for (const auto& item : items) {
                std::string p_name = item["name"].asString();
                int qty = item["quantity"].asInt();
                double total = item["price"].asDouble() * qty;

                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(stmt, 1, buyer_email.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 2, p_name.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(stmt, 3, qty);
                    sqlite3_bind_double(stmt, 4, total);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }

            ret["status"] = "success";
            ret["message"] = "Order placed successfully!";
            callback(HttpResponse::newHttpJsonResponse(ret));
        },
        {Post}
    );

    // 7. Get All Orders API
    app().registerHandler(
        "/api/orders",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
            Json::Value ret;
            ret["orders"] = Json::arrayValue;

            const char* sql = "SELECT id, buyer_email, product_name, quantity, total_price, COALESCE(status, 'Pending'), order_date FROM orders ORDER BY id DESC;";
            sqlite3_stmt* stmt = nullptr;

            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    Json::Value item;
                    item["id"] = sqlite3_column_int(stmt, 0);
                    item["buyer_email"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    item["product_name"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                    item["quantity"] = sqlite3_column_int(stmt, 3);
                    item["total_price"] = sqlite3_column_double(stmt, 4);
                    item["status"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                    item["order_date"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
                    ret["orders"].append(item);
                }
                sqlite3_finalize(stmt);
                ret["status"] = "success";
            }
            callback(HttpResponse::newHttpJsonResponse(ret));
        },
        {Get}
    );

    // 8. Update Order Status API (Pending -> Shipped -> Delivered)
    app().registerHandler(
        "/api/orders/update-status",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
            auto json = req->getJsonObject();
            Json::Value ret;

            if (!json || !(*json).isMember("id") || !(*json).isMember("status")) {
                ret["status"] = "error";
                ret["message"] = "ID and status required!";
                callback(HttpResponse::newHttpJsonResponse(ret));
                return;
            }

            int orderId = (*json)["id"].asInt();
            std::string newStatus = (*json)["status"].asString();

            const char* sql = "UPDATE orders SET status = ? WHERE id = ?;";
            sqlite3_stmt* stmt = nullptr;

            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, newStatus.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 2, orderId);

                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    ret["status"] = "success";
                    ret["message"] = "Order status updated!";
                } else {
                    ret["status"] = "error";
                    ret["message"] = "Failed to update order status!";
                }
                sqlite3_finalize(stmt);
            }
            callback(HttpResponse::newHttpJsonResponse(ret));
        },
        {Post}
    );

    // Server Setup
    app()
        .setDocumentRoot("../public")
        .addListener("0.0.0.0", 8080)
        .run();

    if (db) sqlite3_close(db);
    return 0;
}