/* $Id: tiffdiff.c,v 1.5 2007/02/19 23:48:30 rip Exp $ */

/*
 * A generalized processing for tiff. This tool computes an absolute difference
 * pixel by pixel between the input1 and output1 file and outputs it to outputfile. 
 *
 * CST (2007) (roneil@cst.fr)
 *
 * tiffdiff [-h] input1 input2 output
 *     -r n		- create output with n rows/strip of data
 *
 */

#include <tiffconf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <tiffio.h>

#define	COLOR_DEPTH	16
#define	CopyField(tag, v) if (TIFFGetField(in, tag, &v)) TIFFSetField(out, tag, v)

static	void usage(void);
static 	int  prepare_images(TIFF *, TIFF *, TIFF *, uint32);
static 	void diff_image16(TIFF *, TIFF *, TIFF *);


/****************************************************************************************************/
int
main(int argc, char* argv[])
{
	int c;
	uint32	rowsperstrip = (uint32) -1;
	TIFF	*in, *in2, *out;

	while ((c = getopt(argc, argv, "r:")) != -1)
		switch (c) {
		case 'r':		/* rows/strip */
			rowsperstrip = atoi(optarg);
			break;
		case '?':
			usage();
			/*NOTREACHED*/
		}
		
	if (argc - optind < 3)
		usage();
	
	/* open the files */
	in = TIFFOpen(argv[optind], "r");
	if (in == NULL)
		return (-1);

	in2 = TIFFOpen(argv[optind+1], "r");
	if (in2 == NULL)
		return (-1);

	out = TIFFOpen(argv[optind+2], "w");
	if (out == NULL)
		return (-2);
	
	/* Prepare them */
	if (prepare_images(in, in2, out, rowsperstrip))
		return(-3);
	
	diff_image16(in, in2, out);
	
	(void) TIFFClose(out);
	return (0);
}

/****************************************************************************************************/
/* prepare_image prepares the output image.															*/
/****************************************************************************************************/
int prepare_images(TIFF *in, TIFF *in2, TIFF *out, uint32 rowsperstrip)
{
	float floatv;
	uint32 longv;
	uint16 shortv, config, config2, photometric;
	uint16	bitspersample = 1;
	uint16	samplesperpixel;
	uint32	imagewidth;
	uint32	imagelength;
	uint32	imagewidth2;
	uint32	imagelength2;
	uint16	samplesperpixel2;
	uint16	bitspersample2 = 1;

	TIFFGetField(in, TIFFTAG_IMAGEWIDTH, &imagewidth);
	TIFFGetField(in, TIFFTAG_IMAGELENGTH, &imagelength);
	TIFFGetField(in, TIFFTAG_BITSPERSAMPLE, &bitspersample);
	TIFFGetField(in, TIFFTAG_SAMPLESPERPIXEL, &samplesperpixel);
	if (bitspersample != 8 && bitspersample != 16) 
	{
		fprintf(stderr, "INPUT Image must have at least 8-bits/sample\n");
		return (-3);
	}
	
	if (!TIFFGetField(in, TIFFTAG_PHOTOMETRIC, &photometric) || photometric != PHOTOMETRIC_RGB || samplesperpixel < 3) 
	{
		fprintf(stderr, "INPUT image must have RGB data\n");
		return (-4);
	}
	
	TIFFGetField(in, TIFFTAG_PLANARCONFIG, &config);
	TIFFGetField(in, TIFFTAG_PLANARCONFIG, &config2);
	if ((config != PLANARCONFIG_CONTIG) || (config2 != PLANARCONFIG_CONTIG)) 
	{
		fprintf(stderr, "Can only handle contiguous data packing\n");
		return (-5);
	}

	TIFFGetField(in2, TIFFTAG_IMAGEWIDTH, &imagewidth2);
	TIFFGetField(in2, TIFFTAG_IMAGELENGTH, &imagelength2);
	TIFFGetField(in2, TIFFTAG_BITSPERSAMPLE, &bitspersample2);
	TIFFGetField(in2, TIFFTAG_SAMPLESPERPIXEL, &samplesperpixel2);
	if ((imagewidth2 != imagewidth) || (imagelength2 != imagelength) || (bitspersample2 !=bitspersample) || (samplesperpixel2 != samplesperpixel))
	{ 
		printf("\n\n---->Sorry, input images don't have the same specifications. \n A comparison does not make sense!\n");
		return(-1);
	}

	CopyField(TIFFTAG_SUBFILETYPE, longv);
	CopyField(TIFFTAG_IMAGEWIDTH, longv);
	TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, bitspersample);
		
	CopyField(TIFFTAG_PHOTOMETRIC, shortv);
	CopyField(TIFFTAG_SAMPLESPERPIXEL, shortv);
	TIFFSetField(out, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(out, rowsperstrip));
	
	CopyField(TIFFTAG_ORIENTATION, shortv);
	CopyField(TIFFTAG_PLANARCONFIG, shortv);
	CopyField(TIFFTAG_MINSAMPLEVALUE, shortv);
	CopyField(TIFFTAG_MAXSAMPLEVALUE, shortv);
	CopyField(TIFFTAG_RESOLUTIONUNIT, shortv);
	CopyField(TIFFTAG_XRESOLUTION, floatv);
	CopyField(TIFFTAG_YRESOLUTION, floatv);
	CopyField(TIFFTAG_XPOSITION, floatv);
	CopyField(TIFFTAG_YPOSITION, floatv);

	return(0);
}	

