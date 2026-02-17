/**
 * mcpd — Platform singleton
 */

#include "Platform.h"

namespace mcpd {
namespace hal {

static Platform* _platformInstance = nullptr;

Platform& platform() {
    return *_platformInstance;
}

void setPlatform(Platform* p) {
    _platformInstance = p;
}

} // namespace hal
} // namespace mcpd
