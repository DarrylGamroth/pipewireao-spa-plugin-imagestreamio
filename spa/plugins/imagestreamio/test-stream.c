/* SPDX-FileCopyrightText: Copyright © 2026 PipeWireAO contributors */
/* SPDX-License-Identifier: MIT */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <ImageStreamIO/ImageStreamIO.h>

#include <spa/support/plugin.h>

#include "stream.h"

static void test_element_types(void)
{
	static const enum spa_element_type types[] = {
		SPA_ELEMENT_TYPE_U8, SPA_ELEMENT_TYPE_I8,
		SPA_ELEMENT_TYPE_U16_LE, SPA_ELEMENT_TYPE_I16_LE,
		SPA_ELEMENT_TYPE_U32_LE, SPA_ELEMENT_TYPE_I32_LE,
		SPA_ELEMENT_TYPE_U64_LE, SPA_ELEMENT_TYPE_I64_LE,
		SPA_ELEMENT_TYPE_F16_LE, SPA_ELEMENT_TYPE_F32_LE, SPA_ELEMENT_TYPE_F64_LE,
		SPA_ELEMENT_TYPE_COMPLEX_F32_LE, SPA_ELEMENT_TYPE_COMPLEX_F64_LE,
	};
	const uint32_t width[] = { 2, 0 };
	const uint32_t huge[] = { INT32_MAX, INT32_MAX };
	const unsigned char written[32] = { 0x42, 0x23, 0x17 };
	struct isio_format format, opened;

	spa_assert_se(isio_format_from_element(SPA_ELEMENT_TYPE_U8, 0, width, &format) == -EINVAL);
	spa_assert_se(isio_format_from_element(SPA_ELEMENT_TYPE_U8, 2, width, &format) == -EINVAL);
	spa_assert_se(isio_format_from_element(SPA_ELEMENT_TYPE_U8, 2, huge, &format) == -EOVERFLOW);
	spa_assert_se(isio_format_from_element(SPA_ELEMENT_TYPE_F64_LE, 2, huge, &format) == -EOVERFLOW);
	spa_assert_se(isio_format_from_element(SPA_ELEMENT_TYPE_UNKNOWN, 1, width, &format) == -ENOTSUP);
	for (size_t i = 0; i < SPA_N_ELEMENTS(types); i++) {
		struct isio_stream *writer = NULL, *reader = NULL;
		unsigned char actual[sizeof(written)];
		uint64_t sequence;

		spa_assert_se(isio_format_from_element(types[i], 1, width, &format) == 0);
		spa_assert_se(format.bytes == 2 * spa_element_type_size(types[i]));
		spa_assert_se(format.bytes <= sizeof(written));
		spa_assert_se(isio_stream_create("element-type", &format, &writer) == 0);
		spa_assert_se(isio_stream_open("element-type", true, &reader, &opened) == 0);
		spa_assert_se(opened.element_type == types[i] && opened.rank == 1 &&
				opened.shape[0] == 2 && opened.bytes == format.bytes);
		spa_assert_se(isio_stream_write(writer, written, format.bytes) == 0);
		spa_assert_se(isio_stream_read(reader, actual, format.bytes, &sequence) == 0);
		spa_assert_se(sequence == 1 && memcmp(written, actual, format.bytes) == 0);
		isio_stream_close(reader);
		isio_stream_close(writer);
	}
}