/****************************************************************************************************/
static void diff_image16(TIFF *in, TIFF *in2, TIFF *out)
{
	uint16 *outline, *inputline, *inputline2, i, j;
	uint16	*outptr, *inptr, *inptr2;
	int32 l1, l2;
	uint32	imagewidth;
	uint32	imagelength;

		
	//printf("Scanline is %d %d\n", TIFFScanlineSize(in), TIFFScanlineSize(out));
	inputline 	= (uint16 *) _TIFFmalloc(TIFFScanlineSize(in));
	inputline2 	= (uint16 *) _TIFFmalloc(TIFFScanlineSize(in2));
	outline 	= (uint16 *) _TIFFmalloc(TIFFScanlineSize(out));
	TIFFGetField(in2, TIFFTAG_IMAGEWIDTH, &imagewidth);
	TIFFGetField(in2, TIFFTAG_IMAGELENGTH, &imagelength);
	
	for (i = 0; i < imagelength; i++) 
	{
		if (TIFFReadScanline(in, inputline, i, 0) <= 0)	
			break;						
		if (TIFFReadScanline(in2, inputline2, i, 0) <= 0)	
			break;						
		inptr = inputline;
		inptr2 = inputline2;
		outptr = outline;
		for (j = 0; j < imagewidth; j++) 
		{
			l1 = *inptr++;
			l2 = *inptr2++;
			*outptr++ = abs(l1 - l2);
			l1 = *inptr++;
			l2 = *inptr2++;
			*outptr++ = abs(l1 - l2);
			l1 = *inptr++;
			l2 = *inptr2++;
			*outptr++ = abs(l1 - l2);
		}
		if (TIFFWriteScanline(out, outline, i, 0) < 0)
			break;
	}
	_TIFFfree(inputline);
	_TIFFfree(inputline2);
	_TIFFfree(outline);
}

/****************************************************************************************************/
char* stuff[] = {
"usage: tiffdiff [options] input.tif input2.tif output.tif",
"where options are:",
" -r #		make each strip have no more than # rows",
"",
NULL
};


/*   */

/****************************************************************************************************/
static void
usage(void)
{
	char buf[BUFSIZ];
	int i;

	setbuf(stderr, buf);
	for (i = 0; stuff[i] != NULL; i++)
		fprintf(stderr, "%s\n", stuff[i]);
	exit(-1);
}

