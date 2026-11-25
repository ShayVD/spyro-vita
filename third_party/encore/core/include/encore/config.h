/* config.txt: `key=value` lines, read once at start.  Lines starting with '#' and lines without '=' are skipped;
 * the value loses its trailing newline, carriage return and spaces; the key is everything before the first '='
 * (nothing trimmed).  Lines are read 255 bytes at a time (a longer line goes on as the next "line").
 * From kh1-decomp host/vita/main.c config() @ e1575e1a. */
#ifndef ENC_CONFIG_H
#define ENC_CONFIG_H

typedef void (*EncConfigFn)(char *key, char *value, void *user);

/* calls fn for each key=value line of the file at path (stdio: on the Vita an ux0: path); 0 when there is no
 * such file, else 1 */
int enc_config_read(const char *path, EncConfigFn fn, void *user);

#endif /* ENC_CONFIG_H */
