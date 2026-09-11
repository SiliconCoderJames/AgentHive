#pragma once
// Agent 异步交流：留言 / 提问 / 指派任务（不要求同时在线）。
#include <string>
#include <vector>

#include "core/db/database.h"
#include "core/types.h"

namespace zp {

class MessageService {
public:
    explicit MessageService(Database& db) : db_(db) {}

    bool send(const std::string& kind, const std::string& sender, const std::string& recipient,
              const std::string& subject, const std::string& body, const std::string& parentUuid,
              Message& out, std::string& err);
    // viewer 非空时按可见性收敛：广播（recipient 为空）+ 发给 viewer + viewer 发出的。
    // viewer 为空表示进程内 GUI/管理视角，不做收敛。
    bool list(const std::string& recipientFilter, const std::string& kindFilter,
              const std::string& statusFilter, const std::string& sinceIso, int limit,
              std::vector<Message>& out, std::string& err, const std::string& viewer = {});
    bool get(const std::string& uuid, Message& out, std::string& err);
    bool reply(const std::string& parentUuid, const std::string& sender, const std::string& body,
               Message& out, std::string& err);
    // 状态流转（调用方先做权限校验）：
    //   note/question: unread -> read
    //   task: pending -> accepted/declined ; accepted -> done
    bool setStatus(const std::string& uuid, const std::string& newStatus, Message& out,
                   std::string& err);

private:
    Database& db_;
};

}  // namespace zp
