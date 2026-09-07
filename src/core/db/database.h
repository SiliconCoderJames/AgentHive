#pragma once
// SQLite 封装：所有外部输入一律通过 sqlite3_bind_* 参数绑定，
// 禁止任何字符串拼接 / format 组装 SQL。
#include <sqlite3.h>

#include <functional>
#include <string>

namespace zp {

class Stmt {
public:
    explicit Stmt(sqlite3_stmt* s = nullptr) : s_(s) {}
    ~Stmt() { if (s_) sqlite3_finalize(s_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    void bind(int idx, const std::string& v);
    void bind(int idx, int64_t v);
    void bind(int idx, double v);
    void bindBlob(int idx, const void* data, size_t n);
    int step();                    // SQLITE_ROW / SQLITE_DONE / 错误码
    void reset();
    std::string text(int col) const;
    int64_t i64(int col) const;
    double dbl(int col) const;
    bool isNull(int col) const;
    int lastError() const;

private:
    sqlite3_stmt* s_;
};

class Database {
public:
    Database() = default;
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // 打开（或创建）数据库并启用 WAL。
    bool open(const std::string& path, std::string& err);
    void close();

    // 单条语句：bind 回调填充参数（参数绑定），row 回调处理每行结果。
    bool query(const std::string& sql, const std::function<void(Stmt&)>& bind,
               const std::function<void(Stmt&)>& row, std::string& err);
    // 执行脚本（建表语句；不含任何外部输入）。
    bool execScript(const std::string& sql, std::string& err);

    int64_t lastInsertId() const { return last_insert_id_; }
    sqlite3* handle() { return db_; }

private:
    sqlite3* db_ = nullptr;
    int64_t last_insert_id_ = 0;
};

}  // namespace zp
