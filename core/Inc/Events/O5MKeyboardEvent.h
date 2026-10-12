#ifndef __O5MKEYBOARDEVENT__H
#define __O5MKEYBOARDEVENT__H

#include "Events/O5MEvent.h"

class O5MKeyboardEvent : public O5MEvent {
public:
    enum E_KeyboardEventType {
        Pressed,
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
        Key_0,
        Key_1,
        Key_2,
        Key_3,
        Key_4,
        Key_5,
        Key_6,
        Key_7,
        Key_8,
        Key_9,
        Key_Escape,
        Key_Space,
        Key_Enter,
        Key_Tab,
        Key_Backspace,
        Key_Up,
        Key_Down,
        Key_Left,
        Key_Right,
        Key_LeftShift,
        Key_RightShift,
        Key_LeftControl,
        Key_RightControl,
        Key_LeftAlt,
        Key_RightAlt,
        Key_None,
        Key_Count = Key_None
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
