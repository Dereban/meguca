#define _XOPEN_SOURCE
#include <errno.h>
#include <iconv.h>
#include <unistd.h>
#include <node.h>
#include <string.h>
#include <nan.h>

using namespace v8;

static char SECURE_SALT[21] = "$5$";
#define TRIP_MAX 128

static NAN_METHOD(setup_callback) {
	NanScope();

	if (args.Length() != 1)
		NanReturnValue(NanFalse());
	NanUtf8String saltVal(args[0]);
	if (saltVal.length() != 16)
		NanReturnValue(NanFalse());
	char *salt = *saltVal;
	if (!salt)
		NanReturnValue(NanFalse());
	memcpy(SECURE_SALT + 3, salt, 16);
	SECURE_SALT[19] = '$';
	SECURE_SALT[20] = 0;
	NanReturnValue(NanTrue());
}

static void fix_char(char &c) {
	static const char *from = ":;<=>?@[\\]^_`", *to = "ABCDEFGabcdef";
	const char *p;
	if (c < '.' || c > 'z')
		c = '.';
	else if ((p = strchr(from, c)))
		c = to[p - from];
}

static void hash_trip(char *key, size_t len, char *dest) {
	char *digest, salt[3] = "..";
	if (len == 1)
		salt[0] = 'H';
	else if (len == 2) {
		salt[0] = key[1];
		salt[1] = 'H';
	}
	else if (len)
		strncpy(salt, key + 1, 2);
	fix_char(salt[0]);
	fix_char(salt[1]);
	digest = crypt(key, salt);
	if (!digest)
		return;
	len = strlen(digest);
	if (len < 11)
		return;
	digest += len - 11;
	digest[0] = '!';
	strncpy(dest, digest, 12);
}

static void hash_secure(char *key, size_t len, char *dest) {
	size_t i;
	char *digest;
	if (len > TRIP_MAX) {
		len = TRIP_MAX;
		key[TRIP_MAX] = 0;
	}
	for (i = 0; i < len; i++)
		fix_char(key[i]);
	digest = crypt(key, SECURE_SALT);
	if (!digest)
		return;
	len = strlen(digest);
	if (len < 12)
		return;
	digest += len - 12;
	digest[0] = digest[1] = '!';
	strncpy(dest, digest, 13);
}

static iconv_t conv_desc;

static int setup_conv() {
	conv_desc = iconv_open("SHIFT_JIS", "UTF-8");
	if (conv_desc == (iconv_t) -1) {
		fprintf(stderr, "Can't convert to SHIFT_JIS.\n");
		return 0;
	}
	return 1;
}

typedef void (*trip_f)(char *, size_t, char *);

static void with_SJIS(NanUtf8String &trip, trip_f func, char *ret) {
	char *src = *trip;
	if (!src)
		return;
	size_t src_left = trip.length(), dest_left = TRIP_MAX;
	if (!src_left)
		return;
	if (src_left > TRIP_MAX / 2)
		src_left = TRIP_MAX / 2;
	char sjis[TRIP_MAX+1];
	char *dest = sjis;
	size_t result = iconv(conv_desc, &src, &src_left, &dest, &dest_left);
	if (result == (size_t) -1 && errno != EILSEQ && errno != EINVAL) {
		perror("iconv");
		return;
	}
	ssize_t len = TRIP_MAX - dest_left;
	if (len > 0) {
		sjis[len] = 0;
		func(sjis, len, ret);
	}
}

NAN_METHOD(hash_callback) {
	NanScope();
	if (args.Length() != 2)
		NanReturnValue(NanNull());

	NanUtf8String trip(args[0]),secure(args[1]);
	char digest[24];
	digest[0] = 0;
	with_SJIS(trip, &hash_trip, digest);
	with_SJIS(secure, &hash_secure, digest + strlen(digest));
	NanReturnValue(NanNew<String>(digest));
}

extern "C" void init(Handle<Object> target) {
	if (!setup_conv())
		return;
	target->Set(NanNew<String>("setSalt"),
			NanNew<FunctionTemplate>(&setup_callback)->GetFunction());
	target->Set(NanNew<String>("hash"),
		NanNew<FunctionTemplate>(&hash_callback)->GetFunction());
}

NODE_MODULE(tripcode, init)
