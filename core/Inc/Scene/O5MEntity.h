#ifndef __O5MENTITY__H
#define __O5MENTITY__H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

class O5MComponent;
class O5MEntity {
private:
    std::string name;
    bool active = true;

    std::vector<std::unique_ptr<O5MComponent>> components;
    std::unordered_map<size_t, O5MComponent*> componentMap;
public:
    explicit O5MEntity(const std::string& entityName) : name(entityName) {}

    const std::string& getName(void) const { return name; }
    void setName(const std::string& name) { this->name = name; }

    bool isActive(void) const { return active; }
    void setActive(bool isActive) { active = isActive; }

    void init(void);
    void update(void);
    void render(void);

    template<typename T, typename... Args>
    T* addComponent(Args&&... args);

    template<typename T>
    T* getComponent(void);

    template<typename T>
    bool removeComponent(void);
};

#endif // !__O5MENTITY__H
