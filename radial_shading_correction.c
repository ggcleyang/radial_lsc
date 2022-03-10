//
// Created by LY on 2022/3/1.
//
#include<stdio.h>
#include<math.h>
#include<string.h>
#include <stdlib.h>

typedef unsigned long int uint64;
typedef unsigned int uint32;
typedef int int32;
typedef unsigned short uint16;
typedef short int16;
typedef unsigned char uint8;
typedef char int8;


#define  BPRG 0
#define  BPGR 1
#define  BPGB 2
#define  BPBG 3
#define MIN2(x,y)       ( (x)<(y) ? (x):(y) )
#define CLIP_MAX(x,max)          ((x)>(max)?(max):(x))

static const uint8 bayerPattLUT[4][2][2] =
        {
                {{BPRG, BPGR}, {BPGB, BPBG}}, // 0 RGGB
                {{BPGR, BPRG}, {BPBG, BPGB}}, // 1 GRBG
                {{BPGB, BPBG}, {BPRG, BPGR}}, // 2 GBRG
                {{BPBG, BPGB}, {BPGR, BPRG}}  // 3 BGGR
        };

#define RLSC_NODE_NUM 129
typedef struct _RLSC_GAIN
{
    uint16 r_gain[RLSC_NODE_NUM];
    uint16 gr_gain[RLSC_NODE_NUM];
    uint16 gb_gain[RLSC_NODE_NUM];
    uint16 b_gain[RLSC_NODE_NUM];
} RLSC_GAIN;

typedef struct _RLSC_CFG
{
    uint32 u32Height;
    uint32 u32Width;
    uint32 u32WindowSize;
    uint32 u32clip_thresh; //clip dark or white pixel in calculate mean value
    uint32 u32BitDepth;
    uint32 BayerFormat;//0-R,1-GR,2-GB,3-B
    uint32 u32GainScale;//0-7
} RLSC_CFG;

uint32 read_BayerImg(char* fileName,int height,int width, uint16* outData)
{
    FILE* inFile = NULL;
    int length = width * height * 2;
    inFile = fopen(fileName, "rb");
    if ( NULL == inFile)
    {
        printf("fopen fail !!!");
        return 0;
    }
    fread(outData, 1, length, inFile);
    fclose(inFile);
    return 0;

}

uint16 LinearInter( uint32 v, uint32 x0, uint16 y0, uint32 x1, uint16 y1 )
{
    uint16 res;

    if ( v <= x0 )
    {
        return y0;
    }
    if ( v >= x1 )
    {
        return y1;
    }
    float k;
    uint16 b = y0;
    k = (float)(y1 - y0)/(x1 - x0);
    res = (uint16)(k * (uint16)(v - x0)) + b;
    //res = (uint32)(y1 - y0) * (uint32)(v - x0) / (x1 - x0) + y0;
    return res;
}

static uint8 Get_node_index(uint32 u32Value, uint8 u8Count, uint32* pu32Thresh)
{
    uint8 u8Level = 0;

    for (u8Level = 0; u8Level < u8Count; u8Level++)
    {
        if (u32Value <= pu32Thresh[u8Level])
        {
            break;
        }
    }

//    if (u8Level == u8Count)
//    {
//        u8Level = u8Count - 1;
//    }

    return u8Level;
}
uint32 rlsc_dataEnlarge(uint32 data, uint32 maxData, uint8 u8Scale)
{

    uint32 ratio;;
    uint32 QValue;
    if (u8Scale < 7)
    {
        QValue = 1U << (15 - u8Scale);
        ratio = (uint32)(((float)maxData / (float)data) * QValue);
    }
    else
    {
        QValue = 1U << (23 - u8Scale);
        ratio = (uint32)(((float)maxData / (float)data - 1) * QValue);
    }
    return MIN2(ratio, 65535);

}

uint32 getMaxData(uint16 *u16data, int length)
{
    int i;

    uint16 u16MaxData = 0;
    for (i = 0; i < length; i++)
    {

        if (u16data[i] > u16MaxData)
        {
            u16MaxData = u16data[i];
        }
    }
    return u16MaxData;
}


