// Offline rendering helper: runs the engine over whole buffers in fixed-size blocks.
#pragma once

#include "../Source/dsp/SpatialEngine.h"

namespace amb::tools
{
// in: numIn channels of equal length. Returns numOut channels.
inline std::vector<std::vector<float>> render (const EngineParams& p, const std::vector<std::vector<float>>& in, int numOut,
                                               double fs = 48000.0, int blockSize = 256,
                                               const std::vector<std::pair<int, EngineParams>>* automation = nullptr)
{
    SpatialEngine engine;
    engine.prepare (fs, blockSize);
    engine.setParams (p);
    const int n = (int) in[0].size();
    const int numIn = (int) in.size();
    std::vector<std::vector<float>> out ((size_t) numOut, std::vector<float> ((size_t) n, 0.0f));
    size_t autoIdx = 0;
    for (int start = 0; start < n; start += blockSize)
    {
        if (automation)
            while (autoIdx < automation->size() && (*automation)[autoIdx].first <= start)
                engine.setParams ((*automation)[autoIdx++].second);
        const int len = std::min (blockSize, n - start);
        const float* ip[2] = { in[0].data() + start, numIn > 1 ? in[1].data() + start : nullptr };
        float* op[2] = { out[0].data() + start, numOut > 1 ? out[1].data() + start : nullptr };
        engine.process (ip, numIn, op, numOut, len);
    }
    return out;
}
} // namespace amb::tools
