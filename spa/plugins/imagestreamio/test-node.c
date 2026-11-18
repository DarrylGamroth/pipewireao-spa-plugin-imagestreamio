/* SPDX-FileCopyrightText: Copyright © 2026 PipeWireAO contributors */
/* SPDX-License-Identifier: MIT */

#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ImageStreamIO/ImageStreamIO.h>

#include <spa/buffer/meta.h>
#include <spa/node/command.h>
#include <spa/node/io.h>
#include <spa/node/node.h>
#include <spa/param/ndarray-utils.h>
#include <spa/param/buffers.h>
#include <spa/pod/parser.h>
#include <spa/support/log.h>
#include <spa/support/plugin.h>
#include <spa/utils/string.h>

#include <pipewireao-plugins/imagestreamio.h>

#define HEIGHT 2u
#define WIDTH 3u
#define SAMPLE_COUNT (HEIGHT * WIDTH)

static const char schema[] = "org.pipewireao.test.image/1";

struct test_buffer {
	struct spa_buffer buffer;
	struct spa_data data;
	struct spa_chunk chunk;
	struct spa_meta meta;
	struct spa_meta_header header;
	uint16_t samples[SAMPLE_COUNT];
};

static void init_buffer(struct test_buffer *storage, bool output)
{
	memset(storage, 0, sizeof(*storage));
	storage->data.type = SPA_DATA_MemPtr;
	storage->data.fd = -1;
	storage->data.maxsize = sizeof(storage->samples);
	storage->data.data = storage->samples;
	storage->data.chunk = &storage->chunk;
	storage->buffer.n_datas = 1;
	storage->buffer.datas = &storage->data;
	if (output) {
		storage->meta.type = SPA_META_Header;
		storage->meta.size = sizeof(storage->header);
		storage->meta.data = &storage->header;
		storage->buffer.n_metas = 1;
		storage->buffer.metas = &storage->meta;
	}
}

static struct spa_pod *build_format(uint8_t *storage, size_t size, const char *selected_schema,
		uint32_t width)
{
	struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage,
			(uint32_t)size);
	const int32_t shape[] = { HEIGHT, (int32_t)width };

	return spa_pod_builder_add_object(&builder,
			SPA_TYPE_OBJECT_Format, SPA_PARAM_Format,
			SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_application),
			SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_ndarray),
			SPA_FORMAT_NDARRAY_schema, SPA_POD_String(selected_schema),
			SPA_FORMAT_NDARRAY_elementType,
			SPA_POD_Id(SPA_ELEMENT_TYPE_U16_LE),
			SPA_FORMAT_NDARRAY_shape, SPA_POD_Array(sizeof(int32_t),
					SPA_TYPE_Int, SPA_N_ELEMENTS(shape), shape),
			SPA_FORMAT_NDARRAY_layout,
			SPA_POD_Id(SPA_NDARRAY_LAYOUT_ROW_MAJOR));
}

static struct spa_handle *make_node(const struct spa_handle_factory *factory,
		const char *name, struct spa_node **node)
{
	const struct spa_dict_item items[] = {
		SPA_DICT_ITEM_INIT(SPA_KEY_API_IMAGESTREAMIO_NAME, name),
		SPA_DICT_ITEM_INIT(SPA_KEY_API_IMAGESTREAMIO_SCHEMA, schema),
	};
	const struct spa_dict info = SPA_DICT_INIT(items, SPA_N_ELEMENTS(items));
	struct spa_handle *handle = calloc(1,
			spa_handle_factory_get_size(factory, &info));

	spa_assert_se(handle != NULL);
	spa_assert_se(spa_handle_factory_init(factory, handle, &info, NULL, 0) == 0);
	spa_assert_se(spa_handle_get_interface(handle, SPA_TYPE_INTERFACE_Node,
			(void **)node) == 0);
	return handle;
}

struct port_param {
	uint32_t id;
	uint8_t storage[1024];
};

static void receive_param(void *data, int seq SPA_UNUSED, int result,
		uint32_t type, const void *param)
{
	struct port_param *result_data = data;
	const struct spa_result_node_params *value = param;

	spa_assert_se(result == 0 && type == SPA_RESULT_TYPE_NODE_PARAMS);
	spa_assert_se(SPA_POD_SIZE(value->param) <= sizeof(result_data->storage));
	result_data->id = value->id;
	memcpy(result_data->storage, value->param, SPA_POD_SIZE(value->param));
}

