#ifndef O5M_RENDERGRAPH_TYPE_H
#define O5M_RENDERGRAPH_TYPE_H

namespace O5MRendergraphNS {
    enum class TexRead  { Color, Depth, Storage, TransferSrc }; 
    enum class TexWrite { ColorClear, ColorStore, Depth, Storage, TransferDst }; //mind the difference with Color
    enum class TexRW    { Storage }; //Host or Device
    enum class BufRead  { Uniform, Storage, VertexIndex, Indirect, TransferSrc };
    enum class BufWrite { Storage, TransferDst, Uniform };


};

#endif // !O5M_RENDERGRAPH_TYPE_H
