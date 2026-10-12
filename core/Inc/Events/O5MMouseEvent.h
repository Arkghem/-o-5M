#ifndef __O5MMOUSEEVENT_H__
#define __O5MMOUSEEVENT_H__

#include "Events/O5MEvent.h"

class O5MMouseMovedEvent : public O5MEvent {
public:
    O5MMouseMovedEvent(float dx, float dy)
        : m_dx(dx), m_dy(dy) {}

    float getX(void) const { return m_dx; }
    float getY(void) const { return m_dy; }

private:
    float m_dx;
    float m_dy;
};

class O5MMouseScrolledEvent : public O5MEvent {
public:
    O5MMouseScrolledEvent(float xOffset, float yOffset)
        : m_xOffset(xOffset), m_yOffset(yOffset) {}

    float getXOffset(void) const { return m_xOffset; }
    float getYOffset(void) const { return m_yOffset; }

private:
    float m_xOffset;
    float m_yOffset;
};

class O5MMouseButtonEvent : public O5MEvent {
public:
    enum E_MouseButton {
        Button_Left,
        Button_Right,
        Button_Middle,
        Button_None
    };

    enum E_MouseButtonAction {
        Pressed,
        Released,
        None
    };

    O5MMouseButtonEvent(E_MouseButton button = E_MouseButton::Button_None,
                        E_MouseButtonAction action = E_MouseButtonAction::None)
        : m_button(button), m_action(action) {}

    E_MouseButton getButton(void) const { return m_button; }
    E_MouseButtonAction getAction(void) const { return m_action; }

private:
    E_MouseButton m_button;
    E_MouseButtonAction m_action;
};

#endif // __O5MMOUSEEVENT_H__
