/* $Id: toXYZ.c,v 1.10 2007/03/08 00:40:54 rip Exp $ */

/*
 * A program to transfer RGB data to XYZ color space. 
 *
 *	Author: Rip O'Neil, CST, France.
 *
 * toXYZ input output
 *     -r n		- create output with n rows/strip of data
 *	   -g input_gamma - set the impout gamma. Defaults to 2.6
 *	   -S 			- use the StEM matrix
 *     -1 			- use an identity matrix
 *	   -p 			- use power function for gamma conversion
 *	   -v			- print version
 * (by default the rows/strip are taken from the input file)
 *
  ******************************************************************************
 * Copyright (c) 2007, CST, France <roneil@cst.fr>
 *
 * Permission to use, copy, modify, and distribute this software and 
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * CST (France) may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of CST (Paris, France).
 * 
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY 
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  
 * 
 * IN NO EVENT SHALL CST (Paris, FRANCE) BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF 
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE 
 * OF THIS SOFTWARE.
*/
#define VERSION "0.9.3"
#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

# include <unistd.h>

#include <tiffconf.h>
#include <tiffio.h>

#define	COLOR_DEPTH	16

#define	B_DEPTH		16		/* # bits/pixel to use */
#define	B_LEN		(1L<<B_DEPTH)
#define P_DEPTH		12		/* actual precision used (12 bits) */
#define P_LEN		(4096)
#define PRECISION	8		/* how much more in the linear space do we want over 16 bits... */

/* default gamma values */
#define GAMMA (2.6)		
#define DEGAMMA (1/2.6)	

#define MAT_IDENT 	0
#define MAT_SMPTE	1
#define MAT_StEM 	2

/* the LookUpTables for the gamma function  */
static float 	lut_in[B_LEN];
static uint16 	lut_out[B_LEN<<PRECISION];
static int		use_power = 0;

typedef struct {
	float r;
	float g;
	float b;
} pixelf;

/* the different matrix definitions */
static float TheMatrix[3][3][3]= {
/* identity */
	 {{1.0, 0.0, 0.0},
	  {0.0, 1.0, 0.0},
	  {0.0, 0.0, 1.0}},
			 
/* DC28.30 (2006-02-24) */
	 {{0.4451698156, 0.2771344092, 0.1722826698},
	  {0.2094916779, 0.7215952542, 0.0689130679},
	  {0.0000000000, 0.0470605601, 0.9073553944}},
			 
/*StEM: http://www.cinematography.net/4KDemoOfFilmDigitalCinema.htm*/
	 {{0.464,  0.2692, 0.1610},
	  {0.2185, 0.7010, 0.0805},
	  {0.0000, 0.0457, 0.9087}}
};

/* Some prototyping */
static	void usage(void);
static 	void make_lut(float,float);
static 	void do_matrix( pixelf *, pixelf *, int );
static 	void process_image16(TIFF *, TIFF *, int);
static 	void process_image16p(TIFF *, TIFF *, int, float, float);
static 	int  prepare_image(TIFF *, TIFF *, char *, uint32 , char *);
static  int  readline(TIFF *, uint16 *, int);

/****************************************************************************************************/
int main(int argc, char* argv[])
{
	TIFF	*in, *out;
	uint32	rpp = (uint32) -1;
	float gamma_in = GAMMA, gamma_out = DEGAMMA;
	int c, ret, matrix = MAT_SMPTE;
	char buf[256], matrix_used[256] = "SMPTE DC28.30 2006-02-24";

	while ((c = getopt(argc, argv, "r:l:g:1Spv")) != -1)
		switch (c) {
		case 'r':		/* rows/strip */
			rpp = atoi(optarg);
			break;
		case 'g':		/* gamma in */
			sscanf(optarg, "%f", &gamma_in);
			break;
		case '1':
			strcpy(matrix_used, "Identity (1:1)");
			matrix = MAT_IDENT;
			break;
		case 'S':
			strcpy(matrix_used, "StEM");
			matrix = MAT_StEM;
			break;
		case 'p':
			use_power = 1;
			break;
		case 'v':
			fprintf(stderr, "Ver %s \n", VERSION);
			exit(0);
		case '?':
			usage();
			/*NOTREACHED*/
		}
	if (argc - optind < 2)
		usage();

	/* Open images and ready for data processing */
	in = TIFFOpen(argv[optind], "r");
	if (in == NULL)
		return (-1);

	out = TIFFOpen(argv[optind+1], "w");
	if (out == NULL)
		return (-2);
	
	sprintf(buf, "RGB->X'Y'Z' photometric interpretation with %4.2f input gamma, 1/%4.2f output gamma, Matrix used: %s", gamma_in, 1/gamma_out, matrix_used); 
	ret = prepare_image(in, out, buf, rpp, argv[optind]);
	if (ret)
		return(ret);
	
	/* do the actual processing of image */
	if (use_power)
		process_image16p(in, out, matrix, gamma_in, gamma_out);
	else
	{
		/* make LUT for gamma transfers */
		make_lut(gamma_in, gamma_out);
		process_image16(in, out, matrix);
	}
	
	/* and do some cleanup */
	(void) TIFFClose(out);
	(void) TIFFClose(in);
	return (0);
}

