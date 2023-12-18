#include "return_codes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct
{
	int code;
	char *message;
} ERROR;

typedef struct
{
	unsigned int length;
	unsigned char type[4];
	unsigned char data[];
} chunk;

typedef struct
{
	int width;
	int height;
	int d;
	int color_type;
} inf;

int isIHDR(const unsigned char chunk[4])
{
	if (strncmp(chunk, "IHDR", 4) == 0)
	{
		return 1;
	}
	return 0;
}

int isIDAT(const unsigned char chunk[4])
{
	if (strncmp(chunk, "IDAT", 4) == 0)
	{
		return 1;
	}
	return 0;
}

int isPLTE(const unsigned char chunk[4])
{
	if (strncmp(chunk, "PLTE", 4) == 0)
	{
		return 1;
	}
	return 0;
}

int isIEND(const unsigned char chunk[4])
{
	if (strncmp(chunk, "IEND", 4) == 0)
	{
		return 1;
	}
	return 0;
}

unsigned char paeth_algorithm(int x, int y, int z)
{
	int p = x + y - z;
	int px = abs(p - x);
	int py = abs(p - y);
	int pz = abs(p - z);
	unsigned char result = (px <= py && px <= pz) ? x : ((py <= pz) ? y : z);
	return result;
}

void write_pixels(unsigned char *pixels, FILE *out, inf *inf, chunk *ihdr_chunk, int format)
{
	fprintf(out, "P%d\n", format);

	int max_color_value = (1 << ihdr_chunk->data[8]) - 1;
	fprintf(out, "%d %d\n%d\n", inf->width, inf->height, max_color_value);

	int height = inf->height;
	int width = inf->width;
	int d = inf->d;

	int w_temp = d * width + 1;
	for (int i = 0; i < height; ++i)
	{
		unsigned char filt = pixels[i * w_temp];
		for (int j = 1; j < w_temp; j++)
		{
			switch (filt)
			{
			case 1:
				if (j > d)
				{
					pixels[w_temp * i + j] += pixels[w_temp * i + j - d];
				}
				break;
			case 2:
				if (i > 0)
				{
					pixels[w_temp * i + j] += pixels[w_temp * (i - 1) + j];
				}
				break;
			case 3:
				if (i > 0)
				{
					if (j > d)
					{
						pixels[w_temp * i + j] += ((int)pixels[w_temp * (i - 1) + j] + (int)pixels[w_temp * i + j - d]) / 2;
					}
					else
					{
						pixels[w_temp * i + j] += ((int)pixels[w_temp * (i - 1) + j]) / 2;
					}
				}
				else if (j > d)
				{
					pixels[w_temp * i + j] += pixels[w_temp * i + j - d];
				}
				break;
			case 4:
				if (i > 0)
				{
					if (j > d)
					{
						pixels[w_temp * i + j] +=
							paeth_algorithm(pixels[w_temp * i + j - d], pixels[w_temp * (i - 1) + j], pixels[w_temp * (i - 1) + j - d]);
					}
					else
					{
						pixels[w_temp * i + j] += pixels[w_temp * (i - 1) + j];
					}
				}
				else if (j > d)
				{
					pixels[w_temp * i + j] += pixels[w_temp * i + j - d];
				}
				else
				{
					pixels[w_temp * i + j] += 0;
				}
				break;
			default:
				break;
			}
		}
	}
}

int read_png_signature(FILE *fp, ERROR *error)
{
	unsigned char signature[8];
	if (fread(signature, sizeof(unsigned char), 8, fp) != 8)
	{
		error->code = ERROR_DATA_INVALID;
		error->message = "Not a valid PNG file.";
		return error->code;
	}

	const unsigned char expectedSignature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
	for (int i = 0; i < 8; i++)
	{
		if (signature[i] != expectedSignature[i])
		{
			error->code = ERROR_DATA_INVALID;
			error->message = "Not a valid PNG file.";
			return error->code;
		}
	}

	return 0;
}

unsigned int s_int32(unsigned int x)
{
	x = ((x << 8) & 0xFF00FF00) | ((x >> 8) & 0xFF00FF);
	x = (x << 16) | (x >> 16);
	return x;
}

