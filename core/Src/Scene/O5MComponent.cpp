#include "Scene/O5MComponent.h"

void O5MComponent::init(void) {
    if (state == EState::Uninitialized) {
        state = EState::Initializing;
        onInitializing();
        state = EState::Active;
    }
}

void O5MComponent::destroy(void) {
    if (state == EState::Active) {
        state = EState::Destroying;
        onDestroying();
        state = EState::Destroyed;
    }
}

