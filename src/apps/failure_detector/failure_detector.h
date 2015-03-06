# pragma once

# include <rdsn/serviceletex.h>

using namespace rdsn::service;

namespace rdsn { namespace fd {

DEFINE_THREAD_POOL_CODE(THREAD_POOL_FAILURE_DETECTOR)

DEFINE_TASK_CODE(LPC_BEACON_CHECK, TASK_PRIORITY_HIGH, THREAD_POOL_FAILURE_DETECTOR)

DEFINE_TASK_CODE_RPC(RPC_BEACON, TASK_PRIORITY_HIGH, THREAD_POOL_FAILURE_DETECTOR)

class failure_detector_callback
{
public:
    // client side
    virtual void on_master_disconnected( const std::vector<end_point>& nodes ) = 0;
    virtual void on_master_connected( const end_point& node) = 0;

    // server side
    virtual void on_worker_disconnected( const std::vector<end_point>& nodes ) = 0;
    virtual void on_worker_connected( const end_point& node ) = 0;
};

class failure_detector : public serviceletex<failure_detector>, public failure_detector_callback
{
public:
    failure_detector(const char* service_name)
    : serviceletex<failure_detector>(service_name)
    {
        _is_started = false;
        _currentTask = nullptr;
    }

    ~failure_detector()
    {
    }

private:
    enum record_status
    {
        ST_Disconnected,
        ST_Connected
    };

    class master_record
    {
    public:
        end_point       node;
        uint64_t        last_send_time_for_beacon_with_ack;
        uint64_t        next_beacon_time;
        record_status   status;
        bool            rejected;

        // masters are always considered *disconnected* initially which is ok even when master thinks workers are connected
        master_record(const end_point& n, uint64_t last_send_time_for_beacon_with_ack_, uint64_t next_beacon_time_)
        {
            node                               = n;
            last_send_time_for_beacon_with_ack = last_send_time_for_beacon_with_ack_;
            next_beacon_time        = next_beacon_time_;
            status                 = ST_Disconnected;
            rejected               = false;
        }
    };

    class worker_record
    {
    public:
        end_point       node;
        uint64_t        last_beacon_recv_time;
        record_status   status;

        // workers are always considered *connected* initially which is ok even when workers think master is disconnected
        worker_record(const end_point& node, uint64_t last_beacon_recv_time)
        {
            this->node                  = node;
            this->last_beacon_recv_time = last_beacon_recv_time;
            status                      = ST_Connected;
        }
    };

public:

    bool init( uint32_t check_interval_seconds,  uint32_t beacon_interval_seconds,
               uint32_t lease_seconds,  uint32_t grace_seconds, bool use_allow_list = false) ;

    bool uninit();

    void on_configuration_changed(
         uint32_t beacon_interval_seconds,
         uint32_t lease_seconds,
         uint32_t grace_seconds,
         uint32_t check_interval_seconds);

    int  start();

    int  stop();

    void register_master( const end_point& target);

    bool switch_master(const end_point& from, const end_point& to);

    bool unregister_master( const end_point& node);

    bool is_master_connected( const end_point& node) const;

    // ATTENTION: be very careful to set is_connected to false as
    // workers are always considered *connected* initially which is ok even when workers think master is disconnected
    // Considering workers *disconnected* initially is *dangerous* coz it may violate the invariance when workers think they are online 
    void register_worker( const end_point& node, bool is_connected = true);

    bool unregister_worker( const end_point& node);

    void clear_workers();

    bool is_worker_connected( const end_point& node) const;

    void add_allow_list( const end_point& node);

    bool remove_from_allow_list( const end_point& node);

    int  worker_count() const { return (int)_workers.size(); }

    int  master_count() const { return (int)_masters.size(); }

public:
    struct beacon_msg
    {
        uint64_t time;
        end_point from;
        end_point to;
    };

    struct beacon_ack
    {
        uint64_t time;
        bool is_master;
        end_point primary_node;
        bool allowed;
    };

protected:
    virtual void on_beacon(const beacon_msg& beacon, __out beacon_ack& ack);

    virtual void on_beacon_ack(error_code err, boost::shared_ptr<beacon_msg> beacon, boost::shared_ptr<beacon_ack> ack);

    bool is_time_greater_than(uint64_t ts, uint64_t base); 

    void report(const end_point& node, bool is_master, bool is_connected);

private:
    void process_all_records();

private:    
    typedef std::map<end_point, master_record>    master_map;
    typedef std::map<end_point, worker_record>     worker_map;

    // allow list are set on machine name (port can vary)
    typedef std::set<end_point>   allow_list;

    mutable zlock        _lock;
    master_map           _masters;
    worker_map            _workers;

    uint32_t             _beacon_interval_milliseconds;
    uint32_t             _check_interval_milliseconds;
    uint32_t             _lease_milliseconds;
    uint32_t             _grace_milliseconds;
    bool                 _is_started;
    task_ptr             _currentTask;

    bool                 _use_allow_list;
    allow_list           _allow_list;

protected:
    // subClass can rewrite these method.
    virtual void send_beacon(const end_point& node, uint64_t time);
};

}} // end namespace
