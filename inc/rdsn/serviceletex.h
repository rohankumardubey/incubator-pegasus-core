
# pragma once

# include <rdsn/servicelet.h>
# include <rdsn/internal/serialization.h>

namespace rdsn {
    namespace service {

        //
        // for TRequest/TResponse, we assume that the following routines are defined:
        //    marshall(binary_writer& writer, const T& val); 
        //    unmarshall(binary_reader& reader, __out T& val);
        // either in the namespace of ::rdsn::utils or T
        // developers may write these helper functions by their own, or use tools
        // such as protocol-buffer, thrift, or bond to generate these functions automatically
        // for their TRequest and TResponse
        //
        template <typename T>
        class serviceletex : public servicelet<T>
        {
        public:
            serviceletex(const char* nm);
            ~serviceletex();

        protected:
            template<typename TRequest, typename TResponse>
            rpc_response_task_ptr rpc_typed(
                const end_point& server_addr,
                task_code code,
                boost::shared_ptr<TRequest> req,
                std::function<void(error_code, boost::shared_ptr<TRequest>, boost::shared_ptr<TResponse>)> callback,
                int request_hash = 0,                
                int timeout_milliseconds = 0,
                int reply_hash = 0
                );

            template<typename TRequest, typename TResponse>
            rpc_response_task_ptr rpc_typed(
                const end_point& server_addr,
                task_code code,
                boost::shared_ptr<TRequest> req,
                void (T::*callback)(error_code, boost::shared_ptr<TRequest>, boost::shared_ptr<TResponse>),
                int request_hash = 0,                
                int timeout_milliseconds = 0,
                int reply_hash = 0
                );

            template<typename TRequest>
            rpc_response_task_ptr rpc_typed(
                const end_point& server_addr,
                task_code code,
                const TRequest& req,
                int hash = 0
                )
            {
                message_ptr msg = message::create_request(code, 0, hash);
                marshall(msg->writer(), req);

                return service_base::rpc_call(server_addr, msg);
            }

            void rpc_response(message_ptr& response)
            {
                return service_base::rpc_response(response);
            }

            template<typename TResponse>
            void rpc_response(message_ptr& request, TResponse& resp)
            {
                auto response = request->create_response();
                marshall(response, resp);
                service_base::rpc_response(response);
            }

        protected:
            //
            // routines for rpc handler registration
            //
            void register_rpc_handler(task_code rpc_code, const char* name, rpc_handler handler)
            {
                return servicelet<T>::register_rpc_handler(rpc_code, name, handler);
            }

            void register_rpc_handler(task_code rpc_code, const char* name_, void (T::*handler)(message_ptr&))
            {
                return servicelet<T>::register_rpc_handler(rpc_code, name_, handler);
            }

            template<typename TRequest>
            void register_rpc_handler(task_code rpc_code, const char* rpc_name_, void (T::*handler)(const TRequest&));

            template<typename TRequest, typename TResponse>
            void register_rpc_handler(task_code rpc_code, const char* rpc_name_, void (T::*handler)(const TRequest&, TResponse&));
            
        private:
            template<typename TRequest>
            void internal_rpc_handler1(message_ptr& request, void (T::*handler)(const TRequest&));

            template<typename TRequest, typename TResponse>
            void internal_rpc_handler2(message_ptr& request, void (T::*handler)(const TRequest&, TResponse&));

            template<typename TRequest, typename TResponse>
            void internal_rpc_reply_handler1(
                error_code err,
                message_ptr& request,
                message_ptr& response,
                std::function<void(error_code, boost::shared_ptr<TRequest>, boost::shared_ptr<TResponse>)> callback,
                boost::shared_ptr<TRequest> req
                );

            template<typename TRequest, typename TResponse>
            void internal_rpc_reply_handler2(
                error_code err,
                message_ptr& request,
                message_ptr& response,
                void (T::*callback)(error_code, boost::shared_ptr<TRequest>, boost::shared_ptr<TResponse>),
                boost::shared_ptr<TRequest> req
                );
        };

        // ------------- inline implementation ----------------
        template<typename T>
        serviceletex<T>::serviceletex(const char* nm)
            : servicelet<T>(nm)
        {
        }

        template<typename T>
        serviceletex<T>::~serviceletex()
        {
        }

        template<typename T> template<typename TRequest, typename TResponse>
        inline rpc_response_task_ptr serviceletex<T>::rpc_typed(
            const end_point& server_addr,
            task_code code,
            boost::shared_ptr<TRequest> req,
            std::function<void(error_code, boost::shared_ptr<TRequest>, boost::shared_ptr<TResponse>)> callback,
            int request_hash/* = 0*/,            
            int timeout_milliseconds /*= 0*/,
            int reply_hash /*= 0*/
            )
        {
            message_ptr msg = message::create_request(code, timeout_milliseconds, request_hash);
            ::rdsn::marshall(msg->writer(), *req);

            return service_base::rpc_call(
                server_addr,
                msg,
                std::bind(
                    &serviceletex<T>::internal_rpc_reply_handler1<TRequest, TResponse>, 
                    static_cast<T*>(this),
                    std::placeholders::_1, 
                    std::placeholders::_2, 
                    std::placeholders::_3,
                    callback, req),
                (reply_hash == 0 ? request_hash : reply_hash)
                );
        }
        
