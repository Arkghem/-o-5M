#ifndef __O5MEVENT__H
#define __O5MEVENT__H

#include <vector>
#include <queue>
#include <thread>

class O5MEvent {
public:
    virtual ~O5MEvent(void) = default;
    virtual const char* getType(void) const = 0;
    virtual O5MEvent* clone(void) const  = 0;
};

#define DEFINE_EVENT_TYPE(type) \
    static const char* getStaticType(void) { return #type; } \
    virtual const char* getType(void) const override { return getStaticType(); } \
    virtual O5MEvent* clone(void) const override { return new type(*this); }

class O5MEventListener {
public:
    virtual ~O5MEventListener(void) = default;
    virtual void onEvent(const O5MEvent& event) = 0;
};

class O5MEventDispatcher {
private:
    const O5MEvent& m_event;
public:
    explicit O5MEventDispatcher(const O5MEvent& e): m_event(e) {}
public:
    template<typename T, typename F>
    bool Dispatch(const F& handler) {
        if (m_event.getType() == T::getStaticType()) {
            handler(static_cast<const T&>(m_event));
            return true;
        }
        return false;
    }
};

class O5MEventBus {
public:
    using Token = uint64_t;
    using Handler = std::function<void(const O5MEvent&)>;
private:
    std::unordered_map<const char*, std::vector<std::pair<Token, Handler>>> handlers;
    Token nextToken = 0;
public:
    template<typename T>
    Token addEvent(Handler handler) {
        handlers[T::getStaticType()].push_back({nextToken, std::move(handler)});
        return nextToken++;
    }

    template<typename T>
    void removeEvent(Token token) {
        std::erase_if(handlers.at(T::getStaticType()).begin(), 
                       handlers.at(T::getStaticType()).end(), 
                       [token](const std::pair<Token, Handler>& p) {
                           return p.first == token;
                       });
    }

    void publishEvent(O5MEvent& event) {
        auto& typeHandlers = handlers.at(event.getType());
        for (auto& [token, handler] : typeHandlers) {
            handler(event);
        }
    }
};

#endif  //!__O5MEVENT__H
