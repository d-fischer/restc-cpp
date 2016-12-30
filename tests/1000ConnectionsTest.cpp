


// Include before boost::log headers
#include "restc-cpp/restc-cpp.h"
#include "restc-cpp/logging.h"
#include "restc-cpp/RequestBuilder.h"
#include "restc-cpp/SerializeJson.h"

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

#include "UnitTest++/UnitTest++.h"

using namespace std;
using namespace restc_cpp;


const string http_url = "http://localhost:3000/manyposts";

/* The goal is to test with 1000 connections.
 * However, I am unable to get more than 500 working reliable (with 100
 * connections increment) before I see connection errors.
 *
 * I don't know at this time if it is caused by OS limits in the test
 * application, Docker, the container with the mock server or the Linux
 * machine itself.
 *
 * May be I have to write a simple HTTP Mock sever in C++ or use
 * nginx-lua with some tweaking / load-balancing to get this test
 * to work with the 1000 connection goal.
 *
 * However, 500 connections is sufficient to prove that the client
 * works as expected with many co-routines in parallell.
 */

#define CONNECTIONS 500

struct Post {
    int id = 0;
    string username;
    string motto;
};

BOOST_FUSION_ADAPT_STRUCT(
    Post,
    (int, id)
    (string, username)
    (string, motto)
)


namespace restc_cpp{
namespace unittests {


TEST(TestCRUD)
{
    mutex mutex;
    mutex.lock();

    std::vector<std::future<int>> futures;
    std::vector<std::promise<int>> promises;

    futures.reserve(CONNECTIONS);
    promises.reserve(CONNECTIONS);

    Request::Properties properties;
    properties.cacheMaxConnections = CONNECTIONS;
    properties.cacheMaxConnectionsPerEndpoint = CONNECTIONS;
    auto rest_client = RestClient::Create(properties);

    for(int i = 0; i < CONNECTIONS; ++i) {

        promises.emplace_back();
        futures.push_back(promises.back().get_future());

        rest_client->Process([i, &promises, &rest_client, &mutex](Context& ctx) {

            auto reply = RequestBuilder(ctx)
                .Get(http_url)
                .Execute();

            // Use an iterator to make it simple to fetch some data and
            // then wait on the mutex before we finish.
            IteratorFromJsonSerializer<Post> results(*reply);

            auto it = results.begin();
            RESTC_CPP_LOG_DEBUG << "Iteration #" << i
                << " Read item # " << it->id;

            promises[i].set_value(i);
            // Wait for all connections to be ready

            // We can't just wait on the lock since we are in a co-routine.
            // So we use the async_wait() to poll in stead.
            while(!mutex.try_lock()) {
                boost::asio::deadline_timer timer(rest_client->GetIoService(),
                    boost::posix_time::milliseconds(1));
                timer.async_wait(ctx.GetYield());
            }
            mutex.unlock();

            // Fetch the rest
            for(; it != results.end(); ++it)
                ;

        });
    }

    int successful_connections = 0;
    for(auto& future : futures) {
        try {
            auto i = future.get();
            RESTC_CPP_LOG_DEBUG << "Iteration #" << i << " is done";
            ++successful_connections;
        } catch (const std::exception& ex) {
            RESTC_CPP_LOG_ERROR << "Future threw up: " << ex.what();
        }
    }

    RESTC_CPP_LOG_INFO << "We had " << successful_connections
        << " successful connections.";

    CHECK_EQUAL(CONNECTIONS, successful_connections);

    mutex.unlock();

    rest_client->CloseWhenReady();
}


}} // namespaces

int main(int, const char *[])
{
    namespace logging = boost::log;
    logging::core::get()->set_filter
    (
        logging::trivial::severity >= logging::trivial::debug
    );

    return UnitTest::RunAllTests();
}

