#include "mqtt_log_client.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/fs/fs.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "../mqtt_module/mqtt_module.h"
#include "../dev_info/dev_info.h"
#include "../rtc/rtc.h"
#include <time.h>

LOG_MODULE_REGISTER(mqtt_log, LOG_LEVEL_INF);

#if defined(CONFIG_RPR_MQTT_LOG_CLIENT)

#define LOG_TOPIC_PREFIX CONFIG_RPR_MQTT_LOG_TOPIC_PREFIX

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

struct mqtt_log_entry {
	int64_t timestamp;
	char level[8];
	char module[16];
	char message[160];
};

static struct {
	/* RAM ring buffer */
	struct mqtt_log_entry *buffer;
	uint16_t head;
	uint16_t tail;
	uint16_t count;
	uint16_t capacity;

	/* Filesystem overflow */
	bool fs_overflow_enabled;
	uint32_t fs_log_count;

	/* State */
	bool initialized;
	struct k_work_delayable flush_work;
	struct k_mutex mutex;

	/* Backoff */
	uint32_t backoff_ms;
	uint32_t consecutive_failures;

	/* Identity */
	char short_id[7];
} s;

/* Backoff configuration */
#define INITIAL_BACKOFF_MS      1000
#define MAX_BACKOFF_MS          60000
#define BACKOFF_MULTIPLIER      2

/* FS ring file (simple, like HTTP client) */
#define FS_LOG_FILE_PATH        "/lfs/mqtt_logs.bin"
#define FS_LOG_MAGIC            0x4D4C4F47 /* "MLOG" */
#define FS_LOG_VERSION          1

struct fs_log_header {
	uint32_t magic;
	uint32_t version;
	uint32_t total_logs;
	uint32_t write_index;
	uint32_t read_index;
};

static int fs_log_init(void);
static int fs_log_write(const struct mqtt_log_entry *entry);
static int fs_log_read(struct mqtt_log_entry *entry);

static int schedule_flush(int delay_ms)
{
	return k_work_schedule(&s.flush_work, K_MSEC(delay_ms));
}

static void update_backoff(bool success)
{
	if (success) {
		s.consecutive_failures = 0;
		s.backoff_ms = INITIAL_BACKOFF_MS;
	} else {
		s.consecutive_failures++;
		s.backoff_ms = MIN(s.backoff_ms * BACKOFF_MULTIPLIER, MAX_BACKOFF_MS);
	}
}

static int publish_logs_json(const char *json, size_t len)
{
	if (!mqtt_is_connected()) {
		return -1;
	}
	char topic[48];
	int n = snprintf(topic, sizeof(topic), "%s/%.6s", LOG_TOPIC_PREFIX, s.short_id);
	if (n <= 0 || n >= (int)sizeof(topic)) {
		return -1;
	}
	return mqtt_module_publish(topic, json, len) == MQTT_SUCCESS ? 0 : -1;
}

