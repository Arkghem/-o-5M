#ifndef __O5MEVENT__H
#define __O5MEVENT__H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

class O5MEvent {
public:
    virtual ~O5MEvent(void) = default;
};

class O5MEventBus {
public:
    using Token = uint64_t;
    using Handler = std::function<void(const O5MEvent&)>;
private:
    std::unordered_map<std::type_index, std::vector<std::pair<Token, Handler>>> handlers;
    Token nextToken = 0;
    size_t dispatchDepth = 0;
    std::vector<std::pair<std::type_index, Token>> pendingRemovals;

    void eraseToken(std::type_index type, Token token) {
        auto it = handlers.find(type);
        if (it == handlers.end())
            return;
        std::erase_if(it->second, [token](const std::pair<Token, Handler>& entry) {
            return entry.first == token;
        });
    }

    void flushPendingRemovals(void) {
        auto pending = std::move(pendingRemovals);
        pendingRemovals.clear();
        for (const auto& [type, token] : pending)
            eraseToken(type, token);
    }
public:
    template<typename T>
    Token addEvent(Handler handler) {
        handlers[std::type_index(typeid(T))].push_back({nextToken, std::move(handler)});
        return nextToken++;
    }

    template<typename T>
    void removeEvent(Token token) {
        if (dispatchDepth > 0) {
            pendingRemovals.emplace_back(std::type_index(typeid(T)), token);
            return;
        }
        eraseToken(std::type_index(typeid(T)), token);
    }

    void publishEvent(const O5MEvent& event) {
        auto it = handlers.find(std::type_index(typeid(event)));
        if (it == handlers.end())
            return;

        ++dispatchDepth;
        auto& typeHandlers = it->second;
        const size_t subscribed = typeHandlers.size();
        for (size_t i = 0; i < subscribed && i < typeHandlers.size(); ++i)
            typeHandlers[i].second(event);
        --dispatchDepth;

        if (dispatchDepth == 0 && !pendingRemovals.empty())
            flushPendingRemovals();
    }
};

#endif  //!__O5MEVENT__H
