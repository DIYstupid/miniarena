#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

namespace miniarena {

// Generic connection pool with borrow/return semantics.
// T must be a pointer-like type (MYSQL*, redisContext*, etc.).
// Creator: T → creates a new connection. Returns nullptr on failure.
// Destroyer: void(T) → closes the connection.
template <typename T>
class ConnectionPool {
public:
    using Creator = std::function<T()>;
    using Destroyer = std::function<void(T)>;

    ConnectionPool(size_t size, Creator create, Destroyer destroy)
        : create_(std::move(create)), destroy_(std::move(destroy)) {
        for (size_t i = 0; i < size; ++i) {
            T conn = create_();
            if (!conn) {
                while (!pool_.empty()) {
                    destroy_(pool_.front());
                    pool_.pop();
                }
                throw std::runtime_error("ConnectionPool: failed to create connection " +
                                         std::to_string(i));
            }
            pool_.push(conn);
        }
    }

    ~ConnectionPool() {
        std::lock_guard<std::mutex> lock(mtx_);
        while (!pool_.empty()) {
            destroy_(pool_.front());
            pool_.pop();
        }
    }

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    // Borrow a connection. Blocks until one is available.
    // Returns a RAII guard that auto-returns on destruction.
    class Guard {
    public:
        Guard(ConnectionPool* pool, T conn) : pool_(pool), conn_(conn) {}
        ~Guard() { if (pool_ && conn_) pool_->returnConn(conn_); }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        Guard(Guard&& other) noexcept
            : pool_(other.pool_), conn_(other.conn_) {
            other.pool_ = nullptr;
            other.conn_ = nullptr;
        }
        Guard& operator=(Guard&& other) noexcept {
            if (this != &other) {
                if (pool_ && conn_) pool_->returnConn(conn_);
                pool_ = other.pool_;
                conn_ = other.conn_;
                other.pool_ = nullptr;
                other.conn_ = nullptr;
            }
            return *this;
        }

        T operator*() const { return conn_; }
        T get() const { return conn_; }
        explicit operator bool() const { return conn_ != nullptr; }

    private:
        ConnectionPool* pool_;
        T conn_;
    };

    Guard borrow() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return !pool_.empty(); });
        T conn = pool_.front();
        pool_.pop();
        return Guard(this, conn);
    }

    size_t available() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return pool_.size();
    }

private:
    void returnConn(T conn) {
        std::lock_guard<std::mutex> lock(mtx_);
        pool_.push(conn);
        cv_.notify_one();
    }

    std::queue<T> pool_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    Creator create_;
    Destroyer destroy_;
};

}  // namespace miniarena
