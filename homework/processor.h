#pragma once

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <list>
#include <chrono>
#include <sstream>
#include <memory>
#include <condition_variable>
#include <atomic>
#include <mutex>
#include <deque>


class Observer
{
public:
    virtual void startBlock() = 0;
    virtual void setNextCommand(const std::string &) = 0;
    virtual void finalizeBlock() = 0;
};

class Observable
{
public:
    virtual ~Observable() = default;
    virtual void subscribe(const std::shared_ptr<Observer> &obs) = 0;
};

class CommandProcessor : public Observable
{
    //std::list<std::weak_ptr<Observer>> m_subs;
    std::list<std::shared_ptr<Observer>> m_subs;
    int bulk_depth{0};
    int bulk_size{0};
    int max_bulk{3};

    //std::atomic<bool> request_stop_async{false};
    //std::atomic<bool> confirm_stop_async{false};
    bool request_stop_async{false};
    bool confirm_stop_async{false};

public:
    std::mutex mutexLockBuf;
    std::condition_variable condVarLockBuf;

    CommandProcessor() = default;
    CommandProcessor(int N) : max_bulk{N} {}

    enum Events
    {
        StartBlock,
        EndBlock,
        NewCommand
    };

    void subscribe(const std::shared_ptr<Observer> &obs) override
    {
        m_subs.emplace_back(obs);
    }

    void notify(Events e, const std::string &cmd)
    {
        auto iter = m_subs.begin();
        while (iter != m_subs.end())
        {
            auto ptr = *iter;
            if (ptr)
            { // notify subscriber if it still survived
                switch (e)
                {
                case StartBlock:
                    ptr->startBlock();
                    break;
                case EndBlock:
                    ptr->finalizeBlock();
                    break;
                case NewCommand:
                    ptr->setNextCommand(cmd);
                    break;
                }
                ++iter;
            }
            else
            { // subscriber is dead
                m_subs.erase(iter++);
            }
        }
    }

    void addCommand(const std::string &cmd)
    {
        if (!bulk_size)
            notify(StartBlock, "");
        bulk_size++;
        notify(NewCommand, cmd);
    }

    void endBlock()
    {
        notify(EndBlock, "");
        bulk_size = 0;
    }

    void onInput(const std::string &cmd)
    {

        if (bulk_size < max_bulk || bulk_depth > 0)
        {

            if (cmd.find('{') != std::string::npos)
            {
                if (!bulk_depth && bulk_size)
                {
                    endBlock();
                }
                ++bulk_depth;
            }
            else if (bulk_depth && cmd.find('}') != std::string::npos)
            {
                if (--bulk_depth == 0)
                {
                    endBlock();
                }
            }
            else
            {
                addCommand(cmd);
            }
        }
        if (bulk_size >= max_bulk && !bulk_depth)
        {
            endBlock();
        }
    }

    void terminate()
    {
        if (bulk_size && !bulk_depth) {
            endBlock();
        }
    }

    void runAsync(std::deque<std::string> &buf); /// @brief intended to run as a detached thread

    void stopAsync() {
        request_stop_async = true; // this terminates runAsync()
        condVarLockBuf.notify_one();
        {
            std::unique_lock<std::mutex> lck{mutexLockBuf};
            condVarLockBuf.wait(lck, [this](){return confirm_stop_async == true;});
        }
    }

    ~CommandProcessor() {
        //std::cout << "~CommandProcessor()\n";
    }

};

struct pcout : public std::stringstream {
    static inline std::mutex m;
    ~pcout() {
        std::lock_guard<std::mutex> l{m};
        std::cout << rdbuf();
        std::cout.flush();
    }
};

class OstreamLogger : public Observer, public std::enable_shared_from_this<OstreamLogger>
{
public:
    ~OstreamLogger() {
        //std::cout << "~OstreamLogger()\n";
    }
    static std::shared_ptr<OstreamLogger> create(CommandProcessor *cp)
    {
        auto ptr = std::shared_ptr<OstreamLogger>{new OstreamLogger{}};
        ptr->subscribe(cp);
        return ptr;
    }
    void subscribe(CommandProcessor *cp)
    {
        cp->subscribe(shared_from_this());
    }

    void startBlock() override
    {
    }

    void setNextCommand(const std::string &cmd) override
    {
        data.push_back(cmd);
    }

    void finalizeBlock() override
    {
        pcout pc;
        //std::cout << "bulk: ";
        pc << "bulk: ";
        for (auto &c : data)
        {
            if (&c != &(*data.begin()))
                //std::cout << ", ";
                pc << ", ";
            //std::cout << c;
            pc << c;
        }
        //std::cout << '\n';
        pc << '\n';

        data.clear();
    }

private:
    OstreamLogger() = default;
    std::list<std::string> data;
};

class FileLogger : public Observer, public std::enable_shared_from_this<FileLogger>
{
public:
    ~FileLogger() {
        //std::cout << "~FileLogger()\n";
    }
    static std::shared_ptr<FileLogger> create(CommandProcessor *cp)
    {
        auto ptr = std::shared_ptr<FileLogger>{new FileLogger{}};
        ptr->subscribe(cp);
        return ptr;
    }
    void subscribe(CommandProcessor *cp)
    {
        cp->subscribe(shared_from_this());
    }

    void startBlock() override
    {
        log_name = time_to_filename(std::chrono::steady_clock::now());
    }

    void setNextCommand(const std::string &cmd) override
    {
        data.push_back(cmd);
    }

    void finalizeBlock() override
    {
        std::ofstream log(log_name);

        log << "bulk: ";
        for (auto &c : data)
        {
            if (&c != &(*data.begin()))
                log << ", ";
            log << c;
        }
        log << '\n';

        data.clear();
    }

    /**
     * @brief Create file name string based on time point.
     * @param [in] t Reference to time_point structure.
     * @details Converts time point to a readable string of a microseconds since epoch start,
     *           then combines file name starting with "bulk" and ending on ".log" extension.
     */
    std::string time_to_filename(const std::chrono::time_point<std::chrono::steady_clock> &t) const
    {
        std::string fn{"bulk" +
                       std::to_string(
                           std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count()) +
                       ".log"};
        return fn;
    }

private:
    FileLogger() = default;
    std::list<std::string> data;
    std::string log_name;
};
