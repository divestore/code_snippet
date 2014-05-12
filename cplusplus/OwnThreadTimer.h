#ifndef __timer_own_thread__h__
#define __timer_own_thread__h__

#include <thread>
#include <mutex>
#include <map>
#include <chrono>
#include <memory>
#include <boost/any.hpp>

using namespace std;

// 定时器时间流失步长
#define POLL_DURATION 50000

namespace kit{

class OwnThreadTimerCB
{
    public:
        virtual void timeout_cb(uint64_t timer_id, boost::any data) = 0;
};

class OwnThreadTimer
{
    struct TimerEntry
    {
        TimerEntry(OwnThreadTimerCB* a_cb, boost::any a_data)
        {
            cb   = a_cb;
            data = a_data;
        }

        OwnThreadTimerCB* cb;
        boost::any        data;
    };
typedef map<uint64_t, shared_ptr<TimerEntry> > TimerMap; // {timeout_period:(callback, arg)}
public:
    OwnThreadTimer():m_work_thread(nullptr)
    {
    }

    virtual ~OwnThreadTimer() 
    {
        m_stop_flag = true;
        if (m_work_thread)
        {
            m_work_thread->join();
            delete m_work_thread;
            m_work_thread = nullptr;
        }
    }

    virtual bool Start()
    {
        m_work_thread = new std::thread(&OwnThreadTimer::Processing, this);
        return true;
    }

    uint64_t set_timer(chrono::microseconds period, OwnThreadTimerCB* callback, boost::any data)
    {
        std::lock_guard<recursive_mutex> lock(m_mutex);
        chrono::steady_clock::time_point wake_time = chrono::steady_clock::now() + period;
        uint64_t timer_id = chrono::duration_cast<chrono::microseconds>(wake_time.time_since_epoch()).count();
        // 保证没有重复的timer_id
        while (m_waits.find(timer_id) != m_waits.end())
        {
            ++timer_id;
        }
        m_waits[timer_id] = make_shared<TimerEntry>(callback, data);
        return timer_id;
    }

    void cancel_timer(uint64_t timer_id)
    {
        std::lock_guard<recursive_mutex> lock(m_mutex);
        m_waits.erase(timer_id);
    }
    
protected:
    int Processing()   // thread fun
    {
        uint64_t          timer_id;

        while (!m_stop_flag)
        {
            std::chrono::microseconds dura(POLL_DURATION);
            std::this_thread::sleep_for( dura );
            uint64_t now = chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now().time_since_epoch()).count();
            {
                // 将已经超时的定时器存入一个临时队列
                shared_ptr<TimerMap> notice = make_shared<TimerMap>();
                {
                    std::lock_guard<recursive_mutex> lock(m_mutex);
                    auto should_wake = m_waits.lower_bound(now);
                    if (should_wake != m_waits.end())
                    {
                        notice->insert(m_waits.begin(), should_wake);

                        // 将已经超时的定时器删除
                        m_waits.erase(m_waits.begin(), should_wake);
                    }
                }

                // 触发已经超时的定时器
                for (auto it = notice->begin(); it != notice->end() && !m_stop_flag; ++it)
                {
                    timer_id = it->first;
                    shared_ptr<TimerEntry> entry = it->second;
                    entry->cb->timeout_cb(timer_id, entry->data);
                }
            }
        }
    }

private:
    recursive_mutex     m_mutex;
    std::thread*        m_work_thread;    
    atomic<bool>        m_stop_flag;
    TimerMap            m_waits;
};

}// end of namespace kit

#endif //__timer_own_thread__h__

