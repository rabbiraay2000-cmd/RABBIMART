# RABBIMART!! — E-commerce Website (Capstone)

Frontend: **HTML/CSS/JS** (`public/`) · Backend: **Java 17 + Javalin** (`src/main/java/shop/Main.java`) · Database: **SQLite** (`shop.db`, auto-created)

## Roles
| Role | Page | Login |
|---|---|---|
| Buyer | `buyer.html` — browse, search, filter, cart, checkout, invoice | `buyer@rabbimart.com` / `buyer123` (seeded) |
| Seller | `seller.html` — add product, view store orders | `seller@rabbimart.com` / `seller123` (seeded) |
| Admin | `admin.html` — stats, all orders + status control, delete products | `admin@rabbimart.com` / `admin123` (seeded) |

New accounts: use `register.html` (or `POST /api/register`) — stored SHA-256 hashed in `users` table.

## Run
1. Install Java 17+ and Maven, then double-click **`setup.bat`** (first time only).
2. Double-click **`run.bat`**, open http://localhost:8080/login.html

## APIs (same JSON as the old C++ Drogon backend)
- `POST /api/register` `{name,email,password,role}`
- `POST /api/login` `{email,password}` → `{status,name,role,email}`
- `GET /api/products` → `{products:[{id,name,price,seller_email,category,image_url,rating}]}`
- `POST /api/products/add` `{name,price,seller_email,category?,image_url?}`
- `POST /api/products/delete` `{id}`
- `POST /api/orders/place` `{buyer_email, items:[{name,price,quantity}]}`
- `GET /api/orders`
- `POST /api/orders/update-status` `{id,status}`

Passwords are SHA-256 hashed, never plain text. `shop.db` is created automatically on first start.
