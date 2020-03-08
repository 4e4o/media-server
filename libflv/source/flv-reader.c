#include "flv-reader.h"
#include "flv-header.h"
#include "flv-proto.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#define FLV_HEADER_SIZE		9 // DataOffset included
#define FLV_TAG_HEADER_SIZE	11 // StreamID included

struct flv_reader_t
{
	FILE* fp;
	int (*read)(void* param, void* buf, int len);
	void* param;
};

static uint32_t be_read_uint32(const uint8_t* ptr)
{
	return (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
}

static int flv_read_header(struct flv_reader_t* flv)
{
	uint8_t data[FLV_HEADER_SIZE];
	struct flv_header_t h;
	int n;

	if (FLV_HEADER_SIZE != flv->read(flv->param, data, FLV_HEADER_SIZE))
		return -1;

	if(FLV_HEADER_SIZE != flv_header_read(&h, data, FLV_HEADER_SIZE))
		return -1;

	assert(h.offset >= FLV_HEADER_SIZE);
	for(n = (int)(h.offset - FLV_HEADER_SIZE); n > 0; n -= sizeof(data))
		flv->read(flv->param, data, n >= sizeof(data) ? sizeof(data) : n); // skip

	// PreviousTagSize0
	if (4 != flv->read(flv->param, data, 4))
		return -1;

	assert(be_read_uint32(data) == 0);
	return be_read_uint32(data) == 0 ? 0 : -1;
}

static int file_read(void* param, void* buf, int len)
{
	return (int)fread(buf, 1, len, (FILE*)param);
}

void* flv_reader_create(const char* file)
{
	FILE* fp;
	struct flv_reader_t* flv;
	fp = fopen(file, "rb");
	if (!fp)
		return NULL;

	flv = flv_reader_create2(file_read, fp);
	if (!flv)
	{
		fclose(fp);
		return NULL;
	}

	flv->fp = fp;
	return flv;
}

void* flv_reader_create2(int (*read)(void* param, void* buf, int len), void* param)
{
	struct flv_reader_t* flv;
	flv = (struct flv_reader_t*)calloc(1, sizeof(*flv));
	if (!flv)
		return NULL;
	
	flv->read = read;
	flv->param = param;
	if (0 != flv_read_header(flv))
	{
		flv_reader_destroy(flv);
		return NULL;
	}

	return flv;
}

void flv_reader_destroy(void* p)
{
	struct flv_reader_t* flv;
	flv = (struct flv_reader_t*)p;
	if (NULL != flv)
	{
		if (flv->fp)
			fclose(flv->fp);
		free(flv);
	}
}

int flv_reader_read(void* p, int* tagtype, uint32_t* timestamp, void* buffer, size_t bytes)
{
	uint8_t header[FLV_TAG_HEADER_SIZE];
	struct flv_tag_header_t tag;
	struct flv_reader_t* flv;
	flv = (struct flv_reader_t*)p;

	if (FLV_TAG_HEADER_SIZE != flv->read(flv->param, &header, FLV_TAG_HEADER_SIZE))
		return -1; // read file error

	if (FLV_TAG_HEADER_SIZE != flv_tag_header_read(&tag, header, FLV_TAG_HEADER_SIZE))
		return -1;

	if (bytes < tag.size)
		return tag.size;

	// FLV stream
	if(tag.size != (uint32_t)flv->read(flv->param, buffer, tag.size))
		return -1;

	// PreviousTagSizeN
	if (4 != flv->read(flv->param, header, 4))
		return -1;

	*tagtype = tag.type;
	*timestamp = tag.timestamp;
	assert(0 == tag.streamId); // StreamID Always 0
	assert(be_read_uint32(header) == tag.size + FLV_TAG_HEADER_SIZE);
	return (be_read_uint32(header) == tag.size + FLV_TAG_HEADER_SIZE) ? tag.size : -1;
}
