// Copyright (c) 2017, Xiaomi, Inc.  All rights reserved.
// This source code is licensed under the Apache License Version 2.0, which
// can be found in the LICENSE file in the root directory of this source tree.

#include "pegasus_mutation_duplicator.h"
#include "pegasus_server_impl.h"
#include "base/pegasus_rpc_types.h"

#include <dsn/cpp/message_utils.h>
#include <dsn/utility/chrono_literals.h>
#include <dsn/dist/replication/duplication_common.h>
#include <rrdb/rrdb.client.h>

namespace dsn {
namespace replication {

/// static definition of mutation_duplicator::creator.
/*static*/ std::function<std::unique_ptr<mutation_duplicator>(
    const replica_base &, string_view, string_view)>
    mutation_duplicator::creator = [](const replica_base &r, string_view remote, string_view app) {
        return make_unique<pegasus::server::pegasus_mutation_duplicator>(r, remote, app);
    };

} // namespace replication
} // namespace dsn

namespace pegasus {
namespace server {

using namespace dsn::literals::chrono_literals;

/*extern*/ uint64_t get_hash_from_request(dsn::task_code tc, const dsn::blob &data)
{
    if (tc == dsn::apps::RPC_RRDB_RRDB_PUT) {
        dsn::apps::update_request thrift_request;
        dsn::from_blob_to_thrift(data, thrift_request);
        return pegasus_key_hash(thrift_request.key);
    }
    if (tc == dsn::apps::RPC_RRDB_RRDB_REMOVE) {
        dsn::blob raw_key;
        dsn::from_blob_to_thrift(data, raw_key);
        return pegasus_key_hash(raw_key);
    }
    if (tc == dsn::apps::RPC_RRDB_RRDB_MULTI_PUT) {
        dsn::apps::multi_put_request thrift_request;
        dsn::from_blob_to_thrift(data, thrift_request);
        return pegasus_hash_key_hash(thrift_request.hash_key);
    }
    if (tc == dsn::apps::RPC_RRDB_RRDB_MULTI_REMOVE) {
        dsn::apps::multi_remove_request thrift_request;
        dsn::from_blob_to_thrift(data, thrift_request);
        return pegasus_hash_key_hash(thrift_request.hash_key);
    }
    dfatal("unexpected task code: %s", tc.to_string());
    __builtin_unreachable();
}

pegasus_mutation_duplicator::pegasus_mutation_duplicator(const dsn::replication::replica_base &r,
                                                         dsn::string_view remote_cluster,
                                                         dsn::string_view app)
    : dsn::replication::replica_base(r), _remote_cluster(remote_cluster)
{
    static bool _dummy = pegasus_client_factory::initialize(nullptr);

    pegasus_client *client = pegasus_client_factory::get_client(remote_cluster.data(), app.data());
    _client = static_cast<client::pegasus_client_impl *>(client);

    auto ret = dsn::replication::get_duplication_cluster_id(remote_cluster.data());
    dassert_replica(ret.is_ok(),
                    "invalid remote cluster: {}, err_ret: {}",
                    remote_cluster,
                    ret.get_error().description());
    _remote_cluster_id = static_cast<uint8_t>(ret.get_value());

    ddebug_replica("initialize mutation duplicator for local cluster [id:{}], "
                   "remote cluster [id:{}, addr:{}]",
                   get_current_cluster_id(),
                   _remote_cluster_id,
                   remote_cluster);
    dassert_replica(get_current_cluster_id() != _remote_cluster_id,
                    "invalid configuration of cluster_id");

    std::string str_gpid = fmt::format("{}", get_gpid());
    std::string name;

    name = fmt::format("duplicate_qps@{}", str_gpid);
    _duplicate_qps.init_app_counter(
        "app.pegasus", name.c_str(), COUNTER_TYPE_RATE, "statistic the qps of DUPLICATE request");

    name = fmt::format("duplicate_latency@{}", str_gpid);
    _duplicate_latency.init_app_counter("app.pegasus",
                                        name.c_str(),
                                        COUNTER_TYPE_NUMBER_PERCENTILES,
                                        "statistic the latency of DUPLICATE request");

    name = fmt::format("duplicate_failed_qps@{}", str_gpid);
    _duplicate_failed_qps.init_app_counter("app.pegasus",
                                           name.c_str(),
                                           COUNTER_TYPE_RATE,
                                           "statistic the qps of failed DUPLICATE request");
}

static bool is_delete_operation(dsn::task_code code)
{
    return code == dsn::apps::RPC_RRDB_RRDB_REMOVE || code == dsn::apps::RPC_RRDB_RRDB_MULTI_REMOVE;
}

void pegasus_mutation_duplicator::send(duplicate_rpc rpc, callback cb)
{
    uint64_t start = dsn_now_ns();

    _client->async_duplicate(
        rpc, [ cb = std::move(cb), rpc, start, this ](dsn::error_code err) mutable {
            _duplicate_qps->increment();

            if (err == dsn::ERR_OK) {
                err = dsn::error_code(rpc.response().error);
            }
            if (err == dsn::ERR_OK) {
                /// failure is not taken into latency calculation
                _duplicate_latency->set(dsn_now_ns() - start);

                dsn::service::zauto_lock _(_lock);
                _total_duplicated += 1;
            } else {
                _duplicate_failed_qps->increment();
                derror_replica("failed to ship mutation: {}, remote: {}", err, _remote_cluster);
                _failed = true;

                // retry in next batch
                uint64_t timestamp = extract_timestamp_from_timetag(rpc.request().timetag);
                mutation_tuple mt =
                    std::make_tuple(timestamp, rpc.request().task_code, rpc.request().raw_message);
                _pendings.insert(std::move(mt));
            }

            {
                dsn::service::zauto_lock _(_lock);
                _inflights.erase(rpc);
                if (_inflights.empty()) {
                    ddebug_replica("total duplicated: ", _total_duplicated);
                    cb(_failed, std::move(_pendings));
                }
            }
        });
}

void pegasus_mutation_duplicator::duplicate(mutation_tuple_set muts, callback cb)
{
    _failed = false;
    _request_hash_set.clear();

    for (auto mut : muts) {
        uint64_t timestamp = std::get<0>(mut);
        dsn::task_code rpc_code = std::get<1>(mut);
        dsn::blob data = std::get<2>(mut);
        uint64_t hash;

        // must be a write
        dsn::task_spec *task = dsn::task_spec::get(rpc_code);
        dassert_replica(task != nullptr && task->rpc_request_is_write_operation,
                        "invalid rpc type({})",
                        rpc_code);

        // extract the rpc wrapped inside if this is a DUPLICATE rpc
        if (rpc_code == dsn::apps::RPC_RRDB_RRDB_DUPLICATE) {
            dsn::apps::duplicate_request dreq;
            dsn::from_blob_to_thrift(data, dreq);

            auto timetag = static_cast<uint64_t>(dreq.timetag);
            uint8_t from_cluster_id = extract_cluster_id_from_timetag(timetag);
            if (from_cluster_id == _remote_cluster_id) {
                // ignore this mutation to prevent infinite duplication loop.
                continue;
            }

            hash = static_cast<uint64_t>(dreq.hash);
            data = std::move(dreq.raw_message);
            rpc_code = dreq.task_code;
            timestamp = extract_timestamp_from_timetag(timetag);
        } else {
            hash = get_hash_from_request(rpc_code, data);
        }

        mut = std::make_tuple(timestamp, rpc_code, data);
        if (_request_hash_set.find(hash) != _request_hash_set.end()) {
            _pendings.insert(mut);
        } else {
            _request_hash_set.insert(hash);

            auto dreq = dsn::make_unique<dsn::apps::duplicate_request>();
            dreq->task_code = rpc_code;
            dreq->hash = hash;
            dreq->raw_message = std::move(data);
            dreq->timetag = generate_timetag(
                timestamp, get_current_cluster_id(), is_delete_operation(rpc_code));
            duplicate_rpc rpc(std::move(dreq),
                              dsn::apps::RPC_RRDB_RRDB_DUPLICATE,
                              10_s, // TODO(wutao1): configurable timeout.
                              hash);
            _inflights.insert(std::move(rpc));
        }
    }

    dsn::service::zauto_lock _(_lock);
    for (auto drpc : _inflights) {
        send(std::move(drpc), cb);
    }
}

} // namespace server
} // namespace pegasus
