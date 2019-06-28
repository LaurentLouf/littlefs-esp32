/* mbed Microcontroller Library
 * Copyright (c) 2017 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "LittleFileSystem.h"
#include <esp_log.h>
#include "errno.h"
#include "lfs_util.h"

static const char *TAG = "LittleFS";

////// Conversion functions //////
static int lfs_toerror(int err) {
    switch (err) {
        case LFS_ERR_OK:
            return 0;
        case LFS_ERR_IO:
            return -EIO;
        case LFS_ERR_NOENT:
            return -ENOENT;
        case LFS_ERR_EXIST:
            return -EEXIST;
        case LFS_ERR_NOTDIR:
            return -ENOTDIR;
        case LFS_ERR_ISDIR:
            return -EISDIR;
        case LFS_ERR_INVAL:
            return -EINVAL;
        case LFS_ERR_NOSPC:
            return -ENOSPC;
        case LFS_ERR_NOMEM:
            return -ENOMEM;
        case LFS_ERR_CORRUPT:
            return -EILSEQ;
        default:
            return err;
    }
}

static int lfs_fromflags(int flags) {
    return ((((flags & 3) == O_RDONLY) ? LFS_O_RDONLY : 0) |
            (((flags & 3) == O_WRONLY) ? LFS_O_WRONLY : 0) |
            (((flags & 3) == O_RDWR) ? LFS_O_RDWR : 0) | ((flags & O_CREAT) ? LFS_O_CREAT : 0) |
            ((flags & O_EXCL) ? LFS_O_EXCL : 0) | ((flags & O_TRUNC) ? LFS_O_TRUNC : 0) |
            ((flags & O_APPEND) ? LFS_O_APPEND : 0));
}

static int lfs_fromwhence(int whence) {
    switch (whence) {
        case SEEK_SET:
            return LFS_SEEK_SET;
        case SEEK_CUR:
            return LFS_SEEK_CUR;
        case SEEK_END:
            return LFS_SEEK_END;
        default:
            return whence;
    }
}

static int lfs_tomode(int type) {
    int mode = LFS_S_IRWXU | LFS_S_IRWXG | LFS_S_IRWXO;
    switch (type) {
        case LFS_TYPE_DIR:
            return mode | LFS_S_IFDIR;
        case LFS_TYPE_REG:
            return mode | LFS_S_IFREG;
        default:
            return 0;
    }
}

static int lfs_totype(int type) {
    switch (type) {
        case LFS_TYPE_DIR:
            return LFS_DT_DIR;
        case LFS_TYPE_REG:
            return LFS_DT_REG;
        default:
            return LFS_DT_UNKNOWN;
    }
}

////// Block device operations //////
static int lfs_bd_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer,
                       lfs_size_t size) {
    BlockDevice *bd = (BlockDevice *)c->context;
    return bd->read(buffer, (bd_addr_t)block * c->block_size + off, size);
}

static int lfs_bd_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                       const void *buffer, lfs_size_t size) {
    BlockDevice *bd = (BlockDevice *)c->context;
    return bd->program(buffer, (bd_addr_t)block * c->block_size + off, size);
}

static int lfs_bd_erase(const struct lfs_config *c, lfs_block_t block) {
    BlockDevice *bd = (BlockDevice *)c->context;
    return bd->erase((bd_addr_t)block * c->block_size, c->block_size);
}

static int lfs_bd_sync(const struct lfs_config *c) {
    BlockDevice *bd = (BlockDevice *)c->context;
    return bd->sync();
}

////// Generic filesystem operations //////

// Filesystem implementation (See LittleFileSystem.h)
LittleFileSystem::LittleFileSystem(const char *name, BlockDevice *bd, lfs_size_t read_size,
                                   lfs_size_t prog_size, lfs_size_t block_size,
                                   lfs_size_t lookahead)
    : _name(name),
      _read_size(read_size),
      _prog_size(prog_size),
      _block_size(block_size),
      _lookahead(lookahead) {
    // Create mutex
    _mutex = xSemaphoreCreateMutex();

    // Mount block device
    if (bd) {
        mount(bd);
    }
}

LittleFileSystem::~LittleFileSystem() {
    // nop if unmounted
    unmount();
}

int LittleFileSystem::mount(BlockDevice *bd) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "mount(%p)", bd);
    _bd = bd;
    int err = _bd->init();
    if (err) {
        _bd = NULL;
        ESP_LOGE(TAG, "mount(%p) -> %d", bd, err);
        xSemaphoreGive(_mutex);
        return err;
    }

    memset(&_config, 0, sizeof(_config));
    _config.context = bd;
    _config.read = lfs_bd_read;
    _config.prog = lfs_bd_prog;
    _config.erase = lfs_bd_erase;
    _config.sync = lfs_bd_sync;
    _config.read_size = bd->get_read_size();
    if (_config.read_size < _read_size) {
        _config.read_size = _read_size;
    }
    _config.prog_size = bd->get_program_size();
    if (_config.prog_size < _prog_size) {
        _config.prog_size = _prog_size;
    }
    _config.block_size = bd->get_erase_size();
    if (_config.block_size < _block_size) {
        _config.block_size = _block_size;
    }
    _config.block_count = bd->size() / _config.block_size;
    _config.lookahead = 32 * ((_config.block_count + 31) / 32);
    if (_config.lookahead > _lookahead) {
        _config.lookahead = _lookahead;
    }

    err = lfs_mount(&_lfs, &_config);
    if (err) {
        _bd = NULL;
        ESP_LOGE(TAG, "mount(%p) -> %d", bd, lfs_toerror(err));
        xSemaphoreGive(_mutex);
        return lfs_toerror(err);
    }

    xSemaphoreGive(_mutex);
    ESP_LOGV(TAG, "mount(%p) -> %d", bd, 0);
    return 0;
}

int LittleFileSystem::unmount() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "unmount(%s)", "");
    int res = 0;
    if (_bd) {
        int err = lfs_unmount(&_lfs);
        if (err && !res) {
            res = lfs_toerror(err);
        }

        err = _bd->deinit();
        if (err && !res) {
            res = err;
        }

        _bd = NULL;
    }

    ESP_LOGV(TAG, "unmount -> %d", res);
    xSemaphoreGive(_mutex);
    return res;
}

int LittleFileSystem::format(BlockDevice *bd, lfs_size_t read_size, lfs_size_t prog_size,
                             lfs_size_t block_size, lfs_size_t lookahead) {
    ESP_LOGV(TAG, "format(%p, %ud, %ud, %ud, %ud)", bd, read_size, prog_size, block_size,
             lookahead);
    int err = bd->init();
    if (err) {
        ESP_LOGE(TAG, "format -> %d", err);
        return err;
    }

    lfs_t _lfs;
    struct lfs_config _config;

    memset(&_config, 0, sizeof(_config));
    _config.context = bd;
    _config.read = lfs_bd_read;
    _config.prog = lfs_bd_prog;
    _config.erase = lfs_bd_erase;
    _config.sync = lfs_bd_sync;
    _config.read_size = bd->get_read_size();
    if (_config.read_size < read_size) {
        _config.read_size = read_size;
    }
    _config.prog_size = bd->get_program_size();
    if (_config.prog_size < prog_size) {
        _config.prog_size = prog_size;
    }
    _config.block_size = bd->get_erase_size();
    if (_config.block_size < block_size) {
        _config.block_size = block_size;
    }
    _config.block_count = bd->size() / _config.block_size;
    _config.lookahead = 32 * ((_config.block_count + 31) / 32);
    if (_config.lookahead > lookahead) {
        _config.lookahead = lookahead;
    }

    err = lfs_format(&_lfs, &_config);
    if (err) {
        ESP_LOGE(TAG, "format -> %d", lfs_toerror(err));
        return lfs_toerror(err);
    }

    err = bd->deinit();
    if (err) {
        ESP_LOGE(TAG, "format -> %d", err);
        return err;
    }

    ESP_LOGV(TAG, "format -> %d", 0);
    return 0;
}

int LittleFileSystem::reformat(BlockDevice *bd) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "reformat(%p)", bd);
    if (_bd) {
        if (!bd) {
            bd = _bd;
        }

        int err = unmount();
        if (err) {
            ESP_LOGE(TAG, "reformat -> %d", err);
            xSemaphoreGive(_mutex);
            return err;
        }
    }

    if (!bd) {
        ESP_LOGE(TAG, "reformat -> %d", -ENODEV);
        xSemaphoreGive(_mutex);
        return -ENODEV;
    }

    int err = LittleFileSystem::format(bd, _read_size, _prog_size, _block_size, _lookahead);
    if (err) {
        ESP_LOGE(TAG, "reformat -> %d", err);
        xSemaphoreGive(_mutex);
        return err;
    }

    err = mount(bd);
    if (err) {
        ESP_LOGE(TAG, "reformat -> %d", err);
        xSemaphoreGive(_mutex);
        return err;
    }

    ESP_LOGV(TAG, "reformat -> %d", 0);
    xSemaphoreGive(_mutex);
    return 0;
}

int LittleFileSystem::remove(const char *filename) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "remove(\"%s\")", filename);
    int err = lfs_remove(&_lfs, filename);
    ESP_LOGD(TAG, "remove(\"%s\") -> %d", filename, lfs_toerror(err));
    xSemaphoreGive(_mutex);
    return lfs_toerror(err);
}

int LittleFileSystem::rename(const char *oldname, const char *newname) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "rename(\"%s\", \"%s\")", oldname, newname);
    int err = lfs_rename(&_lfs, oldname, newname);
    ESP_LOGD(TAG, "rename(\"%s\", \"%s\") -> %d", oldname, newname, lfs_toerror(err));
    xSemaphoreGive(_mutex);
    return lfs_toerror(err);
}

int LittleFileSystem::mkdir(const char *name, mode_t mode) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "mkdir(\"%s\", 0x%x)", name, mode);
    int err = lfs_mkdir(&_lfs, name);
    ESP_LOGD(TAG, "mkdir(\"%s\", 0x%x) -> %d", name, mode, lfs_toerror(err));
    xSemaphoreGive(_mutex);
    return lfs_toerror(err);
}

int LittleFileSystem::stat(const char *name, struct lfs_stat *st) {
    struct lfs_info info;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "stat(\"%s\", %p)", name, st);
    int err = lfs_stat(&_lfs, name, &info);
    ESP_LOGD(TAG, "stat(\"%s\", %p) -> %d", name, st, lfs_toerror(err));
    xSemaphoreGive(_mutex);
    st->st_size = info.size;
    st->st_mode = lfs_tomode(info.type);
    return lfs_toerror(err);
}

static int lfs_statvfs_count(void *p, lfs_block_t b) {
    *(lfs_size_t *)p += 1;
    return 0;
}

int LittleFileSystem::statvfs(const char *name, struct lfs_statvfs *st) {
    memset(st, 0, sizeof(struct lfs_statvfs));

    lfs_size_t in_use = 0;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "statvfs(\"%s\", %p)", name, st);
    int err = lfs_traverse(&_lfs, lfs_statvfs_count, &in_use);
    xSemaphoreGive(_mutex);
    if (err) {
        ESP_LOGE(TAG, "statvfs(\"%s\", %p) -> %d", name, st, lfs_toerror(err));
        return err;
    }

    st->f_bsize = _config.block_size;
    st->f_frsize = _config.block_size;
    st->f_blocks = _config.block_count;
    st->f_bfree = _config.block_count - in_use;
    st->f_bavail = _config.block_count - in_use;
    st->f_namemax = LFS_NAME_MAX;
    return 0;
}

////// File operations //////
int LittleFileSystem::file_open(fs_file_t *file, const char *path, int flags) {
    lfs_file_t *f = new lfs_file_t;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "file_open(%p, \"%s\", 0x%x)", *file, path, flags);
    int err = lfs_file_open(&_lfs, f, path, lfs_fromflags(flags));
    xSemaphoreGive(_mutex);
    if (!err) {
        *file = f;
    } else {
        ESP_LOGE(TAG, "file_open(%p, \"%s\", 0x%x) -> %d", *file, path, flags, lfs_toerror(err));
        delete f;
    }
    return lfs_toerror(err);
}

int LittleFileSystem::file_close(fs_file_t file) {
    lfs_file_t *f = (lfs_file_t *)file;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "file_close(%p)", file);
    int err = lfs_file_close(&_lfs, f);
    ESP_LOGD(TAG, "file_close(%p) -> %d", file, lfs_toerror(err));
    xSemaphoreGive(_mutex);
    delete f;
    return lfs_toerror(err);
}

ssize_t LittleFileSystem::file_read(fs_file_t file, void *buffer, size_t len) {
    lfs_file_t *f = (lfs_file_t *)file;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "file_read(%p, %p, %d)", file, buffer, len);
    lfs_ssize_t res = lfs_file_read(&_lfs, f, buffer, len);
    ESP_LOGD(TAG, "file_read(%p, %p, %d) -> %d", file, buffer, len, lfs_toerror(res));
    xSemaphoreGive(_mutex);
    return lfs_toerror(res);
}

ssize_t LittleFileSystem::file_write(fs_file_t file, const void *buffer, size_t len) {
    lfs_file_t *f = (lfs_file_t *)file;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "file_write(%p, %p, %d)", file, buffer, len);
    lfs_ssize_t res = lfs_file_write(&_lfs, f, buffer, len);
    ESP_LOGD(TAG, "file_write(%p, %p, %d) -> %d", file, buffer, len, lfs_toerror(res));
    xSemaphoreGive(_mutex);
    return lfs_toerror(res);
}

int LittleFileSystem::file_sync(fs_file_t file) {
    lfs_file_t *f = (lfs_file_t *)file;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "file_sync(%p)", file);
    int err = lfs_file_sync(&_lfs, f);
    ESP_LOGD(TAG, "file_sync(%p) -> %d", file, lfs_toerror(err));
    xSemaphoreGive(_mutex);
    return lfs_toerror(err);
}

off_t LittleFileSystem::file_seek(fs_file_t file, off_t offset, int whence) {
    lfs_file_t *f = (lfs_file_t *)file;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "file_seek(%p, %ld, %d)", file, offset, whence);
    off_t res = lfs_file_seek(&_lfs, f, offset, lfs_fromwhence(whence));
    ESP_LOGD(TAG, "file_seek(%p, %ld, %d) -> %d", file, offset, whence, lfs_toerror(res));
    xSemaphoreGive(_mutex);
    return lfs_toerror(res);
}

off_t LittleFileSystem::file_tell(fs_file_t file) {
    lfs_file_t *f = (lfs_file_t *)file;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "file_tell(%p)", file);
    off_t res = lfs_file_tell(&_lfs, f);
    ESP_LOGD(TAG, "file_tell(%p) -> %d", file, lfs_toerror(res));
    xSemaphoreGive(_mutex);
    return lfs_toerror(res);
}

off_t LittleFileSystem::file_size(fs_file_t file) {
    lfs_file_t *f = (lfs_file_t *)file;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "file_size(%p)", file);
    off_t res = lfs_file_size(&_lfs, f);
    ESP_LOGD(TAG, "file_size(%p) -> %d", file, lfs_toerror(res));
    xSemaphoreGive(_mutex);
    return lfs_toerror(res);
}

////// Dir operations //////
int LittleFileSystem::dir_open(fs_dir_t *dir, const char *path) {
    lfs_dir_t *d = new lfs_dir_t;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "dir_open(%p, \"%s\")", *dir, path);
    int err = lfs_dir_open(&_lfs, d, path);
    xSemaphoreGive(_mutex);
    if (!err) {
        *dir = d;
    } else {
        ESP_LOGE(TAG, "dir_open(%p, \"%s\") -> %d", *dir, path, lfs_toerror(err));
        delete d;
    }
    return lfs_toerror(err);
}

int LittleFileSystem::dir_close(fs_dir_t dir) {
    lfs_dir_t *d = (lfs_dir_t *)dir;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "dir_close(%p)", dir);
    int err = lfs_dir_close(&_lfs, d);
    ESP_LOGD(TAG, "dir_close(%p) -> %d", dir, lfs_toerror(err));
    xSemaphoreGive(_mutex);
    delete d;
    return lfs_toerror(err);
}

ssize_t LittleFileSystem::dir_read(fs_dir_t dir, struct lfs_dirent *ent) {
    lfs_dir_t *d = (lfs_dir_t *)dir;
    struct lfs_info info;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "dir_read(%p, %p)", dir, ent);
    int res = lfs_dir_read(&_lfs, d, &info);
    ESP_LOGD(TAG, "dir_read(%p, %p) -> %d", dir, ent, lfs_toerror(res));
    xSemaphoreGive(_mutex);
    if (res == 1) {
        ent->d_type = lfs_totype(info.type);
        strcpy(ent->d_name, info.name);
    }
    return lfs_toerror(res);
}

void LittleFileSystem::dir_seek(fs_dir_t dir, off_t offset) {
    lfs_dir_t *d = (lfs_dir_t *)dir;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "dir_seek(%p, %ld)", dir, offset);
    lfs_dir_seek(&_lfs, d, offset);
    ESP_LOGD(TAG, "dir_seek(%p, %ld) -> %s", dir, offset, "void");
    xSemaphoreGive(_mutex);
}

off_t LittleFileSystem::dir_tell(fs_dir_t dir) {
    lfs_dir_t *d = (lfs_dir_t *)dir;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "dir_tell(%p)", dir);
    lfs_soff_t res = lfs_dir_tell(&_lfs, d);
    ESP_LOGD(TAG, "dir_tell(%p) -> %d", dir, lfs_toerror(res));
    xSemaphoreGive(_mutex);
    return lfs_toerror(res);
}

void LittleFileSystem::dir_rewind(fs_dir_t dir) {
    lfs_dir_t *d = (lfs_dir_t *)dir;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ESP_LOGV(TAG, "dir_rewind(%p)", dir);
    lfs_dir_rewind(&_lfs, d);
    ESP_LOGD(TAG, "dir_rewind(%p) -> %s", dir, "void");
    xSemaphoreGive(_mutex);
}
