#ifndef O5MPASSBUILDER_H
#define O5MPASSBUILDER_H

#include <concepts>
#include <string>

#include "O5MRendergraphType.h"

using namespace O5MRendergraphNS;

class O5MRendergraph;

class O5MPassBuilder {
private:
    PassDesc& m_passDesc;   
public:
    O5MPassBuilder(void) = delete;
    O5MPassBuilder(PassDesc& passDesc) : m_passDesc(passDesc) {}
    ~O5MPassBuilder(void);

    void read(UseDecl useDecl) { m_passDesc.reads.push_back(useDecl); }
    void write(UseDecl useDecl) { m_passDesc.writes.push_back(useDecl); }
    void readWrites(UseDecl useDecl) { m_passDesc.readWrites.push_back(useDecl); }
};

#endif // O5MPASSBUILDER_H
