#ifndef __O5MCOMPONENT__H
#define __O5MCOMPONENT__H

#include "Scene/O5MComponentTypeIDSystem.h"

class O5MEntity;

class O5MComponent {
public:
    template<typename T>
    static size_t getTypeID(void) { 
        return O5MComponentTypeIDSystem::getTypeID<T>(); 
    }
public:
    enum class EState {
        Uninitialized,
        Initializing,
        Active,
        Destroying,
        Destroyed
    };
protected:
    EState state = EState::Uninitialized;
    O5MEntity* owner = nullptr;
public:
    virtual ~O5MComponent(void) = default;

    void init(void);
    void destroy(void);    

    void setOwner(O5MEntity* owner) { this->owner = owner; }
    O5MEntity* setOwner(void) { return owner; }

    bool isActive(void) const { return state == EState::Active; }
protected:
    virtual void onInitializing(void) = 0;
    virtual void onDestroying(void) = 0;
    virtual void update(float deltaTime) = 0;
    virtual void render(void) = 0;

    friend class O5MEntity;
};

#endif // __O5MCOMPONENT__H