/****************************************************************************************************/
/* prepare_image prepares the output image.															*/
/****************************************************************************************************/
int prepare_image(TIFF *in, TIFF *out, char *str, uint32 rpp, char *im)
{
	char buf[256];
	uint16	spp;
	uint32	i_width;
	uint32	i_length;
	uint16  bps;
	uint16 planar, config, photometric;

	TIFFGetField(in, TIFFTAG_IMAGEWIDTH, &i_width);
	TIFFGetField(in, TIFFTAG_IMAGELENGTH, &i_length);
	TIFFGetField(in, TIFFTAG_BITSPERSAMPLE, &bps);
	TIFFGetField(in, TIFFTAG_SAMPLESPERPIXEL, &spp);
	if (bps != 8 && bps != 16) 
	{
		fprintf(stderr, "%s: Image must have at least 8-bits/sample\n", im);
		return (-3);
	}
	if (!TIFFGetField(in, TIFFTAG_PHOTOMETRIC, &photometric) || photometric != PHOTOMETRIC_RGB || spp < 3) 
	{
		fprintf(stderr, "%s: Image must have RGB data\n", im);
		return (-4);
	}
	TIFFGetField(in, TIFFTAG_PLANARCONFIG, &config);
	if (config != PLANARCONFIG_CONTIG) 
	{
		fprintf(stderr, "%s: Can only handle contiguous data packing\n", im);
		return (-5);
	}
	/* define the size of the output image */
	TIFFSetField(out, TIFFTAG_IMAGEWIDTH, i_width);
	TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, (short)COLOR_DEPTH);
	TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, spp);
	TIFFSetField(out, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(out, rpp));
	/* Copy original info into output image */
	if (TIFFGetField(in, TIFFTAG_PHOTOMETRIC, &photometric)) TIFFSetField(out, TIFFTAG_PHOTOMETRIC, photometric);
	if (TIFFGetField(in, TIFFTAG_PLANARCONFIG, &planar)) TIFFSetField(out, TIFFTAG_PLANARCONFIG, planar);
	
	/* Add some info as to how the image was processed */
	TIFFSetField(out, TIFFTAG_IMAGEDESCRIPTION, str);
	sprintf(buf, "toXYZ (Version: %s) (c)2007 CST, France", VERSION); 
	TIFFSetField(out, TIFFTAG_SOFTWARE, buf);
	return(0);
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
			*ptr++ = (uint16) *sp++ << 8;
			*ptr++ = (uint16) *sp++ << 8;
			*ptr++ = (uint16) *sp++ << 8;
		}
		_TIFFfree(sline);
		 
	}
	return (1);
}

/****************************************************************************************************/
/* here we do the matrix processing. 																*/
/****************************************************************************************************/
void do_matrix(pixelf *pi, pixelf *po, int matrix)
{
	po->r = (pi->r * TheMatrix[matrix][0][0]) + (pi->g * TheMatrix[matrix][0][1]) + (pi->b * TheMatrix[matrix][0][2]);
	po->g = (pi->r * TheMatrix[matrix][1][0]) + (pi->g * TheMatrix[matrix][1][1]) + (pi->b * TheMatrix[matrix][1][2]);
	po->b = (pi->r * TheMatrix[matrix][2][0]) + (pi->g * TheMatrix[matrix][2][1]) + (pi->b * TheMatrix[matrix][2][2]);
}

/****************************************************************************************************/
/*  make_lut makes a lut to not have to use the power function on each pixel. */
/* It assumes a 12 bit precision in log space						*/
/****************************************************************************************************/
void make_lut(float g_in, float g_out)
{
	uint32 i;
	
	for (i = 0; i < B_LEN; i++)
		lut_in[i]  = powf((float)i/(float)(B_LEN - 1), g_in);
	
	for (i = 0; i < B_LEN*PRECISION; i++)
		lut_out[i] = (uint16)(powf((float)i/(float)(B_LEN*PRECISION - 1), g_out) * (B_LEN - 1));

}

