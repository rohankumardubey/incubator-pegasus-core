
# include <rdsn/serviceletex.h>
# include <rdsn/internal/singleton.h>
# include <rdsn/internal/file_server.h>
# include <iostream>

namespace rdsn {
namespace service {

class service_objects : public ::rdsn::utils::singleton<service_objects>
{
public:
    void add(service_base* obj)
    {
        std::lock_guard<std::mutex> l(_lock);
        _services.insert(obj);
    }

    void remove(service_base* obj)
    {
        std::lock_guard<std::mutex> l(_lock);
        _services.erase(obj);
    }

private:
    std::mutex                _lock;
    std::set<service_base*> _services;
};

static service_objects* s_services = &(service_objects::instance());

service_base::service_base(const char* nm)
: _name(nm)
{
    _access_thread_id_inited = false;
    service_objects::instance().add(this);
}

service_base::~service_base()
{
    for (auto it = _events.begin(); it != _events.end(); it++)
    {
        bool r = unregister_rpc_handler(*it);
        rdsn_assert(r, "rpc handler unregister failed");
    }

    clear_outstanding_tasks();
    service_objects::instance().remove(this);
}

/*static*/ handle_t service_base::file_open(const char* file_name, int flag, int pmode)
{
    return file::open(file_name, flag, pmode);
}

void service_base::add_outstanding_task(task* tsk)
{
    std::lock_guard<std::mutex> l(_outstanding_tasks_lock);
    auto pr = _outstanding_tasks.insert(std::map<uint64_t, task*>::value_type(tsk->id(), tsk));
    rdsn_assert(pr.second, "task %llu must not be added to the hash table before", tsk->id());
}

void service_base::remove_outstanding_task(task* tsk)
{
    std::lock_guard<std::mutex> l(_outstanding_tasks_lock);
    auto pr = _outstanding_tasks.erase(tsk->id());
    rdsn_assert(pr == 1, "task %llu is not found in the hash table", tsk->id());
}

void service_base::clear_outstanding_tasks()
{
    std::lock_guard<std::mutex> l(_outstanding_tasks_lock);
    for (auto it = _outstanding_tasks.begin(); it != _outstanding_tasks.end(); it++)
    {
        it->second->cancel(true);
    }
    _outstanding_tasks.clear();
}

void service_base::check_hashed_access()
{
    if (_access_thread_id_inited)
    {
        rdsn_assert(std::this_thread::get_id() == _access_thread_id, "the service is assumed to be accessed by one thread only!");
    }
    else
    {
        _access_thread_id = std::this_thread::get_id();
        _access_thread_id_inited = true;
    }
}

/*static*/ task_ptr service_base::enqueue_task(
    task_code evt,
    service_base* svc,
    task_handler callback,
    int hash/* = 0*/,
    int delay_milliseconds /*= 0*/,
    int timer_interval_milliseconds /*= 0*/
    )
{
    task_ptr tsk;
    if (timer_interval_milliseconds == 0)
    {
        tsk = new service_task(evt, svc, callback, hash);
    }
    else
    {
        tsk = new service_timer_task(evt, svc, callback, timer_interval_milliseconds, hash);
    }

    tasking::enqueue(tsk, delay_milliseconds);
    return tsk;
}


void service_base::register_rpc_handler(task_code rpc_code, const char* name, rpc_handler handler)
{
    rpc::register_rpc_handler(rpc_code, name, new service_rpc_server_handler(this, handler));
    _events.insert(rpc_code);
}


bool service_base::unregister_rpc_handler(task_code rpc_code)
{
    return rpc::unregister_rpc_handler(rpc_code);
}

/*static*/ rpc_response_task_ptr service_base::rpc_call(
    const end_point& server_addr,
    message_ptr& request,
    service_base* svc /*= null*/,
    rpc_reply_handler callback /*= null*/,
    int reply_hash /*= 0*/
    )
{
    rpc_response_task_ptr tsk = nullptr;
    if (nullptr != callback)
    {
        tsk = new service_rpc_response_task(request, svc, callback, reply_hash);
    }

    return rpc::call(server_addr, request, tsk);
}


/*static*/ void service_base::rpc_response(message_ptr& response)
{
    return rpc::reply(response);
}


/*static*/ aio_task_ptr service_base::file_read(handle_t hFile, char* buffer, int count, uint64_t offset, task_code callback_code, service_base* svc, aio_handler callback, int hash)
{
    aio_task_ptr tsk = new service_aio_task(callback_code, svc, callback, hash);
    file::read(hFile, buffer, count, offset, tsk);
    return tsk;
}

/*static*/ aio_task_ptr service_base::file_write(handle_t hFile, const char* buffer, int count, uint64_t offset, task_code callback_code, service_base* svc, aio_handler callback, int hash)
{
    aio_task_ptr tsk = new service_aio_task(callback_code, svc, callback, hash);
    file::write(hFile, buffer, count, offset, tsk);
    return tsk;
}

/*static*/ void service_base::copy_remote_files(
    const end_point& remote,
    std::string& source_dir,
    std::vector<std::string>& files,  // empty for all
    std::string& dest_dir,
    bool overwrite,
    task_code callback_code,
    service_base* svc,
    aio_handler callback,
    int hash /*= 0*/
    )
{
    aio_task_ptr tsk = new service_aio_task(callback_code, svc, callback, hash);
    auto rci = new ::rdsn::service::remote_copy_request();
    rci->source = remote;
    rci->source_dir = source_dir;
    rci->files = files;
    rci->dest_dir = dest_dir;
    rci->overwrite = overwrite;

    rdsn_assert(false, "not implemented yet!!!");
}


}} // end namespace rdsn::service
