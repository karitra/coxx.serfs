#include <iostream>
#include <functional>
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

using rx_type = fw::channel<io::app::enqueue>::receiver_type;


namespace app {

    auto on_send(send_move_future future, rx_type rx) -> chunk_future {
        dbg("[send]");
        future.get();
        dbg("[send] done");
        return rx.recv();
    }

    auto on_chunk(chunk_move_future future, rx_type rx) -> chunk_future {
        dbg("[chunk]");

        auto result = future.get();
        if (!result) {
            throw std::runtime_error("[chunk] the `result` must be set");
        }

        dbg("[chunk] result " << *result);
        return rx.recv();
    }

    auto on_choke(choke_move_future future) -> void {
        dbg("[choke]");
        auto result = future.get();
        if (!result) {
            dbg("[choke] no result");
        } else {
            dbg("[choke] result " << *result);
        }
    }

    auto on_invoke(invoke_move_future future, const std::string& message) -> void_future {
        auto ch = future.get();

        auto rx = std::move(ch.rx);
        auto tx = std::move(ch.tx);

        dbg("[invoke] sending");
        return tx.send<scope::chunk>(message)
            .then(std::bind(on_send, ph::_1, rx))
            .then(std::bind(on_chunk, ph::_1, rx))
            .then(std::bind(on_choke, ph::_1));
    }

    auto on_finalize(void_move_future future) -> void {
        dbg("[finalize]");
        future.get();
        dbg("[finalize] done");
    }
}


template<typename Application>
auto mass_requests(Application&& client, const int iters, const std::string&event, const std::string& message) -> void {
    using task_type = fw::task<void>::future_type;
    std::vector<task_type> completions;
    completions.reserve(ITERS);

    for(int i = 0; i < iters; ++i) {
        std::ostringstream os;
        os << message << '_'<< (i+1);
        auto f = client.template invoke<io::app::enqueue>(event)
            .then(std::bind(app::on_invoke, ph::_1, os.str()))
            .then(std::bind(app::on_finalize, ph::_1));

        completions.push_back(std::move(f));
        // std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    for(auto& f: completions) {
        f.get();
    }
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

    fw::service_manager_t manager(threads_count);

    auto cli = manager.create<cocaine::io::app_tag>(app_name);
    cli.connect().then([&] (void_move_future f) {
        f.get();
        std::cerr << "[connect] done\n";
    });

    mass_requests(cli, iters, method_name, payload);

    std::cout << "Have a nice day.\n";
    return EXIT_SUCCESS;
} catch(const std::exception& e) {
    std::cerr << "[error] " << e.what() << '\n';
    return EXIT_FAILURE;
}
