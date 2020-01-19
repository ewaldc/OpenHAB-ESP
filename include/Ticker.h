/* Ticker.h - simplified esp8266 library that calls functions periodically */

#ifndef TICKER_H
#define TICKER_H

#include <functional>
#include <Schedule.h>
#include <ets_sys.h>

class Ticker {
public:
    Ticker() : _timer(nullptr) {}
    ~Ticker() { detach(); }

    //typedef void (*callback_with_arg_t)(void*);
    typedef std::function<void(void)> callback_function_t;

    void attach(uint32_t seconds, callback_function_t callback) {
        _callback_function = std::move(callback);
        if (_timer) os_timer_disarm(_timer);
        else _timer = &_etsTimer;
        os_timer_setfn(_timer, _static_callback, this);
        os_timer_arm(_timer, 1000UL * seconds, true);
    }

    void attach_scheduled(uint32_t seconds, callback_function_t callback) {
        _callback_function = [callback]() { schedule_function(callback); };
        if (_timer) os_timer_disarm(_timer);
        else _timer = &_etsTimer;
        os_timer_setfn(_timer, _static_callback, this);
        os_timer_arm(_timer, 1000UL * seconds, true);
    }

    void detach() {
        if (!_timer) return;
        os_timer_disarm(_timer);
        _timer = nullptr;
        _callback_function = nullptr;
    }
    bool active() const { return _timer; }

protected:
    static void _static_callback(void* arg) {
        Ticker* _this = reinterpret_cast<Ticker*>(arg);
        if (_this && _this->_callback_function)
            _this->_callback_function();
    }

    ETSTimer* _timer;
    callback_function_t _callback_function = nullptr;

private:
    ETSTimer _etsTimer;
};

#endif //TICKER_H
