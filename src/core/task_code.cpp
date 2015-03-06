# include <rdsn/internal/task_code.h>
# include <rdsn/internal/singleton.h>
# include <rdsn/internal/perf_counters.h>
# include <vector>
# include <rdsn/internal/logging.h>

#define __TITLE__ "task_spec"

namespace rdsn {

task_code::task_code(const char* xxx, task_type type, threadpool_code pool, task_priority pri, int rpcPairedCode) 
    : rdsn::utils::customized_id<task_code>(xxx)
{
    if (!rdsn::utils::singleton_vector_store<task_spec*, nullptr>::instance().Contains(*this))
    {
        task_spec* spec = new task_spec(*this, xxx, type, pool, rpcPairedCode, pri);
        rdsn::utils::singleton_vector_store<task_spec*, nullptr>::instance().put(*this, spec);
    }
}

task_spec* task_spec::get(int code)
{
    return rdsn::utils::singleton_vector_store<task_spec*, nullptr>::instance().get(code);
}

task_spec::task_spec(int code, const char* name, task_type type, threadpool_code pool, int paired_code, task_priority pri)
    : code(code), name(name), type(type), pool_code(pool), rpc_paired_code(paired_code), priority(pri),
    on_task_enqueue((std::string(name) + std::string(".enqueue")).c_str()), 
    on_task_begin((std::string(name) + std::string(".begin")).c_str()), 
    on_task_end((std::string(name) + std::string(".end")).c_str()), 
    on_task_wait_pre((std::string(name) + std::string(".wait.pre")).c_str()), 
    on_task_wait_post((std::string(name) + std::string(".wait.post")).c_str()), 
    on_task_cancel_post((std::string(name) + std::string(".cancel.post")).c_str()), 
    on_task_cancelled((std::string(name) + std::string(".cancelled")).c_str()),
    on_aio_call((std::string(name) + std::string(".aio.call")).c_str()), 
    on_aio_enqueue((std::string(name) + std::string(".aio.enqueue")).c_str()), 
    on_rpc_call((std::string(name) + std::string(".rpc.call")).c_str()), 
    on_rpc_request_enqueue((std::string(name) + std::string(".rpc.request.enqueue")).c_str()),
    on_rpc_reply((std::string(name) + std::string(".rpc.reply")).c_str()), 
    on_rpc_response_enqueue((std::string(name) + std::string(".rpc.response.enqueue")).c_str()),
    rpc_message_channel(RPC_CHANNEL_TCP)
{
    rdsn_assert (
        strlen(name) <= MAX_TASK_CODE_NAME_LENGTH, 
        "task code name '%s' is too long: length must not be larger than MAX_TASK_CODE_NAME_LENGTH (%u)", 
        name, MAX_TASK_CODE_NAME_LENGTH
        );

    rejection_handler = nullptr;

    // TODO: config for following values
    rpc_message_channel = RPC_CHANNEL_TCP;
    rpc_default_timeout_milliseconds = 3600 * 1000; // 1 hr
    rpc_retry_interval_milliseconds = 3000;
    rpc_min_timeout_milliseconds_for_retry = 4000;
    async_rpc_max_send_time_milliseconds = 5000;
}

bool task_spec::init(configuration_ptr config)
{
    /*
    [task.default]
    is_trace = false
    is_profile = false

    [task.RPC_PREPARE]
    pool_code = THREAD_POOL_REPLICATION
    priority = TASK_PRIORITY_HIGH
    is_trace = true
    is_profile = true
    */

    task_spec defaultSpec(0, "placeholder", TASK_TYPE_COMPUTE, THREAD_POOL_DEFAULT, 0, TASK_PRIORITY_COMMON);
    defaultSpec.priority = enum_from_string(config->get_string_value("task.default", "priority", "TASK_PRIORITY_COMMON").c_str(), TASK_PRIORITY_INVALID);
    if (defaultSpec.priority == TASK_PRIORITY_INVALID)
    {
        rdsn_error("invalid task priority in [task.default]");
        return false;
    }

    defaultSpec.allow_inline = config->get_value<bool>("task.default", "allow_inline", false);

    auto cn = config->get_string_value("task.default", "rpc_message_channel", RPC_CHANNEL_TCP.to_string());
    if (!rpc_channel::is_exist(cn.c_str()))
    {
        rdsn_error("invalid task rpc_message_channel in [task.default]");
        return false;
    }
    defaultSpec.rpc_message_channel = rpc_channel::from_string(cn.c_str(), RPC_CHANNEL_TCP);    
    
    for (int code = 0; code <= task_code::max_value(); code++)
    {
        if (code == TASK_CODE_INVALID)
            continue;

        std::string section_name = std::string("task.") + std::string(task_code::to_string(code));
        task_spec* spec = task_spec::get(code);
        rdsn_assert (spec != nullptr, "task_spec cannot be null");

        if (config->has_section(section_name.c_str()))
        {
            auto pool = threadpool_code::from_string(config->get_string_value(section_name.c_str(), "pool_code", spec->pool_code.to_string()).c_str(), THREAD_POOL_INVALID);
            if (pool == THREAD_POOL_INVALID)
            {
                rdsn_error("invalid ThreadPool in [%s]", section_name.c_str());
                return false;
            }

            spec->pool_code.reset(pool);

            auto pri = enum_from_string(config->get_string_value(section_name.c_str(), "priority", enum_to_string(spec->priority)).c_str(), TASK_PRIORITY_INVALID);
            if (pri == TASK_PRIORITY_INVALID)
            {
                rdsn_error("invalid priority in [%s]", section_name.c_str());
                return false;
            }
            spec->priority = pri;

            spec->allow_inline = config->get_value<bool>(section_name.c_str(), "allow_inline", defaultSpec.allow_inline);

            auto cn = config->get_string_value(section_name.c_str(), "rpc_message_channel", defaultSpec.rpc_message_channel.to_string());
            if (!rpc_channel::is_exist(cn.c_str()))
            {
                rdsn_error("invalid task rpc_message_channel in [%s]", section_name.c_str());
                return false;
            }

            spec->rpc_message_channel = rpc_channel::from_string(cn.c_str(), RPC_CHANNEL_TCP);
        }
        else
        {
            spec->allow_inline = defaultSpec.allow_inline;
            spec->rpc_message_channel = defaultSpec.rpc_message_channel;
        }
    }

    return true;
}

} // end namespace
