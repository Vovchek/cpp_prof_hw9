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
#include <filesystem>

extern bool allDone;

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
    // std::list<std::weak_ptr<Observer>> m_subs;
    std::list<std::shared_ptr<Observer>> m_subs;
    int bulk_depth{0};
    int bulk_size{0};
    int max_bulk{3};

public:

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
        if (bulk_size && !bulk_depth)
        {
            endBlock();
        }
    }

    ~CommandProcessor()
    {
        // std::cout << "~CommandProcessor()\n";
    }
};

struct pcout : public std::stringstream
{
    static inline std::mutex cout_mutex;
    ~pcout()
    {
        std::lock_guard<std::mutex> l{cout_mutex};
        std::cout << rdbuf();
        std::cout.flush();
    }
};

class OstreamLogger : public Observer, public std::enable_shared_from_this<OstreamLogger>
{
public:
    ~OstreamLogger()
    {
        // std::cout << "~OstreamLogger()\n";
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
        std::lock_guard<std::mutex> l{m};
        que.emplace_back(std::move(data));
        cv.notify_one();
    }

    static void writer()
    {
        do
        {
            std::unique_lock<std::mutex> l{m, std::defer_lock};
            l.lock();
            cv.wait(l, []
                    { return !que.empty() || allDone; });

            if (!que.empty())
            {
                auto bulk = std::move(que.front());
                que.pop_front();
                l.unlock();

                pcout log;
                log << "bulk: ";
                for (auto &c : bulk)
                {
                    if (&c != &(*bulk.begin()))
                        log << ", ";
                    log << c;
                }
                log << '\n';
            }
            else
                l.unlock();
        } while (!allDone || !que.empty());
    }

    static inline std::condition_variable cv;
private:
    OstreamLogger() = default;
    std::list<std::string> data;
    static inline std::deque<std::list<std::string>> que;
    static inline std::mutex m;
};

class FileLogger : public Observer, public std::enable_shared_from_this<FileLogger>
{
public:
    ~FileLogger()
    {
        // std::cout << "~FileLogger()\n";
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
        std::lock_guard<std::mutex> l{m};
        que.emplace_back(std::pair(std::move(log_name), std::move(data)));
        cv.notify_one();
    }

    static void writer(int n)
    {
        do {
            std::unique_lock<std::mutex> l{m, std::defer_lock};
            l.lock();
            cv.wait(l, []
                    { return !que.empty() || allDone; });

            if (!que.empty())
            {
                auto bulk = std::move(que.front());
                que.pop_front();
                l.unlock();
                //adding  _n does not ensure uniqueness, but helps to distinguish writers visually
                auto suffix = "_" + std::to_string(n) + ".log";
                std::string unique_name = bulk.first + suffix;
                std::unique_lock<std::mutex> l{fn_mutex};
                for (auto i{0}; std::filesystem::exists(unique_name); ++i)
                {
                    unique_name = bulk.first + std::to_string(i) + suffix;
                }
                std::ofstream log(unique_name);
                fn_mutex.unlock();

                log << "bulk: ";
                for (auto &c : bulk.second)
                {
                    if (&c != &(*bulk.second.begin()))
                        log << ", ";
                    log << c;
                }
                log << '\n';
            }
            else
                l.unlock();
        } while (!allDone || !que.empty());

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
                           std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count())};
        return fn;
    }

    static inline std::condition_variable cv;
private:
    FileLogger() = default;
    std::list<std::string> data;
    std::string log_name;
    static inline std::mutex fn_mutex;
    static inline std::mutex m;
    static inline std::deque<std::pair<std::string, std::list<std::string>>> que;
};
