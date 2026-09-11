#ifndef __O5MMOUSEEVENT_H__
#define __O5MMOUSEEVENT_H__

#include "O5MEvent.h"

class O5MMouseEvent : public O5MEvent {
public:
    DEFINE_EVENT_TYPE(O5MMouseEvent)

    O5MMouseEvent(float x, float y, float scrollOffset = 0.0f)
        : m_xOffset(x), m_yOffset(y), m_scrollOffset(scrollOffset) {}

    float getX(void) const { return m_xOffset; }
    float getY(void) const { return m_yOffset; }
    float getScrollOffset(void) const { return m_scrollOffset; }

private:
    float m_xOffset;
    float m_yOffset;

    float m_scrollOffset;
};

#endif // __O5MMOUSEEVENT_H__
