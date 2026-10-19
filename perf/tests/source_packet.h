#ifndef PF_SOURCE_PACKET_H
#define PF_SOURCE_PACKET_H
/* Offline numeric commands only. Handles/pointers are worker-local. */
typedef struct {
    unsigned semantic,transpose;
    float value[16];
} source_matrix_command;
void native_source_capture_init(const char *,const char *,const char *);
void native_source_capture(const float [3][16],source_matrix_command [3]);
void native_source_capture_close(void);
unsigned native_source_capture_calls(void);
void material_fixture_source_seed(float [3][16]);
void material_fixture_source_reference(const float (*)[16]);
void material_fixture_source_packets(const source_matrix_command *,unsigned,int);
#endif