uint32 get_rlsc_node_mean(uint16 *BayerImg,uint8 BayerChannel,uint32 loc_x,uint32 loc_y,uint32 half_window,uint32 height,uint32 width,uint32 clip_value){

    //
    uint8 first_pixel;
    uint32  mean = 0;
    uint32 x_start,x_end,y_start,y_end;
    if (loc_x < 0 || loc_x >= width || loc_y < 0 || loc_y >= height)
    {
        return mean;
    }

    x_start = (loc_x - half_window);
    x_end = (loc_x + half_window);
    y_start = (loc_y - half_window);
    y_end = (loc_y + half_window);
    if (x_start < 0)
    {
        x_start = 0;
    }
    if (x_end >= width)
    {
        x_end = width - 1;
    }
    if (y_start < 0)
    {
        y_start = 0;
    }
    if (y_end >= height)
    {
        y_end = height - 1;
    }
    uint32 count = 0;
    uint32 sum =0;
//    printf("LINE-%d,width:%d,y_start:%d,y_end:%d,x_start:%d,x_end:%d \n",__LINE__,width,y_start,y_end,x_start,x_end);
//    printf("LINE-%d,BayerChannel:%d,\n",__LINE__,BayerChannel);
    for(uint32 y =y_start; y < y_end;y++ ){
        for (uint32 x =x_start; x < x_end;x++) {
            first_pixel = bayerPattLUT[0][y & 0x1][x & 0x1];
            if(BayerChannel == first_pixel){
                if((BayerImg[y*width+x] > 4095* clip_value/100) && (BayerImg[y*width+x] < 4095* (100-clip_value)/100) ){
                    sum = sum + BayerImg[y*width+x];
                    count++;
                }

            }
        }
    }
    if(count){
        mean = sum/count;
    }

    //printf("LINE-%d,mean_value:%d,count:%d \n",__LINE__,mean,count);
    return mean;
}

