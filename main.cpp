//HOW TO RUN: [g++ main.cpp -o main -I include -std=c++17 -lws2_32 -lmswsock]
//npm run dev
#include "crow_all.h"
#include <string>
#include <mutex>
#include <atomic>
#include <fstream>
#include <sstream>

struct OrderData {
    int         order_id;
    std::string customer;
    std::string menu;
    int         quantity;

    crow::json::wvalue to_json() const {
        crow::json::wvalue j;
        j["order_id"] = order_id;
        j["customer"] = customer;
        j["menu"]     = menu;
        j["quantity"] = quantity;
        return j;
    }
};

struct Node {
    OrderData data;
    Node*     next;
    Node(OrderData d) : data(d), next(nullptr) {}
};

class OrderQueue {
private:
    Node* front;
    Node* rear;
    int   count;
public:
    OrderQueue() : front(nullptr), rear(nullptr), count(0) {}
    ~OrderQueue() {
        while (front != nullptr) {
            Node* temp = front;
            front = front->next;
            delete temp;
        }
    }
    void enqueue(const OrderData& data) {
        Node* newNode = new Node(data);
        if (rear == nullptr) { front = rear = newNode; }
        else { rear->next = newNode; rear = newNode; }
        count++;
    }
    bool dequeue(OrderData& out) {
        if (front == nullptr) return false;
        Node* temp = front;
        out = front->data;
        front = front->next;
        if (front == nullptr) rear = nullptr;
        delete temp;
        count--;
        return true;
    }
    bool peek(OrderData& out) const {
        if (front == nullptr) return false;
        out = front->data;
        return true;
    }
    bool isEmpty() const { return front == nullptr; }
    int  size()    const { return count; }
    crow::json::wvalue toJson() const {
        crow::json::wvalue result;
        std::vector<crow::json::wvalue> items;
        Node* current = front;
        int position = 1;
        while (current != nullptr) {
            crow::json::wvalue item = current->data.to_json();
            item["position"] = position++;
            items.push_back(std::move(item));
            current = current->next;
        }
        result["count"] = count;
        result["queue"] = std::move(items);
        return result;
    }
};

class OrderStack {
private:
    Node* top;
    int   count;
public:
    OrderStack() : top(nullptr), count(0) {}
    ~OrderStack() {
        while (top != nullptr) {
            Node* temp = top;
            top = top->next;
            delete temp;
        }
    }
    void push(const OrderData& data) {
        Node* newNode = new Node(data);
        newNode->next = top;
        top = newNode;
        count++;
    }
    bool pop(OrderData& out) {
        if (top == nullptr) return false;
        Node* temp = top;
        out = top->data;
        top = top->next;
        delete temp;
        count--;
        return true;
    }
    bool peek(OrderData& out) const {
        if (top == nullptr) return false;
        out = top->data;
        return true;
    }
    bool isEmpty() const { return top == nullptr; }
    int  size()    const { return count; }
    crow::json::wvalue toJson() const {
        crow::json::wvalue result;
        std::vector<crow::json::wvalue> items;
        Node* current = top;
        while (current != nullptr) {
            items.push_back(current->data.to_json());
            current = current->next;
        }
        result["count"]   = count;
        result["history"] = std::move(items);
        return result;
    }
};

// ============================================================
// Helper: tambahkan CORS header ke setiap response
// Ini wajib agar browser tidak memblokir request ke API kita
// ============================================================
void addCorsHeaders(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin", "*");
    res.add_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.add_header("Access-Control-Allow-Headers", "Content-Type");
}

// ============================================================
// GLOBAL STATE
// ============================================================
OrderQueue orderQueue;
OrderStack orderHistory;
std::mutex dataMutex;
std::atomic<int> nextId{1};

