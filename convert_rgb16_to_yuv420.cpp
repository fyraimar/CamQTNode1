#include <stdio.h>
#include <string.h>
#include <stdlib.h>


static int tables_initialized = 0;
unsigned char *gYTable, *gCbTable, *gCrTable;

static int
clamp(int  x)
{
    if (x > 255) return 255;
    if (x < 0)   return 0;
    return x;
}

/* the equation used by the video code to translate YUV to RGB looks like this
 *
 *    Y  = (Y0 - 16)*k0
 *    Cb = Cb0 - 128
 *    Cr = Cr0 - 128
 *
 *    G = ( Y - k1*Cr - k2*Cb )
 *    R = ( Y + k3*Cr )
 *    B = ( Y + k4*Cb )
 *
 */

static const double  k0 = 1.164;
static const double  k1 = 0.813;
static const double  k2 = 0.391;
static const double  k3 = 1.596;
static const double  k4 = 2.018;

/* let's try to extract the value of Y
 *
 *   G + k1/k3*R + k2/k4*B = Y*( 1 + k1/k3 + k2/k4 )
 *
 *   Y  = ( G + k1/k3*R + k2/k4*B ) / (1 + k1/k3 + k2/k4)
 *   Y0 = ( G0 + k1/k3*R0 + k2/k4*B0 ) / ((1 + k1/k3 + k2/k4)*k0) + 16
 *
 * let define:
 *   kYr = k1/k3
 *   kYb = k2/k4
 *   kYy = k0 * ( 1 + kYr + kYb )
 *
 * we have:
 *    Y  = ( G + kYr*R + kYb*B )
 *    Y0 = clamp[ Y/kYy + 16 ]
 */

static const double kYr = k1/k3;
static const double kYb = k2/k4;
static const double kYy = k0*( 1. + kYr + kYb );

static void initYtab()
{
    const  int imax = (int)( (kYr + kYb)*(31 << 2) + (61 << 3) + 0.1 );
    int    i;

    gYTable = (unsigned char *)malloc(imax);

    for(i=0; i<imax; i++) {
        int  x = (int)(i/kYy + 16.5);
        if (x < 16) x = 16;
        else if (x > 235) x = 235;
        gYTable[i] = (unsigned char) x;
    }
}

/*
 *   the source is RGB565, so adjust for 8-bit range of input values:
 *
 *   G = (pixels >> 3) & 0xFC;
 *   R = (pixels >> 8) & 0xF8;
 *   B = (pixels & 0x1f) << 3;
 *
 *   R2 = (pixels >> 11)      R = R2*8
 *   B2 = (pixels & 0x1f)     B = B2*8
 *
 *   kYr*R = kYr2*R2 =>  kYr2 = kYr*8
 *   kYb*B = kYb2*B2 =>  kYb2 = kYb*8
 *
 *   we want to use integer multiplications:
 *
 *   SHIFT1 = 9
 *
 *   (ALPHA*R2) >> SHIFT1 == R*kYr  =>  ALPHA = kYr*8*(1 << SHIFT1)
 *
 *   ALPHA = kYr*(1 << (SHIFT1+3))
 *   BETA  = kYb*(1 << (SHIFT1+3))
 */

static const int  SHIFT1  = 9;
static const int  ALPHA   = (int)( kYr*(1 << (SHIFT1+3)) + 0.5 );
static const int  BETA    = (int)( kYb*(1 << (SHIFT1+3)) + 0.5 );

/*
 *  now let's try to get the values of Cb and Cr
 *
 *  R-B = (k3*Cr - k4*Cb)
 *
 *    k3*Cr = k4*Cb + (R-B)
 *    k4*Cb = k3*Cr - (R-B)
 *
 *  R-G = (k1+k3)*Cr + k2*Cb
 *      = (k1+k3)*Cr + k2/k4*(k3*Cr - (R-B)/k0)
 *      = (k1 + k3 + k2*k3/k4)*Cr - k2/k4*(R-B)
 *
 *  kRr*Cr = (R-G) + kYb*(R-B)
 *
 *  Cr  = ((R-G) + kYb*(R-B))/kRr
 *  Cr0 = clamp(Cr + 128)
 */

static const double  kRr = (k1 + k3 + k2*k3/k4);

static void initCrtab()
{
    unsigned char *pTable;
    int i;

    gCrTable = (unsigned char *)malloc(768*2);

    pTable = gCrTable + 384;
    for(i=-384; i<384; i++)
        pTable[i] = (unsigned char) clamp( i/kRr + 128.5 );
}

