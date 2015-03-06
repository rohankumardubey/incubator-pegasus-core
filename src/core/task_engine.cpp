# include "task_engine.h"
# include <rdsn/internal/perf_counters.h>
# include <rdsn/internal/factory_store.h>
# include <rdsn/service_api.h>

#define __TITLE__ "task_engine"

using namespace rdsn::utils;

namespace rdsn {

task_worker_pool::task_worker_pool(const threadpool_spec& opts, task_engine* owner)
    : _spec(opts), _owner(owner), _node(owner->node())
{
    _is_running = false;
    _pending_task_counter = rdsn::utils::perf_counters::instance().get_counter((_spec.name + std::string(".PendingTask#")).c_str(),  COUNTER_TYPE_NUMBER, true);
}

void task_worker_pool::start()
{
    if (_is_running)
        return;
    
    int qCount = _spec.partitioned ?  _spec.worker_count : 1;
    for (int i = 0; i < qCount; i++)
    {
        task_queue* q = factory_store<task_queue>::create(_spec.queue_factory_name.c_str(), PROVIDER_TYPE_MAIN, this, i, nullptr);
        for (auto it = _spec.queue_aspects.begin();
            it != _spec.queue_aspects.end();
            it++)
        {
            q = factory_store<task_queue>::create(it->c_str(), PROVIDER_TYPE_ASPECT, this, i, q);
        }
        _queues.push_back(q);

        if (_spec.admission_controller_factory_name != "")
        {
            admission_controller* controller = factory_store<admission_controller>::create(_spec.admission_controller_factory_name.c_str(), 
                PROVIDER_TYPE_MAIN, 
                q, _spec.admission_controller_arguments.c_str());
        
            if (controller)
            {
                _controllers.push_back(controller);
                q->set_controller(controller);
            }
            else
            {
                _controllers.push_back(nullptr);
            }
        }
        else
        {
            _controllers.push_back(nullptr);
        }
    }

    for (int i = 0; i < _spec.worker_count; i++)
    {
        auto q = _queues[qCount == 1 ? 0 : i];
        task_worker* worker = factory_store<task_worker>::create(_spec.worker_factory_name.c_str(), PROVIDER_TYPE_MAIN, this, q, i, nullptr);
        for (auto it = _spec.worker_aspects.begin();
            it != _spec.worker_aspects.end();
            it++)
        {
            worker = factory_store<task_worker>::create(it->c_str(), PROVIDER_TYPE_ASPECT, this, q, i, worker);
        }
        task_worker::on_create.execute(worker);

        _workers.push_back(worker);
        worker->start();
    }

    _is_running = true;
}

void task_worker_pool::enqueue_task(task_ptr& task)
{
    rdsn_assert (task->spec().pool_code == spec().pool_code || task->spec().type == TASK_TYPE_RPC_RESPONSE, "Invalid thread pool used");

    if (_is_running)
    {
        int idx = (_spec.partitioned ? task->hash() % _queues.size() : 0);
        task_queue* q = _queues[idx];
        if (task->delay_milliseconds() == 0)
        {
            auto controller = _controllers[idx];
            if (controller != nullptr)
            {
                while (!controller->is_task_accepted(task))
                {
                    // any customized rejection handler?
                    if (task->spec().rejection_handler != nullptr)
                    {
                        task->spec().rejection_handler(task.get(), controller);

                        rdsn_log(log_level_DEBUG, __TITLE__,
                                "timer_task %s (%016llx) is rejected",                            
                                task->spec().name,
                                task->id()
                                );

                        return;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            else if (task->spec().type == TASK_TYPE_RPC_REQUEST && _spec.max_input_queue_length != INFINITE)
            {
                while ((uint32_t)q->count() >= _spec.max_input_queue_length)
                {
                    // any customized rejection handler?
                    if (task->spec().rejection_handler != nullptr)
                    {
                        task->spec().rejection_handler(task.get(), controller);

                        rdsn_log(log_level_DEBUG, __TITLE__,
                                "task %s (%016llx) is rejected because the target queue is full",                            
                                task->spec().name,
                                task->id()
                                );

                        return;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }

            _pending_task_counter->increment();
        }
                
        return q->enqueue(task);
    }
    else
    {
        rdsn_assert (false, "worker pool %s must be started before enqueue task %s",
            spec().name.c_str(),
            task->spec().name
            );
    }
}
//
//void task_worker_pool::on_dequeue(int count)
//{
//    _pending_task_counter->Subtract((unsigned long long)count);
//}

bool task_worker_pool::shared_same_worker_with_current_task(task* tsk) const
{
    task* current = task::get_current_task();
    if (nullptr != current)
    {
        if (current->spec().pool_code != tsk->code())
            return false;
        else if (_workers.size() == 1)
            return true;
        else if (_spec.partitioned)
        {
            int sz = (int)_workers.size();
            return current->hash() % sz == tsk->hash() % sz;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
}

task_engine::task_engine(service_node* node)
{
    _is_running = false;
    _node = node;
}

void task_engine::start(const std::vector<threadpool_spec>& spec)
{
    if (_is_running)
        return;

    // init pools
    _pools.resize(threadpool_code::max_value() + 1);
    for (auto it = spec.begin(); it != spec.end(); it++)
    {
        if ((*it).run)
        {
            auto workerPool = new task_worker_pool(*it, this);
            workerPool->start();
            _pools[workerPool->spec().pool_code] = workerPool;
        }
    }

    _is_running = true;
}

} // end namespace