static size_t append_json_item(char *buf, size_t buf_size, const struct mqtt_log_entry *e, bool first)
{
	/* naive string escape for quotes and backslashes */
	char esc[sizeof e->message * 2];
	const char *src = e->message;
	char *dst = esc;
	size_t rem = sizeof(esc) - 1;
	while (*src && rem > 1) {
		if (*src == '"' || *src == '\\') {
			if (rem <= 2) break;
			*dst++ = '\\';
			rem--;
		}
		*dst++ = *src++;
		rem--;
	}
	*dst = '\0';

	/* Try to get actual time if RTC is available and timestamp looks like uptime */
	int64_t timestamp_to_send = e->timestamp;
	if (e->timestamp > 0 && e->timestamp < 1000000000) {
		/* This looks like uptime in ms, try to get real time */
		struct rtc_time current_time;
		if (get_date_time(&current_time) == 0) {
			/* The RTC might be storing the actual year (like 2025) in tm_year
			 * instead of years since 1900. Check for both cases. */
			int actual_year;
			bool valid_rtc = false;
			
			if (current_time.tm_year > 1900) {
				/* RTC is storing actual year */
				actual_year = current_time.tm_year;
				/* Need to adjust tm_year to standard format for mktime */
				current_time.tm_year = actual_year - 1900;
				valid_rtc = (actual_year >= 2020 && actual_year <= 2099);
			} else if (current_time.tm_year >= 120 && current_time.tm_year < 200) {
				/* RTC is storing years since 1900 (standard) */
				actual_year = current_time.tm_year + 1900;
				valid_rtc = true;
			}
			
			if (valid_rtc) {
				/* RTC is available and has valid time, convert to Unix timestamp */
				time_t current_unix = mktime((struct tm *)&current_time);
				if (current_unix != (time_t)-1) {
					/* Convert to milliseconds and subtract uptime to get boot time */
					int64_t current_ms = (int64_t)current_unix * 1000;
					int64_t boot_time_ms = current_ms - k_uptime_get();
					timestamp_to_send = boot_time_ms + e->timestamp;
				}
			}
		}
		/* If RTC is not valid, keep the uptime-based timestamp */
		/* The server will handle it appropriately */
	}

	size_t used = 0;
	used += snprintf(buf + used, buf_size - used,
				   "%s{\"timestamp\":%lld,\"level\":\"%s\",\"module\":\"%s\",\"message\":\"%s\"}",
				   first ? "" : ",",
				   (long long)timestamp_to_send, e->level, e->module, esc);
	return used;
}

static int build_and_publish_batch(struct mqtt_log_entry *batch, uint16_t count)
{
	/* Respect size cap */
	static char json[CONFIG_RPR_MQTT_LOG_BATCH_BYTES];
	size_t used = 0;
	used += snprintf(json + used, sizeof(json) - used, "{\"source\":\"%.6s\",\"logs\":[", s.short_id);
	uint16_t i = 0;
	for (; i < count; i++) {
		size_t added = append_json_item(json + used, sizeof(json) - used, &batch[i], i == 0);
		if (used + added + 2 >= sizeof(json)) { /* reserve for closing */
			break;
		}
		used += added;
	}
	used += snprintf(json + used, sizeof(json) - used, "]}");
	return publish_logs_json(json, used) == 0 ? (int)i : -1;
}

static void flush_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!s.initialized) return;

	if (s.count == 0 && s.fs_log_count == 0) {
		goto resched;
	}

	/* Gather batch: FS first (older), then RAM */
	static struct mqtt_log_entry batch[CONFIG_RPR_MQTT_LOG_BATCH_MAX];
	uint16_t batch_count = 0;

	k_mutex_lock(&s.mutex, K_FOREVER);
	while (batch_count < CONFIG_RPR_MQTT_LOG_BATCH_MAX && s.fs_log_count > 0) {
		if (fs_log_read(&batch[batch_count]) == 0) {
			batch_count++;
			s.fs_log_count--;
		} else {
			break;
		}
	}
	while (batch_count < CONFIG_RPR_MQTT_LOG_BATCH_MAX && s.count > 0) {
		batch[batch_count++] = s.buffer[s.tail];
		s.tail = (s.tail + 1) % s.capacity;
		s.count--;
	}
	k_mutex_unlock(&s.mutex);

	if (batch_count > 0) {
		int sent = build_and_publish_batch(batch, batch_count);
		if (sent < 0) {
			/* failed: put back RAM logs (not FS), at tail reverse order */
			k_mutex_lock(&s.mutex, K_FOREVER);
			for (int i = batch_count - 1; i >= 0 && s.count < s.capacity; i--) {
				s.tail = (s.tail - 1 + s.capacity) % s.capacity;
				s.buffer[s.tail] = batch[i];
				s.count++;
			}
			k_mutex_unlock(&s.mutex);
			update_backoff(false);
		} else {
			update_backoff(true);
		}
	}

resched:
	/* backoff on failures */
	uint32_t next_ms = s.consecutive_failures ? s.backoff_ms : CONFIG_RPR_MQTT_LOG_FLUSH_INTERVAL_MS;
	schedule_flush(next_ms);
}