uint32 generate_rlsc_gain(uint16 *BayerImg,RLSC_CFG *RLSCfg,RLSC_GAIN *RLSCGain) {

    uint32 height = RLSCfg->u32Height;
    uint32 width = RLSCfg->u32Width;
    uint32 window_size = RLSCfg->u32WindowSize;
    uint32 half_window = window_size / 2;
    uint32 last_node_half_window = half_window;
    uint32 ratio_scale = RLSCfg->u32GainScale;
    uint32 clip_threshold = RLSCfg->u32clip_thresh;
    uint32 center_x, center_y;
    uint32 loc_x2, loc_x3, loc_x4, loc_y2, loc_y3, loc_y4;
    uint32 last_node_x, last_node_y;
    uint32 curr_node_x, curr_node_y;
    uint32 delta_x, delta_y;
    uint32 SquareRadius;
    uint32 Sec_SquareRadius;
    uint32 two_node_distance;
    uint8 BayerChannel;
    float fsin, fcos;



    SquareRadius = height / 2 * height / 2 + width / 2 * width / 2;
    Sec_SquareRadius = SquareRadius / (RLSC_NODE_NUM - 1);
    fsin = (float) width / 2 / (float) (sqrt(SquareRadius));
    fcos = (float) height / 2 / (float) (sqrt(SquareRadius));
    center_x = width / 2;
    center_y = height / 2;

    curr_node_x = center_x;
    curr_node_y = center_y;
    last_node_x = center_x;
    last_node_y = center_y;
    //printf("LINE-%d,loc_x:%d,loc_y:%d \n",__LINE__,curr_node_x,curr_node_y);
    //node_mean[0] = get_rlsc_node_mean(BayerImg,BayerChannel,center_x,center_y,half_window,height,width);//R channel node 0

    //for(uint8 bc=0;bc <4; bc++){
    RLSCGain->r_gain[0] = get_rlsc_node_mean(BayerImg, 0, center_x, center_y, half_window, height, width,clip_threshold);
    RLSCGain->gr_gain[0] = get_rlsc_node_mean(BayerImg, 1, center_x, center_y, half_window, height, width,clip_threshold);
    RLSCGain->gb_gain[0] = get_rlsc_node_mean(BayerImg, 2, center_x, center_y, half_window, height, width,clip_threshold);
    RLSCGain->b_gain[0] = get_rlsc_node_mean(BayerImg, 3, center_x, center_y, half_window, height, width,clip_threshold);
    //}
    //printf("LINE-%d,r_node0:%d \n", __LINE__, RLSCGain->r_gain[0]);
    //printf("LINE-%d,gr_node0:%d \n",__LINE__,RLSCGain->gr_gain[0]);
    //printf("LINE-%d,gb_node0:%d \n",__LINE__,RLSCGain->gb_gain[0]);
    //printf("LINE-%d,b_node0:%d \n",__LINE__,RLSCGain->b_gain[0]);
#if 1
    uint32 temp = 0;
    uint32 mean = 0;
    uint32 cnt = 0;
    uint32 i;
    for (int BC = 0; BC < 4; BC++) {
        for (i = 1; i < RLSC_NODE_NUM; i++) {

            delta_x = (uint32) (fsin * sqrt((double) i * Sec_SquareRadius));
            delta_y = (uint32) (fcos * sqrt((double) i * Sec_SquareRadius));
            curr_node_x = center_x + delta_x;
            curr_node_y = center_y + delta_y;

            //printf("LINE-%d,node-%d,curr_node_x-%d,curr_node_y:%d \n", __LINE__, i, curr_node_x, curr_node_y);
            two_node_distance = (uint32) sqrt((double) (curr_node_x - last_node_x) * (curr_node_x - last_node_x) +
                                              (curr_node_y - last_node_y) * (curr_node_y - last_node_y));
            if ((last_node_half_window + half_window) > two_node_distance && half_window > 3) {
                half_window--;
            }
            //printf("LINE-%d,node-%d,half_window:%d \n",__LINE__,i,half_window);
            loc_x2 = center_x - delta_x;
            loc_y2 = center_y + delta_y;

            loc_x3 = center_x - delta_x;
            loc_y3 = center_y - delta_y;

            loc_x4 = center_x + delta_x;
            loc_y4 = center_y - delta_y;
            temp = get_rlsc_node_mean(BayerImg, BC, curr_node_x, curr_node_y, half_window, height, width,clip_threshold);

            if (temp) {
                mean = mean + temp;
                cnt++;
            }
            temp = get_rlsc_node_mean(BayerImg, BC, loc_x2, loc_y2, half_window, height, width,clip_threshold);
            if (temp) {
                mean = mean + temp;
                cnt++;
            }
            temp = get_rlsc_node_mean(BayerImg, BC, loc_x3, loc_y3, half_window, height, width,clip_threshold);
            if (temp) {
                mean = mean + temp;
                cnt++;
            }
            temp = get_rlsc_node_mean(BayerImg, BC, loc_x4, loc_y4, half_window, height, width,clip_threshold);
            if (temp) {
                mean = mean + temp;
                cnt++;
            }
            if (cnt) {
                if(BC==BPRG){
                    RLSCGain->r_gain[i] = mean / cnt;//node mean value_r channel
                }
                else if(BC==BPGR){
                    RLSCGain->gr_gain[i] = mean / cnt;//node mean value_gr channel
                }
                else if(BC==BPGB){
                    RLSCGain->gb_gain[i] = mean / cnt;//node mean value_gb channel
                }
                else if(BC==BPBG){
                    RLSCGain->b_gain[i] = mean / cnt;//node mean value_b channel
                }

            } else {
                RLSCGain->r_gain[i] = mean;
                RLSCGain->gr_gain[i] = mean;
                RLSCGain->gb_gain[i] = mean;
                RLSCGain->b_gain[i] = mean;
            }

            last_node_x = curr_node_x;
            last_node_y = curr_node_y;
            last_node_half_window = half_window;
            temp = 0;
            mean = 0;
            cnt = 0;
        }
    }
    for (int j = 0; j < RLSC_NODE_NUM; j++) {

        //printf("LINE-%d,%d_node_r_gain:%ld,gr_gain:%ld,gb_gain:%ld,b_gain:%ld \n", __LINE__, j, RLSCGain->r_gain[j],RLSCGain->gr_gain[j],RLSCGain->gb_gain[j],RLSCGain->b_gain[j]);
    }
    uint32 node_max_r_mean =0;
    uint32 node_max_gr_mean =0;
    uint32 node_max_gb_mean =0;
    uint32 node_max_b_mean =0;


     node_max_r_mean= getMaxData( RLSCGain->r_gain,RLSC_NODE_NUM);
     node_max_gr_mean= getMaxData( RLSCGain->gr_gain,RLSC_NODE_NUM);
     node_max_gb_mean= getMaxData( RLSCGain->gb_gain,RLSC_NODE_NUM);
     node_max_b_mean= getMaxData( RLSCGain->b_gain,RLSC_NODE_NUM);
//     printf("LINE-%d,node_max_r_mean:%ld \n",__LINE__,node_max_r_mean);
//     printf("LINE-%d,node_max_gr_mean:%ld \n",__LINE__,node_max_gr_mean);
//     printf("LINE-%d,node_max_gb_mean:%ld \n",__LINE__,node_max_gb_mean);
//     printf("LINE-%d,node_max_b_mean:%ld \n",__LINE__,node_max_b_mean);
     for(uint8 m =0;m < RLSC_NODE_NUM; m++){

         RLSCGain->r_gain[m] = rlsc_dataEnlarge(RLSCGain->r_gain[m], node_max_r_mean, ratio_scale);//r gain
         RLSCGain->gr_gain[m] = rlsc_dataEnlarge(RLSCGain->gr_gain[m], node_max_gr_mean, ratio_scale);//gr gain
         RLSCGain->gb_gain[m] = rlsc_dataEnlarge(RLSCGain->gb_gain[m], node_max_gb_mean, ratio_scale);//gb gain
         RLSCGain->b_gain[m] = rlsc_dataEnlarge(RLSCGain->b_gain[m], node_max_b_mean, ratio_scale);// b gain
     }
    for (int j = 0; j < RLSC_NODE_NUM; j++) {

        //printf("LINE-%d,%d_node_r_gain:%ld,gr_gain:%ld,gb_gain:%ld,b_gain:%ld \n", __LINE__, j, RLSCGain->r_gain[j],RLSCGain->gr_gain[j],RLSCGain->gb_gain[j],RLSCGain->b_gain[j]);
    }

    return 0;
}
#endif
uint32 apply_rlsc_gain(uint16 *BayerImg,RLSC_CFG *RLSCfg,RLSC_GAIN *RLSCGain){

    uint32 height = RLSCfg->u32Height;
    uint32 width = RLSCfg->u32Width;
    uint16 pixel_max_value = 1U << RLSCfg->u32BitDepth -1;
    uint32 QValue = 1U << (15 - RLSCfg->u32GainScale);
    uint32 center_x =  width /2;
    uint32 center_y =  height /2;
    uint32 Pixel_SquareRadius;
    uint32 rlsc_node[RLSC_NODE_NUM]={ 0 };
    uint32 SquareRadius = height / 2 * height / 2 + width / 2 * width / 2;
    uint32 Sec_SquareRadius = SquareRadius / (RLSC_NODE_NUM - 1);
    uint8 node_index;
    uint16 inter_gain;
    uint8 first_pixel;//0-R,1-GR,2-GB,3-B
    for(uint32 num =0;num<RLSC_NODE_NUM;num++){
       if((RLSC_NODE_NUM - 1)==num){

            rlsc_node[RLSC_NODE_NUM - 1] = SquareRadius;
        }
        else{
            rlsc_node[num] = Sec_SquareRadius*num;
        }
    }
    //FILE *inter_gain_head;
    //inter_gain_head = fopen("D:\\leetcode_project\\inter_gain_head.h", "wb");
    for(uint16  y = 0;y < height;y++){
        for(uint16 x = 0;x < width;x++){

            Pixel_SquareRadius = (y-center_y)*(y-center_y)+(x-center_x)*(x-center_x);
            node_index = Get_node_index(Pixel_SquareRadius, RLSC_NODE_NUM, rlsc_node);
            first_pixel = bayerPattLUT[0][y & 0x1][x & 0x1];
            if(0 == node_index){
                inter_gain = 4096;
            }
            else{
                if(0 == first_pixel){
                    //printf("LINE-%d,node_index:%ld \n",__LINE__,node_index);
                    //printf("Pixel_SquareRadius %ld,x0:%ld,y0:%ld,x1:%ld,y1:%ld\n", Pixel_SquareRadius,rlsc_node[node_index-1],RLSCGain->r_gain[node_index-1],rlsc_node[node_index],RLSCGain->r_gain[node_index]);
                    inter_gain = LinearInter( Pixel_SquareRadius, rlsc_node[node_index-1], RLSCGain->r_gain[node_index-1], rlsc_node[node_index], RLSCGain->r_gain[node_index]);
                }
                else if(1 == first_pixel){
                    inter_gain = LinearInter( Pixel_SquareRadius, rlsc_node[node_index-1], RLSCGain->gr_gain[node_index-1], rlsc_node[node_index], RLSCGain->gr_gain[node_index]);
                }
                else if(2 == first_pixel){
                    inter_gain = LinearInter( Pixel_SquareRadius, rlsc_node[node_index-1], RLSCGain->gb_gain[node_index-1], rlsc_node[node_index], RLSCGain->gb_gain[node_index]);
                }
                else if(3 == first_pixel){
                    inter_gain = LinearInter( Pixel_SquareRadius, rlsc_node[node_index-1], RLSCGain->b_gain[node_index-1], rlsc_node[node_index], RLSCGain->b_gain[node_index]);
                }
            }

            //fprintf(inter_gain_head, "%ld ",inter_gain);
            //if((x+1)%1920 == 0){
            //    fprintf(inter_gain_head, "\n");
            //}
            //printf("inter_gain %ld\n", inter_gain);

            BayerImg[y*width+x] =  (uint16)(BayerImg[y*width+x]*(float)inter_gain/QValue);
            BayerImg[y*width+x] = CLIP_MAX(BayerImg[y*width+x],pixel_max_value);

        }
    }
    //fclose(inter_gain_head);
    return 0;
}
int main()
{
    /*   int in = 0;
       struct stat sb;
       in = open("lsc_test.raw", O_RDONLY);
       fstat(in, &sb);
       printf("File size is %ld\n", sb.st_size);
   */
    RLSC_CFG *RLSCfg;
    RLSCfg = (RLSC_CFG *) malloc(sizeof(RLSC_CFG));
    if (NULL == RLSCfg) {
        printf("RLSCGain allocation fail!\n");
        return 0;
    }
    RLSCfg->u32Width = 1920;
    RLSCfg->u32Height = 1080;
    RLSCfg->u32WindowSize = 10;
    RLSCfg->u32BitDepth = 12;
    RLSCfg->u32clip_thresh = 10; //10%
    RLSCfg->BayerFormat = 0;//0-R,1-GR,2-GB,3-B
    RLSCfg->u32GainScale = 3;


    RLSC_GAIN *RLSCGain;
    RLSCGain = (RLSC_GAIN *) malloc(sizeof(RLSC_GAIN));
    if (NULL == RLSCGain) {
        printf("RLSCGain allocation fail!\n");
        return 0;
    }
    memset(RLSCGain, 1, sizeof(RLSC_GAIN));
    uint8 BayerChannel =0;//bayer channel R
    uint16* BayerImg =(uint16*) malloc(1920*1080*sizeof (uint16)) ;
    read_BayerImg("D:\\leetcode_project\\lsc_1920x1080_12bits_RGGB_Linear.raw",1080,1920,BayerImg);

    //start writing rlsc gain to table
    generate_rlsc_gain(BayerImg,RLSCfg,RLSCGain);

    //apply lsc gain to RAW
    apply_rlsc_gain(BayerImg,RLSCfg,RLSCGain);

    //start writing rlsc gain to table
    FILE *f_head;
    f_head = fopen("D:\\leetcode_project\\rlsc_table.h", "wb");
    fprintf(f_head, "static RLSC_GAIN rlsc_gain  = {\n");
    fprintf(f_head, "//r_gain\n");
    fprintf(f_head, "{\n");
    for(int i=0;i<129;i++){
        fprintf(f_head, "%ld ",RLSCGain->r_gain[i]);
        if((i+1)%10==0 || i==128){
            fprintf(f_head, "\n");
        }
    }
    fprintf(f_head, "}\n");

    fprintf(f_head, "//gr_gain\n");
    fprintf(f_head, "{\n");
    for(int i=0;i<129;i++){
        fprintf(f_head, "%ld ",RLSCGain->gr_gain[i]);
        if((i+1)%10==0 || i==128){
            fprintf(f_head, "\n");
        }
    }
    fprintf(f_head, "}\n");

    fprintf(f_head, "//gb_gain\n");
    fprintf(f_head, "{\n");
    for(int i=0;i<129;i++){
        fprintf(f_head, "%ld ",RLSCGain->gb_gain[i]);
        if((i+1)%10==0 || i==128){
            fprintf(f_head, "\n");
        }
    }
    fprintf(f_head, "}\n");

    fprintf(f_head, "//b_gain\n");
    fprintf(f_head, "{\n");
    for(int i=0;i<129;i++){
        fprintf(f_head, "%ld ",RLSCGain->b_gain[i]);
        if((i+1)%10==0 || i==128){
            fprintf(f_head, "\n");
        }
    }
    fprintf(f_head, "}\n");

    fprintf(f_head, "}\n");
    // end rlsc_gain table

    //print corrected pixel value to FILE for checking lens compensation
    for(uint32 y=0;y<1080;y++){
        for(uint32 x=0;x<1920;x++) {
            fprintf(f_head, "%d ",BayerImg[y*1920+x]);
            if((y*1920+x+1)%1920 == 0)
                fprintf(f_head, "\n");

        }
    }
   ///////////////////////////

    fclose(f_head);
    free(BayerImg);
    free(RLSCGain);
    free(RLSCfg);
    return 0;

}
