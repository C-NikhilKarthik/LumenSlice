// RAII owner of a LumenVolume* over the pure-C bridge (src/bridge/lumen_bridge.h).
//
// The whole Windows UI talks to the C++ core exclusively through this handle and
// the free lumen_* functions — it never includes a core C++ header, honouring the
// UI/data isolation rule in docs/agent.md §1.
#pragma once

#include <string>
#include <utility>

#include "lumen_bridge.h"

namespace lumenwin {

class BridgeVolume {
public:
    BridgeVolume() = default;
    ~BridgeVolume() { reset(); }

    BridgeVolume(const BridgeVolume&) = delete;
    BridgeVolume& operator=(const BridgeVolume&) = delete;

    // Load a DICOM folder, replacing any currently held volume only on success.
    // `status` receives the bridge's human-readable message either way.
    bool load(const std::string& path, std::string& status) {
        char msg[512] = {0};
        LumenVolume* loaded = lumen_load_folder(path.c_str(), msg, sizeof(msg));
        status.assign(msg);
        if (!loaded) return false;
        reset();
        volume_ = loaded;
        return true;
    }

    void reset() {
        if (volume_) {
            lumen_free(volume_);
            volume_ = nullptr;
        }
    }

    bool valid() const { return volume_ != nullptr; }
    LumenVolume* get() const { return volume_; }

private:
    LumenVolume* volume_ = nullptr;
};

}  // namespace lumenwin
