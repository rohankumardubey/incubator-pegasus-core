// Copyright (c) 2017-present, Xiaomi, Inc.  All rights reserved.
// This source code is licensed under the Apache License Version 2.0, which
// can be found in the LICENSE file in the root directory of this source tree.

#include <nlohmann/json.hpp>
#include <fmt/format.h>
#include <dsn/utility/output_utils.h>
#include "replica_http_service.h"
#include "duplication/duplication_sync_timer.h"

namespace dsn {
namespace replication {

void replica_http_service::query_duplication_handler(const http_request &req, http_response &resp)
{
    if (!_stub->_duplication_sync_timer) {
        resp.body = "duplication is not enabled [duplication_enabled=false]";
        resp.status_code = http_status_code::not_found;
        return;
    }
    auto it = req.query_args.find("appid");
    if (it == req.query_args.end()) {
        resp.body = "appid should not be empty";
        resp.status_code = http_status_code::bad_request;
        return;
    }
    int32_t appid = -1;
    if (!buf2int32(it->second, appid) || appid < 0) {
        resp.status_code = http_status_code::bad_request;
        resp.body = fmt::format("invalid appid={}", it->second);
        return;
    }
    bool app_found = false;
    auto states = _stub->_duplication_sync_timer->get_dup_states(appid, &app_found);
    if (!app_found) {
        resp.status_code = http_status_code::not_found;
        resp.body = fmt::format("no primary for app [appid={}]", appid);
        return;
    }
    if (states.empty()) {
        resp.status_code = http_status_code::not_found;
        resp.body = fmt::format("no duplication assigned for app [appid={}]", appid);
        return;
    }

    nlohmann::json json;
    for (const auto &s : states) {
        json[std::to_string(s.first)][s.second.id.to_string()] = nlohmann::json{
            {"duplicating", s.second.duplicating},
            {"not_confirmed_mutations_num", s.second.not_confirmed},
            {"not_duplicated_mutations_num", s.second.not_duplicated},
            {"fail_mode", duplication_fail_mode_to_string(s.second.fail_mode)},
        };
    }
    resp.status_code = http_status_code::ok;
    resp.body = json.dump();
}

void replica_http_service::query_app_data_version_handler(const http_request &req,
                                                          http_response &resp)
{
    auto it = req.query_args.find("app_id");
    if (it == req.query_args.end()) {
        resp.body = "app_id should not be empty";
        resp.status_code = http_status_code::bad_request;
        return;
    }

    int32_t app_id = -1;
    if (!buf2int32(it->second, app_id) || app_id < 0) {
        resp.body = fmt::format("invalid app_id={}", it->second);
        resp.status_code = http_status_code::bad_request;
        return;
    }

    // partition_index -> data_version
    std::unordered_map<int32_t, uint32_t> version_map;
    _stub->query_app_data_version(app_id, version_map);

    if (version_map.size() == 0) {
        resp.body = fmt::format("app_id={} not found", it->second);
        resp.status_code = http_status_code::not_found;
        return;
    }

    dsn::utils::table_printer tp;
    tp.add_title("pidx");
    tp.add_column("data_version");
    for (const auto &kv : version_map) {
        tp.add_row(kv.first);
        tp.append_data(kv.second);
    }
    std::ostringstream out;
    tp.output(out, dsn::utils::table_printer::output_format::kJsonCompact);
    resp.body = out.str();
    resp.status_code = http_status_code::ok;
}

void replica_http_service::query_compaction_handler(const http_request &req, http_response &resp)
{
    auto it = req.query_args.find("app_id");
    if (it == req.query_args.end()) {
        resp.body = "app_id should not be empty";
        resp.status_code = http_status_code::bad_request;
        return;
    }

    int32_t app_id = -1;
    if (!buf2int32(it->second, app_id) || app_id < 0) {
        resp.body = fmt::format("invalid app_id={}", it->second);
        resp.status_code = http_status_code::bad_request;
        return;
    }

    std::unordered_map<gpid, manual_compaction_status> partition_compaction_status;
    _stub->query_app_compact_status(app_id, partition_compaction_status);

    int32_t running_count = 0;
    int32_t queue_count = 0;
    int32_t finish_count = 0;
    for (const auto &kv : partition_compaction_status) {
        if (kv.second == kRunning) {
            running_count++;
        } else if (kv.second == kQueue) {
            queue_count++;
        } else if (kv.second == kFinish) {
            finish_count++;
        }
    }
    dsn::utils::table_printer tp("status");
    tp.add_row_name_and_data(manual_compaction_status_to_string(kRunning), running_count);
    tp.add_row_name_and_data(manual_compaction_status_to_string(kQueue), queue_count);
    tp.add_row_name_and_data(manual_compaction_status_to_string(kFinish), finish_count);
    std::ostringstream out;
    tp.output(out, dsn::utils::table_printer::output_format::kJsonCompact);
    resp.body = out.str();
    resp.status_code = http_status_code::ok;
}

} // namespace replication
} // namespace dsn