int mqtt_log_client_init(void)
{
	if (s.initialized) return 0;

	/* Compute short id */
	size_t id_len = 0;
	const char *id = dev_info_get_device_id_str(&id_len);
	memset(s.short_id, 0, sizeof(s.short_id));
	if (id && id_len >= 6) {
		memcpy(s.short_id, id, 6);
	} else if (id) {
		strncpy(s.short_id, id, sizeof(s.short_id) - 1);
	}

	/* Allocate RAM buffer */
	s.capacity = CONFIG_RPR_MQTT_LOG_RAM_ENTRIES;
	s.buffer = k_malloc(sizeof(struct mqtt_log_entry) * s.capacity);
	if (!s.buffer) {
		return -ENOMEM;
	}
	s.head = s.tail = s.count = 0;

	k_mutex_init(&s.mutex);
	k_work_init_delayable(&s.flush_work, flush_work_handler);

	s.backoff_ms = INITIAL_BACKOFF_MS;
	s.consecutive_failures = 0;

	/* Try to enable FS overflow */
	if (fs_log_init() == 0) {
		s.fs_overflow_enabled = true;
	} else {
		s.fs_overflow_enabled = false;
	}

	s.initialized = true;
	schedule_flush(CONFIG_RPR_MQTT_LOG_FLUSH_INTERVAL_MS);
	return 0;
}

int mqtt_log_client_put(const char *level, const char *message, uint64_t timestamp_ms)
{
	if (!s.initialized) return -EINVAL;

	struct mqtt_log_entry e;
	e.timestamp = (int64_t)timestamp_ms;
	strncpy(e.level, level ? level : "info", sizeof(e.level) - 1);
	e.level[sizeof(e.level) - 1] = '\0';
	
	/* Extract module/source from message if it contains "module_name: " pattern */
	const char *msg_start = message;
	char extracted_module[32] = "app";  /* Default if no module found */
	
	if (message) {
		/* Look for first colon */
		const char *colon = strchr(message, ':');
		if (colon && colon > message) {
			/* Check if there's a space after the colon */
			if (*(colon + 1) == ' ') {
				/* Extract the module name */
				size_t module_len = colon - message;
				if (module_len < sizeof(extracted_module)) {
					memcpy(extracted_module, message, module_len);
					extracted_module[module_len] = '\0';
					/* Skip past "module: " for the actual message */
					msg_start = colon + 2;
				}
			}
		}
	}
	
	strncpy(e.module, extracted_module, sizeof(e.module) - 1);
	e.module[sizeof(e.module) - 1] = '\0';
	strncpy(e.message, msg_start ? msg_start : "", sizeof(e.message) - 1);
	e.message[sizeof(e.message) - 1] = '\0';

	k_mutex_lock(&s.mutex, K_FOREVER);
	if (s.count >= s.capacity) {
		/* spill to FS if possible */
		if (s.fs_overflow_enabled && fs_log_write(&e) == 0) {
			s.fs_log_count++;
		} else {
			/* drop oldest */
			s.tail = (s.tail + 1) % s.capacity;
			/* add new */
			s.buffer[s.head] = e;
			s.head = (s.head + 1) % s.capacity;
		}
	} else {
		s.buffer[s.head] = e;
		s.head = (s.head + 1) % s.capacity;
		s.count++;
	}

	bool reach_batch = s.count >= CONFIG_RPR_MQTT_LOG_BATCH_MAX;
	k_mutex_unlock(&s.mutex);

	if (reach_batch) {
		schedule_flush(0);
	} else {
		schedule_flush(CONFIG_RPR_MQTT_LOG_FLUSH_INTERVAL_MS);
	}
	return 0;
}

int mqtt_log_client_flush(void)
{
	return schedule_flush(0);
}

