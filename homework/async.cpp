#include "async.h"
#include "processor.h"

#include <map>
#include <thread>

namespace async {

std::map<handle_t, std::unique_ptr<std::deque<std::string>>> procs;
std::mutex m;

handle_t connect(std::size_t bulk) {
    std::unique_ptr<std::deque<std::string>> buf {new std::deque<std::string>};
    CommandProcessor *cmd = new CommandProcessor(bulk);
    std::shared_ptr<FileLogger> filePtr = FileLogger::create(cmd);
    std::shared_ptr<OstreamLogger> coutPtr = OstreamLogger::create(cmd);
    {
        std::lock_guard<std::mutex> l(m);
        procs.emplace(cmd, std::move(buf));
    }
    // std::cout << cmd << " connected\n";

    return cmd;
}

void receive(handle_t handle, const char *data, std::size_t size) {
    std::unique_lock<std::mutex> l(m);
    auto it = procs.find(handle);
    if(it != procs.end()) {
        l.unlock();
        CommandProcessor &cmd = *reinterpret_cast<CommandProcessor *>(handle);
        {
            std::string s{data, size};
            for(auto start = s.find_first_not_of("\n\r\0"),
                end = s.find_first_of("\n\r\0", start);
                start != std::string::npos;
                start = s.find_first_not_of("\n\r\0", end),
                end = s.find_first_of("\n\r\0", start)
                ) {
                cmd.onInput(s.substr(start, end != std::string::npos?(end-start):(s.length()-start)));
            }
        }
    }
}

void disconnect(handle_t handle) {

    std::lock_guard<std::mutex> l(m);
    auto it = procs.find(handle);
    if(it != procs.end()) {
        CommandProcessor &cmd = *reinterpret_cast<CommandProcessor *>(handle);
        cmd.terminate();
        delete &cmd;
        procs.erase(handle);
    }
    // std::cout << handle << " disconnected\n";
}
}
