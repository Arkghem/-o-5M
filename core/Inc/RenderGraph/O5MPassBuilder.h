#ifndef O5MPASSBUILDER_H
#define O5MPASSBUILDER_H

#include <concepts>
#include <string>

#include "O5MRendergraphType.h"

using namespace O5MRendergraphNS;

class O5MRendergraph;

class O5MPassBuilder {
private:
    O5MRendergraph* m_graph;
    PassDesc& m_passDesc;   
public:
    O5MPassBuilder(void) = delete;
    O5MPassBuilder(O5MRendergraph* graph, PassDesc& passDesc) : 
        m_graph(graph),
        m_passDesc(passDesc) {}
    ~O5MPassBuilder(void);

    //shit code here, alil' fuction push this info into rendergraph
    void addResource(ResourceInfo&);

    void read(ResourceInfo& info, std::variant<TexRead, TexWrite, TexRW, BufRead, BufWrite> use) {
        m_passDesc.reads.emplace_back(info, use); 
        addResource(info);
    }
    void write(ResourceInfo& info, std::variant<TexRead, TexWrite, TexRW, BufRead, BufWrite> use) {
        m_passDesc.writes.emplace_back(info, use); 
        addResource(info);
    }
    void readwrite(ResourceInfo& info, std::variant<TexRead, TexWrite, TexRW, BufRead, BufWrite> use) {
        m_passDesc.readWrites.emplace_back(info, use);
        addResource(info);
    }
};

#endif // O5MPASSBUILDER_H
