#ifndef __O5MKEYBOARDEVENT__H
#define __O5MKEYBOARDEVENT__H

#include "O5MEvent.h"

class O5MKeyboardEvent : public O5MEvent {
public:
    DEFINE_EVENT_TYPE(O5MKeyboardEvent);

    enum E_KeyboardEventType {
        Pressed,
        Holded,
        Released,
        None
    };

    enum E_KeyboardKey {
        Key_A,
        Key_B,
        Key_C,
        Key_D,
        Key_E,
        Key_F,
        Key_G,
        Key_H,
        Key_I,
        Key_J,
        Key_K,
        Key_L,
        Key_M,
        Key_N,
        Key_O,
        Key_P,
        Key_Q,
        Key_R,
        Key_S,
        Key_T,
        Key_U,
        Key_V,
        Key_W,
        Key_X,
        Key_Y,
        Key_Z,
        Key_None
    };

private:
    E_KeyboardEventType m_type;
    E_KeyboardKey m_key;
public:
    O5MKeyboardEvent(E_KeyboardEventType type = E_KeyboardEventType::None,
                     E_KeyboardKey key = E_KeyboardKey::Key_None)
        : m_type(type), m_key(key) {}

    E_KeyboardEventType getEventType(void) const { return m_type; }
    E_KeyboardKey getKey(void) const { return m_key; }
};

#endif // !__O5MKEYBOARDEVENT__H