static void check_port_params(struct spa_node *node, enum spa_direction direction)
{
	static const struct spa_node_events events = {
		SPA_VERSION_NODE_EVENTS, .result = receive_param,
	};
	struct port_param result;
	struct spa_hook listener;
	uint32_t advertised;
	int32_t size;
	struct spa_pod *param = (struct spa_pod *)result.storage;

	spa_assert_se(spa_node_add_listener(node, &listener, &events, &result) == 0);
	spa_assert_se(spa_node_port_enum_params(node, 1, direction, 0,
			SPA_PARAM_EnumFormat, 0, 1, NULL) == 0);
	spa_assert_se(result.id == SPA_PARAM_EnumFormat);
	spa_assert_se(spa_node_port_enum_params(node, 2, direction, 0,
			SPA_PARAM_Format, 0, 1, NULL) == 0);
	spa_assert_se(result.id == SPA_PARAM_Format);
	spa_assert_se(spa_node_port_enum_params(node, 3, direction, 0,
			SPA_PARAM_Buffers, 0, 1, NULL) == 0);
	spa_assert_se(result.id == SPA_PARAM_Buffers);
	spa_assert_se(spa_pod_parse_object(param, SPA_TYPE_OBJECT_ParamBuffers, NULL,
			SPA_PARAM_BUFFERS_size, SPA_POD_Int(&size)) >= 0);
	spa_assert_se(size == SAMPLE_COUNT * sizeof(uint16_t));
	spa_assert_se(spa_node_port_enum_params(node, 4, direction, 0,
			SPA_PARAM_IO, 0, 1, NULL) == 0);
	spa_assert_se(result.id == SPA_PARAM_IO);
	spa_assert_se(spa_pod_parse_object(param, SPA_TYPE_OBJECT_ParamIO, NULL,
			SPA_PARAM_IO_id, SPA_POD_Id(&advertised)) >= 0);
	spa_assert_se(advertised == SPA_IO_Buffers);
	result.id = SPA_ID_INVALID;
	spa_assert_se(spa_node_port_enum_params(node, 5, direction, 0,
			SPA_PARAM_Meta, 0, 2, NULL) == 0);
	spa_assert_se(result.id == (direction == SPA_DIRECTION_OUTPUT ? SPA_PARAM_Meta : SPA_ID_INVALID));
	spa_assert_se(spa_node_port_enum_params(node, 6, direction, 0,
			SPA_PARAM_Props, 0, 1, NULL) == -ENOENT);
	spa_hook_remove(&listener);
}

static void configure_buffers(struct spa_node *node,
		enum spa_direction direction, struct test_buffer storage[2],
		struct spa_buffer *buffers[2], struct spa_io_buffers *io)
{
	uint8_t format_storage[1024];
	struct spa_pod *format = build_format(format_storage,
			sizeof(format_storage), schema, WIDTH);
	uint32_t i;

	static const struct spa_node_callbacks callbacks = { SPA_VERSION_NODE_CALLBACKS };
	const struct spa_command start = SPA_NODE_COMMAND_INIT(SPA_NODE_COMMAND_Start);
	const struct spa_command flush = SPA_NODE_COMMAND_INIT(SPA_NODE_COMMAND_Flush);
	uint8_t invalid_storage[1024];

	spa_assert_se(spa_node_set_callbacks(node, &callbacks, NULL) == 0);
	spa_assert_se(spa_node_enum_params(node, 0, SPA_PARAM_Props, 0, 1, NULL) == -ENOENT);
	spa_assert_se(spa_node_set_param(node, SPA_PARAM_Props, 0, NULL) == -ENOENT);
	spa_assert_se(spa_node_set_io(node, SPA_IO_Clock, NULL, 0) == -ENOENT);
	spa_assert_se(spa_node_add_port(node, direction, 1, NULL) == -ENOTSUP);
	spa_assert_se(spa_node_remove_port(node, direction, 0) == -ENOTSUP);
	spa_assert_se(spa_node_port_reuse_buffer(node, 0, 0) == -ENOTSUP);
	spa_assert_se(spa_node_send_command(node, &start) == -EIO);
	spa_assert_se(spa_node_send_command(node, &flush) == -ENOTSUP);
	spa_assert_se(spa_node_process(node) == SPA_STATUS_OK);
	spa_assert_se(spa_node_port_set_param(node, direction, 0,
			SPA_PARAM_Props, 0, NULL) == -ENOENT);
	const struct spa_pod *bad = build_format(invalid_storage,
			sizeof(invalid_storage), "wrong-schema", WIDTH);
	spa_assert_se(spa_node_port_set_param(node, direction, 0,
			SPA_PARAM_Format, 0, bad) == -EINVAL);
	if (direction == SPA_DIRECTION_OUTPUT) {
		bad = build_format(invalid_storage, sizeof(invalid_storage), schema, WIDTH + 1);
		spa_assert_se(spa_node_port_set_param(node, direction, 0,
				SPA_PARAM_Format, 0, bad) == -EINVAL);
	}
	spa_assert_se(format != NULL);
	spa_assert_se(spa_node_port_set_param(node, direction, 0,
			SPA_PARAM_Format, 0, format) == 0);
	for (i = 0; i < 2; i++) {
		init_buffer(&storage[i], direction == SPA_DIRECTION_OUTPUT);
		buffers[i] = &storage[i].buffer;
	}
	check_port_params(node, direction);
	spa_assert_se(spa_node_port_use_buffers(node, direction, 0, 0, buffers, 1) == -EINVAL);
	storage[0].data.maxsize--;
	spa_assert_se(spa_node_port_use_buffers(node, direction, 0, 0, buffers, 2) == -EINVAL);
	storage[0].data.maxsize++;
	spa_assert_se(spa_node_port_set_io(node, direction, 0, SPA_IO_Clock,
			io, sizeof(*io)) == -ENOENT);
	spa_assert_se(spa_node_port_set_io(node, direction, 0, SPA_IO_Buffers,
			io, sizeof(*io) - 1) == -ENOSPC);
	spa_assert_se(spa_node_port_use_buffers(node, direction, 0, 0,
			buffers, 2) == 0);
	spa_assert_se(spa_node_port_set_io(node, direction, 0, SPA_IO_Buffers,
			io, sizeof(*io)) == 0);
}

