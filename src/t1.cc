#include <iostream>

#include <cocaine/framework/service.hpp>
#include <cocaine/framework/channel.hpp>
#include <cocaine/framework/manager.hpp>
//#include <cocaine/traits/error_code.hpp>
#include <cocaine/idl/node.hpp>


using namespace cocaine;
namespace fw = cocaine::framework;

using scope = io::protocol<io::app::enqueue::dispatch_type>::scope;


constexpr int DEFAULT_THREADS_COUNT = 1 << 7;
constexpr int ITERS = 3;


template<typename Application>
auto mass_requests(Application&& client, const int iters, const std::string&event, const std::string& message) -> void {
    using task_type = fw::task<void>::future_type;
    std::vector<task_type> completions;
    completions.reserve(ITERS);

    using invoke_future = fw::task<fw::channel<io::app::enqueue>>::future_move_type;
    using send_future = fw::task<fw::channel<io::app::enqueue>::sender_type>::future_move_type;

    using chunk_future = fw::task<boost::optional<std::string>>::future_move_type;
    using choke_future = chunk_future;

    using rx_type = fw::channel<io::app::enqueue>::receiver_type;

    for(int i = 0; i < iters; ++i) {
        // client.template invoke<io::app::enqueue>(event);
        auto f = client.template invoke<io::app::enqueue>(event)
            .then([&](invoke_future future) {
                auto ch = future.get();

                auto rx = std::move(ch.rx);
                auto tx = std::move(ch.tx);

                return tx.send<scope::chunk>(message)
                    .then([rx = std::move(rx)](send_future future) mutable {
                        std::cerr << "on send\n";
                        try {
                            future.get();
                        } catch (const std::exception& e) {
                            std::cerr << "send error: " << e.what() << '\n';
                        }
                        return rx.recv();
                    })
                    .then([rx = std::move(rx)](chunk_future future) mutable {
                        std::cerr << "on chunk\n";
                        try {
                            auto result = future.get();
                            if (!result) {
                                throw std::runtime_error("result must be true");
                            }
                            std::cerr << "result " << result << '\n';
                        } catch(const std::exception& e) {
                            std::cerr << "error: " << e.what() << '\n';
                            throw;
                        }
                        return rx.recv();
                    })
                    .then([](chunk_future future){
                        future.get();
                    });
            });

        // completions.push_back(std::move(f));
    }

    for(auto& f: completions) {
        f.get();
    }
}

auto main(int argc, char *argv[]) -> int {
    fw::service_manager_t manager(DEFAULT_THREADS_COUNT);

    auto echo = manager.create<cocaine::io::app_tag>("Echo4");

    std::cout << "Sample 'Echo' client.\n";

    echo.connect().then(+[] (fw::task<void>::future_move_type f) -> void {
        f.get();
        std::cerr << "connected!\n";
    });

    try {
        mass_requests(echo, ITERS, "meta", "message");
    } catch(const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
    }

    std::cout << "Have a nice day.\n";
}
