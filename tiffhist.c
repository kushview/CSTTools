/* $Id: tiffhist.c,v 1.5 2007/02/22 11:36:43 rip Exp $ */

/*
 * A program to produce a histogram from a tiff file.
 *
 * CST (2007) (contact roneil@cst.fr)
 *
 * Usage:
 * tiffhist input
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <tiffconf.h>
#include <tiffio.h>

#define	B_DEPTH		16		/* # bits/pixel to use */
#define	B_LEN		(1L<<B_DEPTH)

uint32	hist_red[B_LEN];
uint32	hist_green[B_LEN];
uint32	hist_blue[B_LEN];

static  int  readline(TIFF *, uint16 *, int);
static	void get_histogram(TIFF*, int);
static	void usage(void);

/****************************************************************************************************/
int main(int argc, char* argv[])
{
	TIFF	*in;
	uint i, div;
	uint32 b_len;
	int c;
	uint16	bitspersample = 1;
	uint16	samplesperpixel;

	while ((c = getopt(argc, argv, "")) != -1)
		switch (c) 
		{
		case '?':
			usage();
			/*NOTREACHED*/
		}
		
	if (argc - optind < 1)
		usage();
	
	/* open the input image */
	in = TIFFOpen(argv[optind], "r");
	if (in == NULL)
		return (-1);

	TIFFGetField(in, TIFFTAG_BITSPERSAMPLE, &bitspersample);
	if (bitspersample != 8 && bitspersample != 16) 
	{
		fprintf(stderr, "%s: Image must have at least 8-bits/sample\n", argv[optind]);
		return (-3);
	}
	
	TIFFGetField(in, TIFFTAG_SAMPLESPERPIXEL, &samplesperpixel);
	if (samplesperpixel != 3)
	{
		fprintf(stderr, "%s: Image must have 3 samples/image\n", argv[optind]);
		return (-3);
	}
	
	b_len = 1L<<bitspersample;
	
	/* compute the histogram */
	get_histogram(in, b_len);
	
	/* and print the values out */
	div = (bitspersample == 8)? 1.0 : 16.0;
	for (i = 0; i < b_len; i++)
		printf("%f %d %d %d\n", (float)i/div, hist_red[i], hist_green[i], hist_blue[i]);
		
	(void) TIFFClose(in);
	return (0);
}

/****************************************************************************************************/
/* readline converts an 8 bit line to 16 bits otherwise returns after reading line.					*/
/****************************************************************************************************/
static int readline(TIFF *in, uint16 *line, int which)
{
	uint16 bps, i;
	
	TIFFGetField(in, TIFFTAG_BITSPERSAMPLE, &bps);

	if (bps == 16)
	{
		if (TIFFReadScanline(in, line, which, 0) <= 0)
			return (-1);
	}
	else
	{
		uint8 *sline, *sp;
		uint16 *ptr;
		uint32 width;
		
		TIFFGetField(in, TIFFTAG_IMAGEWIDTH, &width);
		sline 	= (uint8 *) _TIFFmalloc(TIFFScanlineSize(in));
		if (TIFFReadScanline(in, sline, which, 0) <= 0)
			return (-1);
		sp = sline;
		ptr= line;
		for (i = 0; i < width; i++)
		{
			*ptr++ = *sp++ ;
			*ptr++ = *sp++ ;
			*ptr++ = *sp++ ;
		}
		_TIFFfree(sline);
		 
	}
	return (1);
}
/****************************************************************************************************/
static void get_histogram(TIFF* in, int b_len)
{
	uint16 red, green, blue;
	uint16 *inputline, *inptr;
	uint32 j, i;
	uint32	imagewidth;
	uint32	imagelength;

	TIFFGetField(in, TIFFTAG_IMAGEWIDTH, &imagewidth);
	TIFFGetField(in, TIFFTAG_IMAGELENGTH, &imagelength);
	inputline = (uint16 *)_TIFFmalloc(TIFFScanlineSize(in));
	if (inputline == NULL) 
	{
		fprintf(stderr, "No space for scanline buffer\n");
		exit(-1);
	}

	for (i = b_len; i-- > 0;)
	{
		hist_red[i] 	= 0;
		hist_green[i] 	= 0;
		hist_blue[i] 	= 0;
	}
	
	for (i = 0; i < imagelength; i++) 
	{
		if (readline(in, inputline, i) <= 0)
			break;
		inptr = inputline;
		for (j = imagewidth; j-- > 0;) 
		{
			red 	= *inptr++;
			green 	= *inptr++;
			blue 	= *inptr++;
			hist_red[red]++;
			hist_green[green]++;
			hist_blue[blue]++;
		}
	}
	
	_TIFFfree(inputline);
}


/****************************************************************************************************/
static void
usage(void)
{
	fprintf(stderr, "usage: tiffhisto input.tif\n");
	exit(-1);
}

