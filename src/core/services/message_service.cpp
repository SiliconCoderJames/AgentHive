#include "core/services/message_service.h"

#include "core/util.h"

namespace zp {

bool MessageService::send(const std::string& kind, const std::string& sender,
                          const std::string& recipient, const std::string& subject,
                          const std::string& body, const std::string& parentUuid, Message& out,
                          std::string& err) {
    if (kind != "note" && kind != "question" && kind != "task") {
        err = "invalid message kind: " + kind;
        return false;
    }
    std::string uuid = uuid4();
    std::string initialStatus = kind == "task" ? "pending" : "unread";
    if (!db_.query(
            "INSERT INTO messages(uuid, kind, sender, recipient, subject, body, status, parent_uuid, created_at) "
            "VALUES (?,?,?,?,?,?,?,?,?)",
            [&](Stmt& st) {
                st.bind(1, uuid);
                st.bind(2, kind);
                st.bind(3, sender);
                st.bind(4, recipient);
                st.bind(5, subject);
                st.bind(6, body);
                st.bind(7, initialStatus);
                st.bind(8, parentUuid);
                st.bind(9, nowIso());
            },
            nullptr, err))
        return false;
    return get(uuid, out, err);
}

bool MessageService::list(const std::string& recipientFilter, const std::string& kindFilter,
                          const std::string& statusFilter, const std::string& sinceIso, int limit,
                          std::vector<Message>& out, std::string& err) {
    std::string sql =
        "SELECT id, uuid, kind, sender, recipient, subject, body, status, parent_uuid, created_at "
        "FROM messages WHERE 1=1";
    if (!recipientFilter.empty()) sql += " AND (recipient IS NULL OR recipient = ?)";
    if (!kindFilter.empty()) sql += " AND kind = ?";
    if (!statusFilter.empty()) sql += " AND status = ?";
    if (!sinceIso.empty()) sql += " AND created_at >= ?";
    sql += " ORDER BY id DESC LIMIT ?";
    int idx = 1;
    out.clear();
    return db_.query(
        sql,
        [&](Stmt& st) {
            if (!recipientFilter.empty()) st.bind(idx++, recipientFilter);
            if (!kindFilter.empty()) st.bind(idx++, kindFilter);
            if (!statusFilter.empty()) st.bind(idx++, statusFilter);
            if (!sinceIso.empty()) st.bind(idx++, sinceIso);
            st.bind(idx, static_cast<int64_t>(limit > 0 ? limit : 100));
        },
        [&](Stmt& st) {
            Message m;
            m.id = st.i64(0);
            m.uuid = st.text(1);
            m.kind = st.text(2);
            m.sender = st.text(3);
            m.recipient = st.isNull(4) ? std::string() : st.text(4);
            m.subject = st.isNull(5) ? std::string() : st.text(5);
            m.body = st.text(6);
            m.status = st.text(7);
            m.parent_uuid = st.isNull(8) ? std::string() : st.text(8);
            m.created_at = st.text(9);
            out.push_back(std::move(m));
        },
        err);
}

bool MessageService::get(const std::string& uuid, Message& out, std::string& err) {
    bool found = false;
    bool ok = db_.query(
        "SELECT id, uuid, kind, sender, recipient, subject, body, status, parent_uuid, created_at "
        "FROM messages WHERE uuid=?",
        [&](Stmt& st) { st.bind(1, uuid); },
        [&](Stmt& st) {
            out.id = st.i64(0);
            out.uuid = st.text(1);
            out.kind = st.text(2);
            out.sender = st.text(3);
            out.recipient = st.isNull(4) ? std::string() : st.text(4);
            out.subject = st.isNull(5) ? std::string() : st.text(5);
            out.body = st.text(6);
            out.status = st.text(7);
            out.parent_uuid = st.isNull(8) ? std::string() : st.text(8);
            out.created_at = st.text(9);
            found = true;
        },
        err);
    if (!ok) return false;
    if (!found) { err = "message not found: " + uuid; return false; }
    return true;
}

bool MessageService::reply(const std::string& parentUuid, const std::string& sender,
                           const std::string& body, Message& out, std::string& err) {
    Message parent;
    if (!get(parentUuid, parent, err)) return false;
    std::string recipient = parent.sender == sender ? parent.recipient : parent.sender;
    if (!send("note", sender, recipient, std::string("Re: ") + (parent.subject.empty() ? "" : parent.subject),
              body, parentUuid, out, err))
        return false;
    // 回复即视为已读父消息（状态流转，不修改内容）
    db_.query("UPDATE messages SET status='read' WHERE uuid=? AND kind!='task'",
              [&](Stmt& st) { st.bind(1, parentUuid); }, nullptr, err);
    return true;
}

bool MessageService::setStatus(const std::string& uuid, const std::string& newStatus, Message& out,
                               std::string& err) {
    Message cur;
    if (!get(uuid, cur, err)) return false;

    bool allowed = false;
    if (cur.kind == "task") {
        if (cur.status == "pending" && (newStatus == "accepted" || newStatus == "declined"))
            allowed = true;
        else if (cur.status == "accepted" && newStatus == "done")
            allowed = true;
    } else {
        if (cur.status == "unread" && newStatus == "read") allowed = true;
    }
    if (!allowed) {
        err = "illegal status transition: " + cur.kind + " " + cur.status + " -> " + newStatus;
        return false;
    }
    if (!db_.query("UPDATE messages SET status=? WHERE uuid=?",
                   [&](Stmt& st) { st.bind(1, newStatus); st.bind(2, uuid); }, nullptr, err))
        return false;
    return get(uuid, out, err);
}

}  // namespace zp