int main() {
    crow::SimpleApp app;

    // ----------------------------------------------------------
    // GET / — Serve file index.html ke browser
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/")
    ([]() {
        std::ifstream file("index.html");
        if (!file.is_open()) {
            return crow::response(404, "index.html tidak ditemukan");
        }
        std::stringstream ss;
        ss << file.rdbuf();
        crow::response res(ss.str());
        res.add_header("Content-Type", "text/html");
        return res;
    });

    // ----------------------------------------------------------
    // OPTIONS — Handle preflight CORS request dari browser
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/order").methods(crow::HTTPMethod::Options)
    ([]() {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin", "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type");
        return res;
    });

    // ----------------------------------------------------------
    // POST /order — Tambah pesanan ke antrean
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/order").methods(crow::HTTPMethod::Post)
    ([](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("customer") || !body.has("menu") || !body.has("quantity")) {
            crow::response res(400);
            crow::json::wvalue err;
            err["status"]  = "error";
            err["message"] = "Field 'customer', 'menu', dan 'quantity' wajib diisi";
            res.write(err.dump());
            addCorsHeaders(res);
            return res;
        }
        OrderData newOrder;
        newOrder.order_id = nextId++;
        newOrder.customer = body["customer"].s();
        newOrder.menu     = body["menu"].s();
        newOrder.quantity = body["quantity"].i();
        if (newOrder.quantity <= 0) {
            crow::response res(400);
            crow::json::wvalue err;
            err["status"]  = "error";
            err["message"] = "Quantity harus lebih dari 0";
            res.write(err.dump());
            addCorsHeaders(res);
            return res;
        }
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            orderQueue.enqueue(newOrder);
        }
        crow::response res(201);
        crow::json::wvalue r;
        r["status"]  = "success";
        r["message"] = "Pesanan berhasil ditambahkan ke antrean";
        r["data"]    = newOrder.to_json();
        res.write(r.dump());
        addCorsHeaders(res);
        return res;
    });

    // ----------------------------------------------------------
    // POST /process — Proses pesanan terdepan
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/process").methods(crow::HTTPMethod::Post)
    ([]() {
        std::lock_guard<std::mutex> lock(dataMutex);
        if (orderQueue.isEmpty()) {
            crow::response res(404);
            crow::json::wvalue err;
            err["status"]  = "error";
            err["message"] = "Tidak ada pesanan dalam antrean";
            res.write(err.dump());
            addCorsHeaders(res);
            return res;
        }
        OrderData processed;
        orderQueue.dequeue(processed);
        orderHistory.push(processed);
        crow::response res(200);
        crow::json::wvalue r;
        r["status"]  = "success";
        r["message"] = "Pesanan berhasil diproses";
        r["data"]    = processed.to_json();
        res.write(r.dump());
        addCorsHeaders(res);
        return res;
    });

    // ----------------------------------------------------------
    // GET /queue — Tampilkan antrean
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/queue").methods(crow::HTTPMethod::Get)
    ([]() {
        std::lock_guard<std::mutex> lock(dataMutex);
        crow::response res(200);
        crow::json::wvalue r = orderQueue.toJson();
        r["status"] = "success";
        res.write(r.dump());
        addCorsHeaders(res);
        return res;
    });

    // ----------------------------------------------------------
    // GET /history — Tampilkan riwayat
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/history").methods(crow::HTTPMethod::Get)
    ([]() {
        std::lock_guard<std::mutex> lock(dataMutex);
        crow::response res(200);
        crow::json::wvalue r = orderHistory.toJson();
        r["status"] = "success";
        res.write(r.dump());
        addCorsHeaders(res);
        return res;
    });

    // ----------------------------------------------------------
    // GET /history/last — Pesanan terakhir diproses
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/history/last").methods(crow::HTTPMethod::Get)
    ([]() {
        std::lock_guard<std::mutex> lock(dataMutex);
        if (orderHistory.isEmpty()) {
            crow::response res(404);
            crow::json::wvalue err;
            err["status"]  = "error";
            err["message"] = "Belum ada pesanan yang diproses";
            res.write(err.dump());
            addCorsHeaders(res);
            return res;
        }
        OrderData last;
        orderHistory.peek(last);
        crow::response res(200);
        crow::json::wvalue r;
        r["status"]  = "success";
        r["message"] = "Pesanan terakhir yang diproses";
        r["data"]    = last.to_json();
        res.write(r.dump());
        addCorsHeaders(res);
        return res;
    });

    // ----------------------------------------------------------
    // GET /stats — Statistik jumlah pesanan
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/stats").methods(crow::HTTPMethod::Get)
    ([]() {
        std::lock_guard<std::mutex> lock(dataMutex);
        crow::response res(200);
        crow::json::wvalue r;
        r["status"]          = "success";
        r["queue_count"]     = orderQueue.size();
        r["history_count"]   = orderHistory.size();
        r["total_processed"] = orderHistory.size();
        res.write(r.dump());
        addCorsHeaders(res);
        return res;
    });

    app.port(8080).multithreaded().run();
    return 0;
}