static void stop_and_clear(struct spa_handle *handle, struct spa_node *node,
		enum spa_direction direction, struct spa_io_buffers *io)
{
	struct spa_command pause = SPA_NODE_COMMAND_INIT(SPA_NODE_COMMAND_Pause);

	spa_assert_se(spa_node_send_command(node, &pause) == 0);
	spa_assert_se(spa_node_port_set_io(node, direction, 0, SPA_IO_Buffers,
			NULL, 0) == 0);
	spa_assert_se(spa_node_port_use_buffers(node, direction, 0, 0,
			NULL, 0) == 0);
	spa_assert_se(spa_node_port_set_param(node, direction, 0, SPA_PARAM_Format, 0, NULL) == 0);
	spa_assert_se(spa_handle_clear(handle) == 0);
	free(handle);
	(void)io;
}

static void test_sink(const struct spa_handle_factory *factory)
{
	const uint16_t expected[SAMPLE_COUNT] = { 11, 12, 13, 21, 22, 23 };
	struct test_buffer storage[2];
	struct spa_buffer *buffers[2];
	struct spa_io_buffers io = SPA_IO_BUFFERS_INIT;
	struct spa_command start = SPA_NODE_COMMAND_INIT(SPA_NODE_COMMAND_Start);
	struct spa_handle *handle;
	struct spa_node *node = NULL;
	IMAGE image = { 0 };

	handle = make_node(factory, "pwao_sink_test", &node);
	configure_buffers(node, SPA_DIRECTION_INPUT, storage, buffers, &io);
	spa_assert_se(spa_node_send_command(node, &start) == 0);
	spa_assert_se(spa_node_send_command(node, &start) == 0);
	spa_assert_se(spa_node_port_set_param(node, SPA_DIRECTION_INPUT, 0,
			SPA_PARAM_Format, 0, NULL) == -EBUSY);
	spa_assert_se(spa_node_port_use_buffers(node, SPA_DIRECTION_INPUT, 0, 0,
			NULL, 0) == -EBUSY);
	spa_assert_se(spa_node_process(node) == SPA_STATUS_NEED_DATA);
	io.status = SPA_STATUS_HAVE_DATA;
	io.buffer_id = 2;
	spa_assert_se(spa_node_process(node) == -EPROTO);
	io.buffer_id = 0;
	storage[0].chunk.flags = SPA_CHUNK_FLAG_CORRUPTED;
	spa_assert_se(spa_node_process(node) == -EINVAL);
	storage[0].chunk.flags = 0;
	spa_assert_se(spa_node_process(node) == -EINVAL);
	memcpy(storage[0].samples, expected, sizeof(expected));
	storage[0].chunk.size = sizeof(expected);
	storage[0].chunk.stride = WIDTH * sizeof(uint16_t);
	io.buffer_id = 0;
	io.status = SPA_STATUS_HAVE_DATA;
	spa_assert_se(spa_node_process(node) == SPA_STATUS_NEED_DATA);
	spa_assert_se(io.status == SPA_STATUS_NEED_DATA);
	spa_assert_se(ImageStreamIO_openIm(&image, "pwao_sink_test") ==
			IMAGESTREAMIO_SUCCESS);
	spa_assert_se(image.md->naxis == 2 && image.md->size[0] == WIDTH &&
			image.md->size[1] == HEIGHT &&
			image.md->datatype == _DATATYPE_UINT16 && image.md->cnt0 == 1);
	spa_assert_se(memcmp(image.array.raw, expected, sizeof(expected)) == 0);
	spa_assert_se(ImageStreamIO_closeIm(&image) == IMAGESTREAMIO_SUCCESS);
	stop_and_clear(handle, node, SPA_DIRECTION_INPUT, &io);
	spa_assert_se(ImageStreamIO_openIm(&image, "pwao_sink_test") !=
			IMAGESTREAMIO_SUCCESS);
}

