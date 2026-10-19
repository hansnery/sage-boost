#ifndef PF_POSE_PACKET_H
#define PF_POSE_PACKET_H
void native_pose_init(const char *image,const char *manifest,unsigned advance);
void native_pose_fill(const float *base,float *output,unsigned bones);
void native_pose_close(unsigned expected_fills);
#endif