/****************************************************************************************************/
/*  Do the actual processing of the image line by line.	It assumes a 12 bit precision in log space  */
/****************************************************************************************************/
static void process_image16(TIFF *in, TIFF *out, int matrix)
{

	uint32	i_length, i_width;
	uint16 *outline, *inputline;
	uint16 *outptr, *inptr;
	uint32 i, j, r, g, b;
	pixelf pi, po;
		
	TIFFGetField(out, TIFFTAG_IMAGEWIDTH, &i_width);
	TIFFGetField(in, TIFFTAG_IMAGELENGTH, &i_length);
	inputline 	= (uint16 *) _TIFFmalloc(TIFFScanlineSize(out));
	outline 	= (uint16 *) _TIFFmalloc(TIFFScanlineSize(out));

	for (i = 0; i < i_length; i++) 
	{
		if (readline(in, inputline, i) <= 0)	
			break;						

		inptr = inputline;
		outptr = outline;
		for (j = 0; j < i_width; j++) 
		{
			/* bring it into 12 bits Ah... Well not just yet! seems to work better in a 16 bit space*/
			r = (*inptr++);
			g = (*inptr++);
			b = (*inptr++);
			
			/* Put into Linear space */
			pi.r = lut_in[r];
			pi.g = lut_in[g];
			pi.b = lut_in[b];
	
			/* Perform transform RGB -> XYZ */
			do_matrix(&pi, &po, matrix);
			
			/* put back to the Digital gamma space */
			r = lut_out[(uint32)(((po.r > 1.0)? 1.0 : po.r) * (B_LEN*PRECISION - 1))];
			g = lut_out[(uint32)(((po.g > 1.0)? 1.0 : po.g) * (B_LEN*PRECISION - 1))];
			b = lut_out[(uint32)(((po.b > 1.0)? 1.0 : po.b) * (B_LEN*PRECISION - 1))];
			
			/* pad it out to 16 bits Ah... Well not just yet!*/			
			*outptr++ = (uint16) r;
			*outptr++ = (uint16) g;
			*outptr++ = (uint16) b;
		}
		if (TIFFWriteScanline(out, outline, i, 0) < 0)
			break;
	}
	_TIFFfree(inputline);
	_TIFFfree(outline);
}

/****************************************************************************************************/
/*  Do the actual processing of the image line by line.	It assumes a 12 bit precision in log space  */
/* this uses the power function thus much much slower													*/
/****************************************************************************************************/
static void process_image16p(TIFF *in, TIFF *out, int matrix, float g_in, float g_out)
{

	uint32	i_length, i_width;
	uint16 *outline, *inputline;
	uint16 *outptr, *inptr;
	uint32 i, j, r, g, b;
	pixelf pi, po;
		
	TIFFGetField(out, TIFFTAG_IMAGEWIDTH, &i_width);
	TIFFGetField(in, TIFFTAG_IMAGELENGTH, &i_length);
	inputline 	= (uint16 *) _TIFFmalloc(TIFFScanlineSize(out));
	outline 	= (uint16 *) _TIFFmalloc(TIFFScanlineSize(out));

	for (i = 0; i < i_length; i++) 
	{
		if (readline(in, inputline, i) <= 0)	
			break;						

		inptr = inputline;
		outptr = outline;
		for (j = 0; j < i_width; j++) 
		{
			/* bring it into 12 bits */
			r = (*inptr++);
			g = (*inptr++);
			b = (*inptr++);
			
			/* Put into Linear space */
			pi.r = powf((float)r/(float)(B_LEN - 1), g_in);
			pi.g = powf((float)g/(float)(B_LEN - 1), g_in);
			pi.b = powf((float)b/(float)(B_LEN - 1), g_in);
			
			/* Perform transform RGB -> XYZ */
			do_matrix(&pi, &po, matrix);
						
			/* put back to the Digital gamma space */
			r = (uint16)(powf(po.r, g_out) * (P_LEN - 1));
			g = (uint16)(powf(po.g, g_out) * (P_LEN - 1));
			b = (uint16)(powf(po.b, g_out) * (P_LEN - 1));
			
			/* pad it out to 16 bits */			
			*outptr++ = r * 16;
			*outptr++ = g * 16 ;
			*outptr++ = b * 16 ;
		}
		if (TIFFWriteScanline(out, outline, i, 0) < 0)
			break;
	}
	_TIFFfree(inputline);
	_TIFFfree(outline);
}


/****************************************************************************************************/
char* usage_txt[] = {
"usage: toXYZ [options] input.tif output.tif",
"where options are:",
" -r #		make each strip have no more than # rows",
" -g gamma	use the value 'gamma' for input data (default 2.6)",		
" -S 		use StEM specified Matrix",
" -1 		use an identity matrix (1:1)",
" -p		use power function to calculate gamma (very expensive!)",
" -v		print version and exit",
" ",
"The DC28.30 matrix (2006-02-24) is used by default.",
"",
NULL
};

/****************************************************************************************************/
static void
usage(void)
{
	int i;

	fprintf(stderr, "Ver %s \n", VERSION);
	for (i = 0; usage_txt[i] != NULL; i++)
		fprintf(stderr, "%s\n", usage_txt[i]);
	exit(-1);
}