        template<typename T> template<typename TRequest, typename TResponse>
        inline void serviceletex<T>::internal_rpc_reply_handler1(
            error_code err,
            message_ptr& request,
            message_ptr& response,
            std::function<void(error_code, boost::shared_ptr<TRequest>, boost::shared_ptr<TResponse>)> callback,
            boost::shared_ptr<TRequest> req
            )
        {
            if (!err)
            {
                // TODO: exception handling
                boost::shared_ptr<TResponse> resp(new TResponse);
                unmarshall(response->reader(), *resp);
                callback(err, req, resp);
            }
            else
            {
                callback(err, req, nullptr);
            }
        }

        template<typename T> template<typename TRequest, typename TResponse>
        inline rpc_response_task_ptr serviceletex<T>::rpc_typed(
            const end_point& server_addr,
            task_code code,
            boost::shared_ptr<TRequest> req,
            void (T::*callback)(error_code, boost::shared_ptr<TRequest>, boost::shared_ptr<TResponse>),
            int request_hash/* = 0*/,            
            int timeout_milliseconds /*= 0*/,
            int reply_hash /*= 0*/
            )
        {
            message_ptr msg = message::create_request(code, timeout_milliseconds, request_hash);
            marshall(msg->writer(), *req);

            return service_base::rpc_call(
                server_addr,
                msg,
                std::bind(
                &serviceletex<T>::internal_rpc_reply_handler2<TRequest, TResponse>,
                static_cast<T*>(this),
                std::placeholders::_1,
                std::placeholders::_2,
                std::placeholders::_3,
                callback, req),
                (reply_hash == 0 ?  request_hash : reply_hash)
                );
        }
        
        template<typename T> template<typename TRequest, typename TResponse>
        inline void serviceletex<T>::internal_rpc_reply_handler2(
            error_code err,
            message_ptr& request,
            message_ptr& response,
            void (T::*callback)(error_code, boost::shared_ptr<TRequest>, boost::shared_ptr<TResponse>),
            boost::shared_ptr<TRequest> req
            )
        {
            if (!err)
            {
                // TODO: exception handling
                boost::shared_ptr<TResponse> resp(new TResponse);
                unmarshall(response->reader(), *resp);
                (static_cast<T*>(this)->*callback)(err, req, resp);
            }
            else
            {
                (static_cast<T*>(this)->*callback)(err, req, nullptr);
            }
        }

        template<typename T> template<typename TRequest>
        inline void serviceletex<T>::register_rpc_handler(task_code rpc_code, const char* rpc_name_, void (T::*handler)(const TRequest&))
        {
            std::string rpc_name = std::string(service_base::name()).append(".").append(rpc_name_);
            return service_base::register_rpc_handler(
                rpc_code, rpc_name.c_str(),
                std::bind(&serviceletex<T>::internal_rpc_handler1<TRequest>, static_cast<T*>(this), std::placeholders::_1, handler)
                );
        }

        template<typename T> template<typename TRequest, typename TResponse>
        inline void serviceletex<T>::register_rpc_handler(task_code rpc_code, const char* rpc_name_, void (T::*handler)(const TRequest&, TResponse&))
        {
            std::string rpc_name = std::string(service_base::name()).append(".").append(rpc_name_);
            return service_base::register_rpc_handler(
                rpc_code, rpc_name.c_str(),
                std::bind(&serviceletex<T>::internal_rpc_handler2<TRequest, TResponse>, static_cast<T*>(this), std::placeholders::_1, handler)
                );
        }
                
        template<typename T> template<typename TRequest>
        inline void serviceletex<T>::internal_rpc_handler1(message_ptr& request, void (T::*handler)(const TRequest&))
        {
            // TODO: exception handling
            TRequest req;
            unmarshall(request->reader(), req);
            (static_cast<T*>(this)->*handler)(req);
        }

        template<typename T> template<typename TRequest, typename TResponse>
        inline void serviceletex<T>::internal_rpc_handler2(message_ptr& request, void (T::*handler)(const TRequest&, TResponse&))
        {
            // TODO: exception handling
            TRequest req;
            TResponse resp;
            unmarshall(request->reader(), req);
            (static_cast<T*>(this)->*handler)(req, resp);

            rpc_response(request, resp);
        }
    } // end namespace service
} // end namespace