chunk *readChunk(FILE *in)
{
	unsigned int x;
	if (fread(&x, sizeof(x), 1, in) < 1)
	{
		return NULL;
	}
	x = s_int32(x);
	if (x < 0)
	{
		return NULL;
	}
	chunk *ch = malloc(sizeof(*ch) + sizeof(unsigned char) * x);
	if (ch == NULL)
	{
		return NULL;
	}
	ch->length = x;
	if (fread(&(ch->type), sizeof(unsigned char), 4, in) < 4)
	{
		return NULL;
	}
	if (fread(&(ch->data), sizeof(unsigned char), ch->length, in) < ch->length)
	{
		return NULL;
	}
	fseek(in, 4, SEEK_CUR);
	return ch;
}

int main(int argc, char *argv[])
{
	ERROR error = { SUCCESS, "" };
	inf inf;
	FILE *in = fopen(argv[1], "rb");
	if (!in)
	{
		error.code = ERROR_CANNOT_OPEN_FILE;
		error.message = "Input file not found";
		goto returning;
	}
	if (argc < 3)
	{
		error.code = ERROR_PARAMETER_INVALID;
		error.message = "no input/output file specified";
		goto returning;
	}
	int format;
	size_t n = 0;
	size_t len = 1024;
	unsigned char *r = NULL, *g = NULL, *b = NULL;
	unsigned char *p_data = malloc(len);
	if (read_png_signature(in, &error) != 0)
	{
		free(p_data);
		goto returning;
	}
	chunk *ihdr = readChunk(in);
	if (ihdr == NULL)
	{
		error.code = ERROR_DATA_INVALID;
		error.message = "Invalid chunk";
		goto returning;
	}
	if (isIHDR(ihdr->type) == 0)
	{
		free(p_data);
		free(ihdr);
		error.code = ERROR_DATA_INVALID;
		error.message = "Invalid IHDR chunk";
		goto returning;
	}
	inf.color_type = ihdr->data[9];
	format = (inf.color_type == 0) ? 5 : 6;
	inf.d = (inf.color_type == 2) ? 3 : 1;
	inf.width = (ihdr->data[0] << 24) | (ihdr->data[1] << 16) | (ihdr->data[2] << 8) | ihdr->data[3];
	inf.height = (ihdr->data[4] << 24) | (ihdr->data[5] << 16) | (ihdr->data[6] << 8) | ihdr->data[7];
	if (inf.color_type != 0 && inf.color_type != 2 && inf.color_type != 3)
	{
		free(p_data);
		free(ihdr);
		error.code = ERROR_DATA_INVALID;
		error.message = "Color type not supported";
		goto returning;
	}
	if (p_data == NULL)
	{
		error.code = ERROR_OUT_OF_MEMORY;
		error.message = "Out of memory";
		goto returning;
	}

	int plteSecond = 0;
	while (1)
	{
		chunk *curr_chunk = readChunk(in);
		if (curr_chunk == NULL)
		{
			free(p_data);
			free(r);
			free(g);
			free(b);
			free(curr_chunk);
			error.code = ERROR_DATA_INVALID;
			error.message = "Invalid chunk";
			return error.code;
		}
		else if (isIDAT(curr_chunk->type) == 1)
		{
			if (curr_chunk->length > 0)
			{
				size_t t = len;
				while (curr_chunk->length + n >= len)
				{
					len *= 2;
				}
				if (len != t)
				{
					unsigned char *datat = realloc(p_data, sizeof(unsigned char) * len);
					if (datat)
					{
						p_data = datat;
					}
					else
					{
						error.code = ERROR_OUT_OF_MEMORY;
						error.message = "Out of memory";
						free(curr_chunk);
						free(p_data);
						free(datat);
						free(r);
						free(g);
						free(b);
						goto returning;
					}
				}
				memcpy(p_data + n, curr_chunk->data, curr_chunk->length * sizeof(unsigned char));
				n += curr_chunk->length;
			}
		}
		else if (isPLTE(curr_chunk->type) == 1)
		{
			if (inf.color_type == 0)
			{
				error.code = ERROR_DATA_INVALID;
				error.message = "Invalid png file";
				free(curr_chunk);
				free(p_data);
				free(ihdr);
				goto returning;
			}
			if (plteSecond == 1)
			{
				error.code = ERROR_DATA_INVALID;
				error.message = "Invalid data";
				free(curr_chunk);
				free(p_data);
				free(ihdr);
				goto returning;
			}
			plteSecond = 1;
			int counter = 0;
			unsigned int number_of_colors = curr_chunk->length / 3;
			r = malloc(number_of_colors);
			g = malloc(number_of_colors);
			b = malloc(number_of_colors);
			if (r == NULL || g == NULL || b == NULL)
			{
				error.code = ERROR_OUT_OF_MEMORY;
				error.message = "Out of memory";
				free(curr_chunk);
				free(p_data);
				free(ihdr);
				free(r);
				free(g);
				free(b);
				goto returning;
			}
			if (curr_chunk->length % 3 != 0)
			{
				error.code = ERROR_DATA_INVALID;
				error.message = "Invalid PLTE data";
				free(curr_chunk);
				free(p_data);
				free(ihdr);
				free(r);
				free(g);
				free(b);
				goto returning;
			}
			for (int i = 0; i < number_of_colors; i++)
			{
				r[i] = curr_chunk->data[i * 3];
				g[i] = curr_chunk->data[i * 3 + 1];
				b[i] = curr_chunk->data[i * 3 + 2];
				if (r[i] == g[i] && g[i] == b[i])
				{
					counter++;
				}
			}
			format = 6;
			if (counter == number_of_colors)
			{
				format = 5;
			}
		}
		else if (isIEND(curr_chunk->type) == 1)
		{
			if (readChunk(in) != NULL)
			{
				error.code = ERROR_DATA_INVALID;
				error.message = "Extra chunks after IEND";
			}
			size_t decompressSize = inf.height * inf.width * inf.d + inf.height;
			unsigned char *decompressed_data = malloc(decompressSize);
			if (decompressed_data == NULL)
			{
				free(curr_chunk);
				free(p_data);
				free(ihdr);
				free(r);
				free(g);
				free(b);
				error.code = ERROR_OUT_OF_MEMORY;
				error.message = "Out of memory";
				return error.code;
			}

			int result = uncompress(decompressed_data, &decompressSize, p_data, n);
			if (result != Z_OK)
			{
				free(curr_chunk);
				free(p_data);
				free(decompressed_data);
				free(ihdr);
				free(r);
				free(g);
				free(b);
				if (result == Z_MEM_ERROR)
				{
					error.code = ERROR_OUT_OF_MEMORY;
					error.message = "Out of memory";
				}
				else if (result == Z_BUF_ERROR)
				{
					error.code = ERROR_DATA_INVALID;
					error.message = "insufficient buffer size";
				}
				else if (result == Z_DATA_ERROR)
				{
					error.code = ERROR_DATA_INVALID;
					error.message = "Invalid data";
				}
				else
				{
					error.code = ERROR_UNKNOWN;
					error.message = "unknown error in uncompressing";
				}
				goto returning;
			}


			if (n == 0 || curr_chunk->length != 0)
			{
				free(p_data);
				free(curr_chunk);
				free(decompressed_data);
				free(ihdr);
				free(r);
				free(g);
				free(b);
				error.code = ERROR_DATA_INVALID;
				error.message = "Invalid data";
				goto returning;
			}
			FILE *out = fopen(argv[2], "wb");
			if (!out)
			{
				free(curr_chunk);
				free(p_data);
				free(decompressed_data);
				free(ihdr);
				free(r);
				free(g);
				free(b);
				error.code = ERROR_CANNOT_OPEN_FILE;
				error.message = "Output file not found";
				goto returning;
			}
			write_pixels(decompressed_data, out, &inf, ihdr, format);
			if (plteSecond == 1)
			{
				if (format == 5)
				{
					for (int i = 0; i < decompressSize; ++i)
					{
						if (i % (inf.width * inf.d + 1))
						{
							decompressed_data[i] = r[decompressed_data[i]];
						}
					}
				}
				else
				{
					unsigned char *tmp = malloc((inf.height * inf.width * 3 + inf.height) * sizeof(unsigned char));
					for (int i = 0; i < inf.height; i++)
					{
						tmp[i * (inf.width * 3 + 1)] = decompressed_data[i * (inf.width + 1)];
					}
					for (int i = 0; i < inf.width * inf.height * 3 + inf.height; i++)
					{
						if (i % (inf.width * 3 + 1))
						{
							tmp[i] = r[decompressed_data[(i - 1 - i / (inf.width * 3 + 1)) / 3 + 1 + i / (inf.width * 3 + 1)]];
							tmp[i + 1] = g[decompressed_data[(i - 1 - i / (inf.width * 3 + 1)) / 3 + 1 + i / (inf.width * 3 + 1)]];
							tmp[i + 2] = b[decompressed_data[(i - 1 - i / (inf.width * 3 + 1)) / 3 + 1 + i / (inf.width * 3 + 1)]];
							i += 2;
						}
					}
					free(decompressed_data);
					decompressed_data = tmp;
					free(tmp);
					inf.d = 3;
				}
			}
			unsigned char *temp = malloc((inf.height * inf.width * inf.d) * sizeof(unsigned char));
			if (temp == NULL)
			{
				free(ihdr);
				fclose(out);
				free(curr_chunk);
				free(p_data);
				free(r);
				free(g);
				free(b);
				error.code = ERROR_OUT_OF_MEMORY;
				error.message = "Out of memory";
				goto returning;
			}
			int ind = 0;
			for (int i = 0; i < inf.height * (inf.width * inf.d + 1); i++)
			{
				if (i % (inf.width * inf.d + 1))
				{
					temp[ind++] = decompressed_data[i];
				}
			}
			fwrite(temp, sizeof(unsigned char), inf.height * inf.width * inf.d, out);
			if (plteSecond == 0 || (plteSecond == 1 && format == 5))
			{
				free(decompressed_data);
			}
			free(ihdr);
			fclose(out);
			free(curr_chunk);
			free(temp);
			free(p_data);
			free(r);
			free(g);
			free(b);
			goto returning;
		}
		free(curr_chunk);
	}
returning:
	fclose(in);
	if (error.code != SUCCESS)
	{
		fprintf(stderr, "%s\n", error.message);
	}
	return error.code;
}









