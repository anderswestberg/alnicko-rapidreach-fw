# MQTT Phantom Message Fix

## Problem
The MQTT client gets stuck in a loop trying to read "phantom" messages after processing a large file. It repeatedly gets -EAGAIN (-11) errors from mqtt_rx.c line 241.

## Root Cause
The MQTT client's internal state tracking how many payload bytes remain doesn't match reality. This happens when we don't consume EXACTLY the number of bytes advertised in `pub->message.payload.len`.

## Critical Rules for mqtt_read_publish_payload_blocking()

1. **MUST consume exactly pub->message.payload.len bytes** - no more, no less
2. **Partial reads are normal** - the function may return less than requested
3. **Track carefully** - every byte read must be accounted for
4. **No skipping** - even if parsing fails, must consume all bytes

## Common Mistakes

### 1. Not handling partial reads
```c
// WRONG - assumes we always get what we ask for
mqtt_read_publish_payload_blocking(client, buf, 1024);
remaining -= 1024;  // Bug if only 512 bytes were returned!

// CORRECT
int ret = mqtt_read_publish_payload_blocking(client, buf, 1024);
if (ret > 0) remaining -= ret;  // Use actual bytes read
```

### 2. Wrong byte counting on error
```c
// WRONG - if initial read fails, we haven't read json_read bytes
if (ret < 0) {
    remaining = total_len - json_read;  // Bug!
}

// CORRECT
if (ret < 0) {
    remaining = total_len;  // Nothing was read
}
```

### 3. Not updating counters for partial reads
```c
// WRONG - json_read might not match actual bytes read
json_read = MIN(total_len, 512);
ret = mqtt_read_publish_payload_blocking(client, buf, json_read);

// CORRECT
json_read = MIN(total_len, 512);
ret = mqtt_read_publish_payload_blocking(client, buf, json_read);
if (ret > 0 && ret != json_read) {
    json_read = ret;  // Update to actual
}
```

## Solution Checklist

1. ✅ Track total bytes that must be consumed (pub->message.payload.len)
2. ✅ Track actual bytes consumed (sum of all positive return values)
3. ✅ At the end, verify: total_consumed == advertised_length
4. ✅ If mismatch, consume any remaining bytes
5. ✅ Never try to read more than advertised length
6. ✅ Handle partial reads properly

## Debug Output to Add
```
[INFO] MQTT message: advertised_len=61288
[INFO] Initial read: requested=512, got=512
[INFO] File write loop: consumed=61288, remaining=0
[INFO] Final check: advertised=61288, consumed=61288 ✓
```

## Emergency Recovery
If stuck in phantom message loop:
1. Disconnect MQTT client
2. Reconnect 
3. State will be reset
