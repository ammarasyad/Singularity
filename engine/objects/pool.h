#ifndef POOL_H
#define POOL_H
#include <cstdint>

static constexpr uint32_t MAX_SIZE = 1 << 30;

namespace Pool
{
    static uint32_t freeSize = MAX_SIZE;
}
#endif