/* FS helpers (private copy similar to HTTP client) */
static int fs_log_init(void)
{
#ifdef CONFIG_FILE_SYSTEM
	struct fs_file_t file;
	struct fs_log_header header;
	int ret;

	fs_file_t_init(&file);
	ret = fs_open(&file, FS_LOG_FILE_PATH, FS_O_RDWR);
	if (ret < 0) {
		ret = fs_open(&file, FS_LOG_FILE_PATH, FS_O_CREATE | FS_O_RDWR);
		if (ret < 0) {
			return ret;
		}
		header.magic = FS_LOG_MAGIC;
		header.version = FS_LOG_VERSION;
		header.total_logs = 0;
		header.write_index = 0;
		header.read_index = 0;
		ret = fs_write(&file, &header, sizeof(header));
		if (ret != sizeof(header)) {
			fs_close(&file);
			return -EIO;
		}
	} else {
		ret = fs_read(&file, &header, sizeof(header));
		if (ret != sizeof(header) || header.magic != FS_LOG_MAGIC || header.version != FS_LOG_VERSION) {
			fs_close(&file);
			return -EINVAL;
		}
		s.fs_log_count = header.total_logs;
	}
	fs_close(&file);
	return 0;
#else
	return -ENOTSUP;
#endif
}

static int fs_log_write(const struct mqtt_log_entry *entry)
{
#ifdef CONFIG_FILE_SYSTEM
	struct fs_file_t file;
	struct fs_log_header header;
	int ret;
	off_t offset;

	fs_file_t_init(&file);
	ret = fs_open(&file, FS_LOG_FILE_PATH, FS_O_RDWR);
	if (ret < 0) return ret;
	ret = fs_read(&file, &header, sizeof(header));
	if (ret != sizeof(header)) { fs_close(&file); return -EIO; }

	/* conservative cap to avoid unlimited growth */
	const uint32_t FS_MAX_LOGS = 1000;
	if (header.total_logs >= FS_MAX_LOGS) { fs_close(&file); return -ENOSPC; }

	offset = sizeof(header) + (header.write_index * sizeof(struct mqtt_log_entry));
	ret = fs_seek(&file, offset, FS_SEEK_SET);
	if (ret < 0) { fs_close(&file); return ret; }
	ret = fs_write(&file, entry, sizeof(*entry));
	if (ret != sizeof(*entry)) { fs_close(&file); return -EIO; }

	header.write_index = (header.write_index + 1) % FS_MAX_LOGS;
	header.total_logs++;
	fs_seek(&file, 0, FS_SEEK_SET);
	ret = fs_write(&file, &header, sizeof(header));
	fs_close(&file);
	return (ret == sizeof(header)) ? 0 : -EIO;
#else
	return -ENOTSUP;
#endif
}

static int fs_log_read(struct mqtt_log_entry *entry)
{
#ifdef CONFIG_FILE_SYSTEM
	struct fs_file_t file;
	struct fs_log_header header;
	int ret;
	off_t offset;

	fs_file_t_init(&file);
	ret = fs_open(&file, FS_LOG_FILE_PATH, FS_O_RDWR);
	if (ret < 0) return ret;
	ret = fs_read(&file, &header, sizeof(header));
	if (ret != sizeof(header)) { fs_close(&file); return -EIO; }
	if (header.total_logs == 0 || header.read_index == header.write_index) {
		fs_close(&file);
		return -ENOENT;
	}
	offset = sizeof(header) + (header.read_index * sizeof(struct mqtt_log_entry));
	ret = fs_seek(&file, offset, FS_SEEK_SET);
	if (ret < 0) { fs_close(&file); return ret; }
	ret = fs_read(&file, entry, sizeof(*entry));
	if (ret != sizeof(*entry)) { fs_close(&file); return -EIO; }
	header.read_index = (header.read_index + 1) % 1000;
	header.total_logs--;
	fs_seek(&file, 0, FS_SEEK_SET);
	ret = fs_write(&file, &header, sizeof(header));
	fs_close(&file);
	return (ret == sizeof(header)) ? 0 : -EIO;
#else
	return -ENOTSUP;
#endif
}

#else /* !CONFIG_RPR_MQTT_LOG_CLIENT */

int mqtt_log_client_init(void) { return 0; }
int mqtt_log_client_put(const char *level, const char *message, uint64_t timestamp_ms)
{
	ARG_UNUSED(level);
	ARG_UNUSED(message);
	ARG_UNUSED(timestamp_ms);
	return 0;
}
int mqtt_log_client_flush(void) { return 0; }

#endif /* CONFIG_RPR_MQTT_LOG_CLIENT */


