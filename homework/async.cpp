#include "async.h"
#include "processor.h"

#include <map>
#include <thread>

void CommandProcessor::runAsync(std::deque<std::string> &buf)
{

    do {
        std::unique_lock<std::mutex> lck{mutexLockBuf};
        condVarLockBuf.wait(lck, [&buf, this]() {return !buf.empty() || request_stop_async;});

        while(!buf.empty()) {
            auto &s = buf.front();
            for(auto start = s.find_first_not_of("\n\r\0"),
                end = s.find_first_of("\n\r\0", start);
                start != std::string::npos;
                start = s.find_first_not_of("\n\r\0", end),
                end = s.find_first_of("\n\r\0", start)
                ) {
                onInput(s.substr(start, end != std::string::npos?(end-start):(s.length()-start)));
            }
            buf.pop_front();
        }
    } while(!request_stop_async);

    terminate();

    confirm_stop_async = true;
    condVarLockBuf.notify_one();

    //std::cout << "Exit runAsync()\n";
}

namespace async {

std::map<handle_t, std::unique_ptr<std::deque<std::string>>> procs;

handle_t connect(std::size_t bulk) {
    std::unique_ptr<std::deque<std::string>> buf {new std::deque<std::string>};
    CommandProcessor *cmd = new CommandProcessor(bulk);
    std::shared_ptr<FileLogger> filePtr = FileLogger::create(cmd);
    std::shared_ptr<OstreamLogger> coutPtr = OstreamLogger::create(cmd);
    procs.emplace(cmd, std::move(buf));
    std::thread worker(&CommandProcessor::runAsync, cmd, std::ref(*procs[cmd].get()));
    worker.detach();

    return cmd;
}

void receive(handle_t handle, const char *data, std::size_t size) {
    auto it = procs.find(handle);
    if(it != procs.end()) {
        CommandProcessor &cmd = *reinterpret_cast<CommandProcessor *>(handle);
        {
            std::lock_guard<std::mutex> guard{cmd.mutexLockBuf};
            it->second->emplace_back(std::string(data, size));
        }
        cmd.condVarLockBuf.notify_one();
    }
}

void disconnect(handle_t handle) {

    //std::cout << "disconnect " << handle << "\n";

    auto it = procs.find(handle);
    if(it != procs.end()) {
        CommandProcessor &cmd = *reinterpret_cast<CommandProcessor *>(handle);
        cmd.stopAsync();
        delete &cmd;
        procs.erase(handle);
    }
    //std::cout << handle << " disconnected\n";
}
}