int main(void)
{
	const uint32_t shape[] = { 3, 4 };
	const uint16_t expected[] = {
		1, 2, 3, 4,
		5, 6, 7, 8,
		9, 10, 11, 12,
	};
	uint16_t observed[SPA_N_ELEMENTS(expected)] = { 0 };
	struct isio_format format, opened;
	struct isio_stream *writer = NULL, *reader = NULL;
	IMAGE temporal = { 0 };
	char directory[] = "/tmp/pwao-isio-XXXXXX";
	uint32_t temporal_dimensions[] = { 4, 3, 2 };
	void *temporal_write = NULL;
	uint64_t sequence = 0;
	bool available = false;
	int semaphore;

	spa_assert_se(mkdtemp(directory) != NULL);
	spa_assert_se(setenv("MILK_SHM_DIR", directory, 1) == 0);
	test_element_types();
	spa_assert_se(isio_stream_open("", true, &reader, &opened) == -EINVAL);
	spa_assert_se(isio_stream_open("absent", true, &reader, &opened) == -ENOENT);
	spa_assert_se(isio_stream_create("", &format, &writer) == -EINVAL);
	spa_assert_se(isio_format_from_element(SPA_ELEMENT_TYPE_U16_LE, 2,
			shape, &format) == 0);
	spa_assert_se(format.bytes == sizeof(expected));
	spa_assert_se(format.stride == 4u * sizeof(uint16_t));
	spa_assert_se(isio_stream_create("roundtrip", &format, &writer) == 0);
	spa_assert_se(isio_stream_create("roundtrip", &format, &reader) ==
			-EEXIST);
	spa_assert_se(isio_stream_open("roundtrip", true, &reader, &opened) == 0);
	spa_assert_se(opened.rank == 2 && opened.shape[0] == 3 &&
			opened.shape[1] == 4 &&
			opened.element_type == SPA_ELEMENT_TYPE_U16_LE);
	struct isio_stream *second = NULL;
	spa_assert_se(isio_stream_open("roundtrip", true, &second, &opened) == 0);
	spa_assert_se(isio_stream_flush(reader) == -EINVAL);
	spa_assert_se(isio_stream_has_update(reader, true, &available) == -EINVAL);
	spa_assert_se(isio_stream_claim_semaphore(reader, INT32_MAX, &semaphore) == -ERANGE);
	spa_assert_se(isio_stream_claim_semaphore(reader, -1, &semaphore) == 0);
	spa_assert_se(semaphore >= 0);
	int other;
	spa_assert_se(isio_stream_claim_semaphore(second, semaphore, &other) == -EBUSY);
	spa_assert_se(isio_stream_claim_semaphore(reader, -1, &other) == -EINVAL);
	spa_assert_se(isio_stream_read(reader, observed, sizeof(observed) - 1, &sequence) == -EINVAL);
	spa_assert_se(isio_stream_write(writer, expected, sizeof(expected) - 1) == -EINVAL);
	isio_stream_release_semaphore(reader);
	spa_assert_se(isio_stream_claim_semaphore(second, semaphore, &other) == 0);
	spa_assert_se(semaphore == other);
	isio_stream_close(second);
	spa_assert_se(isio_stream_claim_semaphore(reader, semaphore, &other) == 0);
	spa_assert_se(isio_stream_flush(reader) == 0);
	spa_assert_se(isio_stream_has_update(reader, false, &available) == 0);
	spa_assert_se(!available);
	spa_assert_se(isio_stream_write(writer, expected, sizeof(expected)) == 0);
	spa_assert_se(isio_stream_has_update(reader, false, &available) == 0);
	spa_assert_se(available);
	spa_assert_se(isio_stream_read(reader, observed, sizeof(observed),
			&sequence) == 0);
	spa_assert_se(sequence == 1);
	spa_assert_se(memcmp(observed, expected, sizeof(expected)) == 0);
	spa_assert_se(isio_stream_has_update(reader, false, &available) == 0);
	spa_assert_se(!available);
	spa_assert_se(isio_stream_write(writer, expected, sizeof(expected)) == 0);
	spa_assert_se(isio_stream_write(writer, expected, sizeof(expected)) == 0);
	spa_assert_se(isio_stream_has_update(reader, false, &available) == 0);
	spa_assert_se(available);
	spa_assert_se(isio_stream_read(reader, observed, sizeof(observed),
			&sequence) == 0);
	spa_assert_se(sequence == 3);
	spa_assert_se(isio_stream_has_update(reader, false, &available) == 0);
	spa_assert_se(!available);
	isio_stream_close(reader);
	isio_stream_close(writer);

	spa_assert_se(ImageStreamIO_createIm_gpu(&temporal, "temporal", 3,
			temporal_dimensions, _DATATYPE_UINT16, -1, 1, 2, 0,
			CIRCULAR_BUFFER | ZAXIS_TEMPORAL, 0) == IMAGESTREAMIO_SUCCESS);
	spa_assert_se(ImageStreamIO_readBufferAt(&temporal, 1, &temporal_write) ==
			IMAGESTREAMIO_SUCCESS);
	memcpy(temporal_write, expected, sizeof(expected));
	temporal.md->cnt1 = 1;
	spa_assert_se(ImageStreamIO_UpdateIm(&temporal) == IMAGESTREAMIO_SUCCESS);
	reader = NULL;
	spa_assert_se(isio_stream_open("temporal", true, &reader, &opened) == 0);
	spa_assert_se(opened.rank == 2 && opened.shape[0] == 3 &&
			opened.shape[1] == 4 && opened.bytes == sizeof(expected));
	spa_assert_se(isio_stream_read(reader, observed, sizeof(observed),
			&sequence) == 0);
	spa_assert_se(sequence == 1 &&
			memcmp(observed, expected, sizeof(expected)) == 0);
	temporal.md->write = 1;
	spa_assert_se(isio_stream_read(reader, observed, sizeof(observed), &sequence) == -EAGAIN);
	temporal.md->write = 0;
	temporal.md->cnt1 = temporal_dimensions[2];
	spa_assert_se(isio_stream_read(reader, observed, sizeof(observed), &sequence) == -EIO);
	struct isio_stream *sink = NULL;
	spa_assert_se(isio_stream_open("temporal", false, &sink, &opened) == -ENOTSUP);
	isio_stream_close(reader);
	spa_assert_se(ImageStreamIO_destroyIm(&temporal) == IMAGESTREAMIO_SUCCESS);
	spa_assert_se(rmdir(directory) == 0);
	return 0;
}
