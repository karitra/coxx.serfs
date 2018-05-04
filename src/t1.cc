#include <iostream>
#include <functional>
#include <memory>
#include <thread>
#include <chrono>

#include <cstdlib>

#include <cocaine/framework/service.hpp>
#include <cocaine/framework/channel.hpp>
#include <cocaine/framework/manager.hpp>

#include <cocaine/idl/node.hpp>

#include "detail/argagg.hpp"


#if 0
#define dbg(msg) std::cerr << msg << '\n'
#else
#define dbg(msg)
#endif


using namespace cocaine;
namespace fw = cocaine::framework;
namespace ph = std::placeholders;

using scope = io::protocol<io::app::enqueue::dispatch_type>::scope;

constexpr int DEFAULT_THREADS_COUNT = 1 << 5;
constexpr int ITERS = 5;

using void_future = fw::task<void>::future_type;
using void_move_future = fw::task<void>::future_move_type;

using invoke_move_future = fw::task<fw::channel<io::app::enqueue>>::future_move_type;
using send_move_future = fw::task<fw::channel<io::app::enqueue>::sender_type>::future_move_type;

using chunk_future = fw::task<boost::optional<std::string>>::future_type;
using choke_future = chunk_future;

using chunk_move_future = fw::task<boost::optional<std::string>>::future_move_type;
using choke_move_future = chunk_move_future;
using error_move_future = fw::task<void>::future_move_type;

using rx_type = fw::channel<io::app::enqueue>::receiver_type;


namespace app {

    auto on_send(send_move_future future, rx_type rx, const int num) -> chunk_future {
        dbg("[send] " << num);
        try {
            future.get();
        } catch(const std::exception& e) {
            dbg("[send] error " << num << ' ' << e.what());
            throw;
        }
        auto f = rx.recv();
        dbg("[send] done " << num);
        return f;
    }

    auto on_chunk(chunk_move_future future, rx_type rx, const int num) -> chunk_future {
        dbg("[chunk] " << num);

        try {
            auto result = future.get();
            if (!result) {
                throw std::runtime_error("[chunk] the `result` must be set");
            }
            dbg("[chunk] result " << num << ' ' << *result);
        } catch(const std::exception& e) {
            dbg("[chunk] error " << num << e.what());
            throw;
        }

        auto f = rx.recv();
        dbg("[chunk] done " << num);
        return f;
    }

    auto on_choke(choke_move_future future, const int num) -> void {
        dbg("[choke] " << num);
        try {
            auto result = future.get();
            if (!result) {
                dbg("[choke] done " << num);
            } else {
                dbg("[choke] result " << num << ' ' << *result);
            }
        } catch(const std::exception& e) {
            dbg("[choke] throw " << num << e.what());
            throw;
        }
    }

    auto on_invoke(invoke_move_future future, const std::string& message, const int id) -> void_future {
        try {
            dbg("[invoke] " << id);
            auto ch = future.get();

            auto rx = std::move(ch.rx);
            auto tx = std::move(ch.tx);

            dbg("[invoke] sending " << id);
            return tx.send<scope::chunk>(message)
                .then(std::bind(on_send, ph::_1, rx, id))
                .then(std::bind(on_chunk, ph::_1, rx, id))
                .then(std::bind(on_choke, ph::_1, id));
        } catch(const std::exception& e) {
            dbg("[invoke] throw " << id << ' ' << e.what());
            throw;
        }
    }

    auto on_finalize(void_move_future future, const int id) -> void {
        dbg("[finalize] " << id);
        try {
            future.get();
        // } catch(...) {
        } catch(const std::exception& e) {
            dbg("[finalize] throw " << id << ' ' << e.what());
            //dbg("[finalize] throw ");
            throw;
        }
        dbg("[finalize] done "  << id);
    }
}


template<typename Application>
auto mass_requests(Application&& client, const int iters, const std::string& event, const std::string& message) -> void {
    using task_type = fw::task<void>::future_type;
    std::vector<task_type> completions;
    completions.reserve(iters);

    for(int i = 0; i < iters; ++i) {
        std::ostringstream os;
        auto id = i + 1;
        os << message << '_' << id;
        auto f = client.template invoke<io::app::enqueue>(event)
            .then(std::bind(app::on_invoke, ph::_1, os.str(), id))
            .then(std::bind(app::on_finalize, ph::_1, id));

        completions.push_back(std::move(f));
    }

    auto counter = int{};
    for(auto& f: completions) {
        try {
            ++counter;
            dbg("[completion] " << counter);

            f.get();
            dbg("[completion] done " << counter);
        } catch(const std::exception& e) {
            dbg("[error] completions " << counter << ' ' << e.what());
        }
    }
}


auto make_service_manager(const int threads_count)
    -> std::shared_ptr<fw::service_manager_t>
{
    auto manager = std::make_shared<fw::service_manager_t>(threads_count);
    manager->shutdown_policy(fw::service_manager_t::shutdown_policy_t::force);

    return manager;
}

auto make_client(fw::service_manager_t &manager, const std::string& app_name, bool hard_shutdown)
    -> std::shared_ptr<fw::service<cocaine::io::app_tag>>
{
    auto cli = manager.create<cocaine::io::app_tag>(app_name);
    cli.hard_shutdown(true);

    return std::make_shared<fw::service<cocaine::io::app_tag>>(std::move(cli));
}

auto main(int argc, const char *argv[]) -> int try {
    argagg::parser argparser {{
        { "name",    {"-n", "--name"    }, "service name",              1},
        { "method",  {"-m", "--method"  }, "service method to call",    1},
        { "payload", {"-d", "--data"    }, "message to send",           1},
        { "iters",   {"-i", "--iters"   }, "number of requests",        1},
        { "threads", {"-t", "--threads" }, "number of manager threads", 1},
    }};

    auto args = argparser.parse(argc, argv);

    const auto app_name = args["name"].as<std::string>("echo.orig");
    const auto method_name = args["method"].as<std::string>("ping");
    const auto iters = args["iters"].as<int>(ITERS);
    const auto threads_count = args["threads"].as<int>(DEFAULT_THREADS_COUNT);
    const auto payload = args["payload"].as<std::string>("message");

    auto manager = make_service_manager(threads_count);
    auto cli = make_client(*manager, app_name, true);

    //
    // TODO: make loops finite
    //
    auto cli_th = std::thread([&] {
        while(true) {
            mass_requests(*cli, iters, method_name, payload);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10 * 1000));

    auto srv_th = std::thread([&] {
        while(true) {
            manager = make_service_manager(threads_count);
            cli = make_client(*manager, app_name, true);

            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    });


    cli_th.join();
    srv_th.join();

    std::cout << "Have a nice day.\n";
    return EXIT_SUCCESS;
} catch(const std::exception& e) {
    std::cerr << "[except] " << e.what() << '\n';
    return EXIT_FAILURE;
}