/*
 *  B-G = (k2 + k4)*Cb + k1*Cr
 *      = (k2 + k4)*Cb + k1/k3*(k4*Cb + (R-B))
 *      = (k2 + k4 + k1*k4/k3)*Cb + k1/k3*(R-B)
 *
 *  kBb*Cb = (B-G) - kYr*(R-B)
 *
 *  Cb   = ((B-G) - kYr*(R-B))/kBb
 *  Cb0  = clamp(Cb + 128)
 *
 */

static const double  kBb = (k2 + k4 + k1*k4/k3);

static void initCbtab()
{
    unsigned char *pTable;
    int i;

    gCbTable = (unsigned char *)malloc(768*2);

    pTable = gCbTable + 384;
    for(i=-384; i<384; i++)
        pTable[i] = (unsigned char) clamp( i/kBb + 128.5 );
}

/*
 *   SHIFT2 = 16
 *
 *   DELTA = kYb*(1 << SHIFT2)
 *   GAMMA = kYr*(1 << SHIFT2)
 */

static const int  SHIFT2 = 16;
static const int  DELTA  = kYb*(1 << SHIFT2);
static const int  GAMMA  = kYr*(1 << SHIFT2);

int32_t ccrgb16toyuv_wo_colorkey(unsigned char *rgb16, unsigned char *yuv420,
        unsigned int *param, unsigned char *table[])
{
    unsigned short *inputRGB = (unsigned short*)rgb16;
    unsigned char *outYUV = yuv420;
    int32_t width_dst = param[0];
    int32_t height_dst = param[1];
    int32_t pitch_dst = param[2];
    int32_t mheight_dst = param[3];
    int32_t pitch_src = param[4];
    unsigned char *y_tab = table[0];
    unsigned char *cb_tab = table[1];
    unsigned char *cr_tab = table[2];

    int32_t size16 = pitch_dst*mheight_dst;
    int32_t i,j,count;
    int32_t ilimit,jlimit;
    unsigned char *tempY,*tempU,*tempV;
    unsigned short pixels;
    int   tmp;
unsigned int temp;

    tempY = outYUV;
    tempU = outYUV + (height_dst * pitch_dst);
    tempV = tempU + 1;

    jlimit = height_dst;
    ilimit = width_dst;

    for(j=0; j<jlimit; j+=1)
    {
        for (i=0; i<ilimit; i+=2)
        {
            int32_t   G_ds = 0, B_ds = 0, R_ds = 0;
            unsigned char   y0, y1, u, v;

            pixels =  inputRGB[i];
            temp = (BETA*(pixels & 0x001F) + ALPHA*(pixels>>11) );
            y0   = y_tab[(temp>>SHIFT1) + ((pixels>>3) & 0x00FC)];

            G_ds    += (pixels>>1) & 0x03E0;
            B_ds    += (pixels<<5) & 0x03E0;
            R_ds    += (pixels>>6) & 0x03E0;

            pixels =  inputRGB[i+1];
            temp = (BETA*(pixels & 0x001F) + ALPHA*(pixels>>11) );
            y1   = y_tab[(temp>>SHIFT1) + ((pixels>>3) & 0x00FC)];

            G_ds    += (pixels>>1) & 0x03E0;
            B_ds    += (pixels<<5) & 0x03E0;
            R_ds    += (pixels>>6) & 0x03E0;

            R_ds >>= 1;
            B_ds >>= 1;
            G_ds >>= 1;

            tmp = R_ds - B_ds;

            u = cb_tab[(((B_ds-G_ds)<<SHIFT2) - GAMMA*tmp)>>(SHIFT2+2)];
            v = cr_tab[(((R_ds-G_ds)<<SHIFT2) + DELTA*tmp)>>(SHIFT2+2)];

            tempY[0] = y0;
            tempY[1] = y1;
            tempY += 2;

            if ((j&1) == 0) {
                tempU[0] = u;
                tempV[0] = v;
                tempU += 2;
                tempV += 2;
            }
        }

        inputRGB += pitch_src;
    }

    return 1;
}

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

void convert_rgb16_to_yuv420(unsigned char *rgb, unsigned char *yuv, int width, int height)
{
    if (!tables_initialized) {
        initYtab();
        initCrtab();
        initCbtab();
        tables_initialized = 1;
    }

    unsigned int param[6];
    param[0] = (unsigned int) width;
    param[1] = (unsigned int) height;
    param[2] = (unsigned int) width;
    param[3] = (unsigned int) height;
    param[4] = (unsigned int) width;
    param[5] = (unsigned int) 0;

    unsigned char *table[3];
    table[0] = gYTable;
    table[1] = gCbTable + 384;
    table[2] = gCrTable + 384;

    ccrgb16toyuv_wo_colorkey(rgb, yuv, param, table);
}