//#define ISAL

//#if defined(ZLIB)

//#elif defined(LIBDEFLATE)
//#include <libdeflate.h>
//#elif defined(ISAL)
//#include <include/igzip_lib.h>
//#else
//#error ("Unsupported libary")
//#endif

//#if defined(ZLIB)

//#elif defined(LIBDEFLATE)
//			struct libdeflate_decompressor *decomp = libdeflate_alloc_decompressor();
//			size_t x;
//			if (libdeflate_zlib_decompress(decomp, p_data, n, decompressed_data, decompressSize, &x) != LIBDEFLATE_SUCCESS)
//			{
//				free(curr_chunk);
//				free(p_data);
//				free(decompressed_data);
//				free(ihdr);
//				free(r);
//				free(g);
//				free(b);
//				error.code = ERROR_DATA_INVALID;
//				error.message = "Invalid data";
//				goto returning;
//			}
//#elif defined(ISAL)
//			struct inflate_state libUncomp;
//			isal_inflate_init(&libUncomp);
//			libUncomp.next_in = p_data;
//			libUncomp.avail_in = n;
//			libUncomp.next_out = decompressed_data;
//			libUncomp.avail_out = decompressSize;
//			libUncomp.crc_flag = IGZIP_ZLIB;
//			if (isal_inflate(&libUncomp) != COMP_OK)
//			{
//				free(curr_chunk);
//				free(p_data);
//				free(decompressed_data);
//				free(ihdr);
//				free(r);
//				free(g);
//				free(b);
//				error.code = ERROR_DATA_INVALID;
//				error.message = "Invalid data";
//				goto returning;
//			}
//#endif