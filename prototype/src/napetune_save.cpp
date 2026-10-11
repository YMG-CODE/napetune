#include "napetune_save.h"

#include <M5Unified.h>
#include <SD.h>
#include <string.h>

#include "core2_bridge.h"
#include "spi_guard.h"

namespace
{
constexpr uint16_t kSaveVersion = 1;
constexpr char kSaveMagic[8] = {'N', 'A', 'P', 'E', 'S', 'A', 'V', 'E'};
constexpr const char* kRoot = "/napetune";
constexpr const char* kSystem = "/napetune/system";
constexpr const char* kSaveDir = "/napetune/system/save";
constexpr const char* kMainPath = "/napetune/system/save/save0.dat";
constexpr const char* kBackupPath = "/napetune/system/save/save0.bak";
constexpr const char* kTempPath = "/napetune/system/save/save0.tmp";

NapetuneSaveData s_save = {};
bool s_available = false;
bool s_dirty = false;

uint32_t crc32Bytes(const uint8_t* data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

uint32_t saveCrc(const NapetuneSaveData& source)
{
    NapetuneSaveData copy = source;
    copy.crc32 = 0;
    return crc32Bytes(reinterpret_cast<const uint8_t*>(&copy), sizeof(copy));
}

void setDefaults()
{
    memset(&s_save, 0, sizeof(s_save));
    memcpy(s_save.magic, kSaveMagic, sizeof(kSaveMagic));
    s_save.format_version = kSaveVersion;
    s_save.struct_size = sizeof(s_save);
    strncpy(s_save.content_pack, "builtin", sizeof(s_save.content_pack) - 1);
    s_save.weapon_slot[0] = 0;
    s_save.weapon_slot[1] = 1;
    s_dirty = true;
}

bool validSave(const NapetuneSaveData& value)
{
    return memcmp(value.magic, kSaveMagic, sizeof(kSaveMagic)) == 0 &&
           value.format_version == kSaveVersion &&
           value.struct_size == sizeof(value) &&
           value.crc32 == saveCrc(value);
}

bool readSaveFile(const char* path, NapetuneSaveData* result)
{
    File file = SD.open(path, FILE_READ);
    if (!file)
        return false;
    if (file.size() != sizeof(NapetuneSaveData))
    {
        file.close();
        return false;
    }
    const size_t got = file.read(
        reinterpret_cast<uint8_t*>(result),
        sizeof(NapetuneSaveData)
    );
    file.close();
    return got == sizeof(NapetuneSaveData) && validSave(*result);
}

void ensureDirectories()
{
    if (!SD.exists(kRoot)) SD.mkdir(kRoot);
    if (!SD.exists(kSystem)) SD.mkdir(kSystem);
    if (!SD.exists(kSaveDir)) SD.mkdir(kSaveDir);
}
}

static_assert(sizeof(NapetuneSaveData) <= 320, "Save slot unexpectedly large");

bool napetune_save_begin()
{
    setDefaults();
    if (!core2_sd_ready())
    {
        Serial.println("[SAVE] SD unavailable; RAM-only state");
        return false;
    }

    napetune_spi_acquire();
    ensureDirectories();

    NapetuneSaveData loaded = {};
    const bool mainOk = readSaveFile(kMainPath, &loaded);
    const bool backupOk = !mainOk && readSaveFile(kBackupPath, &loaded);
    napetune_spi_release();

    if (mainOk || backupOk)
    {
        s_save = loaded;
        s_dirty = false;
        Serial.printf(
            "[SAVE] loaded %s seq=%u pack=%s chapter=%u scene=%u\n",
            mainOk ? "MAIN" : "BACKUP",
            (unsigned)s_save.sequence,
            s_save.content_pack,
            (unsigned)s_save.chapter_id,
            (unsigned)s_save.scene_id
        );
    }
    else
    {
        Serial.println("[SAVE] no valid slot; defaults in RAM");
    }

    ++s_save.launch_count;
    s_dirty = true;
    s_available = true;
    return true;
}

bool napetune_save_available() { return s_available; }
bool napetune_save_dirty() { return s_dirty; }
const NapetuneSaveData& napetune_save_data() { return s_save; }

void napetune_save_set_last_mode(uint8_t mode)
{
    if (s_save.last_mode == mode)
        return;
    s_save.last_mode = mode;
    s_dirty = true;
}

void napetune_save_set_story_position(
    const char* content_pack,
    uint16_t chapter_id,
    uint16_t scene_id
)
{
    if (content_pack && content_pack[0])
    {
        strncpy(s_save.content_pack, content_pack, sizeof(s_save.content_pack) - 1);
        s_save.content_pack[sizeof(s_save.content_pack) - 1] = '\0';
    }
    s_save.chapter_id = chapter_id;
    s_save.scene_id = scene_id;
    s_dirty = true;
}

void napetune_save_set_event_flag(uint16_t flag_id, bool enabled)
{
    if (flag_id >= sizeof(s_save.event_flags) * 8u)
        return;
    const uint8_t mask = (uint8_t)(1u << (flag_id & 7u));
    uint8_t& cell = s_save.event_flags[flag_id >> 3u];
    const uint8_t next = enabled ? (uint8_t)(cell | mask) : (uint8_t)(cell & ~mask);
    if (cell != next)
    {
        cell = next;
        s_dirty = true;
    }
}

bool napetune_save_checkpoint(const char* reason)
{
    if (!s_available || !s_dirty || !core2_sd_ready())
        return !s_dirty;

    const uint32_t started = micros();
    NapetuneSaveData next = s_save;
    ++next.sequence;
    next.crc32 = saveCrc(next);

    napetune_spi_acquire();
    ensureDirectories();
    if (SD.exists(kTempPath)) SD.remove(kTempPath);

    File file = SD.open(kTempPath, FILE_WRITE);
    bool ok = (bool)file;
    if (ok)
    {
        ok = file.write(
            reinterpret_cast<const uint8_t*>(&next),
            sizeof(next)
        ) == sizeof(next);
        file.flush();
        file.close();
    }

    NapetuneSaveData verify = {};
    ok = ok && readSaveFile(kTempPath, &verify);
    if (ok)
    {
        if (SD.exists(kBackupPath)) SD.remove(kBackupPath);
        if (SD.exists(kMainPath)) SD.rename(kMainPath, kBackupPath);
        ok = SD.rename(kTempPath, kMainPath);
    }
    if (!ok && SD.exists(kTempPath)) SD.remove(kTempPath);
    napetune_spi_release();

    if (ok)
    {
        s_save = next;
        s_dirty = false;
    }
    Serial.printf(
        "[SAVE] checkpoint=%s result=%s bytes=%u time=%uus seq=%u\n",
        reason ? reason : "unknown",
        ok ? "OK" : "FAIL",
        (unsigned)sizeof(next),
        (unsigned)(micros() - started),
        (unsigned)next.sequence
    );
    return ok;
}

