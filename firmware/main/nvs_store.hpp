#pragma once

// NVS-backed KvStore for the parameter set (FD section 16). Writes stall the
// flash cache on both cores, so save_params() through this store runs only
// from the low-priority persistence task, never from the control task.

#include <cstddef>
#include <cstdint>

#include "direcsun/core/params.hpp"
#include "nvs.h"

namespace direcsun::app {

class NvsKvStore final : public KvStore {
public:
    // Opens `ns` read-write. ok() is false if NVS could not open it; every
    // operation then fails, so load_params() falls back to the defaults.
    explicit NvsKvStore(const char *ns = "direcsun");
    ~NvsKvStore() override;
    NvsKvStore(const NvsKvStore &) = delete;
    NvsKvStore &operator=(const NvsKvStore &) = delete;

    bool ok() const { return open_; }

    bool read_u32(const char *key, uint32_t &value) override;
    bool read_blob(const char *key, void *data, std::size_t &len) override;
    bool write_u32(const char *key, uint32_t value) override;
    bool write_blob(const char *key, const void *data, std::size_t len) override;
    bool commit() override;

private:
    nvs_handle_t handle_ = 0;
    bool open_ = false;
};

}  // namespace direcsun::app
