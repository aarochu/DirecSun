#include "nvs_store.hpp"

#include "esp_log.h"

namespace direcsun::app {

namespace {

constexpr const char *TAG = "nvs_store";

}  // namespace

NvsKvStore::NvsKvStore(const char *ns)
{
    const esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle_);
    open_ = err == ESP_OK;
    if (!open_) {
        ESP_LOGE(TAG, "nvs_open(%s): %s", ns, esp_err_to_name(err));
    }
}

NvsKvStore::~NvsKvStore()
{
    if (open_) {
        nvs_close(handle_);
    }
}

bool NvsKvStore::read_u32(const char *key, uint32_t &value)
{
    return open_ && nvs_get_u32(handle_, key, &value) == ESP_OK;
}

bool NvsKvStore::read_blob(const char *key, void *data, std::size_t &len)
{
    if (!open_) {
        return false;
    }
    size_t size = len;
    if (nvs_get_blob(handle_, key, data, &size) != ESP_OK) {
        return false;
    }
    len = size;
    return true;
}

bool NvsKvStore::write_u32(const char *key, uint32_t value)
{
    return open_ && nvs_set_u32(handle_, key, value) == ESP_OK;
}

bool NvsKvStore::write_blob(const char *key, const void *data, std::size_t len)
{
    return open_ && nvs_set_blob(handle_, key, data, len) == ESP_OK;
}

bool NvsKvStore::commit()
{
    return open_ && nvs_commit(handle_) == ESP_OK;
}

}  // namespace direcsun::app
