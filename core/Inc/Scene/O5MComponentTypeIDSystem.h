#ifndef __O5MCOMPONENTTYPEIDSYSTEM__H
#define __O5MCOMPONENTTYPEIDSYSTEM__H

#include <stdlib.h>

class O5MComponentTypeIDSystem {
private:
    inline static size_t nextTypeID;
public:
    template<typename T>
    static size_t getTypeID(void) {
        static size_t typeID = nextTypeID++;
        return typeID;
    }
};

#endif // !__O5MCOMPONENTTYPEIDSYSTEM__H
