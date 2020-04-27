/*
 *	Copyright (C) 2006-2009 Vincent Hanquez <tab@snarc.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * SHA512 implementation
 */

#define _GNU_SOURCE
#ifdef _MSC_VER
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#define alloca _alloca
#else
#include <unistd.h>
#endif
#include <fcntl.h>
#include <string.h>
#include "sha512.h"

#define BLKSIZE 4096

static inline int sha512_file(char *filename, sha512_digest *digest)
{
	unsigned char buf[BLKSIZE];
	int fd; ssize_t n;
	struct sha512_ctx ctx;

#ifndef O_CLOEXEC
	fd = open(filename, O_RDONLY);
#else
	fd = open(filename, O_RDONLY | O_CLOEXEC);
#endif
	if (fd == -1)
		return 1;
	sha512_init(&ctx);
	while ((n = read(fd, buf, BLKSIZE)) > 0)
		sha512_update(&ctx, buf, n);
	if (n == 0)
		sha512_finalize(&ctx, digest);
	close(fd);
	return n < 0;
}

/* this part implement the OCaml binding */
#include <caml/mlvalues.h>
#include <caml/memory.h>
#include <caml/alloc.h>
#include <caml/custom.h>
#include <caml/fail.h>
#include <caml/bigarray.h>
#include <caml/threads.h>

#define GET_CTX_STRUCT(a) ((struct sha512_ctx *) a)

CAMLexport value stub_sha512_init(value unit)
{
	CAMLparam1(unit);
	CAMLlocal1(result);

	result = caml_alloc(sizeof(struct sha512_ctx), Abstract_tag);
	sha512_init(GET_CTX_STRUCT(result));

	CAMLreturn(result);
}

CAMLprim value stub_sha512_update(value ctx, value data, value ofs, value len)
{
	CAMLparam4(ctx, data, ofs, len);

	sha512_update(GET_CTX_STRUCT(ctx), (unsigned char *) data
	                                   + Long_val(ofs), Long_val(len));
	CAMLreturn(Val_unit);
}

CAMLprim value stub_sha512_update_bigarray(value ctx, value buf, value pos, value len)
{
	CAMLparam4(ctx, buf, pos, len);
	struct sha512_ctx ctx_dup;
	unsigned char *data = Data_bigarray_val(buf);

	ctx_dup = *GET_CTX_STRUCT(ctx);
	caml_release_runtime_system();
	sha512_update(&ctx_dup,
		data + Long_val(pos),
		Long_val(len));
	caml_acquire_runtime_system();
	*GET_CTX_STRUCT(ctx) = ctx_dup;

	CAMLreturn(Val_unit);
}

CAMLprim value stub_sha512_update_fd(value ctx, value fd, value len)
{
	CAMLparam3(ctx, fd, len);

	unsigned char buf[BLKSIZE];

	struct sha512_ctx ctx_dup = *GET_CTX_STRUCT(ctx);

	intnat ret, rest = Long_val(len);

	caml_release_runtime_system();
	do {
	    ret = rest < sizeof(buf) ? rest : sizeof(buf);
	    ret = read(Long_val(fd), buf, ret);
	    if (ret <= 0) break;
	    rest -= ret;
	    sha512_update(&ctx_dup, buf, ret);
	} while (ret > 0 && rest > 0);
	caml_acquire_runtime_system();

	if (ret < 0)
	    caml_failwith("read error");

	*GET_CTX_STRUCT(ctx) = ctx_dup;
	CAMLreturn(Val_long(Long_val(len) - rest));
}

CAMLprim value stub_sha512_file(value name)
{
	CAMLparam1(name);
	CAMLlocal1(result);

	sha512_digest digest;
	const int len = caml_string_length(name);
	char *name_dup = alloca(len);
	memcpy(name_dup, String_val(name), len);
	name_dup[len] = '\0';

	caml_release_runtime_system();
	if (sha512_file(name_dup, &digest)) {
	    caml_acquire_runtime_system();
	    caml_failwith("file error");
	}
	caml_acquire_runtime_system();
	result = caml_alloc_string(sizeof(digest));
	memcpy(Bytes_val(result), &digest, sizeof(digest));

	CAMLreturn(result);
}

CAMLprim value stub_sha512_finalize(value ctx)
{
	CAMLparam1(ctx);
	CAMLlocal1(result);

	result = caml_alloc_string(64);
	sha512_finalize(GET_CTX_STRUCT(ctx), (sha512_digest *) result);

	CAMLreturn(result);
}