static void test_source(const struct spa_handle_factory *factory)
{
	const uint16_t initial[SAMPLE_COUNT] = { 1, 2, 3, 4, 5, 6 };
	const uint16_t updated[SAMPLE_COUNT] = { 7, 8, 9, 10, 11, 12 };
	uint32_t dimensions[] = { WIDTH, HEIGHT };
	struct test_buffer storage[2];
	struct spa_buffer *buffers[2];
	struct spa_io_buffers io = SPA_IO_BUFFERS_INIT;
	struct spa_command start = SPA_NODE_COMMAND_INIT(SPA_NODE_COMMAND_Start);
	struct spa_handle *handle;
	struct spa_node *node = NULL;
	IMAGE image = { 0 };
	uint32_t first_id;

	spa_assert_se(ImageStreamIO_createIm(&image, "pwao_source_test", 2,
			dimensions, _DATATYPE_UINT16, 1, 0, 0) == IMAGESTREAMIO_SUCCESS);
	image.md->write = 1;
	memcpy(image.array.raw, initial, sizeof(initial));
	spa_assert_se(ImageStreamIO_UpdateIm(&image) == IMAGESTREAMIO_SUCCESS);
	handle = make_node(factory, "pwao_source_test", &node);
	configure_buffers(node, SPA_DIRECTION_OUTPUT, storage, buffers, &io);
	spa_assert_se(spa_node_send_command(node, &start) == 0);
	spa_assert_se(spa_node_process(node) == SPA_STATUS_HAVE_DATA);
	spa_assert_se(io.status == SPA_STATUS_HAVE_DATA && io.buffer_id < 2);
	first_id = io.buffer_id;
	spa_assert_se(memcmp(storage[first_id].samples, initial, sizeof(initial)) == 0);
	spa_assert_se(storage[first_id].header.seq == 1);
	spa_assert_se((storage[first_id].header.flags &
			SPA_META_HEADER_FLAG_DISCONT) != 0);
	spa_assert_se(spa_node_process(node) == SPA_STATUS_HAVE_DATA);
	spa_assert_se(io.buffer_id == first_id);
	io.status = SPA_STATUS_NEED_DATA;
	io.buffer_id = 2;
	spa_assert_se(spa_node_process(node) == -EPROTO);
	io.buffer_id = first_id;
	io.status = SPA_STATUS_NEED_DATA;
	spa_assert_se(spa_node_process(node) == SPA_STATUS_OK);
	image.md->write = 1;
	memcpy(image.array.raw, updated, sizeof(updated));
	spa_assert_se(ImageStreamIO_UpdateIm(&image) == IMAGESTREAMIO_SUCCESS);
	spa_assert_se(spa_node_process(node) == SPA_STATUS_HAVE_DATA);
	spa_assert_se(io.buffer_id < 2);
	spa_assert_se(memcmp(storage[io.buffer_id].samples, updated,
			sizeof(updated)) == 0);
	spa_assert_se(storage[io.buffer_id].header.seq == 2);
	stop_and_clear(handle, node, SPA_DIRECTION_OUTPUT, &io);
	spa_assert_se(ImageStreamIO_destroyIm(&image) == IMAGESTREAMIO_SUCCESS);
}

int main(int argc, char **argv)
{
	spa_handle_factory_enum_func_t enumerate;
	const struct spa_handle_factory *source = NULL, *sink = NULL;
	char directory[] = "/tmp/pwao-isio-node-XXXXXX";
	uint32_t index = 0;
	void *library, *symbol;

	spa_assert_se(argc == 2);
	spa_assert_se(mkdtemp(directory) != NULL);
	spa_assert_se(setenv("MILK_SHM_DIR", directory, 1) == 0);
	library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	spa_assert_se(library != NULL);
	symbol = dlsym(library, SPA_HANDLE_FACTORY_ENUM_FUNC_NAME);
	spa_assert_se(symbol != NULL && sizeof(enumerate) == sizeof(symbol));
	memcpy(&enumerate, &symbol, sizeof(enumerate));
	spa_assert_se(enumerate(&source, &index) == 1 && source != NULL &&
			spa_streq(source->name, SPA_NAME_API_IMAGESTREAMIO_SOURCE));
	spa_assert_se(enumerate(&sink, &index) == 1 && sink != NULL &&
			spa_streq(sink->name, SPA_NAME_API_IMAGESTREAMIO_SINK));
	test_sink(sink);
	test_source(source);
	spa_assert_se(dlclose(library) == 0);
	spa_assert_se(rmdir(directory) == 0);
	return 0;
}
