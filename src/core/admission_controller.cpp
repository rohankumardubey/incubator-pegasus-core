# include <rdsn/internal/admission_controller.h>
# include "task_engine.h"

namespace rdsn {

//
////-------------------------- BoundedQueueAdmissionController --------------------------------------------------
//
//// arguments: MaxTaskQueueSize
//BoundedQueueAdmissionController::BoundedQueueAdmissionController(task_queue* q, std::vector<std::string>& sargs)
//    : admission_controller(q, sargs)
//{
//    if (sargs.size() > 0)
//    {
//        _maxTaskQueueSize = atoi(sargs[0].c_str());
//        if (_maxTaskQueueSize <= 0)
//        {
//            rdsn_assert (false, "Invalid arguments for BoundedQueueAdmissionController: MaxTaskQueueSize = '%s'", sargs[0].c_str());
//        }
//    }
//    else
//    {
//        rdsn_assert (false, "arguments for BoundedQueueAdmissionController is missing: MaxTaskQueueSize");
//    }
//}
//
//BoundedQueueAdmissionController::~BoundedQueueAdmissionController(void)
//{
//}
//
//bool BoundedQueueAdmissionController::is_task_accepted(task_ptr& task)
//{
//    if (InQueueTaskCount() < _maxTaskQueueSize || task->spec().pool->shared_same_worker_with_current_task(task))
//    {
//        return true;
//    }
//    else
//    {
//        return false;
//    }
//}
//
//int  BoundedQueueAdmissionController::get_syste_utilization()
//{
//    return (int)(100.0 * (double)InQueueTaskCount() / (double)_maxTaskQueueSize);
//}
//
////------------------------------ SingleRpcClassResponseTimeAdmissionController ----------------------------------------------------------------
//
////      args: task_code PercentileType LatencyThreshold100ns(from task create to end in local process)
////
//
//SingleRpcClassResponseTimeAdmissionController::SingleRpcClassResponseTimeAdmissionController(task_queue* q, std::vector<std::string>& sargs)
//    : admission_controller(q, sargs)
//{
//    if (sargs.size() >= 3)
//    {
//        _rpcCode = enum_from_string(sargs[0].c_str(), TASK_CODE_INVALID);
//        _percentile = atoi(sargs[1].c_str());
//        _latencyThreshold100ns = atoi(sargs[2].c_str());
//
//        if (TASK_CODE_INVALID == _rpcCode || task_spec::get(_rpcCode).type != TASK_TYPE_RPC_REQUEST 
//            || _latencyThreshold100ns <= 0
//            || _percentile < 0
//            || _percentile >= 5
//            )
//        {
//            rdsn_assert (false, "Invalid arguments for SingleRpcClassResponseTimeAdmissionController: RpcRequestEventCode PercentileType(0-4) LatencyThreshold100ns\n"
//                "\tcounter percentile type (0-4): 999,   99,  95,  90,  50\n");
//        }
//
//        _counter = task_spec::get(_rpcCode).rpc_server_latency_100ns;
//    }
//    else
//    {
//        rdsn_assert (false, "arguments for SingleRpcClassResponseTimeAdmissionController is missing: RpcRequestEventCode PercentileType(0-4) LatencyThreshold100ns\n"
//            "\tcounter percentile type (0-4): 999,   99,  95,  90,  50\n");
//    }
//}
//
//SingleRpcClassResponseTimeAdmissionController::~SingleRpcClassResponseTimeAdmissionController(void)
//{
//}
//
//bool SingleRpcClassResponseTimeAdmissionController::is_task_accepted(task_ptr& task)
//{
//    if (task->spec().type != TASK_TYPE_RPC_REQUEST 
//        //|| task->spec().code == _rpcCode 
//        || _counter->get_percentile(_percentile) < _latencyThreshold100ns
//        )
//    {
//        return true;
//    }
//    else
//    {
//        return false;
//    }
//}
//
//int SingleRpcClassResponseTimeAdmissionController::get_syste_utilization()
//{
//    return (int)(100.0 * (double)_counter->get_percentile(_percentile) / (double)_latencyThreshold100ns);
//}

} // end namespace
